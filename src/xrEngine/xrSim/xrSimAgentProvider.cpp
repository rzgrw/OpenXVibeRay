#include "xrSim/xrSimAgentProvider.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace xrSim
{
namespace
{
AgentProviderResult FailResponse(const std::string& provider, const std::string& model, const std::string& error)
{
    AgentProviderResult result;
    result.ok = false;
    result.provider = provider;
    result.model = model;
    result.error = error;
    return result;
}

bool ParseInt32(const std::string& text, int32_t& value)
{
    if (text.empty())
        return false;

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
        return false;

    value = int32_t(parsed);
    return true;
}
} // namespace

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

std::string BuildAgentWakePrompt(const AgentWakeContext& context)
{
    std::ostringstream out;
    out << "xrsim_agent_wake_v1\n";
    out << "agent_id " << context.agentId << "\n";
    out << "game_day " << context.gameDay << "\n";
    out << "observation_begin\n";
    out << context.observation << "\n";
    out << "observation_end\n";
    out << "tools\n";
    out << "intent adjust_population <region> <species> <delta:int>\n";
    out << "rules\n";
    out << "return xrsim_agent_response_v1 with intent lines only\n";
    out << "never emit per-frame or near-player tactical decisions\n";
    out << "end\n";
    return out.str();
}

AgentProviderResult ParseAgentProviderResponse(
    const std::string& text, const std::string& provider, const std::string& model)
{
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "xrsim_agent_response_v1")
        return FailResponse(provider, model, "unsupported agent response version");

    AgentProviderResult result;
    result.ok = true;
    result.provider = provider;
    result.model = model;

    bool sawEnd = false;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;
        if (line == "end")
        {
            sawEnd = true;
            break;
        }

        std::istringstream record(line);
        std::string recordType;
        std::string tool;
        std::string region;
        std::string species;
        std::string deltaText;
        std::string extra;
        if (!(record >> recordType >> tool >> region >> species >> deltaText))
            return FailResponse(provider, model, "invalid agent response record");
        if (record >> extra)
            return FailResponse(provider, model, "invalid agent response record");
        if (recordType != "intent")
            return FailResponse(provider, model, "invalid agent response record");
        if (tool != "adjust_population")
            return FailResponse(provider, model, "unknown intent: " + tool);

        int32_t delta = 0;
        if (!ParseInt32(deltaText, delta))
            return FailResponse(provider, model, "invalid intent delta");

        AgentIntent intent;
        intent.tool = tool;
        intent.region = region;
        intent.species = species;
        intent.delta = delta;
        result.intents.push_back(intent);
    }

    if (!sawEnd)
        return FailResponse(provider, model, "missing agent response end");
    if (result.intents.empty())
        return FailResponse(provider, model, "agent response contained no intents");

    return result;
}

RecordedTextAgentProvider::RecordedTextAgentProvider(
    const std::string& provider, const std::string& model, const std::vector<std::string>& script)
    : m_provider(provider), m_model(model), m_script(script)
{
}

AgentProviderResult RecordedTextAgentProvider::Wake(const AgentWakeContext& context)
{
    (void)context;

    if (m_cursor >= m_script.size())
        return FailResponse(m_provider, m_model, "recorded text provider exhausted");

    return ParseAgentProviderResponse(m_script[m_cursor++], m_provider, m_model);
}
} // namespace xrSim
