#pragma once

#include "xrSim/xrSimWorldState.h"

namespace xrSim
{
class NullAgentRuntime
{
public:
    Result Wake(WorldState& state, uint32_t gameDay);
    uint32_t WakeCount() const;

private:
    uint32_t m_wakeCount = 0;
};
} // namespace xrSim
