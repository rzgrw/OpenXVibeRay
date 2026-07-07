#include "xrSim/xrSimNullAgent.h"

namespace xrSim
{
Result NullAgentRuntime::Wake(WorldState& state, uint32_t gameDay)
{
    const Handle region = state.FindRegionByName("debug_region");
    if (!region.IsValid())
        return Result{ false, 0, "unknown region: debug_region" };

    const Handle species = state.FindSpeciesByName("blind_dog");
    if (!species.IsValid())
        return Result{ false, 0, "unknown species: blind_dog" };

    const uint32_t seq = uint32_t(state.ToolLog().size() + 1);
    const Result result = state.ApplyAdjustPopulation(seq, region, species, 5, gameDay);
    if (result.ok)
        ++m_wakeCount;
    return result;
}

uint32_t NullAgentRuntime::WakeCount() const { return m_wakeCount; }
} // namespace xrSim
