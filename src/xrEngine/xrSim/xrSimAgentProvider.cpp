#include "xrSim/xrSimAgentProvider.h"

namespace xrSim
{
AgentProviderResult NullAgentProvider::Wake(const AgentWakeContext& context)
{
    (void)context;

    AgentProviderResult result;
    result.ok = true;
    result.provider = "null";
    result.model = "deterministic-null";

    AgentIntent intent;
    intent.tool = "adjust_population";
    intent.region = "debug_region";
    intent.species = "blind_dog";
    intent.delta = 5;
    result.intents.push_back(intent);
    return result;
}

RecordedAgentProvider::RecordedAgentProvider(const std::vector<AgentProviderResult>& script) : m_script(script) {}

AgentProviderResult RecordedAgentProvider::Wake(const AgentWakeContext& context)
{
    (void)context;

    if (m_cursor >= m_script.size())
    {
        AgentProviderResult result;
        result.ok = false;
        result.provider = "recorded";
        result.model = "fixture";
        result.error = "recorded provider exhausted";
        return result;
    }

    return m_script[m_cursor++];
}
} // namespace xrSim
