#pragma once

#include "Layers/xrRender/HWCaps.h"
#include "xrCore/ModuleLookup.hpp"

namespace xray::render::RENDER_NAMESPACE
{
// CHW — Metal hardware abstraction layer.
// Mirrors the interface of glHW / dx11HW for drop-in use by shared render code.
// All members that reference Metal types are void* / uint64_t placeholders until
// the Metal SDK wrappers (Tasks 3-5) are in place.
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

    // Framebuffer handle (placeholder — will become MTLTexture* once Metal is wired up)
    uint64_t pFB{};

    SDL_Window* m_window{};

    // Metal layer / device pointers — typed as void* until metal-cpp headers are used
    void* m_metalLayer{};    // CAMetalLayer*
    void* m_device{};        // MTL::Device*
    void* m_commandQueue{};  // MTL::CommandQueue*

    pcstr AdapterName;
};

extern ECORE_API CHW HW;

} // namespace xray::render::RENDER_NAMESPACE
