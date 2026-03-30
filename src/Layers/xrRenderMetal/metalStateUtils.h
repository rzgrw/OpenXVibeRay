#pragma once

#ifdef USE_METAL

#include <Metal/Metal.hpp>

namespace xray::render::RENDER_NAMESPACE
{
namespace metalStateUtils
{
// D3D9 compare function → Metal compare function
MTL::CompareFunction  ToMTLCompareFunction(u32 func);

// D3D9 blend factor → Metal blend factor
MTL::BlendFactor      ToMTLBlendFactor(u32 blend);

// D3D9 blend operation → Metal blend operation
MTL::BlendOperation   ToMTLBlendOperation(u32 op);

// D3D9 stencil operation → Metal stencil operation
MTL::StencilOperation ToMTLStencilOperation(u32 op);

// D3D9 cull mode → Metal cull mode
MTL::CullMode         ToMTLCullMode(u32 d3dCull);

// D3D9 fill mode → Metal triangle fill mode
MTL::TriangleFillMode ToMTLFillMode(u32 d3dFill);

} // namespace metalStateUtils
} // namespace xray::render::RENDER_NAMESPACE

#endif // USE_METAL
