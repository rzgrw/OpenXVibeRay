#pragma once

// metalr_constants_cache.h — stub R_constants class for the Metal backend.
// Mirrors glr_constants_cache.h; real Metal constant buffer uploads
// will be implemented once MTL::Buffer and the argument encoder are in place.

namespace xray::render::RENDER_NAMESPACE
{
class ECORE_API R_constants
{
private:
    ICF void set(R_constant* C, R_constant_load& L, const Fmatrix& A)
    {
        VERIFY(RC_float == C->type);
        // TODO: Write matrix into Metal constant buffer at L.location
        (void)A;
    }

    ICF void set(R_constant* C, R_constant_load& L, const Fvector4& A)
    {
        VERIFY(RC_float == C->type);
        // TODO: Write float4 into Metal constant buffer at L.location
        (void)A;
    }

    ICF void set(R_constant* C, R_constant_load& L, float x, float y, float z, float w)
    {
        VERIFY(RC_float == C->type);
        // TODO: Write float4 into Metal constant buffer at L.location
        (void)x; (void)y; (void)z; (void)w;
    }

    ICF void set(R_constant* C, R_constant_load& L, float A)
    {
        VERIFY(RC_float == C->type);
        VERIFY(RC_1x1 == L.cls);
        // TODO: Write float into Metal constant buffer at L.location
        (void)A;
    }

    ICF void set(R_constant* C, R_constant_load& L, int A)
    {
        VERIFY(RC_int == C->type);
        VERIFY(RC_1x1 == L.cls);
        // TODO: Write int into Metal constant buffer at L.location
        (void)A;
    }

public:
    // fp, non-array versions
    ICF void set(R_constant* C, const Fmatrix& A)
    {
        if (C->destination & RC_dest_pixel)    { set(C, C->ps, A); }
        if (C->destination & RC_dest_vertex)   { set(C, C->vs, A); }
        if (C->destination & RC_dest_geometry) { set(C, C->gs, A); }
        if (C->destination & RC_dest_all)      { set(C, C->pp, A); }
    }

    ICF void set(R_constant* C, const Fvector4& A)
    {
        if (C->destination & RC_dest_pixel)    { set(C, C->ps, A); }
        if (C->destination & RC_dest_vertex)   { set(C, C->vs, A); }
        if (C->destination & RC_dest_geometry) { set(C, C->gs, A); }
        if (C->destination & RC_dest_all)      { set(C, C->pp, A); }
    }

    ICF void set(R_constant* C, float x, float y, float z, float w)
    {
        if (C->destination & RC_dest_pixel)    { set(C, C->ps, x, y, z, w); }
        if (C->destination & RC_dest_vertex)   { set(C, C->vs, x, y, z, w); }
        if (C->destination & RC_dest_geometry) { set(C, C->gs, x, y, z, w); }
        if (C->destination & RC_dest_all)      { set(C, C->pp, x, y, z, w); }
    }

    ICF void set(R_constant* C, float A)
    {
        if (C->destination & RC_dest_pixel)    { set(C, C->ps, A); }
        if (C->destination & RC_dest_vertex)   { set(C, C->vs, A); }
        if (C->destination & RC_dest_geometry) { set(C, C->gs, A); }
        if (C->destination & RC_dest_all)      { set(C, C->pp, A); }
    }

    ICF void set(R_constant* C, int A)
    {
        if (C->destination & RC_dest_pixel)    { set(C, C->ps, A); }
        if (C->destination & RC_dest_vertex)   { set(C, C->vs, A); }
        if (C->destination & RC_dest_geometry) { set(C, C->gs, A); }
        if (C->destination & RC_dest_all)      { set(C, C->pp, A); }
    }

    // fp, array versions
    ICF void seta(R_constant* C, u32 e, const Fmatrix& A)
    {
        R_constant_load L;
        if (C->destination & RC_dest_pixel)    { L = C->ps; }
        if (C->destination & RC_dest_vertex)   { L = C->vs; }
        if (C->destination & RC_dest_geometry) { L = C->gs; }
        if (C->destination & RC_dest_all)      { L = C->pp; }
        L.location += e;
        set(C, L, A);
    }

    ICF void seta(R_constant* C, u32 e, const Fvector4& A)
    {
        R_constant_load L;
        if (C->destination & RC_dest_pixel)    { L = C->ps; }
        if (C->destination & RC_dest_vertex)   { L = C->vs; }
        if (C->destination & RC_dest_geometry) { L = C->gs; }
        if (C->destination & RC_dest_all)      { L = C->pp; }
        L.location += e;
        set(C, L, A);
    }

    ICF void seta(R_constant* C, u32 e, float x, float y, float z, float w)
    {
        R_constant_load L;
        if (C->destination & RC_dest_pixel)    { L = C->ps; }
        if (C->destination & RC_dest_vertex)   { L = C->vs; }
        if (C->destination & RC_dest_geometry) { L = C->gs; }
        if (C->destination & RC_dest_all)      { L = C->pp; }
        L.location += e;
        set(C, L, x, y, z, w);
    }

    // TODO: Implement Metal constant buffer flushing (setVertexBytes / setFragmentBytes or argument buffers)
    ICF void flush() { }
};

} // namespace xray::render::RENDER_NAMESPACE
