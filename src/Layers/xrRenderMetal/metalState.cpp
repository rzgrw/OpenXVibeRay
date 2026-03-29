// metalState.cpp — stub implementation of Metal render state management.
// Real Metal pipeline state objects will be created here in a later task.

#include "stdafx.h"
#include "metalState.h"

namespace xray::render::RENDER_NAMESPACE
{

metalState::metalState()
{
    rasterizerCullMode = D3DCULL_CCW;

    m_pDepthStencilState.DepthEnable        = TRUE;
    m_pDepthStencilState.DepthFunc          = D3DCMP_LESSEQUAL;
    m_pDepthStencilState.DepthWriteMask     = TRUE;
    m_pDepthStencilState.StencilEnable      = TRUE;
    m_pDepthStencilState.StencilFailOp      = D3DSTENCILOP_KEEP;
    m_pDepthStencilState.StencilDepthFailOp = D3DSTENCILOP_KEEP;
    m_pDepthStencilState.StencilPassOp      = D3DSTENCILOP_KEEP;
    m_pDepthStencilState.StencilFunc        = D3DCMP_ALWAYS;
    m_pDepthStencilState.StencilMask        = 0xFFFFFFFF;
    m_pDepthStencilState.StencilWriteMask   = 0xFFFFFFFF;
    m_pDepthStencilState.StencilRef         = 0;

    m_pBlendState.BlendEnable     = TRUE;
    m_pBlendState.SrcBlend        = D3DBLEND_ONE;
    m_pBlendState.DestBlend       = D3DBLEND_ZERO;
    m_pBlendState.SrcBlendAlpha   = D3DBLEND_ONE;
    m_pBlendState.DestBlendAlpha  = D3DBLEND_ZERO;
    m_pBlendState.BlendOp         = D3DBLENDOP_ADD;
    m_pBlendState.BlendOpAlpha    = D3DBLENDOP_ADD;
    m_pBlendState.ColorMask       = 0xF;

    m_uiMipLODBias = 0.0f;
}

metalState* metalState::Create()
{
    // TODO: Allocate and return a Metal-backed state object
    return new metalState();
}

void metalState::Apply()
{
    // TODO: Encode Metal render/depth-stencil/sampler state onto the current encoder
}

void metalState::Release()
{
    // TODO: Release Metal state objects
    delete this;
}

void metalState::UpdateRenderState(u32 /*name*/, u32 /*value*/)
{
    // TODO: Map D3DRS_ name/value pairs to Metal pipeline descriptor fields
}

void metalState::UpdateSamplerState(u32 /*stage*/, u32 /*name*/, u32 /*value*/)
{
    // TODO: Map D3DSAMP_ name/value pairs to MTLSamplerDescriptor fields
}

} // namespace xray::render::RENDER_NAMESPACE
