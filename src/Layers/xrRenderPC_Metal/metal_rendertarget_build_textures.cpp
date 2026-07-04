// metal_rendertarget_build_textures.cpp — engine-generated lookup textures
// for the Metal backend.  Mirrors gl_rendertarget_build_textures.cpp:
//  - material table   (3D, RG8      — GL_RG8)
//  - jitter/noise set (2D, RGBA8    — GL_RGBA8)
//  - HBAO jitter      (2D, RGBA32F  — GL_RGBA32F)
//  - mipped noise     (2D, RGBA8)
//
// The G-buffer / accum / bloom / SSAO / luminance / smap render targets are
// NOT created here: like GL, they are created by the shared constructor in
// r2_rendertarget.cpp via ref_rt.create(), which dispatches to CRT::create in
// metalSH_RT.cpp (D3DFMT_* formats are mapped by metalTextureUtils —
// D24S8/D24X8 become Depth32Float_Stencil8, Apple Silicon has no 24-bit depth).
//
// CPU-filled, sampled-only textures use StorageModeShared (unified memory on
// Apple Silicon) so replaceRegion can write them directly without a blit.

#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
static void generate_jitter(u32* dest, u32 elem_count)
{
    const int cmax = 8;
    svector<Ivector2, cmax> samples;
    while (samples.size() < elem_count * 2)
    {
        Ivector2 test;
        test.set(Random.randI(0, 256), Random.randI(0, 256));
        BOOL valid = TRUE;
        for (auto& sample : samples)
        {
            int dist = _abs(test.x - sample.x) + _abs(test.y - sample.y);
            if (dist < 32)
            {
                valid = FALSE;
                break;
            }
        }
        if (valid)
            samples.push_back(test);
    }
    for (u32 it = 0; it < elem_count; it++, dest++)
        *dest = color_rgba(samples[2 * it].x, samples[2 * it].y, samples[2 * it + 1].y, samples[2 * it + 1].x);
}

// Creates a CPU-fillable, sampled-only 2D texture and returns it as the
// uint64_t opaque handle used throughout the Metal backend.
static uint64_t create_texture_2d(pcstr name, MTL::PixelFormat fmt, u32 w, u32 h)
{
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2D);
    desc->setPixelFormat(fmt);
    desc->setWidth(w);
    desc->setHeight(h);
    desc->setMipmapLevelCount(1);
    desc->setStorageMode(MTL::StorageModeShared);
    desc->setUsage(MTL::TextureUsageShaderRead);

    MTL::Texture* texture = HW.pDevice->newTexture(desc);
    desc->release();
    R_ASSERT2(texture, name);

    texture->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
    return reinterpret_cast<uint64_t>(texture);
}

