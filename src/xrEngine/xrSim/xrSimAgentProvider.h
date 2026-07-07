#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct AgentIntent
{
    std::string tool;
    std::string region;
    std::string species;
    int32_t delta = 0;
};

struct AgentWakeContext
{
    uint32_t agentId = 0;
    uint32_t gameDay = 0;
    std::string observation;
};

struct AgentProviderResult
{
    bool ok = false;
    bool coast = false;
    std::string provider;
    std::string model;
    std::string error;
    std::string coastReason;
    std::vector<AgentIntent> intents;
};

struct AgentConfigVar
{
    std::string name;
    std::string value;
};

struct AgentProviderConfig
{
    bool enabled = true;
    std::string provider = "anthropic";
    std::string model = "claude-sonnet-5";
    std::string apiKey;
    uint32_t timeoutMs = 30000;
};

struct AnthropicMessagesRequest
{
    std::string method;
    std::string path;
    std::string anthropicVersion;
    std::string contentType;
    std::string body;
};

class IAgentProvider
{
public:
    virtual ~IAgentProvider() = default;
    virtual AgentProviderResult Wake(const AgentWakeContext& context) = 0;
};

class NullAgentProvider : public IAgentProvider
{
public:
    AgentProviderResult Wake(const AgentWakeContext& context) override;
};

class AnthropicAgentProviderShell : public IAgentProvider
{
public:
    explicit AnthropicAgentProviderShell(const AgentProviderConfig& config);

    AgentProviderResult Wake(const AgentWakeContext& context) override;

private:
    AgentProviderConfig m_config;
};

class RecordedAgentProvider : public IAgentProvider
{
public:
    explicit RecordedAgentProvider(const std::vector<AgentProviderResult>& script);

    AgentProviderResult Wake(const AgentWakeContext& context) override;

private:
    std::vector<AgentProviderResult> m_script;
    size_t m_cursor = 0;
};

std::string BuildAgentWakePrompt(const AgentWakeContext& context);
AgentProviderResult ParseAgentProviderResponse(
    const std::string& text, const std::string& provider, const std::string& model);
AgentProviderConfig BuildAgentProviderConfig(const std::vector<AgentConfigVar>& vars);
AgentProviderConfig LoadAgentProviderConfigFromEnvironment();
std::string DescribeAgentProviderConfig(const AgentProviderConfig& config);
std::string FormatAgentProviderLedgerRecord(
    uint32_t seq, const AgentWakeContext& context, const AgentProviderResult& result, uint32_t latencyMs);
AnthropicMessagesRequest BuildAnthropicMessagesRequest(
    const AgentProviderConfig& config, const AgentWakeContext& context, uint32_t maxTokens);
AgentProviderResult ParseAnthropicMessagesTextResponse(
    const std::string& text, const std::string& provider, const std::string& model);

class RecordedTextAgentProvider : public IAgentProvider
{
public:
    RecordedTextAgentProvider(
        const std::string& provider, const std::string& model, const std::vector<std::string>& script);

    AgentProviderResult Wake(const AgentWakeContext& context) override;

private:
    std::string m_provider;
    std::string m_model;
    std::vector<std::string> m_script;
    size_t m_cursor = 0;
};
} // namespace xrSim
