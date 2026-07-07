#pragma once

#include "xrSim/xrSimAgentProvider.h"
#include "xrSim/xrSimWorldState.h"

namespace xrSim
{
class NullAgentRuntime
{
public:
    void SetProvider(IAgentProvider* provider);
    Result Wake(WorldState& state, uint32_t gameDay);
    uint32_t WakeCount() const;
    const AgentProviderResult& LastProviderResult() const;

private:
    IAgentProvider* m_provider = nullptr;
    AgentProviderResult m_lastProviderResult;
    uint32_t m_wakeCount = 0;
};
} // namespace xrSim
