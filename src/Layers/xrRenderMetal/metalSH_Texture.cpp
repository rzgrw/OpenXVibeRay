// metalSH_Texture.cpp — CTexture implementation for the Metal backend.
// Mirrors glSH_Texture.cpp. pSurface holds an MTL::Texture* stored as
// uint64_t (the shared-code opaque handle contract); pBuffer holds an
// MTL::Buffer* used as CPU staging storage for streaming video textures.

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"

#include "xrEngine/xrTheora_Surface.h"

#define PRIORITY_HIGH   12
#define PRIORITY_NORMAL 8
#define PRIORITY_LOW    4

namespace xray::render::RENDER_NAMESPACE
{
void resptrcode_texture::create(LPCSTR _name)
{
    _set(RImplementation.Resources->_CreateTexture(_name));
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CTexture::CTexture()
{
    pSurface = 0;
    pBuffer = 0;
    pAVI = nullptr;
    pTheora = nullptr;
    desc = u32(MTL::TextureType2D);
    desc_cache = 0;
    seqMSPF = 0;
    flags.MemoryUsage = 0;
    flags.bLoaded = false;
    flags.bUser = false;
    flags.seqCycles = FALSE;
    m_material = 1.0f;
    bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_load);
}

CTexture::~CTexture()
{
    Unload();
    // release external reference
    RImplementation.Resources->_DeleteTexture(this);
}

void CTexture::surface_set(uint64_t surf)
{
    pSurface = surf;
    if (surf)
        desc = u32(reinterpret_cast<MTL::Texture*>(surf)->textureType());
}

uint64_t CTexture::surface_get() const
{
    return pSurface;
}

// Bind the texture to the currently open render command encoder.
// Stage layout follows CTexture::ResourceShaderType (same as OGL):
// [0..rstVertex) = fragment textures, [rstVertex..rstGeometry) = vertex
// textures. Geometry-shader stages don't exist on Metal and are ignored.
static void metal_bind_texture(uint64_t surface, u32 dwStage)
{
    MTL::RenderCommandEncoder* encoder = HW.pCurrentRenderEncoder;
    if (!encoder)
        return; // no render pass open yet (encoder management arrives with the pipeline tasks)

    MTL::Texture* texture = surface ? reinterpret_cast<MTL::Texture*>(surface) : nullptr;

    if (dwStage < CTexture::rstVertex)
        encoder->setFragmentTexture(texture, dwStage);
    else if (dwStage < CTexture::rstGeometry)
        encoder->setVertexTexture(texture, dwStage - CTexture::rstVertex);
}

void CTexture::PostLoad()
{
    if (pTheora) bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_theora);
    else if (pAVI) bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_avi);
    else if (!seqDATA.empty()) bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_seq);
    else bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_normal);
}

void CTexture::apply_load(CBackend& cmd_list, u32 dwStage)
{
    if (!flags.bLoaded) Load();
    else PostLoad();
    bind(cmd_list, dwStage);
};

void CTexture::apply_theora(CBackend& cmd_list, u32 dwStage)
{
    if (pTheora->Update(m_play_time != 0xFFFFFFFF ? m_play_time : Device.dwTimeContinual))
    {
        u32 _w = pTheora->Width(true);
        u32 _h = pTheora->Height(true);

        // Decompress the frame into the shared staging buffer, then copy
        // it into the texture.
        // TODO: replaceRegion is not synchronized against in-flight GPU
        // reads of this texture — revisit once frame pacing is in place.
        MTL::Buffer* staging = reinterpret_cast<MTL::Buffer*>(pBuffer);
        VERIFY(staging);
        u32* pBits = static_cast<u32*>(staging->contents());

        int _pos = 0;
        pTheora->DecompressFrame(pBits, 0, _pos);

        MTL::Texture* texture = reinterpret_cast<MTL::Texture*>(pSurface);
        texture->replaceRegion(MTL::Region::Make2D(0, 0, _w, _h), 0, pBits, _w * 4);
    }

    metal_bind_texture(pSurface, dwStage);
};

void CTexture::apply_avi(CBackend& cmd_list, u32 dwStage) const
{
    // AVI playback is Windows-only (see glSH_Texture.cpp) — just bind.
    metal_bind_texture(pSurface, dwStage);
};

void CTexture::apply_seq(CBackend& cmd_list, u32 dwStage)
{
    // SEQ
    u32 frame = Device.dwTimeContinual / seqMSPF; //Device.dwTimeGlobal
    u32 frame_data = seqDATA.size();
    if (flags.seqCycles)
    {
        u32 frame_id = frame % (frame_data * 2);
        if (frame_id >= frame_data) frame_id = frame_data - 1 - frame_id % frame_data;
        pSurface = seqDATA[frame_id];
    }
    else
    {
        u32 frame_id = frame % frame_data;
        pSurface = seqDATA[frame_id];
    }

    metal_bind_texture(pSurface, dwStage);
};

void CTexture::apply_normal(CBackend& cmd_list, u32 dwStage) const
{
    metal_bind_texture(pSurface, dwStage);
};

void CTexture::Preload()
{
    m_bumpmap = RImplementation.Resources->m_textures_description.GetBumpName(cName);
    m_material = RImplementation.Resources->m_textures_description.GetMaterial(cName);
}

