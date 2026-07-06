// metalHW.cpp — implementation of the Metal hardware device.
// Uses metal-cpp C++ wrappers for the Metal API.
// (The metal-cpp PRIVATE_IMPLEMENTATION TU is metal_cpp_impl.cpp — it needs
// exemption from unity builds AND PCH, which this file doesn't have.)

#include "stdafx.h"
#pragma hdrstop

#include "metalHW.h"
#include "metalRenderPassManager.h"
#include "metalOcclusionQuery.h"
#include "metalScreenshot.h"
#include "xrEngine/XR_IOConsole.h"

#include <SDL_metal.h>

namespace xray::render::RENDER_NAMESPACE
{
CHW HW;

CHW::CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);
}

CHW::~CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

void CHW::OnAppActivate()
{
    if (m_window)
        SDL_RestoreWindow(m_window);
}

void CHW::OnAppDeactivate()
{
    if (m_window)
    {
        if (psDeviceMode.WindowStyle == rsFullscreen || psDeviceMode.WindowStyle == rsFullscreenBorderless)
            SDL_MinimizeWindow(m_window);
    }
}

//////////////////////////////////////////////////////////////////////
// Device creation / destruction
//////////////////////////////////////////////////////////////////////
void CHW::CreateDevice(SDL_Window* sdlWnd)
{
    ZoneScoped;

    m_window = sdlWnd;
    R_ASSERT(m_window);

    // Create the Metal device
    pDevice = MTL::CreateSystemDefaultDevice();
    if (!pDevice)
    {
        Msg("! Metal: MTL::CreateSystemDefaultDevice() returned nullptr");
        return;
    }

    AdapterName = pDevice->name()->utf8String();
    Msg("* Metal device: [%s]", AdapterName);

    // Create the command queue
    pCommandQueue = pDevice->newCommandQueue();
    if (!pCommandQueue)
    {
        Msg("! Metal: failed to create command queue");
        return;
    }

    // Create the CAMetalLayer-backed view via SDL
    m_metalView = SDL_Metal_CreateView(m_window);
    if (!m_metalView)
    {
        Msg("! Metal: SDL_Metal_CreateView() failed: %s", SDL_GetError());
        return;
    }

    // SDL_Metal_GetLayer returns the CAMetalLayer* (as void*) from the SDL_MetalView
    pMetalLayer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(m_metalView));
    if (!pMetalLayer)
    {
        Msg("! Metal: SDL_Metal_GetLayer() returned nullptr");
        return;
    }

    // Configure the layer
    pMetalLayer->setDevice(pDevice);
    pMetalLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // framebufferOnly forbids using the drawable as a blit source, which the
    // screenshot readback (metalr_screenshot.cpp) requires.  The bandwidth
    // cost of the non-framebufferOnly path is negligible on Apple Silicon.
    pMetalLayer->setFramebufferOnly(false);

    Caps.fTarget = D3DFMT_A8R8G8B8;
    Caps.fDepth  = D3DFMT_D24S8;

    // Shader profiles: ShaderTypeTraits hands these to CRender::shader_compile
    // as pTarget, which dispatches on the first character ('v'/'p') — same
    // convention as the GL backend (glHWCaps.cpp).
    Caps.geometry_profile = "vs_4_0";
    Caps.raster_profile = "ps_4_0";

    BackBufferCount = 1;

    RPManager.OnDeviceCreate();
}

void CHW::DestroyDevice()
{
    // Tear down pass/PSO/occlusion state while the device is still alive
    OcclusionQueries.Destroy();
    RPManager.OnDeviceDestroy();

    // Discard any in-flight frame
    if (pCurrentDrawable)
    {
        pCurrentDrawable->release();
        pCurrentDrawable = nullptr;
    }
    if (pCurrentCommandBuffer)
    {
        pCurrentCommandBuffer->release();
        pCurrentCommandBuffer = nullptr;
    }

    // Release Metal objects (newXxx / CreateXxx objects are owned by us)
    if (pCommandQueue)
    {
        pCommandQueue->release();
        pCommandQueue = nullptr;
    }
    if (pDevice)
    {
        pDevice->release();
        pDevice = nullptr;
    }

    // Destroy the SDL Metal view (must be done before destroying the window)
    if (m_metalView)
    {
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(m_metalView));
        m_metalView   = nullptr;
        pMetalLayer   = nullptr;  // owned by the view; don't release separately
    }

    m_window = nullptr;
}

