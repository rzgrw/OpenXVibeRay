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

bool ParseUint32(const std::string& text, uint32_t& value)
{
    if (text.empty())
        return false;

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    if (parsed > std::numeric_limits<uint32_t>::max())
        return false;

    value = uint32_t(parsed);
    return true;
}

std::string FindVar(const std::vector<AgentConfigVar>& vars, const std::string& name)
{
    for (const AgentConfigVar& var : vars)
    {
        if (var.name == name)
            return var.value;
    }
    return "";
}

AgentProviderResult CoastResponse(const std::string& provider, const std::string& model, const std::string& reason)
{
    AgentProviderResult result;
    result.ok = true;
    result.coast = true;
    result.provider = provider;
    result.model = model;
    result.coastReason = reason;
    return result;
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

AnthropicAgentProviderShell::AnthropicAgentProviderShell(const AgentProviderConfig& config) : m_config(config) {}

AgentProviderResult AnthropicAgentProviderShell::Wake(const AgentWakeContext& context)
{
    (void)context;

    if (!m_config.enabled)
        return CoastResponse(m_config.provider, m_config.model, "provider_disabled");
    if (m_config.provider != "anthropic")
        return CoastResponse(m_config.provider, m_config.model, "unsupported_provider");
    if (m_config.apiKey.empty())
        return CoastResponse(m_config.provider, m_config.model, "missing_api_key");

    return CoastResponse(m_config.provider, m_config.model, "network_adapter_not_linked");
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
    out << "return xrsim_agent_response_v1 with intent lines, or coast if no strategic change is needed\n";
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
        if (line == "coast")
        {
            if (!result.intents.empty())
                return FailResponse(provider, model, "coast cannot be mixed with intents");
            result.coast = true;
            continue;
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
        if (result.coast)
            return FailResponse(provider, model, "coast cannot be mixed with intents");
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
    if (result.intents.empty() && !result.coast)
        return FailResponse(provider, model, "agent response contained no intents");

    return result;
}

AgentProviderConfig BuildAgentProviderConfig(const std::vector<AgentConfigVar>& vars)
{
    AgentProviderConfig config;

    const std::string provider = FindVar(vars, "XRAY_AGENT_PROVIDER");
    if (!provider.empty())
    {
        config.provider = provider;
        config.enabled = provider != "off" && provider != "disabled";
    }

    const std::string model = FindVar(vars, "XRAY_AGENT_MODEL");
    if (!model.empty())
        config.model = model;

    const std::string genericKey = FindVar(vars, "XRAY_AGENT_API_KEY");
    const std::string anthropicKey = FindVar(vars, "ANTHROPIC_API_KEY");
    config.apiKey = genericKey.empty() ? anthropicKey : genericKey;

    uint32_t timeoutMs = 0;
    if (ParseUint32(FindVar(vars, "XRAY_AGENT_TIMEOUT_MS"), timeoutMs) && timeoutMs > 0)
        config.timeoutMs = timeoutMs;

    return config;
}

AgentProviderConfig LoadAgentProviderConfigFromEnvironment()
{
    std::vector<AgentConfigVar> vars;
    const char* names[] = {
        "XRAY_AGENT_PROVIDER",
        "XRAY_AGENT_MODEL",
        "XRAY_AGENT_API_KEY",
        "ANTHROPIC_API_KEY",
        "XRAY_AGENT_TIMEOUT_MS",
    };

    for (const char* name : names)
    {
        const char* value = std::getenv(name);
        if (value)
            vars.push_back(AgentConfigVar{ name, value });
    }

    return BuildAgentProviderConfig(vars);
}

std::string DescribeAgentProviderConfig(const AgentProviderConfig& config)
{
    std::string state = "ready";
    std::string reason;
    if (!config.enabled)
    {
        state = "coast";
        reason = "provider_disabled";
    }
    else if (config.apiKey.empty())
    {
        state = "coast";
        reason = "missing_api_key";
    }

    std::ostringstream out;
    out << "provider=" << config.provider << " model=" << config.model << " state=" << state;
    if (!reason.empty())
        out << " reason=" << reason;
    out << " timeout_ms=" << config.timeoutMs;
    return out.str();
}

std::string FormatAgentProviderLedgerRecord(
    uint32_t seq, const AgentWakeContext& context, const AgentProviderResult& result, uint32_t latencyMs)
{
    std::ostringstream out;
    out << "xrsim_agent_provider_record_v1";
    out << " seq=" << seq;
    out << " agent_id=" << context.agentId;
    out << " game_day=" << context.gameDay;
    out << " provider=" << result.provider;
    out << " model=" << result.model;
    out << " ok=" << (result.ok ? 1 : 0);
    out << " coast=" << (result.coast ? 1 : 0);
    out << " intents=" << result.intents.size();
    if (!result.error.empty())
        out << " error=" << result.error;
    if (!result.coastReason.empty())
        out << " coast_reason=" << result.coastReason;
    out << " latency_ms=" << latencyMs;
    return out.str();
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
