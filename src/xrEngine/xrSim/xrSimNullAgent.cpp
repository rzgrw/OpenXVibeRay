#include "xrSim/xrSimNullAgent.h"

namespace xrSim
{
void NullAgentRuntime::SetProvider(IAgentProvider* provider) { m_provider = provider; }

Result NullAgentRuntime::Wake(WorldState& state, uint32_t gameDay)
{
    AgentWakeContext context;
    context.agentId = 1;
    context.gameDay = gameDay;
    context.observation = state.Digest();

    NullAgentProvider defaultProvider;
    IAgentProvider* provider = m_provider ? m_provider : &defaultProvider;
    m_lastProviderResult = provider->Wake(context);
    if (!m_lastProviderResult.ok)
    {
        const std::string reason =
            m_lastProviderResult.error.empty() ? "agent provider failed" : m_lastProviderResult.error;
        return Result{ false, 0, reason };
    }

    if (m_lastProviderResult.intents.empty())
        return Result{ false, 0, "agent provider returned no intents" };

    int32_t appliedDelta = 0;
    for (const AgentIntent& intent : m_lastProviderResult.intents)
    {
        if (intent.tool != "adjust_population")
            return Result{ false, appliedDelta, "unsupported agent intent: " + intent.tool };

        const Handle region = state.FindRegionByName(intent.region);
        if (!region.IsValid())
            return Result{ false, appliedDelta, "unknown region: " + intent.region };

        const Handle species = state.FindSpeciesByName(intent.species);
        if (!species.IsValid())
            return Result{ false, appliedDelta, "unknown species: " + intent.species };

        const uint32_t seq = uint32_t(state.ToolLog().size() + 1);
        const Result result = state.ApplyAdjustPopulation(seq, region, species, intent.delta, gameDay);
        if (!result.ok)
            return result;

        appliedDelta += result.appliedDelta;
    }

    ++m_wakeCount;
    return Result{ true, appliedDelta, "" };
}

uint32_t NullAgentRuntime::WakeCount() const { return m_wakeCount; }

const AgentProviderResult& NullAgentRuntime::LastProviderResult() const { return m_lastProviderResult; }
} // namespace xrSim
