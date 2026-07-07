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
    std::string provider;
    std::string model;
    std::string error;
    std::vector<AgentIntent> intents;
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

class RecordedAgentProvider : public IAgentProvider
{
public:
    explicit RecordedAgentProvider(const std::vector<AgentProviderResult>& script);

    AgentProviderResult Wake(const AgentWakeContext& context) override;

private:
    std::vector<AgentProviderResult> m_script;
    size_t m_cursor = 0;
};
} // namespace xrSim
