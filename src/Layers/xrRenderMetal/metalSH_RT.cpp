// metalSH_RT.cpp — CRT (render target) implementation for the Metal backend.
// Mirrors glSH_RT.cpp. pRT holds an MTL::Texture* stored as uint64_t.
// Color and depth targets are both plain MTL::Texture objects created with
// RenderTarget|ShaderRead usage; the render pass descriptor decides how a
// texture is attached, so (like GL) pZRT simply aliases pRT.

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/ResourceManager.h"
#include "metalTextureUtils.h"

namespace xray::render::RENDER_NAMESPACE
{
CRT::~CRT()
{
    destroy();

    // release external reference
    RImplementation.Resources->_DeleteRT(this);
}

void CRT::set_slice_read(int slice) {}
void CRT::set_slice_write(u32 context_id, int slice) {}

void CRT::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount /*= 1*/, u32 slices_num /*=1*/, Flags32 /*flags = {}*/)
{
    if (pRT) return;

    R_ASSERT(Name && Name[0] && w && h);
    _order = CPU::QPC(); //Device.GetTimerGlobal()->GetElapsed_clk();

    dwWidth = w;
    dwHeight = h;
    fmt = f;
    sampleCount = SampleCount;
    n_slices = slices_num;

    // Get caps — Metal exposes no direct query; every Apple-silicon GPU
    // (GPUFamilyApple3 and newer) supports 16384, older Macs 8192.
    const u32 max_size =
        HW.pDevice && HW.pDevice->supportsFamily(MTL::GPUFamilyApple3) ? 16384 : 8192;

    // Check width-and-height of render target surface
    if (w > max_size) return;
    if (h > max_size) return;

    RImplementation.Resources->Evict();

    const MTL::PixelFormat mtlFmt = metalTextureUtils::ConvertTextureFormat(fmt);
    if (MTL::PixelFormatInvalid == mtlFmt)
    {
        Msg("! Metal: unsupported render target format %d for '%s'", int(fmt), Name);
        return;
    }

    target = u32((SampleCount > 1) ? MTL::TextureType2DMultisample : MTL::TextureType2D);

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType(target));
    desc->setPixelFormat(mtlFmt);
    desc->setWidth(w);
    desc->setHeight(h);
    desc->setMipmapLevelCount(1);
    if (SampleCount > 1)
        desc->setSampleCount(SampleCount);
    // Render targets live in GPU-private memory; ShaderRead allows sampling
    // them in later passes (gbuffer, bloom, etc.)
    desc->setStorageMode(MTL::StorageModePrivate);
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);

    MTL::Texture* surface = HW.pDevice->newTexture(desc);
    desc->release();

    if (!surface)
    {
        Msg("! Metal: failed to create render target '%s' (%ux%u, fmt=%d, samples=%u)",
            Name, w, h, int(fmt), SampleCount);
        return;
    }

    surface->setLabel(NS::String::string(Name, NS::UTF8StringEncoding));
    pRT = reinterpret_cast<uint64_t>(surface);

    pTexture = RImplementation.Resources->_CreateTexture(Name);
    pTexture->surface_set(pRT);

    // Like OpenGL, Metal doesn't differentiate between color and depth
    // targets at the resource level — the pass descriptor decides.
    pZRT = pRT;
}

void CRT::destroy()
{
    if (pTexture._get())
    {
        // Detach before the ref drops so CTexture::Unload doesn't release
        // a surface it doesn't own.
        pTexture->surface_set(0);
        pTexture = nullptr;
    }
    if (pRT)
    {
        reinterpret_cast<MTL::Texture*>(pRT)->release();
        pRT = 0;
        pZRT = 0;
    }
}

void CRT::reset_begin()
{
    destroy();
}

void CRT::reset_end()
{
    create(cName.c_str(), dwWidth, dwHeight, fmt, sampleCount, n_slices ? n_slices : 1, { dwFlags });
}

void CRT::resolve_into(CRT& destination) const
{
    // TODO Task 17: encode an MSAA resolve (or blit) from this target into
    // `destination` — needs the per-frame command buffer / encoder plumbing.
    VERIFY(destination.pRT);
}

void resptrcode_crt::create(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 SampleCount /*= 1*/, u32 slices_num /*=1*/, Flags32 flags /*= {}*/)
{
    _set(RImplementation.Resources->_CreateRT(Name, w, h, f, SampleCount, 1, flags));
}
} // namespace xray::render::RENDER_NAMESPACE
