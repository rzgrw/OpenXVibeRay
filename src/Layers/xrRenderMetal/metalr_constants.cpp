#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRender/r_constants.h"
#include "Layers/xrRenderMetal/metalConstantBuffer.h"

namespace xray::render::RENDER_NAMESPACE
{
// ===========================================================================
// R_constant_table::parse — registers the constants of one shader stage.
//
// `_desc` is a MetalShaderReflection* produced by the Metal shader pipeline
// (plan Task 16 — SPIRV-Cross MSL reflection); `destination` is the stage
// (RC_dest_vertex or RC_dest_pixel).  Mirrors glr_constants.cpp structurally.
//
// Samplers: recorded with samp.index = texture slot.  Unlike GL there is no
// uniform to poke (Metal binds textures by slot via setFragmentTexture /
// setVertexTexture in CTexture::apply, plan Task 13), so handler stays null.
// ===========================================================================
BOOL R_constant_table::parse(void* _desc, u32 destination)
{
    VERIFY(_desc);
    const MetalShaderReflection& reflection = *static_cast<const MetalShaderReflection*>(_desc);

    VERIFY2(reflection.blockSize <= R_constants::kStageCapacity,
        "Metal: shader uniform block exceeds R_constants::kStageCapacity");

    if (destination & RC_dest_vertex)
        mtl_vs_block_size = std::max(mtl_vs_block_size, reflection.blockSize);
    if (destination & RC_dest_pixel)
        mtl_ps_block_size = std::max(mtl_ps_block_size, reflection.blockSize);

    for (const MetalConstantReflection& rc : reflection.constants)
    {
        if (RC_sampler == rc.type)
        {
            // ***Register sampler***
            ref_constant C = get(rc.name);
            if (!C)
            {
                C = table.emplace_back(xr_new<R_constant>());
                C->name = rc.name;
                C->destination = RC_dest_sampler;
                C->type = RC_sampler;
                C->handler = nullptr; // bound by slot, no setup needed on Metal
                R_constant_load& L = C->samp;
                L.index = u16(rc.offset); // texture slot
                L.cls = RC_sampler;
                L.location = 0;
                L.program = 0;
            }
            else
            {
                R_ASSERT(C->destination == RC_dest_sampler);
                R_ASSERT(C->type == RC_sampler);
                R_ASSERT(C->samp.index == u16(rc.offset));
            }
            continue;
        }

        // ***Register numeric constant***
        ref_constant C = get(rc.name);
        if (!C)
        {
            C = table.emplace_back(xr_new<R_constant>());
            C->name = rc.name;
            C->destination = destination;
            C->type = rc.type;
            R_constant_load& L = C->get_load(destination);
            L.index = u16(rc.offset >> 4); // register line, informational only
            L.cls = rc.cls;
            L.location = rc.offset; // byte offset in the stage uniform block
            L.program = 0;
        }
        else
        {
            C->destination |= destination;
            VERIFY(C->type == rc.type);
            R_constant_load& L = C->get_load(destination);
            L.index = u16(rc.offset >> 4);
            L.cls = rc.cls;
            L.location = rc.offset;
            L.program = 0;
        }
    }

    std::sort(table.begin(), table.end(), [](const ref_constant& C1, const ref_constant& C2)
    {
        return xr_strcmp(C1->name, C2->name) < 0;
    });

    return TRUE;
}

// ===========================================================================
// R_constants — upload & bind (see metalr_constants_cache.h for the model)
// ===========================================================================

void R_constants::set_table(R_constant_table* C)
{
    m_vertex.bindSize = C ? C->mtl_vs_block_size : 0;
    m_pixel.bindSize = C ? C->mtl_ps_block_size : 0;

    // Different shaders alias the same shadow block with different layouts —
    // force a re-upload on the next flush.
    m_vertex.gpuBuffer = nullptr;
    m_pixel.gpuBuffer = nullptr;
}

void R_constants::flush_stage(StageBlock& S, MTL::RenderCommandEncoder* encoder, bool vertexStage)
{
    if (0 == S.bindSize)
        return; // stage declares no uniform block

    const bool dirty = S.dirtyHi > S.dirtyLo;
    const bool stale = (nullptr == S.gpuBuffer) || (S.gpuGeneration != g_ConstantRing.Generation());
    if (dirty || stale)
    {
        const MetalConstantRing::Slice slice = g_ConstantRing.Alloc(S.bindSize);
        if (!slice.cpu)
            return; // allocation failed — keep the previous binding, if any

        // A fresh slice every upload: draws already encoded keep reading
        // their own (older) slices, so no CPU/GPU race is possible.
        CopyMemory(slice.cpu, S.data, S.bindSize);

        S.gpuBuffer = slice.buffer;
        S.gpuOffset = slice.offset;
        S.gpuGeneration = g_ConstantRing.Generation();
        S.dirtyLo = kStageCapacity;
        S.dirtyHi = 0;
    }

    // (Re)bind every draw — encoders do not persist bindings across passes,
    // and the redundant-bind cost inside one encoder is negligible.
    if (vertexStage)
        encoder->setVertexBuffer(S.gpuBuffer, S.gpuOffset, METAL_CBUFFER_BIND_INDEX);
    else
        encoder->setFragmentBuffer(S.gpuBuffer, S.gpuOffset, METAL_CBUFFER_BIND_INDEX);
}

void R_constants::flush()
{
    MTL::RenderCommandEncoder* encoder = metalRenderPass::ActiveEncoder();
    if (!encoder)
        return; // no active pass — nothing to bind to

    flush_stage(m_vertex, encoder, true);
    flush_stage(m_pixel, encoder, false);
}
} // namespace xray::render::RENDER_NAMESPACE
