#include "Precompiled.h"
#include "Model.h"
#include "Mesh.h"
#include "Material.h"
#include "Utilities/StringUtilities.h"
#include "Core/OS/FileSystem.h"
#include "Animation/Skeleton.h"
#include "Animation/Animation.h"
#include "Animation/AnimationController.h"
#include "Animation/SamplingContext.h"
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace
{
#if defined(LUMOS_PLATFORM_WINDOWS)
    std::string QuoteShellArg(const std::string& value)
    {
        std::string out = "\"";
        for(char c : value)
        {
            if(c == '\"')
                out += "\\\"";
            else
                out += c;
        }
        out += "\"";
        return out;
    }
#else
    std::string QuoteShellArg(const std::string& value)
    {
        std::string out = "'";
        for(char c : value)
        {
            if(c == '\'')
                out += "'\"'\"'";
            else
                out += c;
        }
        out += "'";
        return out;
    }
#endif

    std::string EscapePythonString(const std::string& value)
    {
        std::string out;
        out.reserve(value.size() * 2);
        for(char c : value)
        {
            if(c == '\\' || c == '\"')
                out += '\\';
            out += c;
        }
        return out;
    }

    bool ConvertBlendToGLB(const std::string& blendPath, std::string& outGlbPath)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::path srcPath(blendPath);
        if(!fs::exists(srcPath, ec) || fs::is_directory(srcPath, ec))
            return false;

        fs::path cacheDir = srcPath.parent_path() / "Cache" / "ConvertedModels";
        fs::create_directories(cacheDir, ec);

        fs::path outputPath = cacheDir / (srcPath.stem().string() + ".auto.glb");
        outGlbPath          = outputPath.generic_string();

        if(fs::exists(outputPath, ec))
        {
            std::error_code srcTimeError, dstTimeError;
            const auto srcTime = fs::last_write_time(srcPath, srcTimeError);
            const auto dstTime = fs::last_write_time(outputPath, dstTimeError);
            if(!srcTimeError && !dstTimeError && dstTime >= srcTime)
                return true;
        }

        const std::string pythonExpr = "import bpy;bpy.ops.export_scene.gltf(filepath=\"" + EscapePythonString(outGlbPath) + "\", export_format=\"GLB\")";
#if defined(LUMOS_PLATFORM_WINDOWS)
        const std::string suppressOutput = " >NUL 2>NUL";
#else
        const std::string suppressOutput = " >/dev/null 2>&1";
#endif

        std::vector<std::string> blenderExecutables = { "blender" };
#if defined(LUMOS_PLATFORM_MACOS)
        blenderExecutables.emplace_back("/Applications/Blender.app/Contents/MacOS/Blender");
        blenderExecutables.emplace_back("/Applications/Blender.app/Contents/MacOS/blender");
        blenderExecutables.emplace_back("/opt/homebrew/bin/blender");
        blenderExecutables.emplace_back("/usr/local/bin/blender");

        auto appendBlenderApps = [&blenderExecutables](const fs::path& basePath) {
            std::error_code appScanError;
            if(!fs::exists(basePath, appScanError) || !fs::is_directory(basePath, appScanError))
                return;

            for(const auto& entry : fs::directory_iterator(basePath, appScanError))
            {
                if(appScanError)
                    break;
                if(!entry.is_directory())
                    continue;

                std::string appName = Lumos::StringUtilities::ToLower(entry.path().filename().string());
                if(appName.find("blender") == std::string::npos || entry.path().extension() != ".app")
                    continue;

                fs::path candidate = entry.path() / "Contents" / "MacOS" / "Blender";
                if(fs::exists(candidate, appScanError))
                    blenderExecutables.emplace_back(candidate.string());
            }
        };

        appendBlenderApps("/Applications");
        if(const char* home = std::getenv("HOME"))
            appendBlenderApps(fs::path(home) / "Applications");
#endif

        for(const auto& blenderExecutable : blenderExecutables)
        {
            const std::string command = QuoteShellArg(blenderExecutable) + " -b " + QuoteShellArg(blendPath) + " --python-expr " + QuoteShellArg(pythonExpr) + suppressOutput;
            const int result          = std::system(command.c_str());
            if(result == 0 && fs::exists(outputPath, ec))
                return true;
        }

        return false;
    }
} // namespace

namespace Lumos::Graphics
{
    Model::Model()
        : m_FilePath()
        , m_PrimitiveType(PrimitiveType::None)
    {
    }

    Model::Model(const std::string& filePath)
        : m_FilePath(filePath)
        , m_PrimitiveType(PrimitiveType::File)
    {
        LoadModel(m_FilePath);
    }

    Model::Model(const SharedPtr<Mesh>& mesh, PrimitiveType type)
        : m_FilePath("Primitive")
        , m_PrimitiveType(type)
    {
        m_Meshes.PushBack(mesh);
    }

