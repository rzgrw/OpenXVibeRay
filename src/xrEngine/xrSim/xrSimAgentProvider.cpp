#include "xrSim/xrSimAgentProvider.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <utility>

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

std::string EscapeJsonString(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text)
    {
        switch (ch)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

bool ReadJsonStringAt(const std::string& text, size_t& pos, std::string& decoded)
{
    decoded.clear();
    if (pos >= text.size() || text[pos] != '"')
        return false;

    auto appendUtf8 = [&decoded](uint32_t codePoint)
    {
        if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;
        if (codePoint <= 0x7F)
        {
            decoded += char(codePoint);
        }
        else if (codePoint <= 0x7FF)
        {
            decoded += char(0xC0 | ((codePoint >> 6) & 0x1F));
            decoded += char(0x80 | (codePoint & 0x3F));
        }
        else if (codePoint <= 0xFFFF)
        {
            decoded += char(0xE0 | ((codePoint >> 12) & 0x0F));
            decoded += char(0x80 | ((codePoint >> 6) & 0x3F));
            decoded += char(0x80 | (codePoint & 0x3F));
        }
        else
        {
            decoded += char(0xF0 | ((codePoint >> 18) & 0x07));
            decoded += char(0x80 | ((codePoint >> 12) & 0x3F));
            decoded += char(0x80 | ((codePoint >> 6) & 0x3F));
            decoded += char(0x80 | (codePoint & 0x3F));
        }
        return true;
    };

    auto parseHexDigit = [](char ch, uint32_t& value)
    {
        if (ch >= '0' && ch <= '9')
        {
            value = uint32_t(ch - '0');
            return true;
        }
        if (ch >= 'a' && ch <= 'f')
        {
            value = 10u + uint32_t(ch - 'a');
            return true;
        }
        if (ch >= 'A' && ch <= 'F')
        {
            value = 10u + uint32_t(ch - 'A');
            return true;
        }
        return false;
    };

    auto parseHex4 = [&text, &parseHexDigit](size_t pos, uint32_t& value)
    {
        if (pos + 4 > text.size())
            return false;
        value = 0;
        for (size_t i = pos; i < pos + 4; ++i)
        {
            uint32_t digit = 0;
            if (!parseHexDigit(text[i], digit))
                return false;
            value = (value << 4u) | digit;
        }
        return true;
    };

    for (size_t i = pos + 1; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == '"')
        {
            pos = i + 1;
            return true;
        }
        if (ch != '\\')
        {
            decoded += ch;
            continue;
        }

        if (++i >= text.size())
            return false;
        switch (text[i])
        {
        case '"': decoded += '"'; break;
        case '\\': decoded += '\\'; break;
        case '/': decoded += '/'; break;
        case 'n': decoded += '\n'; break;
        case 'r': decoded += '\r'; break;
        case 't': decoded += '\t'; break;
        case 'b': decoded += '\b'; break;
        case 'f': decoded += '\f'; break;
        case 'u':
        {
            uint32_t codePoint = 0;
            if (!parseHex4(i + 1, codePoint))
                return false;
            i += 4;

            if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
            {
                if (i + 2 >= text.size() || text[i + 1] != '\\' || text[i + 2] != 'u')
                    return false;
                uint32_t lowSurrogate = 0;
                if (!parseHex4(i + 3, lowSurrogate))
                    return false;
                if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF)
                    return false;
                codePoint = 0x10000 + (((codePoint - 0xD800) << 10u) | (lowSurrogate - 0xDC00));
                i += 6;
            }
            else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
            {
                return false;
            }

            if (!appendUtf8(codePoint))
                return false;
            break;
        }
        default: return false;
        }
    }
    return false;
}

void SkipJsonWhitespace(const std::string& text, size_t& pos)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
        ++pos;
}

bool SkipJsonValue(const std::string& text, size_t& pos);

bool SkipJsonObject(const std::string& text, size_t& pos)
{
    if (pos >= text.size() || text[pos] != '{')
        return false;
    ++pos;
    SkipJsonWhitespace(text, pos);
    if (pos < text.size() && text[pos] == '}')
    {
        ++pos;
        return true;
    }

    while (pos < text.size())
    {
        std::string ignoredKey;
        if (!ReadJsonStringAt(text, pos, ignoredKey))
            return false;

        SkipJsonWhitespace(text, pos);
        if (pos >= text.size() || text[pos] != ':')
            return false;
        ++pos;
        if (!SkipJsonValue(text, pos))
            return false;
        SkipJsonWhitespace(text, pos);
        if (pos >= text.size())
            return false;
        if (text[pos] == '}')
        {
            ++pos;
            return true;
        }
        if (text[pos] != ',')
            return false;
        ++pos;
        SkipJsonWhitespace(text, pos);
    }
    return false;
}

