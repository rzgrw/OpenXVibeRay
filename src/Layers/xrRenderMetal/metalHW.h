#pragma once

#include "Layers/xrRender/HWCaps.h"
#include "xrCore/ModuleLookup.hpp"

#if defined(USE_METAL)
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#endif

namespace xray::render::RENDER_NAMESPACE
{
// CHW — Metal hardware abstraction layer.
// Mirrors the interface of glHW / dx11HW for drop-in use by shared render code.
class CHW
    : public pureAppActivate,
      public pureAppDeactivate
{
public:
    CHW();
    ~CHW();

    void CreateDevice(SDL_Window* sdlWnd);
    void DestroyDevice();

    void Reset();

    void SetPrimaryAttributes(u32& windowFlags);

    IRender::RenderContext GetCurrentContext() const;
    int MakeContextCurrent(IRender::RenderContext context) const;

    static std::pair<u32, u32> GetSurfaceSize();
    DeviceState GetDeviceState() const;

public:
    void BeginScene();
    void EndScene();
    void Present();

public:
    void OnAppActivate()   override;
    void OnAppDeactivate() override;

private:
    void UpdateViews();
    bool ThisInstanceIsGlobal() const;

public:
    void BeginPixEvent(pcstr name) const;
    void EndPixEvent()             const;

public:
    static constexpr auto IMM_CTX_ID = 0;

    CHWCaps Caps;

    u32 BackBufferCount{};
    u32 CurrentBackBuffer{};

    SDL_Window* m_window{};

    // Metal objects
#if defined(USE_METAL)
    MTL::Device*           pDevice        = nullptr;
    MTL::CommandQueue*     pCommandQueue  = nullptr;
    CA::MetalLayer*        pMetalLayer    = nullptr;  // backed by SDL_Metal_CreateView
    MTL::CommandBuffer*    pCurrentCommandBuffer = nullptr;
    CA::MetalDrawable*     pCurrentDrawable      = nullptr;
    // Render command encoder for the currently open render pass.
    // Owned/managed by the render-pass code (CBackend); consumers
    // (e.g. CTexture::apply_*) must tolerate nullptr.
    MTL::RenderCommandEncoder* pCurrentRenderEncoder = nullptr;
#else
    void* pDevice        = nullptr;
    void* pCommandQueue  = nullptr;
    void* pMetalLayer    = nullptr;
    void* pCurrentRenderEncoder = nullptr;
#endif

    // SDL_MetalView handle (NSView* on macOS) — retained so we can destroy it
    void* m_metalView{};  // SDL_MetalView

    pcstr AdapterName;
};

extern ECORE_API CHW HW;

} // namespace xray::render::RENDER_NAMESPACE