//////////////////////////////////////////////////////////////////////
// Reset / present
//////////////////////////////////////////////////////////////////////
void CHW::Reset()
{
    ZoneScoped;
    // CAMetalLayer automatically tracks the window size; nothing to recreate.
}

void CHW::SetPrimaryAttributes(u32& windowFlags)
{
    windowFlags |= SDL_WINDOW_METAL;
}

IRender::RenderContext CHW::GetCurrentContext() const
{
    // Metal has no thread-bound context; always report primary.
    return IRender::PrimaryContext;
}

int CHW::MakeContextCurrent(IRender::RenderContext /*context*/) const
{
    // No-op — Metal command buffers are not bound to a thread context.
    return 0;
}

std::pair<u32, u32> CHW::GetSurfaceSize()
{
    if (HW.pMetalLayer)
    {
        const CGSize sz = HW.pMetalLayer->drawableSize();
        return { static_cast<u32>(sz.width), static_cast<u32>(sz.height) };
    }
    return { psDeviceMode.Width, psDeviceMode.Height };
}

DeviceState CHW::GetDeviceState() const
{
    return DeviceState::Normal;
}

//////////////////////////////////////////////////////////////////////
// Per-frame begin / end / present
//////////////////////////////////////////////////////////////////////
void CHW::BeginScene()
{
    if (!pCommandQueue)
        return;

    pCurrentCommandBuffer = pCommandQueue->commandBuffer();
    if (pMetalLayer)
        pCurrentDrawable = pMetalLayer->nextDrawable();

    // Recycle the transient constant-buffer ring and drop pass state cached
    // against the previous frame's drawable (Task 19 frame-lifecycle contract).
    RPManager.OnFrameBegin();
}

void CHW::EndScene()
{
    if (!pCurrentCommandBuffer)
        return;

    // End the open encoder and flush unconsumed pending clears — a command
    // buffer must not be committed while an encoder is recording.
    RPManager.OnFrameEnd();

    // rt_Base is a plain offscreen texture (metalSH_RT has no swapchain
    // wiring): copy the final frame into the CAMetalDrawable before the
    // command buffer is committed.  The GL backend does the equivalent in
    // CHW::Present via glBlitFramebuffer — see
    // metal_rendertarget_phase_flip.cpp.
    if (RImplementation.Target)
        RImplementation.Target->phase_flip();

    // Deferred screenshot: the drawable now holds the finished frame. Record the
    // readback blit on this still-open command buffer, then (below) wait for it.
    const bool capturing = MetalScreenshot_Pending();
    if (capturing)
        MetalScreenshot_RecordBlit(pCurrentCommandBuffer, pCurrentDrawable, pDevice);

    pCurrentCommandBuffer->commit();
    if (capturing)
    {
        pCurrentCommandBuffer->waitUntilCompleted(); // screenshot frame only — not the steady path
        MetalScreenshot_Finalize();
    }
    pCurrentCommandBuffer->release();
    pCurrentCommandBuffer = nullptr;
}

void CHW::Present()
{
    // The frame image was already copied into the drawable by
    // CRenderTarget::phase_flip() (called from EndScene) — just present.
    if (pCurrentDrawable)
    {
        pCurrentDrawable->present();
        pCurrentDrawable->release();
        pCurrentDrawable = nullptr;
    }

    CurrentBackBuffer = (CurrentBackBuffer + 1) % BackBufferCount;
}

//////////////////////////////////////////////////////////////////////
// Debug markers
//////////////////////////////////////////////////////////////////////
void CHW::BeginPixEvent(pcstr name) const
{
    if (pCurrentCommandBuffer)
        pCurrentCommandBuffer->pushDebugGroup(
            NS::String::string(name, NS::UTF8StringEncoding));
}

void CHW::EndPixEvent() const
{
    if (pCurrentCommandBuffer)
        pCurrentCommandBuffer->popDebugGroup();
}

//////////////////////////////////////////////////////////////////////
// Private helpers
//////////////////////////////////////////////////////////////////////
void CHW::UpdateViews()
{
    // Nothing to do for Metal — layer manages the drawable surface.
}

bool CHW::ThisInstanceIsGlobal() const
{
    return this == &HW;
}

} // namespace xray::render::RENDER_NAMESPACE
