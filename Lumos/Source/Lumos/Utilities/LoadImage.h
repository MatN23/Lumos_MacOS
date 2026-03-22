#pragma once

namespace Lumos
{
    struct ImageLoadDesc
    {
        const char* filePath  = nullptr;
        uint32_t outWidth     = 0;
        uint32_t outHeight    = 0;
        uint32_t outBits      = 0;
        bool isHDR            = false;
        bool flipY         = false;
        bool srgb          = true;
        uint32_t maxWidth  = 2048;
        uint32_t maxHeight = 2048;
        uint8_t* outPixels = nullptr;
    };

    LUMOS_EXPORT uint8_t* LoadImageFromFile(const char* filename, uint32_t* width = nullptr, uint32_t* height = nullptr, uint32_t* bits = nullptr, bool* isHDR = nullptr, bool flipY = false, bool srgb = true);
    LUMOS_EXPORT uint8_t* LoadImageFromFile(const std::string& filename, uint32_t* width = nullptr, uint32_t* height = nullptr, uint32_t* bits = nullptr, bool* isHDR = nullptr, bool flipY = false, bool srgb = true);

    LUMOS_EXPORT bool LoadImageFromFile(ImageLoadDesc& desc);

    LUMOS_EXPORT void SetMaxImageDimensions(uint32_t width, uint32_t height);

    LUMOS_EXPORT void GetMaxImageDimensions(uint32_t& width, uint32_t& height);
}