bool SkipJsonArray(const std::string& text, size_t& pos)
{
    if (pos >= text.size() || text[pos] != '[')
        return false;
    ++pos;
    SkipJsonWhitespace(text, pos);
    if (pos < text.size() && text[pos] == ']')
    {
        ++pos;
        return true;
    }

    while (pos < text.size())
    {
        if (!SkipJsonValue(text, pos))
            return false;
        SkipJsonWhitespace(text, pos);
        if (pos >= text.size())
            return false;
        if (text[pos] == ']')
        {
            ++pos;
            return true;
        }
        if (text[pos] != ',')
            return false;
        ++pos;
        SkipJsonWhitespace(text, pos);
    }
    return false;
}

bool SkipJsonPrimitive(const std::string& text, size_t& pos)
{
    const size_t start = pos;
    while (pos < text.size())
    {
        const char ch = text[pos];
        if (ch == ',' || ch == ']' || ch == '}' || std::isspace(static_cast<unsigned char>(ch)) != 0)
            break;
        ++pos;
    }
    return pos > start;
}

bool SkipJsonValue(const std::string& text, size_t& pos)
{
    SkipJsonWhitespace(text, pos);
    if (pos >= text.size())
        return false;
    if (text[pos] == '"')
    {
        std::string ignored;
        return ReadJsonStringAt(text, pos, ignored);
    }
    if (text[pos] == '{')
        return SkipJsonObject(text, pos);
    if (text[pos] == '[')
        return SkipJsonArray(text, pos);
    return SkipJsonPrimitive(text, pos);
}

bool ReadJsonString(const std::string& text, size_t& pos, std::string& decoded)
{
    return ReadJsonStringAt(text, pos, decoded);
}

bool ExtractAnthropicTextContentItem(
    const std::string& text, size_t& pos, std::string& responseText, bool& foundTextBlock)
{
    if (pos >= text.size() || text[pos] != '{')
        return false;
    ++pos;
    SkipJsonWhitespace(text, pos);

    std::string typeValue;
    std::string textValue;
    bool hasTextValue = false;

    if (pos < text.size() && text[pos] == '}')
    {
        ++pos;
        return true;
    }

    while (pos < text.size())
    {
        std::string key;
        if (!ReadJsonString(text, pos, key))
            return false;
        SkipJsonWhitespace(text, pos);
        if (pos >= text.size() || text[pos] != ':')
            return false;
        ++pos;
        SkipJsonWhitespace(text, pos);

        if (key == "type")
        {
            if (!ReadJsonString(text, pos, typeValue))
                return false;
        }
        else if (key == "text")
        {
            if (!ReadJsonString(text, pos, textValue))
                return false;
            hasTextValue = true;
        }
        else if (!SkipJsonValue(text, pos))
        {
            return false;
        }

        SkipJsonWhitespace(text, pos);
        if (pos >= text.size())
            return false;
        if (text[pos] == '}')
        {
            ++pos;
            if (typeValue == "text")
            {
                foundTextBlock = true;
                if (!hasTextValue)
                    return false;
                responseText = textValue;
            }
            return true;
        }
        if (text[pos] != ',')
            return false;
        ++pos;
        SkipJsonWhitespace(text, pos);
    }
    return false;
}

bool ExtractAnthropicTextContent(const std::string& text, std::string& responseText, bool& foundTextBlock)
{
    foundTextBlock = false;
    responseText.clear();

    size_t pos = 0;
    SkipJsonWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != '{')
        return false;
    ++pos;
    SkipJsonWhitespace(text, pos);

    if (pos < text.size() && text[pos] == '}')
        return false;

    while (pos < text.size())
    {
        std::string key;
        if (!ReadJsonString(text, pos, key))
            return false;
        SkipJsonWhitespace(text, pos);
        if (pos >= text.size() || text[pos] != ':')
            return false;
        ++pos;
        SkipJsonWhitespace(text, pos);

        if (key == "content")
        {
            if (pos >= text.size() || text[pos] != '[')
                return false;
            ++pos;
            SkipJsonWhitespace(text, pos);
            if (pos < text.size() && text[pos] == ']')
            {
                ++pos;
            }
            else
            {
                while (pos < text.size())
                {
                    if (pos < text.size() && text[pos] == '{')
                    {
                        if (!ExtractAnthropicTextContentItem(text, pos, responseText, foundTextBlock))
                            return false;
                        if (foundTextBlock && !responseText.empty())
                            return true;
                    }
                    else if (!SkipJsonValue(text, pos))
                    {
                        return false;
                    }

                    SkipJsonWhitespace(text, pos);
                    if (pos >= text.size())
                        return false;
                    if (text[pos] == ']')
                    {
                        ++pos;
                        break;
                    }
                    if (text[pos] != ',')
                        return false;
                    ++pos;
                    SkipJsonWhitespace(text, pos);
                }
            }
        }
        else if (!SkipJsonValue(text, pos))
        {
            return false;
        }

        SkipJsonWhitespace(text, pos);
        if (pos >= text.size())
            return false;
        if (text[pos] == '}')
            return true;
        if (text[pos] != ',')
            return false;
        ++pos;
        SkipJsonWhitespace(text, pos);
    }
    return false;
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