void CRenderTarget::build_textures()
{
    // Build material(s)
    {
        // Surface (GL_RG8 -> RG8Unorm; addr: x=dot(L,N), y=dot(L,H), z=material)
        MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
        desc->setTextureType(MTL::TextureType3D);
        desc->setPixelFormat(MTL::PixelFormatRG8Unorm);
        desc->setWidth(TEX_material_LdotN);
        desc->setHeight(TEX_material_LdotH);
        desc->setDepth(TEX_material_Count);
        desc->setMipmapLevelCount(1);
        desc->setStorageMode(MTL::StorageModeShared);
        desc->setUsage(MTL::TextureUsageShaderRead);

        MTL::Texture* material = HW.pDevice->newTexture(desc);
        desc->release();
        R_ASSERT2(material, r2_material);
        material->setLabel(NS::String::string(r2_material, NS::UTF8StringEncoding));

        t_material_surf = reinterpret_cast<uint64_t>(material);
        t_material = RImplementation.Resources->_CreateTexture(r2_material);
        t_material->surface_set(t_material_surf);

        // Fill it (addr: x=dot(L,N),y=dot(L,H))
        static constexpr u32 RowPitch = TEX_material_LdotN * 2;
        static constexpr u32 SlicePitch = TEX_material_LdotH * RowPitch;
        u16 pBits[TEX_material_LdotN * TEX_material_LdotH * TEX_material_Count];
        for (u32 slice = 0; slice < TEX_material_Count; slice++)
        {
            for (u32 y = 0; y < TEX_material_LdotH; y++)
            {
                for (u32 x = 0; x < TEX_material_LdotN; x++)
                {
                    u16* p = (u16*)((u8*)(pBits)+slice * SlicePitch +
                        y * RowPitch + x * 2);
                    float ld = float(x) / float(TEX_material_LdotN - 1);
                    float ls = float(y) / float(TEX_material_LdotH - 1) + EPS_S;
                    ls *= powf(ld, 1 / 32.f);
                    float fd, fs;

                    switch (slice)
                    {
                    case 0:
                    { // looks like OrenNayar
                        fd = powf(ld, 0.75f); // 0.75
                        fs = powf(ls, 16.f) * .5f;
                    }
                    break;
                    case 1:
                    { // looks like Blinn
                        fd = powf(ld, 0.90f); // 0.90
                        fs = powf(ls, 24.f);
                    }
                    break;
                    case 2:
                    { // looks like Phong
                        fd = ld; // 1.0
                        fs = powf(ls * 1.01f, 128.f);
                    }
                    break;
                    case 3:
                    { // looks like Metal
                        float s0 = _abs(1 - _abs(0.05f * _sin(33.f * ld) + ld - ls));
                        float s1 = _abs(1 - _abs(0.05f * _cos(33.f * ld * ls) + ld - ls));
                        float s2 = _abs(1 - _abs(ld - ls));
                        fd = ld; // 1.0
                        fs = powf(_max(_max(s0, s1), s2), 24.f);
                        fs *= powf(ld, 1 / 7.f);
                    }
                    break;
                    default: fd = fs = 0;
                    }
                    s32 _d = clampr(iFloor(fd * 255.5f), 0, 255);
                    s32 _s = clampr(iFloor(fs * 255.5f), 0, 255);
                    if (y == (TEX_material_LdotH - 1) && x == (TEX_material_LdotN - 1))
                    {
                        _d = 255;
                        _s = 255;
                    }
                    *p = u16(_s * 256 + _d);
                }
            }
        }
        material->replaceRegion(
            MTL::Region::Make3D(0, 0, 0, TEX_material_LdotN, TEX_material_LdotH, TEX_material_Count),
            0, 0, pBits, RowPitch, SlicePitch);
    }

    // Build noise table
    if (true)
    {
        static const int sampleSize = 4;
        u32 tempData[TEX_jitter_count][TEX_jitter * TEX_jitter];

        // Surfaces (raw bytes uploaded as-is, matching the GL_RGBA/GL_UNSIGNED_BYTE upload path)
        for (u32 it1 = 0; it1 < TEX_jitter_count - 1; it1++)
        {
            string_path name;
            xr_sprintf(name, "%s%d", r2_jitter, it1);
            t_noise_surf[it1] = create_texture_2d(name, MTL::PixelFormatRGBA8Unorm, TEX_jitter, TEX_jitter);
            t_noise[it1] = RImplementation.Resources->_CreateTexture(name);
            t_noise[it1]->surface_set(t_noise_surf[it1]);
        }

        // Fill it,
        static const u32 Pitch = TEX_jitter * sampleSize;
        for (u32 y = 0; y < TEX_jitter; y++)
        {
            for (u32 x = 0; x < TEX_jitter; x++)
            {
                u32 data[TEX_jitter_count - 1];
                generate_jitter(data, TEX_jitter_count - 1);
                for (u32 it2 = 0; it2 < TEX_jitter_count - 1; it2++)
                {
                    u32* p = (u32*)((u8*)(tempData[it2]) + y * Pitch + x * 4);
                    *p = data[it2];
                }
            }
        }
        u32 it3 = 0;
        while (it3 < TEX_jitter_count - 1)
        {
            reinterpret_cast<MTL::Texture*>(t_noise_surf[it3])->replaceRegion(
                MTL::Region::Make2D(0, 0, TEX_jitter, TEX_jitter), 0, tempData[it3], Pitch);
            it3++;
        }

        float tempDataHBAO[TEX_jitter * TEX_jitter * 4];

        // generate HBAO jitter texture (last)
        int it = TEX_jitter_count - 1;
        string_path name;
        xr_sprintf(name, "%s%d", r2_jitter, it);
        t_noise_surf[it] = create_texture_2d(name, MTL::PixelFormatRGBA32Float, TEX_jitter, TEX_jitter);
        t_noise[it] = RImplementation.Resources->_CreateTexture(name);
        t_noise[it]->surface_set(t_noise_surf[it]);

        // Fill it,
        static const int HBAOPitch = TEX_jitter * sampleSize * sizeof(float);
        for (u32 y = 0; y < TEX_jitter; y++)
        {
            for (u32 x = 0; x < TEX_jitter; x++)
            {
                float numDir = 1.0f;
                switch (ps_r_ssao)
                {
                case 1: numDir = 4.0f; break;
                case 2: numDir = 6.0f; break;
                case 3: numDir = 8.0f; break;
                }
                float angle = 2 * PI * Random.randF(0.0f, 1.0f) / numDir;
                float dist = Random.randF(0.0f, 1.0f);

                float* p =
                    (float*)((u8*)(tempDataHBAO)+y * HBAOPitch + x * 4 * sizeof(float));
                *p = (float)_cos(angle);
                *(p + 1) = (float)_sin(angle);
                *(p + 2) = (float)dist;
                *(p + 3) = 0;
            }
        }
        reinterpret_cast<MTL::Texture*>(t_noise_surf[it3])->replaceRegion(
            MTL::Region::Make2D(0, 0, TEX_jitter, TEX_jitter), 0, tempDataHBAO, HBAOPitch);

        //	Create noise mipped
        {
            // Note: like GL (glTexStorage2D with 1 level), only the base level
            // exists, so there are no mips to generate.
            t_noise_surf_mipped = create_texture_2d(r2_jitter_mipped, MTL::PixelFormatRGBA8Unorm, TEX_jitter, TEX_jitter);
            t_noise_mipped = RImplementation.Resources->_CreateTexture(r2_jitter_mipped);
            t_noise_mipped->surface_set(t_noise_surf_mipped);

            //	Update texture.
            reinterpret_cast<MTL::Texture*>(t_noise_surf_mipped)->replaceRegion(
                MTL::Region::Make2D(0, 0, TEX_jitter, TEX_jitter), 0, tempData[0], Pitch);
        }
    }
}
} // namespace xray::render::RENDER_NAMESPACE
