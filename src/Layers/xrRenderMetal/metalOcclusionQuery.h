#pragma once

// metalOcclusionQuery — occlusion queries for the Metal backend.
//
// Metal has no query objects; instead a render pass carries one visibility
// result buffer (MTL::RenderPassDescriptor::visibilityResultBuffer) and each
// draw can accumulate the number of passing samples into a u64 slot of that
// buffer via setVisibilityResultMode(Counting, offset).
//
// This class owns a single shared-storage MTL::Buffer sliced into u64 slots.
// One "query" (as seen through QueryHelper.h / R_occlusion) is one slot:
//   * Allocate()/Release() manage slot lifetime (QueryHelper Create/Release)
//   * Begin()/End() toggle the visibility result mode on the active encoder
//   * GetResult() reads the u64 counter back from the shared buffer
//
// The buffer is registered with metalRenderPassManager so every render pass
// descriptor attaches it (Metal requires this before encoder creation).

#if defined(USE_METAL)
#include <Metal/Metal.hpp>
#endif

namespace xray::render::RENDER_NAMESPACE
{

class metalOcclusionQuery
{
public:
    static constexpr u32 InvalidIndex = 0xFFFFFFFF;

    void Create(u32 maxQueries);
    void Destroy();

    // Slot lifetime (free-list). Returns InvalidIndex when exhausted.
    u32 Allocate();
    void Release(u32 queryIndex);

#if defined(USE_METAL)
    void Begin(u32 queryIndex, MTL::RenderCommandEncoder* encoder);
    void End(u32 queryIndex, MTL::RenderCommandEncoder* encoder);
    u64 GetResult(u32 queryIndex);

    MTL::Buffer* Buffer() const { return m_visibilityBuffer; }
#endif

    bool Created() const { return m_queryCount != 0; }

private:
#if defined(USE_METAL)
    MTL::Buffer* m_visibilityBuffer = nullptr;
#endif
    u32 m_queryCount = 0;
    xr_vector<u32> m_freeIndices;
};

extern metalOcclusionQuery OcclusionQueries;

} // namespace xray::render::RENDER_NAMESPACE
