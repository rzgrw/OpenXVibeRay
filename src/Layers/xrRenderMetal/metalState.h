#pragma once

// Forward declaration; CommonTypes.h includes D3D9 compat types.
#include "CommonTypes.h"

namespace xray::render::RENDER_NAMESPACE
{
// metalState — placeholder state management class mirroring glState.
// Will hold MTL render pipeline / depth-stencil / sampler state objects
// once the Metal backend is implemented.
class metalState
{
private:
    D3DCULL              rasterizerCullMode;
    D3D_DEPTH_STENCIL_STATE m_pDepthStencilState;
    D3D_BLEND_STATE      m_pBlendState;
    float                m_uiMipLODBias;

public:
    metalState();

    static metalState* Create();

    void Apply();
    void Release();

    void UpdateRenderState(u32 name, u32 value);
    void UpdateSamplerState(u32 stage, u32 name, u32 value);
};

} // namespace xray::render::RENDER_NAMESPACE
