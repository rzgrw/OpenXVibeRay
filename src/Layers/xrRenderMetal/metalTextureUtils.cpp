#include "stdafx.h"
#include "metalTextureUtils.h"

namespace xray::render::RENDER_NAMESPACE
{
namespace metalTextureUtils
{
struct TextureFormatPairs
{
    D3DFORMAT m_dx9FMT;
    MTL::PixelFormat m_mtlFMT;
};

// Mirrors glTextureUtils::TextureFormatList — only formats the engine
// actually requests for render targets / surfaces are listed.
static const TextureFormatPairs TextureFormatList[] =
{
    {D3DFMT_UNKNOWN, MTL::PixelFormatInvalid},
    // D3D ARGB is BGRA byte order in memory (little endian)
    {D3DFMT_A8R8G8B8, MTL::PixelFormatBGRA8Unorm},
    // 16-bit formats are promoted, matching the GL backend
    {D3DFMT_R5G6B5, MTL::PixelFormatBGRA8Unorm},
    {D3DFMT_A8B8G8R8, MTL::PixelFormatRGBA8Unorm},
    {D3DFMT_G16R16, MTL::PixelFormatRG16Unorm},
    {D3DFMT_A16B16G16R16, MTL::PixelFormatRGBA16Unorm},
    // Note: Use .r swizzle in shader to duplicate red to other components.
    {D3DFMT_L8, MTL::PixelFormatR8Unorm},
    {D3DFMT_V8U8, MTL::PixelFormatRG8Unorm},
    {D3DFMT_Q8W8V8U8, MTL::PixelFormatRGBA8Unorm},
    {D3DFMT_V16U16, MTL::PixelFormatRG16Unorm},
    // Apple Silicon has no 24-bit depth — promote to 32F + S8
    {D3DFMT_D24S8, MTL::PixelFormatDepth32Float_Stencil8},
    {D3DFMT_D24X8, MTL::PixelFormatDepth32Float_Stencil8},
    {D3DFMT_D32F_LOCKABLE, MTL::PixelFormatDepth32Float},
    {D3DFMT_G16R16F, MTL::PixelFormatRG16Float},
    {D3DFMT_A16B16G16R16F, MTL::PixelFormatRGBA16Float},
    {D3DFMT_R32F, MTL::PixelFormatR32Float},
    {D3DFMT_R16F, MTL::PixelFormatR16Float},
    {D3DFMT_A32B32G32R32F, MTL::PixelFormatRGBA32Float},
};

MTL::PixelFormat ConvertTextureFormat(D3DFORMAT dx9FMT)
{
    for (const auto& textureFormat : TextureFormatList)
    {
        if (textureFormat.m_dx9FMT == dx9FMT)
            return textureFormat.m_mtlFMT;
    }

    VERIFY(!"ConvertTextureFormat didn't find appropriate Metal pixel format!");
    return MTL::PixelFormatInvalid;
}

// DDS payload (via GLI) -> Metal pixel format.
// Unlike GL, Metal BC formats natively support 3D textures, so compressed
// volume textures (e.g. water_sbumpvolume.dds) upload directly — no CPU
// decompression pass is needed.
MTL::PixelFormat ConvertGLIFormat(gli::format fmt)
{
    switch (fmt)
    {
    // S3TC / BCn — supported on all Apple Silicon GPUs
    case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
    case gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8:
        return MTL::PixelFormatBC1_RGBA;
    case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
    case gli::FORMAT_RGBA_DXT1_SRGB_BLOCK8:
        return MTL::PixelFormatBC1_RGBA_sRGB;
    case gli::FORMAT_RGBA_DXT3_UNORM_BLOCK16:
        return MTL::PixelFormatBC2_RGBA;
    case gli::FORMAT_RGBA_DXT3_SRGB_BLOCK16:
        return MTL::PixelFormatBC2_RGBA_sRGB;
    case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
        return MTL::PixelFormatBC3_RGBA;
    case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
        return MTL::PixelFormatBC3_RGBA_sRGB;
    case gli::FORMAT_R_ATI1N_UNORM_BLOCK8:
        return MTL::PixelFormatBC4_RUnorm;
    case gli::FORMAT_R_ATI1N_SNORM_BLOCK8:
        return MTL::PixelFormatBC4_RSnorm;
    case gli::FORMAT_RG_ATI2N_UNORM_BLOCK16:
        return MTL::PixelFormatBC5_RGUnorm;
    case gli::FORMAT_RG_ATI2N_SNORM_BLOCK16:
        return MTL::PixelFormatBC5_RGSnorm;
    case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
        return MTL::PixelFormatBC6H_RGBUfloat;
    case gli::FORMAT_RGB_BP_SFLOAT_BLOCK16:
        return MTL::PixelFormatBC6H_RGBFloat;
    case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
        return MTL::PixelFormatBC7_RGBAUnorm;
    case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
        return MTL::PixelFormatBC7_RGBAUnorm_sRGB;

    // Uncompressed
    case gli::FORMAT_RGBA8_UNORM_PACK8:
    case gli::FORMAT_RGBA8_UNORM_PACK32:
        return MTL::PixelFormatRGBA8Unorm;
    case gli::FORMAT_RGBA8_SRGB_PACK8:
    case gli::FORMAT_RGBA8_SRGB_PACK32:
        return MTL::PixelFormatRGBA8Unorm_sRGB;
    case gli::FORMAT_BGRA8_UNORM_PACK8:
        return MTL::PixelFormatBGRA8Unorm;
    case gli::FORMAT_BGRA8_SRGB_PACK8:
        return MTL::PixelFormatBGRA8Unorm_sRGB;
    case gli::FORMAT_R8_UNORM_PACK8:
    case gli::FORMAT_L8_UNORM_PACK8:
        return MTL::PixelFormatR8Unorm;
    case gli::FORMAT_A8_UNORM_PACK8:
        return MTL::PixelFormatA8Unorm;
    case gli::FORMAT_RG8_UNORM_PACK8:
    case gli::FORMAT_LA8_UNORM_PACK8:
        return MTL::PixelFormatRG8Unorm;
    case gli::FORMAT_RG16_UNORM_PACK16:
        return MTL::PixelFormatRG16Unorm;
    case gli::FORMAT_RGBA16_UNORM_PACK16:
        return MTL::PixelFormatRGBA16Unorm;
    case gli::FORMAT_R16_SFLOAT_PACK16:
        return MTL::PixelFormatR16Float;
    case gli::FORMAT_RG16_SFLOAT_PACK16:
        return MTL::PixelFormatRG16Float;
    case gli::FORMAT_RGBA16_SFLOAT_PACK16:
        return MTL::PixelFormatRGBA16Float;
    case gli::FORMAT_R32_SFLOAT_PACK32:
        return MTL::PixelFormatR32Float;
    case gli::FORMAT_RG32_SFLOAT_PACK32:
        return MTL::PixelFormatRG32Float;
    case gli::FORMAT_RGBA32_SFLOAT_PACK32:
        return MTL::PixelFormatRGBA32Float;
    case gli::FORMAT_RGB10A2_UNORM_PACK32:
        return MTL::PixelFormatRGB10A2Unorm;

    // 24-bit formats have no Metal equivalent — caller must expand on the CPU
    case gli::FORMAT_RGB8_UNORM_PACK8:
    case gli::FORMAT_RGB8_SRGB_PACK8:
    case gli::FORMAT_BGR8_UNORM_PACK8:
    case gli::FORMAT_BGR8_SRGB_PACK8:
    default:
        return MTL::PixelFormatInvalid;
    }
}

MTL::TextureType ConvertTextureTarget(gli::target target)
{
    switch (target)
    {
    case gli::TARGET_1D:         return MTL::TextureType1D;
    case gli::TARGET_1D_ARRAY:   return MTL::TextureType1DArray;
    case gli::TARGET_2D:         return MTL::TextureType2D;
    case gli::TARGET_2D_ARRAY:   return MTL::TextureType2DArray;
    case gli::TARGET_3D:         return MTL::TextureType3D;
    case gli::TARGET_CUBE:       return MTL::TextureTypeCube;
    case gli::TARGET_CUBE_ARRAY: return MTL::TextureTypeCubeArray;
    default:
        NODEFAULT;
        return MTL::TextureType2D;
    }
}

bool IsCompressed(MTL::PixelFormat fmt)
{
    return fmt >= MTL::PixelFormatBC1_RGBA && fmt <= MTL::PixelFormatBC7_RGBAUnorm_sRGB;
}

u32 BytesPerBlock(MTL::PixelFormat fmt)
{
    switch (fmt)
    {
    // 8 bytes per 4x4 block
    case MTL::PixelFormatBC1_RGBA:
    case MTL::PixelFormatBC1_RGBA_sRGB:
    case MTL::PixelFormatBC4_RUnorm:
    case MTL::PixelFormatBC4_RSnorm:
        return 8;
    // 16 bytes per 4x4 block
    case MTL::PixelFormatBC2_RGBA:
    case MTL::PixelFormatBC2_RGBA_sRGB:
    case MTL::PixelFormatBC3_RGBA:
    case MTL::PixelFormatBC3_RGBA_sRGB:
    case MTL::PixelFormatBC5_RGUnorm:
    case MTL::PixelFormatBC5_RGSnorm:
    case MTL::PixelFormatBC6H_RGBUfloat:
    case MTL::PixelFormatBC6H_RGBFloat:
    case MTL::PixelFormatBC7_RGBAUnorm:
    case MTL::PixelFormatBC7_RGBAUnorm_sRGB:
        return 16;
    // Uncompressed — bytes per pixel
    case MTL::PixelFormatR8Unorm:
    case MTL::PixelFormatA8Unorm:
        return 1;
    case MTL::PixelFormatRG8Unorm:
    case MTL::PixelFormatR16Float:
        return 2;
    case MTL::PixelFormatRGBA8Unorm:
    case MTL::PixelFormatRGBA8Unorm_sRGB:
    case MTL::PixelFormatBGRA8Unorm:
    case MTL::PixelFormatBGRA8Unorm_sRGB:
    case MTL::PixelFormatRG16Unorm:
    case MTL::PixelFormatRG16Float:
    case MTL::PixelFormatR32Float:
    case MTL::PixelFormatRGB10A2Unorm:
        return 4;
    case MTL::PixelFormatRGBA16Unorm:
    case MTL::PixelFormatRGBA16Float:
    case MTL::PixelFormatRG32Float:
        return 8;
    case MTL::PixelFormatRGBA32Float:
        return 16;
    default:
        VERIFY(!"BytesPerBlock: unhandled Metal pixel format");
        return 4;
    }
}
} // namespace metalTextureUtils
} // namespace xray::render::RENDER_NAMESPACE