void AnthropicAgentProviderShell::SetTransport(IAnthropicTransport* transport)
{
    m_ownedTransport.reset();
    m_transport = transport;
}

void AnthropicAgentProviderShell::SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport)
{
    m_ownedTransport = std::move(transport);
    m_transport = m_ownedTransport.get();
}

AgentProviderResult AnthropicAgentProviderShell::Wake(const AgentWakeContext& context)
{
    if (!m_config.enabled)
        return CoastResponse(m_config.provider, m_config.model, "provider_disabled");
    if (m_config.provider != "anthropic")
        return CoastResponse(m_config.provider, m_config.model, "unsupported_provider");
    if (m_config.apiKey.empty())
        return CoastResponse(m_config.provider, m_config.model, "missing_api_key");
    if (!m_transport)
        return CoastResponse(m_config.provider, m_config.model, "network_adapter_not_linked");

    const AnthropicMessagesRequest request = BuildAnthropicMessagesRequest(m_config, context, 1024);
    const AnthropicTransportResult transportResult = m_transport->Send(m_config, request);
    if (!transportResult.ok)
    {
        AgentProviderResult result = CoastResponse(m_config.provider, m_config.model, "transport_error");
        result.error = transportResult.error;
        return result;
    }
    if (transportResult.status != 200)
    {
        AgentProviderResult result = CoastResponse(m_config.provider, m_config.model, "http_status");
        result.error = std::to_string(transportResult.status);
        return result;
    }

    return ParseAnthropicMessagesTextResponse(transportResult.body, m_config.provider, m_config.model);
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
    bool sawVersion = false;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line == "xrsim_agent_response_v1")
        {
            sawVersion = true;
            break;
        }
    }
    if (!sawVersion)
        return FailResponse(provider, model, "unsupported agent response version");

    AgentProviderResult result;
    result.ok = true;
    result.provider = provider;
    result.model = model;

    bool sawEnd = false;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
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
        if (line == "action coast")
        {
            if (!result.intents.empty())
                return FailResponse(provider, model, "coast cannot be mixed with intents");
            result.coast = true;
            continue;
        }
        if (line.rfind("reason ", 0) == 0)
        {
            if (!result.coast)
                return FailResponse(provider, model, "reason requires coast");
            result.coastReason = line.substr(7);
            continue;
        }

        {
            std::istringstream metadata(line);
            std::string key;
            std::string valueText;
            std::string extra;
            if ((metadata >> key >> valueText) && !(metadata >> extra) &&
                (key == "agent_id" || key == "game_day"))
            {
                uint32_t ignored = 0;
                if (!ParseUint32(valueText, ignored))
                    return FailResponse(provider, model, "invalid agent response metadata");
                continue;
            }
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

AnthropicMessagesRequest BuildAnthropicMessagesRequest(
    const AgentProviderConfig& config, const AgentWakeContext& context, uint32_t maxTokens)
{
    AnthropicMessagesRequest request;
    request.method = "POST";
    request.path = "/v1/messages";
    request.anthropicVersion = "2023-06-01";
    request.contentType = "application/json";

    const std::string prompt = BuildAgentWakePrompt(context);
    std::ostringstream body;
    body << "{";
    body << "\"model\":\"" << EscapeJsonString(config.model) << "\",";
    body << "\"max_tokens\":" << maxTokens << ",";
    body << "\"messages\":[{\"role\":\"user\",\"content\":\"" << EscapeJsonString(prompt) << "\"}]";
    body << "}";
    request.body = body.str();
    return request;
}

AgentProviderResult ParseAnthropicMessagesTextResponse(
    const std::string& text, const std::string& provider, const std::string& model)
{
    std::string responseText;
    bool foundTextBlock = false;
    if (!ExtractAnthropicTextContent(text, responseText, foundTextBlock))
    {
        return FailResponse(provider, model, foundTextBlock ? "invalid anthropic text content"
                                                            : "anthropic response missing text block");
    }
    if (!foundTextBlock)
        return FailResponse(provider, model, "anthropic response missing text block");

    return ParseAgentProviderResponse(responseText, provider, model);
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
