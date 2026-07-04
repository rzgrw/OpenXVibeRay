// metalTexture.cpp: DDS texture loading for the Metal backend.
// Mirrors glTexture.cpp — DDS files are parsed with GLI and uploaded
// into MTL::Texture objects via replaceRegion.
//
// Unlike GL (which forbids S3TC compression on 3D targets and must
// decompress volume DXT textures on the CPU), Metal BC formats support
// 3D textures natively, so compressed volume textures upload directly.

#include "stdafx.h"

#include <gli/gli.hpp>

#include "Layers/xrRenderMetal/metalTextureUtils.h"

namespace xray::render::RENDER_NAMESPACE
{
void fix_texture_name(pstr fn)
{
    pstr _ext = strext(fn);
    if (_ext &&
        (0 == xr_stricmp(_ext, ".tga") ||
            0 == xr_stricmp(_ext, ".dds") ||
            0 == xr_stricmp(_ext, ".bmp") ||
            0 == xr_stricmp(_ext, ".ogm")))
        *_ext = 0;
}

int get_texture_load_lod(LPCSTR fn)
{
    CInifile::Sect& sect = pSettings->r_section("reduce_lod_texture_list");

    for (const auto& item : sect.Data)
    {
        if (strstr(fn, item.first.c_str()))
        {
            if (psTextureLOD < 1)
                return 0;
            if (psTextureLOD < 3)
                return 1;
            return 2;
        }
    }

    if (psTextureLOD < 2)
        return 0;
    if (psTextureLOD < 4)
        return 1;
    return 2;
}

u32 calc_texture_size(int lod, u32 mip_cnt, size_t orig_size)
{
    if (1 == mip_cnt)
        return orig_size;

    int _lod = lod;
    float res = float(orig_size);

    while (_lod > 0)
    {
        --_lod;
        res -= res / 1.333f;
    }
    return iFloor(res);
}

// Metal has no 24-bit pixel formats. Expand tightly-packed RGB8/BGR8
// texel data to 4-byte texels (alpha = 255). Component order is preserved,
// so RGB8 expands for PixelFormatRGBA8Unorm and BGR8 for PixelFormatBGRA8Unorm.
static void expand_rgb8_to_rgba8(const u8* src, size_t texel_count, xr_vector<u8>& out)
{
    out.resize(texel_count * 4);
    u8* dst = out.data();
    for (size_t i = 0; i < texel_count; ++i, src += 3, dst += 4)
    {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 0xFF;
    }
}

uint64_t CRender::texture_load(LPCSTR fRName, u32& ret_msize, u32& ret_desc)
{
    ret_msize = 0;
    R_ASSERT1_CURE(fRName && fRName[0], { return 0; });

    string_path fn;
    {
        // make file name
        string_path fname;
        xr_strcpy(fname, fRName);
        fix_texture_name(fname);

        // Call to FS.exist WRITES to fn !

        if (!FS.exist(fn, "$game_textures$", fname, ".dds") && strstr(fname, "_bump"))
        {
            Msg("! Fallback to default bump map: %s", fname);
            if (strstr(fname, "_bump#"))
                R_ASSERT1_CURE(FS.exist(fn, "$game_textures$", "ed\\ed_dummy_bump#", ".dds"), return 0);
            else
                R_ASSERT1_CURE(FS.exist(fn, "$game_textures$", "ed\\ed_dummy_bump", ".dds"), return 0);
        }
        else
        {
            bool exist = false;

            for (cpcstr folder : { "$level$", "$game_saves$", "$game_textures$" })
            {
                exist = FS.exist(fn, folder, fname, ".dds");
                if (exist)
                    break;
            }

            if (!exist)
            {
                Msg("! Can't find texture '%s'", fname);
                if (!FS.exist(fn, "$game_textures$", "ed\\ed_not_existing_texture", ".dds"))
                    return 0;
            }
        }
    }

    // Load and get header
    IReader* S = FS.r_open(fn);
    R_ASSERT2_CURE(S, fn, { return 0; });
    size_t img_size = S->length();
#ifdef DEBUG
    Msg("* Loaded: %s[%d]b", fn, img_size);
#endif // DEBUG
    gli::texture texture = gli::load((char*)S->pointer(), img_size);
    R_ASSERT2(!texture.empty(), fn);

    u32 mip_cnt = u32(-1); // XXX: write to it when reading with GLI!

    const MTL::TextureType type = metalTextureUtils::ConvertTextureTarget(texture.target());

    MTL::PixelFormat fmt = metalTextureUtils::ConvertGLIFormat(texture.format());

    // Metal has no 24-bit formats — expand RGB8/BGR8 on the CPU
    bool expand24 = false;
    switch (texture.format())
    {
    case gli::FORMAT_RGB8_UNORM_PACK8:
        expand24 = true;
        fmt = MTL::PixelFormatRGBA8Unorm;
        break;
    case gli::FORMAT_RGB8_SRGB_PACK8:
        expand24 = true;
        fmt = MTL::PixelFormatRGBA8Unorm_sRGB;
        break;
    case gli::FORMAT_BGR8_UNORM_PACK8:
        expand24 = true;
        fmt = MTL::PixelFormatBGRA8Unorm;
        break;
    case gli::FORMAT_BGR8_SRGB_PACK8:
        expand24 = true;
        fmt = MTL::PixelFormatBGRA8Unorm_sRGB;
        break;
    default:
        break;
    }

    if (MTL::PixelFormatInvalid == fmt)
    {
        Msg("! Metal: unsupported texture format %d in '%s'", int(texture.format()), fn);
        FS.r_close(S);
        return 0;
    }

    const glm::tvec3<int> tex_extent(texture.extent());

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(type);
    desc->setPixelFormat(fmt);
    desc->setWidth(NS::UInteger(tex_extent.x));
    desc->setHeight(NS::UInteger(tex_extent.y));
    if (gli::TARGET_3D == texture.target())
        desc->setDepth(NS::UInteger(tex_extent.z));
    desc->setMipmapLevelCount(texture.levels());
    if (gli::TARGET_1D_ARRAY == texture.target() || gli::TARGET_2D_ARRAY == texture.target() ||
        gli::TARGET_CUBE_ARRAY == texture.target())
        desc->setArrayLength(texture.layers());
    desc->setStorageMode(MTL::StorageModeShared);
    desc->setUsage(MTL::TextureUsageShaderRead);

    MTL::Texture* pTexture = HW.pDevice->newTexture(desc);
    desc->release();

    if (!pTexture)
    {
        Msg("! Metal: failed to create texture: '%s' (format=%d, %dx%dx%d, levels=%zu)",
            fn, int(fmt), tex_extent.x, tex_extent.y, tex_extent.z, texture.levels());
        FS.r_close(S);
        return 0;
    }

    pTexture->setLabel(NS::String::string(fn, NS::UTF8StringEncoding));

    const bool compressed = metalTextureUtils::IsCompressed(fmt);
    const u32 block_bytes = metalTextureUtils::BytesPerBlock(fmt);
    xr_vector<u8> expanded;

    for (size_t layer = 0; layer < texture.layers(); ++layer)
    {
        for (size_t face = 0; face < texture.faces(); ++face)
        {
            for (size_t level = 0; level < texture.levels(); ++level)
            {
                const glm::tvec3<int> level_extent(texture.extent(level));
                const size_t w = level_extent.x, h = level_extent.y;
                const size_t d = gli::TARGET_3D == texture.target() ? level_extent.z : 1;

                // Metal slice index: cube face for cubemaps, array layer
                // for arrays, layer * 6 + face for cube arrays.
                const NS::UInteger slice = layer * texture.faces() + face;

                const void* data = texture.data(layer, face, level);
                size_t row_bytes, image_bytes;

                if (compressed)
                {
                    row_bytes = ((w + 3) / 4) * block_bytes;
                    image_bytes = row_bytes * ((h + 3) / 4);
                }
                else if (expand24)
                {
                    expand_rgb8_to_rgba8(static_cast<const u8*>(data), w * h * d, expanded);
                    data = expanded.data();
                    row_bytes = w * 4;
                    image_bytes = row_bytes * h;
                }
                else
                {
                    row_bytes = w * block_bytes;
                    image_bytes = row_bytes * h;
                }

                const MTL::Region region = gli::TARGET_3D == texture.target()
                    ? MTL::Region::Make3D(0, 0, 0, w, h, d)
                    : MTL::Region::Make2D(0, 0, w, h);

                pTexture->replaceRegion(region, level, slice, data, row_bytes,
                    d > 1 ? image_bytes : 0);
            }
        }
    }

    FS.r_close(S);

    xr_strlwr(fn);
    ret_desc = u32(type);
    int img_loaded_lod = is_target_cube(texture.target()) ? 0 : get_texture_load_lod(fn);
    ret_msize = calc_texture_size(img_loaded_lod, mip_cnt, img_size);
    return reinterpret_cast<uint64_t>(pTexture);
}
} // namespace xray::render::RENDER_NAMESPACE
