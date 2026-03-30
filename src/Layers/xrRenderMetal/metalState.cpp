// metalState.cpp — Metal render state management.
// Stores D3D9-style render state values and creates MTL::DepthStencilState
// objects on demand.  A hash of the current state is provided so that
// CBackend can look up (or create) the matching MTL::RenderPipelineState.

#include "stdafx.h"
#include "metalState.h"

#if defined(USE_METAL)
#include "metalStateUtils.h"
#include "metalHW.h"
#endif

namespace xray::render::RENDER_NAMESPACE
{

// ---------------------------------------------------------------------------
// Constructor — mirror defaults from glState so shared render code sees the
// same initial state regardless of backend.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------

metalState* metalState::Create()
{
    return new metalState();
}

// ---------------------------------------------------------------------------
// Apply — in Metal, render state is not applied imperatively; it is baked into
// PSOs.  Mark the depth/stencil cache dirty so that the next draw call will
// pick up any pending changes through GetDepthStencilState().
// ---------------------------------------------------------------------------
void metalState::Apply()
{
#if defined(USE_METAL)
    m_dssDirty = true;
#endif
}

// ---------------------------------------------------------------------------

void metalState::Release()
{
#if defined(USE_METAL)
    if (m_dssCache)
    {
        m_dssCache->release();
        m_dssCache = nullptr;
    }
#endif
    delete this;
}

// ---------------------------------------------------------------------------

void metalState::UpdateRenderState(u32 name, u32 value)
{
    switch (name)
    {
    case D3DRS_CULLMODE:
        rasterizerCullMode = (D3DCULL)value;
        break;

    case D3DRS_ZENABLE:
        m_pDepthStencilState.DepthEnable = value ? TRUE : FALSE;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_ZWRITEENABLE:
        m_pDepthStencilState.DepthWriteMask = value ? TRUE : FALSE;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_ZFUNC:
        m_pDepthStencilState.DepthFunc = (D3DCMPFUNC)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILENABLE:
        m_pDepthStencilState.StencilEnable = value ? TRUE : FALSE;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILMASK:
        m_pDepthStencilState.StencilMask = (u32)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILWRITEMASK:
        m_pDepthStencilState.StencilWriteMask = (u32)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILFAIL:
        m_pDepthStencilState.StencilFailOp = (D3DSTENCILOP)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILZFAIL:
        m_pDepthStencilState.StencilDepthFailOp = (D3DSTENCILOP)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILPASS:
        m_pDepthStencilState.StencilPassOp = (D3DSTENCILOP)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILFUNC:
        m_pDepthStencilState.StencilFunc = (D3DCMPFUNC)value;
#if defined(USE_METAL)
        m_dssDirty = true;
#endif
        break;

    case D3DRS_STENCILREF:
        m_pDepthStencilState.StencilRef = value;
        // StencilRef is set on the encoder at draw time, not baked into DSS.
        break;

    case D3DRS_SRCBLEND:
        m_pBlendState.SrcBlend = (D3DBLEND)value;
        break;

    case D3DRS_DESTBLEND:
        m_pBlendState.DestBlend = (D3DBLEND)value;
        break;

    case D3DRS_BLENDOP:
        m_pBlendState.BlendOp = (D3DBLENDOP)value;
        break;

    case D3DRS_SRCBLENDALPHA:
        m_pBlendState.SrcBlendAlpha = (D3DBLEND)value;
        break;

    case D3DRS_DESTBLENDALPHA:
        m_pBlendState.DestBlendAlpha = (D3DBLEND)value;
        break;

    case D3DRS_BLENDOPALPHA:
        m_pBlendState.BlendOpAlpha = (D3DBLENDOP)value;
        break;

    case D3DRS_ALPHABLENDENABLE:
        m_pBlendState.BlendEnable = value ? TRUE : FALSE;
        break;

    case D3DRS_COLORWRITEENABLE:
    case D3DRS_COLORWRITEENABLE1:
    case D3DRS_COLORWRITEENABLE2:
    case D3DRS_COLORWRITEENABLE3:
        m_pBlendState.ColorMask = (u32)value;
        break;

    // Deprecated D3D9 states — ignored
    case D3DRS_LIGHTING:
    case D3DRS_FOGENABLE:
    case D3DRS_ALPHATESTENABLE:
    case D3DRS_ALPHAREF:
        break;

    default:
        VERIFY(!"metalState::UpdateRenderState — render state not implemented");
        break;
    }
}

// ---------------------------------------------------------------------------

void metalState::UpdateSamplerState(u32 /*stage*/, u32 /*name*/, u32 /*value*/)
{
    // Sampler state is handled via MTL::SamplerState objects created by the
    // texture / sampler cache (Task 11).  Nothing to store here for now.
}

// ---------------------------------------------------------------------------
// GetDepthStencilState — lazily create (or return cached) MTL::DepthStencilState
// from the current D3D9 depth/stencil descriptor.
// ---------------------------------------------------------------------------
#if defined(USE_METAL)
MTL::DepthStencilState* metalState::GetDepthStencilState()
{
    if (!m_dssDirty && m_dssCache)
        return m_dssCache;

    auto* desc = MTL::DepthStencilDescriptor::alloc()->init();
    VERIFY(desc);

    // Depth
    if (m_pDepthStencilState.DepthEnable)
    {
        desc->setDepthCompareFunction(
            metalStateUtils::ToMTLCompareFunction(m_pDepthStencilState.DepthFunc));
        desc->setDepthWriteEnabled(m_pDepthStencilState.DepthWriteMask != 0);
    }
    else
    {
        desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
        desc->setDepthWriteEnabled(false);
    }

    // Stencil (front and back — D3D9 uses a single face config)
    if (m_pDepthStencilState.StencilEnable)
    {
        auto* stencil = MTL::StencilDescriptor::alloc()->init();
        VERIFY(stencil);

        stencil->setStencilCompareFunction(
            metalStateUtils::ToMTLCompareFunction(m_pDepthStencilState.StencilFunc));
        stencil->setStencilFailureOperation(
            metalStateUtils::ToMTLStencilOperation(m_pDepthStencilState.StencilFailOp));
        stencil->setDepthFailureOperation(
            metalStateUtils::ToMTLStencilOperation(m_pDepthStencilState.StencilDepthFailOp));
        stencil->setDepthStencilPassOperation(
            metalStateUtils::ToMTLStencilOperation(m_pDepthStencilState.StencilPassOp));
        stencil->setReadMask(m_pDepthStencilState.StencilMask);
        stencil->setWriteMask(m_pDepthStencilState.StencilWriteMask);

        desc->setFrontFaceStencil(stencil);
        desc->setBackFaceStencil(stencil);
        stencil->release();
    }
    else
    {
        desc->setFrontFaceStencil(nullptr);
        desc->setBackFaceStencil(nullptr);
    }

    if (m_dssCache)
        m_dssCache->release();

    m_dssCache = HW.pDevice->newDepthStencilState(desc);
    desc->release();

    VERIFY(m_dssCache);
    m_dssDirty = false;
    return m_dssCache;
}

// ---------------------------------------------------------------------------
// GetStateHash — FNV-1a 64-bit hash over the key render-state fields.
// Used by CBackend to build a PSO cache key.
// ---------------------------------------------------------------------------
u64 metalState::GetStateHash() const
{
    // FNV-1a 64-bit constants
    constexpr u64 FNV_OFFSET = 14695981039346656037ULL;
    constexpr u64 FNV_PRIME  = 1099511628211ULL;

    u64 hash = FNV_OFFSET;

    auto mix = [&](u32 v)
    {
        const u8* bytes = reinterpret_cast<const u8*>(&v);
        for (int i = 0; i < 4; ++i)
        {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
    };

    // Depth/stencil fields
    mix((u32)m_pDepthStencilState.DepthEnable);
    mix((u32)m_pDepthStencilState.DepthWriteMask);
    mix((u32)m_pDepthStencilState.DepthFunc);
    mix((u32)m_pDepthStencilState.StencilEnable);
    mix(m_pDepthStencilState.StencilMask);
    mix(m_pDepthStencilState.StencilWriteMask);
    mix((u32)m_pDepthStencilState.StencilFailOp);
    mix((u32)m_pDepthStencilState.StencilDepthFailOp);
    mix((u32)m_pDepthStencilState.StencilPassOp);
    mix((u32)m_pDepthStencilState.StencilFunc);

    // Blend fields
    mix((u32)m_pBlendState.BlendEnable);
    mix((u32)m_pBlendState.SrcBlend);
    mix((u32)m_pBlendState.DestBlend);
    mix((u32)m_pBlendState.BlendOp);
    mix((u32)m_pBlendState.SrcBlendAlpha);
    mix((u32)m_pBlendState.DestBlendAlpha);
    mix((u32)m_pBlendState.BlendOpAlpha);
    mix(m_pBlendState.ColorMask);

    // Rasterizer fields
    mix((u32)rasterizerCullMode);

    return hash;
}
#endif // USE_METAL

} // namespace xray::render::RENDER_NAMESPACE
