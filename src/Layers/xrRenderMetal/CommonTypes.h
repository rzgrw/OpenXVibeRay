#pragma once

// TODO: Get rid of D3D types.
#if defined(XR_PLATFORM_WINDOWS)
#include <d3d9types.h>
#else
#include "Common/d3d9compat.hpp"
#endif

namespace xray::render::RENDER_NAMESPACE
{
class metalState;

typedef enum D3D_CLEAR_FLAG {
    D3D_CLEAR_DEPTH   = 0x1L,
    D3D_CLEAR_STENCIL = 0x2L
} D3D_CLEAR_FLAG;

typedef enum D3D_COMPARISON_FUNC {
    D3D_COMPARISON_NEVER         = 1,
    D3D_COMPARISON_LESS          = 2,
    D3D_COMPARISON_EQUAL         = 3,
    D3D_COMPARISON_LESS_EQUAL    = 4,
    D3D_COMPARISON_GREATER       = 5,
    D3D_COMPARISON_NOT_EQUAL     = 6,
    D3D_COMPARISON_GREATER_EQUAL = 7,
    D3D_COMPARISON_ALWAYS        = 8
} D3D_COMPARISON_FUNC;

// Viewport struct — integer/float coordinates matching the D3D11 layout.
// Will be bridged to MTLViewport when Metal code is implemented.
struct XR_METAL_VIEWPORT
{
    int   TopLeftX, TopLeftY;
    int   Width, Height;
    float MinDepth, MaxDepth;
};

struct D3D_VIEWPORT : XR_METAL_VIEWPORT
{
    using XR_METAL_VIEWPORT::XR_METAL_VIEWPORT;

    template <typename TopLeftCoords, typename Dimensions>
    D3D_VIEWPORT(TopLeftCoords x, TopLeftCoords y, Dimensions w, Dimensions h, float minZ, float maxZ)
        : XR_METAL_VIEWPORT{
            static_cast<int>(x), static_cast<int>(y),
            static_cast<int>(w), static_cast<int>(h),
            minZ, maxZ,
          }
    {}
};

using D3D_QUERY = enum XR_METAL_QUERY
{
    D3D_QUERY_EVENT,
    D3D_QUERY_OCCLUSION
};

// D3D-style depth/stencil and blend state descriptors.
// Used by metalState and shared render code.
typedef struct
{
    BOOL        DepthEnable;
    BOOL        DepthWriteMask;
    D3DCMPFUNC  DepthFunc;
    BOOL        StencilEnable;
    u32         StencilMask;
    u32         StencilWriteMask;
    D3DSTENCILOP StencilFailOp;
    D3DSTENCILOP StencilDepthFailOp;
    D3DSTENCILOP StencilPassOp;
    D3DCMPFUNC  StencilFunc;
    u32         StencilRef;
} D3D_DEPTH_STENCIL_STATE;

typedef struct
{
    BOOL        BlendEnable;
    D3DBLEND    SrcBlend;
    D3DBLEND    DestBlend;
    D3DBLENDOP  BlendOp;
    D3DBLEND    SrcBlendAlpha;
    D3DBLEND    DestBlendAlpha;
    D3DBLENDOP  BlendOpAlpha;
    u32         ColorMask;
} D3D_BLEND_STATE;

using ID3DState = metalState;

#define DX11_ONLY(expr) do {} while (0)

using unused_t = int[0];

// Buffer handles — placeholder uint64_t until Metal buffer types are defined.
using IndexBufferHandle    = uint64_t;
using VertexBufferHandle   = uint64_t;
using ConstantBufferHandle = uint64_t;
using HostBufferHandle     = void*;

using VertexElement    = D3DVERTEXELEMENT9;
using InputElementDesc = unused_t;

} // namespace xray::render::RENDER_NAMESPACE
