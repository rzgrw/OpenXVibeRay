// Texture.cpp: implementation of the CTexture class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include <gli/gli.hpp>
#include <gli/core/s3tc.hpp>

namespace xray::render::RENDER_NAMESPACE
{
enum class s3tc_kind
{
    none,
    dxt1,
    dxt3,
    dxt5
};

static s3tc_kind s3tc_format_kind(gli::format fmt)
{
    switch (fmt)
    {
    case gli::FORMAT_RGB_DXT1_UNORM_BLOCK8:
    case gli::FORMAT_RGB_DXT1_SRGB_BLOCK8:
    case gli::FORMAT_RGBA_DXT1_UNORM_BLOCK8:
    case gli::FORMAT_RGBA_DXT1_SRGB_BLOCK8:
        return s3tc_kind::dxt1;
    case gli::FORMAT_RGBA_DXT3_UNORM_BLOCK16:
    case gli::FORMAT_RGBA_DXT3_SRGB_BLOCK16:
        return s3tc_kind::dxt3;
    case gli::FORMAT_RGBA_DXT5_UNORM_BLOCK16:
    case gli::FORMAT_RGBA_DXT5_SRGB_BLOCK16:
        return s3tc_kind::dxt5;
    default:
        return s3tc_kind::none;
    }
}

// OpenGL forbids S3TC compression for 3D targets (EXT_texture_compression_s3tc
// only covers 2D/array/cube), so compressed volume textures like
// water_sbumpvolume.dds must be decompressed to RGBA8 and uploaded raw.
static void decompress_s3tc_rgba8(gli::texture const& texture, size_t layer, size_t face, size_t level,
    s3tc_kind kind, xr_vector<u8>& out)
{
    glm::tvec3<GLsizei> const ext(texture.extent(level));
    size_t const width = ext.x, height = ext.y, depth = ext.z;
    size_t const blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
    size_t const block_size = kind == s3tc_kind::dxt1 ? 8 : 16;

    out.resize(width * height * depth * 4);
    auto src = static_cast<u8 const*>(texture.data(layer, face, level));

    for (size_t z = 0; z < depth; ++z)
    {
        u8* const slice = out.data() + z * width * height * 4;
        for (size_t by = 0; by < blocks_y; ++by)
            for (size_t bx = 0; bx < blocks_x; ++bx, src += block_size)
            {
                gli::detail::texel_block4x4 block;
                switch (kind)
                {
                case s3tc_kind::dxt1:
                    block = gli::detail::decompress_dxt1_block(*reinterpret_cast<gli::detail::dxt1_block const*>(src));
                    break;
                case s3tc_kind::dxt3:
                    block = gli::detail::decompress_dxt3_block(*reinterpret_cast<gli::detail::dxt3_block const*>(src));
                    break;
                default:
                    block = gli::detail::decompress_dxt5_block(*reinterpret_cast<gli::detail::dxt5_block const*>(src));
                    break;
                }

                for (size_t row = 0; row < 4 && by * 4 + row < height; ++row)
                    for (size_t col = 0; col < 4 && bx * 4 + col < width; ++col)
                    {
                        u8* const dst = slice + ((by * 4 + row) * width + bx * 4 + col) * 4;
                        glm::vec4 const& texel = block.Texel[row][col];
                        dst[0] = u8(glm::clamp(texel.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                        dst[1] = u8(glm::clamp(texel.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                        dst[2] = u8(glm::clamp(texel.b, 0.0f, 1.0f) * 255.0f + 0.5f);
                        dst[3] = u8(glm::clamp(texel.a, 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
            }
    }
}

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

GLuint CRender::texture_load(LPCSTR fRName, u32& ret_msize, GLenum& ret_desc)
{
    ret_msize = 0;
    R_ASSERT1_CURE(fRName && fRName[0], { return 0; });

    GLuint pTexture = 0;
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

    gli::gl GL(gli::gl::PROFILE_GL33);

    gli::gl::format const format = GL.translate(texture.format(), texture.swizzles());
    GLenum target = GL.translate(texture.target());

    // Drain stale errors so failures below are attributed to THIS load,
    // not to whatever GL call raised them earlier in the frame.
    for (GLenum stale = glGetError(); stale != GL_NO_ERROR; stale = glGetError())
        Msg("! OpenGL: stale error 0x%x pending before loading '%s'", stale, fn);

    glGenTextures(1, &pTexture);
    glBindTexture(target, pTexture);

    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));

    if (gli::gl::EXTERNAL_RED != format.External) // skip for proper greyscale-alpha font textures
        glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, &format.Swizzles[gli::SWIZZLE_RED]);

    glm::tvec3<GLsizei> const tex_extent(texture.extent());

    // GL rejects S3TC-compressed volume textures — those get decompressed on the CPU
    s3tc_kind const decode_3d = texture.target() == gli::TARGET_3D && gli::is_compressed(texture.format())
        ? s3tc_format_kind(texture.format())
        : s3tc_kind::none;

    GLenum err;
    switch (texture.target())
    {
    case gli::TARGET_2D:
    case gli::TARGET_CUBE:
        glTexStorage2D(target, static_cast<GLint>(texture.levels()), format.Internal,
                       tex_extent.x, tex_extent.y);
        err = glGetError();
        if (err != GL_NO_ERROR)
        {
            VERIFY(err == GL_NO_ERROR);
            Msg("! OpenGL: 0x%x: Invalid 2D texture: '%s' (internal=0x%x, %dx%d, levels=%zu, compressed=%d)",
                err, fn, format.Internal, tex_extent.x, tex_extent.y, texture.levels(),
                gli::is_compressed(texture.format()) ? 1 : 0);
        }
        break;
    case gli::TARGET_3D:
    case gli::TARGET_CUBE_ARRAY:
        glTexStorage3D(target, static_cast<GLint>(texture.levels()),
                       decode_3d != s3tc_kind::none ? GL_RGBA8 : format.Internal,
                       tex_extent.x, tex_extent.y, tex_extent.z);
        err = glGetError();
        if (err != GL_NO_ERROR)
        {
            VERIFY(err == GL_NO_ERROR);
            Msg("! OpenGL: 0x%x: Invalid 3D texture: '%s' (internal=0x%x, %dx%dx%d, levels=%zu, compressed=%d)",
                err, fn, format.Internal, tex_extent.x, tex_extent.y, tex_extent.z, texture.levels(),
                gli::is_compressed(texture.format()) ? 1 : 0);
        }
        break;
    default:
        NODEFAULT;
        break;
    }

    for (size_t layer = 0; layer < texture.layers(); ++layer)
    {
        for (size_t face = 0; face < texture.faces(); ++face)
        {
            for (size_t level = 0; level < texture.levels(); ++level)
            {
                glm::tvec3<GLsizei> const tex_level_extent(texture.extent(level));
                GLenum sub_target = gli::is_target_cube(texture.target())
                         ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
                         : target;

                switch (texture.target())
                {
                case gli::TARGET_2D:
                case gli::TARGET_CUBE:
                {
                    if (gli::is_compressed(texture.format()))
                    {
                        glCompressedTexSubImage2D(sub_target, static_cast<GLint>(level),
                                    0, 0, tex_level_extent.x, tex_level_extent.y,
                                    format.Internal, static_cast<GLsizei>(texture.size(level)),
                                    texture.data(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid 2D compressed subtexture: '%s'", err, fn);
                        }
                    }
                    else
                    {
                        glTexSubImage2D(sub_target, static_cast<GLint>(level),
                                    0, 0, tex_level_extent.x, tex_level_extent.y,
                                    format.External, format.Type,
                                    texture.data(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid 2D subtexture: '%s'", err, fn);
                        }

                    }
                    break;
                }
                case gli::TARGET_3D:
                case gli::TARGET_CUBE_ARRAY:
                {
                    if (decode_3d != s3tc_kind::none)
                    {
                        xr_vector<u8> rgba8;
                        decompress_s3tc_rgba8(texture, layer, face, level, decode_3d, rgba8);
                        glTexSubImage3D(target, static_cast<GLint>(level),
                                    0, 0, 0, tex_level_extent.x, tex_level_extent.y, tex_level_extent.z,
                                    GL_RGBA, GL_UNSIGNED_BYTE, rgba8.data());
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid decompressed 3D subtexture: '%s'", err, fn);
                        }
                    }
                    else if (gli::is_compressed(texture.format()))
                    {
                        glCompressedTexSubImage3D(target, static_cast<GLint>(level),
                                    0, 0, 0, tex_level_extent.x, tex_level_extent.y, tex_level_extent.z,
                                    format.Internal, static_cast<GLsizei>(texture.size(level)),
                                    texture.data(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid compressed 3D subtexture: '%s'", err, fn);
                        }
                    }
                    else
                    {
                        glTexSubImage3D(target, static_cast<GLint>(level),
                                    0, 0, 0, tex_level_extent.x, tex_level_extent.y, tex_level_extent.z,
                                    format.External, format.Type,
                                    texture.data(layer, face, level));
                        err = glGetError();
                        if (err != GL_NO_ERROR)
                        {
                            VERIFY(err == GL_NO_ERROR);
                            Msg("! OpenGL: 0x%x: Invalid 3D subtexture: '%s'", err, fn);
                        }
                    }
                    break;
                }
                default:
                    NODEFAULT;
                    break;
                }
            }
        }
    }

    FS.r_close(S);

    xr_strlwr(fn);
    ret_desc = target;
    int img_loaded_lod = is_target_cube(texture.target()) ? 0 : get_texture_load_lod(fn);
    ret_msize = calc_texture_size(img_loaded_lod, mip_cnt, img_size);
    return pTexture;
}
} // namespace xray::render::RENDER_NAMESPACE
