#pragma once

// metalr_constants_cache.h — R_constants for the Metal backend.
//
// Model: unlike GL (per-uniform glUniform* calls), Metal shader constants
// live in per-stage uniform blocks — SPIRV-Cross packs each stage's $Globals
// into a single MSL buffer bound at METAL_CBUFFER_BIND_INDEX (see
// metalConstantBuffer.h).  R_constants keeps a CPU shadow block per stage;
// set()/seta() write into it at R_constant_load::location (a BYTE OFFSET
// into the block, mirroring the DX11 backend's register semantics), and
// flush() — called from CBackend::Render just before the draw — uploads the
// dirty block into a transient MetalConstantRing slice and binds it to the
// active render command encoder.
//
// R_constant_load contract on Metal (populated by R_constant_table::parse
// from Task 16 reflection, see metalr_constants.cpp):
//   * location — byte offset of the constant inside the stage uniform block
//   * program  — unused (0); kept because shared code copies it on merge
//   * cls      — element class (RC_1x1..RC_4x4a), same as DX11
// Matrices are stored transposed (column vectors as consecutive float4s) —
// identical to the DX11 constant buffer layout, which matches the
// column-major HLSL cbuffer packing SPIRV-Cross carries through to MSL.

#include "Layers/xrRenderMetal/CommonTypes.h"

namespace xray::render::RENDER_NAMESPACE
{
// ---------------------------------------------------------------------------
// Reflection input for R_constant_table::parse (produced by the Metal shader
// pipeline, plan Task 16). parse() receives a MetalShaderReflection* as its
// opaque `desc` argument, with `destination` naming the stage
// (RC_dest_vertex / RC_dest_pixel).
// ---------------------------------------------------------------------------
struct MetalConstantReflection
{
    pcstr name; // HLSL constant name
    u16 type; // RC_float / RC_int / RC_bool / RC_sampler
    u16 cls; // element class RC_1x1 .. RC_4x4a (ignored for samplers)
    u32 offset; // byte offset in the stage uniform block; for samplers: texture slot
};

struct MetalShaderReflection
{
    u32 blockSize{}; // total byte size of the stage's packed uniform block (0 = none)
    xr_vector<MetalConstantReflection> constants;
};

class ECORE_API R_constants
{
public:
    // Capacity of the per-stage CPU shadow block. Must hold the largest
    // uniform block any shader declares (worst case today: sbones_array,
    // 256 bones * 3 float4 = 12 KB).
    static constexpr u32 kStageCapacity = 16384;

private:
    struct StageBlock
    {
        alignas(16) u8 data[kStageCapacity];
        u32 dirtyLo = kStageCapacity; // dirty byte range since last upload
        u32 dirtyHi = 0;
        u32 bindSize = 0; // uniform block size of the current ctable stage
        MTL::Buffer* gpuBuffer = nullptr; // ring slice of the last upload
        u32 gpuOffset = 0;
        u32 gpuGeneration = u32(-1); // ring generation the slice was taken in
    };

    StageBlock m_vertex;
    StageBlock m_pixel;

    ICF void write(StageBlock& S, u32 offset, const void* src, u32 size)
    {
        VERIFY2(offset + size <= kStageCapacity, "Metal constant write out of bounds");
        CopyMemory(S.data + offset, src, size);
        if (offset < S.dirtyLo)
            S.dirtyLo = offset;
        if (offset + size > S.dirtyHi)
            S.dirtyHi = offset + size;
    }

    ICF void set(StageBlock& S, R_constant* C, R_constant_load& L, const Fmatrix& A)
    {
        VERIFY(RC_float == C->type);
        Fvector4 it[4];
        switch (L.cls)
        {
        case RC_2x4:
            it[0].set(A._11, A._21, A._31, A._41);
            it[1].set(A._12, A._22, A._32, A._42);
            write(S, u32(L.location), it, 2 * sizeof(Fvector4));
            break;

        case RC_3x4:
        case RC_3x4a:
            it[0].set(A._11, A._21, A._31, A._41);
            it[1].set(A._12, A._22, A._32, A._42);
            it[2].set(A._13, A._23, A._33, A._43);
            write(S, u32(L.location), it, 3 * sizeof(Fvector4));
            break;

        case RC_4x4:
        case RC_4x4a:
            it[0].set(A._11, A._21, A._31, A._41);
            it[1].set(A._12, A._22, A._32, A._42);
            it[2].set(A._13, A._23, A._33, A._43);
            it[3].set(A._14, A._24, A._34, A._44);
            write(S, u32(L.location), it, 4 * sizeof(Fvector4));
            break;

        default:
#ifdef DEBUG
            xrDebug::Fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", C->name.c_str());
#else
            NODEFAULT;
#endif
        }
    }

