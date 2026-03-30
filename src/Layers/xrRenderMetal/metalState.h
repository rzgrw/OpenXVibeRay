#pragma once

// Forward declaration; CommonTypes.h includes D3D9 compat types.
#include "CommonTypes.h"

#if defined(USE_METAL)
#include <Metal/Metal.hpp>
#endif

namespace xray::render::RENDER_NAMESPACE
{

class metalState
{
private:
    D3DCULL              rasterizerCullMode;
    D3D_DEPTH_STENCIL_STATE m_pDepthStencilState;
    D3D_BLEND_STATE      m_pBlendState;
    float                m_uiMipLODBias;

#if defined(USE_METAL)
    // Cached MTL::DepthStencilState — lazily created by GetDepthStencilState().
    MTL::DepthStencilState* m_dssCache = nullptr;
    // Set to true whenever depth/stencil state changes; cleared after DSS creation.
    bool m_dssDirty = true;
#endif

public:
    metalState();

    static metalState* Create();

    void Apply();
    void Release();

    void UpdateRenderState(u32 name, u32 value);
    void UpdateSamplerState(u32 stage, u32 name, u32 value);

#if defined(USE_METAL)
    // Return (or lazily create) the MTL::DepthStencilState matching current settings.
    // The caller does NOT own the returned object — metalState manages its lifetime.
    MTL::DepthStencilState* GetDepthStencilState();

    // FNV-1a hash of the key render-state fields used by the PSO cache.
    // CBackend::Render combines this with shader/vertex-layout hashes to look up
    // or create a MTL::RenderPipelineState.
    u64 GetStateHash() const;

    // Read-only accessors for the blend state — used by PSO creation in CBackend.
    const D3D_BLEND_STATE&        GetBlendState()        const { return m_pBlendState; }
    const D3D_DEPTH_STENCIL_STATE& GetDepthStencilDesc() const { return m_pDepthStencilState; }
    D3DCULL                        GetCullMode()          const { return rasterizerCullMode; }
#endif
};

} // namespace xray::render::RENDER_NAMESPACE
