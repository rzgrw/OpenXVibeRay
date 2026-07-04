// metalHW.cpp — implementation of the Metal hardware device.
// Uses metal-cpp C++ wrappers for the Metal API.
// The PRIVATE_IMPLEMENTATION macros must be defined in exactly one TU.

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include "stdafx.h"
#pragma hdrstop

#include "metalHW.h"
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
    pMetalLayer->setFramebufferOnly(true);

    Caps.fTarget = D3DFMT_A8R8G8B8;
    Caps.fDepth  = D3DFMT_D24S8;

    // Shader profiles: ShaderTypeTraits hands these to CRender::shader_compile
    // as pTarget, which dispatches on the first character ('v'/'p') — same
    // convention as the GL backend (glHWCaps.cpp).
    Caps.geometry_profile = "vs_4_0";
    Caps.raster_profile = "ps_4_0";

    BackBufferCount = 1;
}

void CHW::DestroyDevice()
{
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
}

void CHW::EndScene()
{
    if (!pCurrentCommandBuffer)
        return;

    pCurrentCommandBuffer->commit();
    pCurrentCommandBuffer->release();
    pCurrentCommandBuffer = nullptr;
}

void CHW::Present()
{
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