    Model::Model(PrimitiveType type)
        : m_FilePath("Primitive")
        , m_PrimitiveType(type)
    {
        m_Meshes.PushBack(SharedPtr<Mesh>(CreatePrimative(type)));
    }

    Model::~Model()
    {
    }

    Model::Model(const Model&)            = default;
    Model& Model::operator=(const Model&) = default;
    Model::Model(Model&&)                 = default;
    Model& Model::operator=(Model&&)      = default;

    void Model::LoadModel(const std::string& path)
    {
        LUMOS_PROFILE_FUNCTION();
        ArenaTemp Scratch = ScratchBegin(0, 0);

        String8 physicalPath;
        if(!Lumos::FileSystem::Get().ResolvePhysicalPath(Scratch.arena, Str8StdS(path), &physicalPath))
        {
            LINFO("Failed to load Model - %s", path.c_str());
            ScratchEnd(Scratch);
            return;
        }

        std::string resolvedPath = ToStdString(physicalPath);

        const std::string fileExtension = StringUtilities::ToLower(StringUtilities::GetFilePathExtension(path));
        bool loaded                     = false;

        if(fileExtension == "obj")
        {
            LoadOBJ(resolvedPath);
            loaded = true;
        }
        else if(fileExtension == "gltf" || fileExtension == "glb")
        {
            LoadGLTF(resolvedPath);
            loaded = true;
        }
        else if(fileExtension == "fbx")
        {
            LoadFBX(resolvedPath);
            loaded = true;
        }
        else if(fileExtension == "blend")
        {
            std::string convertedPath;
            if(ConvertBlendToGLB(resolvedPath, convertedPath))
            {
                LINFO("Converted BLEND model to GLB : %s", convertedPath.c_str());
                LoadGLTF(convertedPath);
                loaded = true;
            }
            else
            {
                LERROR("Failed to import BLEND model %s", path.c_str());
                LERROR("Auto-conversion requires Blender CLI in PATH (command: blender)");
            }
        }
        else
            LERROR("Unsupported File Type : %s", fileExtension.c_str());

        if(loaded)
            LINFO("Loaded Model - %s", path.c_str());
        ScratchEnd(Scratch);
    }

    void Model::UpdateAnimation(const TimeStep& dt)
    {
        if(m_Animation.Empty())
            return;

        if(!m_SamplingContext)
        {
            m_SamplingContext = CreateSharedPtr<SamplingContext>();
        }

        if(!m_AnimationController)
        {
            m_AnimationController = CreateSharedPtr<AnimationController>();
            m_AnimationController->SetSkeleton(m_Skeleton);
            for(auto anim : m_Animation)
            {
                m_AnimationController->AddState(anim->GetName(), anim);
            }
            m_AnimationController->SetBindPoses(m_BindPoses);
        }

        static float time = 0.0f;
        time += (float)dt.GetSeconds();
        m_AnimationController->SetCurrentState(m_CurrentAnimation);
        m_AnimationController->Update(time, *m_SamplingContext.get());
    }

    void Model::UpdateAnimation(const TimeStep& dt, float overrideTime)
    {
        if(m_Animation.Empty())
            return;

        if(!m_SamplingContext)
        {
            m_SamplingContext = CreateSharedPtr<SamplingContext>();
        }

        if(!m_AnimationController)
        {
            m_AnimationController = CreateSharedPtr<AnimationController>();
            m_AnimationController->SetSkeleton(m_Skeleton);
            for(auto anim : m_Animation)
            {
                m_AnimationController->AddState(anim->GetName(), anim);
            }
            m_AnimationController->SetBindPoses(m_BindPoses);
        }

        m_AnimationController->SetCurrentState(m_CurrentAnimation);
        m_AnimationController->Update(overrideTime, *m_SamplingContext.get());
    }

    TDArray<Mat4> Model::GetJointMatrices()
    {
        if(m_Animation.Empty())
            return {};

        auto matrices = m_AnimationController->GetJointMatrices();

        for(int i = 0; i < matrices.Size(); i++)
        {
            // matrices[i] = m_BindPoses[i] * matrices[i];
        }

        return matrices;
    }

    TDArray<SharedPtr<Mesh>>& Model::GetMeshesRef()
    {
        return m_Meshes;
    }
    const TDArray<SharedPtr<Mesh>>& Model::GetMeshes() const
    {
        return m_Meshes;
    }
    void Model::AddMesh(SharedPtr<Mesh> mesh)
    {
        m_Meshes.PushBack(mesh);
    }
    SharedPtr<Skeleton> Model::GetSkeleton() const
    {
        return m_Skeleton;
    }

    const TDArray<SharedPtr<Animation>>& Model::GetAnimations() const
    {
        return m_Animation;
    }
    SharedPtr<SamplingContext> Model::GetSamplingContext() const
    {
        return m_SamplingContext;
    }

    SharedPtr<AnimationController> Model::GetAnimationController() const
    {
        return m_AnimationController;
    }
}