    ICF void set(StageBlock& S, R_constant* C, R_constant_load& L, const Fvector4& A)
    {
        VERIFY(RC_float == C->type);
        u32 size;
        switch (L.cls)
        {
        case RC_1x2: size = 2 * sizeof(float); break;
        case RC_1x3: size = 3 * sizeof(float); break;
        case RC_1x4:
        case RC_1x4a: size = 4 * sizeof(float); break;
        default:
#ifdef DEBUG
            xrDebug::Fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", C->name.c_str());
            return;
#else
            NODEFAULT;
            size = 0;
#endif
        }
        write(S, u32(L.location), &A.x, size);
    }

    ICF void set(StageBlock& S, R_constant* C, R_constant_load& L, float x, float y, float z, float w)
    {
        Fvector4 data;
        data.set(x, y, z, w);
        set(S, C, L, data);
    }

    ICF void set(StageBlock& S, R_constant* C, R_constant_load& L, float A)
    {
        VERIFY(RC_float == C->type);
        VERIFY(RC_1x1 == L.cls);
        write(S, u32(L.location), &A, sizeof(float));
    }

    ICF void set(StageBlock& S, R_constant* C, R_constant_load& L, int A)
    {
        VERIFY(RC_int == C->type);
        VERIFY(RC_1x1 == L.cls);
        write(S, u32(L.location), &A, sizeof(int));
    }

    // Array element writes — element stride follows the DX11 backend:
    // one float4 register line (16 bytes) per matrix row / vector element.
    ICF void seta(StageBlock& S, R_constant* C, R_constant_load& L, u32 e, const Fmatrix& A)
    {
        VERIFY(RC_float == C->type);
        u32 rows;
        switch (L.cls)
        {
        case RC_2x4: rows = 2; break;
        case RC_3x4:
        case RC_3x4a: rows = 3; break;
        case RC_4x4:
        case RC_4x4a: rows = 4; break;
        default:
#ifdef DEBUG
            xrDebug::Fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", C->name.c_str());
            return;
#else
            NODEFAULT;
            rows = 0;
#endif
        }
        R_constant_load el = L;
        el.location += rows * sizeof(Fvector4) * e;
        set(S, C, el, A);
    }

    ICF void seta(StageBlock& S, R_constant* C, R_constant_load& L, u32 e, const Fvector4& A)
    {
        VERIFY(RC_float == C->type);
        VERIFY(RC_1x4 == L.cls || RC_1x4a == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);
        write(S, u32(L.location) + sizeof(Fvector4) * e, &A.x, sizeof(Fvector4));
    }

    ICF void seta(StageBlock& S, R_constant* C, R_constant_load& L, u32 e, float x, float y, float z, float w)
    {
        Fvector4 data;
        data.set(x, y, z, w);
        seta(S, C, L, e, data);
    }

    void flush_stage(StageBlock& S, MTL::RenderCommandEncoder* encoder, bool vertexStage);

public:
    // fp, non-array versions
    template <typename... Args>
    ICF void set(R_constant* C, Args&&... args)
    {
        VERIFY2(0 == (C->destination & (RC_dest_geometry | RC_dest_all)),
            "Metal: geometry/program-pipeline constant destinations are not supported");
        if (C->destination & RC_dest_pixel)
            set(m_pixel, C, C->ps, std::forward<Args>(args)...);
        if (C->destination & RC_dest_vertex)
            set(m_vertex, C, C->vs, std::forward<Args>(args)...);
    }

    // fp, array versions
    template <typename... Args>
    ICF void seta(R_constant* C, u32 e, Args&&... args)
    {
        VERIFY2(0 == (C->destination & (RC_dest_geometry | RC_dest_all)),
            "Metal: geometry/program-pipeline constant destinations are not supported");
        if (C->destination & RC_dest_pixel)
            seta(m_pixel, C, C->ps, e, std::forward<Args>(args)...);
        if (C->destination & RC_dest_vertex)
            seta(m_vertex, C, C->vs, e, std::forward<Args>(args)...);
    }

    // Called by CBackend::set_Constants whenever the constant table changes —
    // pulls the per-stage uniform block sizes gathered by parse() and marks
    // the GPU-side slices stale.
    void set_table(R_constant_table* C);

    // Uploads dirty constant data into a fresh per-frame ring slice and binds
    // the per-stage uniform blocks to the active render command encoder.
    // Called from CBackend::Render (metalR_Backend_Runtime.h) per draw.
    void flush();
};
} // namespace xray::render::RENDER_NAMESPACE
