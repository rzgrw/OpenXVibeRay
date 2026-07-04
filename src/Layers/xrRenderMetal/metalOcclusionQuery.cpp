// metalOcclusionQuery.cpp — MTLVisibilityResultBuffer-backed occlusion queries.
#include "stdafx.h"

#include "metalOcclusionQuery.h"
#include "metalRenderPassManager.h"
#include "metalHW.h"

#if defined(USE_METAL)

namespace xray::render::RENDER_NAMESPACE
{

metalOcclusionQuery OcclusionQueries;

void metalOcclusionQuery::Create(u32 maxQueries)
{
    VERIFY(maxQueries);
    if (Created())
        return;
    if (!HW.pDevice)
    {
        Msg("! [Metal] occlusion queries requested before device creation");
        return;
    }

    // One u64 counter per query. Shared storage: CPU reads results directly.
    m_visibilityBuffer =
        HW.pDevice->newBuffer(maxQueries * sizeof(u64), MTL::ResourceStorageModeShared);
    if (!m_visibilityBuffer)
    {
        Msg("! [Metal] failed to create visibility result buffer (%u queries)", maxQueries);
        return;
    }
    m_visibilityBuffer->setLabel(NS::String::string("xr_occq_visibility", NS::UTF8StringEncoding));
    ZeroMemory(m_visibilityBuffer->contents(), m_visibilityBuffer->length());

    m_queryCount = maxQueries;
    m_freeIndices.reserve(maxQueries);
    // LIFO free-list; hand out low indices first
    for (u32 i = maxQueries; i > 0; --i)
        m_freeIndices.push_back(i - 1);

    // Every subsequent render pass attaches the buffer
    RPManager.SetVisibilityBuffer(m_visibilityBuffer);

    Msg("* [Metal] occlusion query pool created: %u queries", maxQueries);
}

void metalOcclusionQuery::Destroy()
{
    RPManager.SetVisibilityBuffer(nullptr);
    if (m_visibilityBuffer)
    {
        m_visibilityBuffer->release();
        m_visibilityBuffer = nullptr;
    }
    m_queryCount = 0;
    m_freeIndices.clear();
}

u32 metalOcclusionQuery::Allocate()
{
    if (m_freeIndices.empty())
        return InvalidIndex;
    const u32 index = m_freeIndices.back();
    m_freeIndices.pop_back();

    // Reset the counter so a stale value is never reported for a fresh query
    static_cast<u64*>(m_visibilityBuffer->contents())[index] = 0;
    return index;
}

void metalOcclusionQuery::Release(u32 queryIndex)
{
    if (queryIndex >= m_queryCount)
        return;
    m_freeIndices.push_back(queryIndex);
}

void metalOcclusionQuery::Begin(u32 queryIndex, MTL::RenderCommandEncoder* encoder)
{
    VERIFY(queryIndex < m_queryCount);
    if (!encoder)
        return;
    // Reset before counting — results accumulate at the given offset
    static_cast<u64*>(m_visibilityBuffer->contents())[queryIndex] = 0;
    encoder->setVisibilityResultMode(MTL::VisibilityResultModeCounting, queryIndex * sizeof(u64));
}

void metalOcclusionQuery::End(u32 /*queryIndex*/, MTL::RenderCommandEncoder* encoder)
{
    if (!encoder)
        return;
    encoder->setVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);
}

u64 metalOcclusionQuery::GetResult(u32 queryIndex)
{
    VERIFY(queryIndex < m_queryCount);
    if (!m_visibilityBuffer)
        return 0;
    // NOTE: valid only after the command buffer that recorded the query has
    // completed. R_occlusion polls after present; per-frame CPU/GPU sync is
    // wired up in the integration pass (Task 20).
    return static_cast<u64*>(m_visibilityBuffer->contents())[queryIndex];
}

} // namespace xray::render::RENDER_NAMESPACE

#endif // USE_METAL
