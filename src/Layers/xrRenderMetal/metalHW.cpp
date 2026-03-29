// metalHW.cpp — stub implementation of the Metal hardware device.
// Real CAMetalLayer / MTL::Device creation will be implemented in a later task.

#include "stdafx.h"
#pragma hdrstop

#include "metalHW.h"
#include "xrEngine/XR_IOConsole.h"

namespace xray::render::RENDER_NAMESPACE
{
CHW HW;

CHW::CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);

    AdapterName = "Metal (stub)";
}

CHW::~CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

void CHW::CreateDevice(SDL_Window* sdlWnd)
{
    m_window = sdlWnd;
    // TODO: Create CAMetalLayer, MTL::Device, MTL::CommandQueue
    Msg("* Metal device creation not yet implemented (stub)");
}

void CHW::DestroyDevice()
{
    // TODO: Release Metal objects
    m_commandQueue = nullptr;
    m_device       = nullptr;
    m_metalLayer   = nullptr;
    m_window       = nullptr;
}

void CHW::Reset()
{
    // TODO: Recreate swapchain / drawable
}

void CHW::SetPrimaryAttributes(u32& /*windowFlags*/)
{
    // SDL_WINDOW_METAL is set externally when needed; nothing else required here
}

IRender::RenderContext CHW::GetCurrentContext() const
{
    return IRender::RenderContext::NoContext;
}

int CHW::MakeContextCurrent(IRender::RenderContext /*context*/) const
{
    return 0;
}

// static
std::pair<u32, u32> CHW::GetSurfaceSize()
{
    // TODO: Query the actual drawable size from CAMetalLayer
    return { 1280, 720 };
}

DeviceState CHW::GetDeviceState() const
{
    return DeviceState::Normal;
}

void CHW::BeginScene()
{
    // TODO: Acquire next drawable, create command buffer
}

void CHW::EndScene()
{
    // TODO: Commit command buffer
}

void CHW::Present()
{
    // TODO: Present drawable
}

void CHW::OnAppActivate()
{
}

void CHW::OnAppDeactivate()
{
}

void CHW::UpdateViews()
{
}

bool CHW::ThisInstanceIsGlobal() const
{
    return this == &HW;
}

void CHW::BeginPixEvent(pcstr /*name*/) const
{
    // TODO: MTL::CaptureManager / signpost
}

void CHW::EndPixEvent() const
{
}

} // namespace xray::render::RENDER_NAMESPACE
