#pragma once

#include "xrSim/xrSimWorldAgents.h"
#include "xrSim/xrSimAnthropicTransport.h"
#include "xrSim/xrSimGeminiTransport.h"

#include <memory>

namespace xrSim
{
struct WorldProviderResult
{
    bool ok = false;
    bool coast = false;
    bool retryable = false;
    bool rateLimited = false;
    std::string provider;
    std::string model;
    std::string error;
    std::string coastReason;
    std::string responseHash;
    IntentEnvelope intent;
    TokenUsage usage;
    uint32_t latencyMs = 0;
};

class IWorldIntentProvider
{
public:
    virtual ~IWorldIntentProvider() = default;
    virtual WorldProviderResult Wake(const WorldWakeContext& context) = 0;
    virtual void Cancel() {}
};

class AnthropicWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit AnthropicWorldIntentProvider(const AgentProviderConfig& config);
    void SetTransport(IAnthropicTransport* transport);
    void SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport);
    WorldProviderResult Wake(const WorldWakeContext& context) override;
    void Cancel() override;

private:
    AgentProviderConfig m_config;
    std::unique_ptr<IAnthropicTransport> m_ownedTransport;
    IAnthropicTransport* m_transport = nullptr;
};

class GeminiWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit GeminiWorldIntentProvider(const AgentProviderConfig& config);
    void SetTransport(IGeminiTransport* transport);
    void SetOwnedTransport(std::unique_ptr<IGeminiTransport> transport);
    WorldProviderResult Wake(const WorldWakeContext& context) override;
    void Cancel() override;

private:
    AgentProviderConfig m_config;
    std::unique_ptr<IGeminiTransport> m_ownedTransport;
    IGeminiTransport* m_transport = nullptr;
};

class RecordedWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit RecordedWorldIntentProvider(std::vector<WorldProviderResult> script);
    WorldProviderResult Wake(const WorldWakeContext& context) override;

private:
    std::vector<WorldProviderResult> m_script;
    size_t m_cursor = 0;
};

enum class InjectedProviderFault : uint8_t
{
    None,
    Timeout,
    RateLimit,
    Malformed,
    Cancelled,
    TransportError
};

class FaultWorldIntentProvider final : public IWorldIntentProvider
{
public:
    explicit FaultWorldIntentProvider(InjectedProviderFault fault);
    WorldProviderResult Wake(const WorldWakeContext& context) override;
    void Cancel() override;

private:
    InjectedProviderFault m_fault;
    bool m_cancelled = false;
};

std::unique_ptr<IWorldIntentProvider> CreateLiveWorldIntentProvider(const AgentProviderConfig& config);
} // namespace xrSim
