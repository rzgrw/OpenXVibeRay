#pragma once

// metalConstantBuffer.h — MTL::Buffer-backed storage for shader constants.
//
// Two building blocks:
//
//  * MetalConstantBuffer — a single fixed-size constant buffer with a CPU
//    staging copy and dirty tracking.  Suitable for data written at most once
//    per frame (the buffer contents are updated in place, so per-draw writes
//    would race with GPU reads from earlier draws — use the ring for those).
//
//  * MetalConstantRing — a triple-buffered linear allocator of transient
//    constant-buffer slices.  R_constants::flush() grabs a fresh slice per
//    dirty upload, so a draw call never observes a later draw's constants.
//    IMPORTANT (Task 19/20): metalRenderPass must call
//    g_ConstantRing.OnFrameBegin() exactly once per frame — this is what
//    recycles slices; without it the ring grows without bound.

#include "Layers/xrRenderMetal/CommonTypes.h"

namespace xray::render::RENDER_NAMESPACE
{
// Buffer bind index for the per-stage uniform block in the MSL argument
// table (31 buffer slots, 0..30).  Vertex streams occupy the low indices
// starting at METAL_VERTEX_STREAM_INDEX (== 0, see metalR_Backend_Runtime.h),
// so the uniform block sits at the top.  The shader pipeline (Task 16) must
// remap SPIRV-Cross uniform-buffer bindings to this index
// (--msl-buffer-index or the runtime MSL resource-binding API).
constexpr u32 METAL_CBUFFER_BIND_INDEX = 30;

class MetalConstantBuffer
{
public:
    MetalConstantBuffer() = default;
    ~MetalConstantBuffer() { Destroy(); }

    MetalConstantBuffer(const MetalConstantBuffer&) = delete;
    MetalConstantBuffer& operator=(const MetalConstantBuffer&) = delete;

    void Create(u32 size);
    void Destroy();

    void Write(u32 offset, const void* data, u32 size);

    // Uploads the staging copy if dirty, then binds the buffer to the
    // encoder at the given index (vertex or fragment stage).
    void Bind(MTL::RenderCommandEncoder* encoder, u32 index, bool vertex);

    bool IsValid() const { return nullptr != m_buffer; }
    u32 GetSize() const { return m_size; }

private:
    MTL::Buffer* m_buffer{};
    u8* m_cpuData{}; // CPU-side staging
    u32 m_size{};
    bool m_dirty{};
};

class MetalConstantRing
{
public:
    struct Slice
    {
        MTL::Buffer* buffer{};
        u32 offset{};
        u8* cpu{}; // write-through pointer into the shared-storage buffer
    };

    // Must match the swapchain depth (CAMetalLayer maximumDrawableCount) —
    // a slice is reused kFramesInFlight frames after it was written, when
    // the GPU is guaranteed to have consumed it.
    static constexpr u32 kFramesInFlight = 3;
    static constexpr u32 kPageSize = 2 * 1024 * 1024;
    // Constant-buffer offsets are conservatively aligned for all Metal GPUs.
    static constexpr u32 kSliceAlignment = 256;

    ~MetalConstantRing() { Destroy(); }

    void Destroy();

    // Advance to the next frame slot and recycle its pages.
    // Called once per frame by the render-pass manager (Task 19).
    void OnFrameBegin();

    // Allocate a transient slice for this frame. Returns a zeroed Slice
    // (cpu == nullptr) on allocation failure.
    Slice Alloc(u32 size);

    // Monotonic frame counter — slices allocated under an older generation
    // must be considered stale (see R_constants::flush).
    u32 Generation() const { return m_generation; }

private:
    struct Page
    {
        MTL::Buffer* buffer{};
        u32 used{};
        u32 slot{};
    };

    xr_vector<Page> m_pages;
    u32 m_slot{};
    u32 m_generation{};
};

extern MetalConstantRing g_ConstantRing;
} // namespace xray::render::RENDER_NAMESPACE