void CTexture::Load()
{
    flags.bLoaded = true;
    desc_cache = 0;
    if (pSurface) return;

    flags.bUser = false;
    flags.MemoryUsage = 0;
    if (nullptr == cName.c_str())
        return;
    if (0 == xr_stricmp(cName.c_str(), "$null")) return;
    // we need to check only the beginning of the string,
    // so let's use strncmp instead of strstr.
    if (0 == strncmp(cName.c_str(), "$user$", sizeof("$user$") - 1))
    {
        flags.bUser = true;
        return;
    }

    ZoneScoped;

    Preload();

    // Check for OGM
    string_path fn;
    if (FS.exist(fn, "$game_textures$", cName.c_str(), ".ogm"))
    {
        // Theora video stream
        pTheora = xr_new<CTheoraSurface>();
        m_play_time = 0xFFFFFFFF;

        if (!pTheora->Load(fn))
        {
            xr_delete(pTheora);
            FATAL("Can't open video stream");
        }
        else
        {
            flags.MemoryUsage = pTheora->Width(true) * pTheora->Height(true) * 4;
            pTheora->Play(TRUE, Device.dwTimeContinual);

            // CPU staging buffer for decompressed frames
            MTL::Buffer* staging =
                HW.pDevice->newBuffer(flags.MemoryUsage, MTL::ResourceStorageModeShared);

            // Now create texture. Theora decodes BGRA-ordered texels
            // (same layout the GL backend uploads as GL_BGRA).
            u32 _w = pTheora->Width(false);
            u32 _h = pTheora->Height(false);

            MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
                MTL::PixelFormatBGRA8Unorm, _w, _h, false);
            td->setStorageMode(MTL::StorageModeShared);
            td->setUsage(MTL::TextureUsageShaderRead);
            MTL::Texture* texture = HW.pDevice->newTexture(td);

            if (!texture || !staging)
            {
                Msg("! Metal: invalid video stream '%s'", fn);
                if (texture) texture->release();
                if (staging) staging->release();
                xr_delete(pTheora);
                pSurface = 0;
                pBuffer = 0;
            }
            else
            {
                texture->setLabel(NS::String::string(cName.c_str(), NS::UTF8StringEncoding));
                pSurface = reinterpret_cast<uint64_t>(texture);
                pBuffer = reinterpret_cast<uint64_t>(staging);
                desc = u32(MTL::TextureType2D);
            }
        }
    }
    else if (FS.exist(fn, "$game_textures$", cName.c_str(), ".avi"))
    {
        // AVI playback is Windows-only (CAviPlayerCustom) — not supported on Metal/macOS.
        Msg("~ Metal: AVI textures are not supported: '%s'", fn);
    }
    else if (FS.exist(fn, "$game_textures$", cName.c_str(), ".seq"))
    {
        // Sequence
        string256 buffer;
        IReader* _fs = FS.r_open(fn);

        flags.seqCycles = FALSE;
        _fs->r_string(buffer, sizeof buffer);
        if (0 == xr_stricmp(buffer, "cycled"))
        {
            flags.seqCycles = TRUE;
            _fs->r_string(buffer, sizeof buffer);
        }
        u32 fps = atoi(buffer);
        seqMSPF = 1000 / fps;

        while (!_fs->eof())
        {
            _fs->r_string(buffer, sizeof buffer);
            _Trim(buffer);
            if (buffer[0])
            {
                // Load another texture
                u32 mem = 0;
                pSurface = RImplementation.texture_load(buffer, mem, desc);
                if (pSurface)
                {
                    seqDATA.push_back(pSurface);
                    flags.MemoryUsage += mem;
                }
            }
        }
        pSurface = 0;
        FS.r_close(_fs);
    }
    else
    {
        // Normal texture
        u32 mem = 0;
        pSurface = RImplementation.texture_load(cName.c_str(), mem, desc);

        // Calc memory usage and preload into vid-mem
        if (pSurface)
        {
            flags.MemoryUsage = mem;
        }
    }

    PostLoad();
}

void CTexture::Unload()
{
    ZoneScoped;
#ifdef DEBUG
    string_path				msg_buff;
    sprintf_s(msg_buff, sizeof(msg_buff), "* Unloading texture [%s] pSurface handle=%llu", cName.c_str(), pSurface);
#endif // DEBUG

    flags.bLoaded = FALSE;
    if (!seqDATA.empty())
    {
        for (uint64_t& frame : seqDATA)
        {
            if (frame)
                reinterpret_cast<MTL::Texture*>(frame)->release();
        }
        seqDATA.clear();
        pSurface = 0;
    }

    // Note: render-target textures reset pSurface to 0 via surface_set()
    // in CRT::destroy() before this runs, so we only ever release
    // surfaces this CTexture owns (created by texture_load / Load).
    if (pSurface)
    {
        reinterpret_cast<MTL::Texture*>(pSurface)->release();
        pSurface = 0;
    }
    if (pBuffer)
    {
        reinterpret_cast<MTL::Buffer*>(pBuffer)->release();
        pBuffer = 0;
    }

    xr_delete(pTheora);

    bind = fastdelegate::FastDelegate2<CBackend&,u32>(this, &CTexture::apply_load);
}

void CTexture::desc_update()
{
    desc_cache = pSurface;
    if (pSurface &&
        (u32(MTL::TextureType2D) == desc || u32(MTL::TextureType2DMultisample) == desc))
    {
        MTL::Texture* texture = reinterpret_cast<MTL::Texture*>(pSurface);
        m_width = u32(texture->width());
        m_height = u32(texture->height());
    }
}

void CTexture::video_Play(BOOL looped, u32 _time)
{
    if (pTheora) pTheora->Play(looped, _time != 0xFFFFFFFF ? (m_play_time = _time) : Device.dwTimeContinual);
}

void CTexture::video_Pause(BOOL state) const
{
    if (pTheora) pTheora->Pause(state);
}

void CTexture::video_Stop() const
{
    if (pTheora) pTheora->Stop();
}

BOOL CTexture::video_IsPlaying() const
{
    return pTheora ? pTheora->IsPlaying() : FALSE;
}
} // namespace xray::render::RENDER_NAMESPACE
