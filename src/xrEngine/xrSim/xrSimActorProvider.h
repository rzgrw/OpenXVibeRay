#pragma once

#include "xrSim/xrSimActorRuntime.h"
#include "xrSim/xrSimAgentProvider.h"

#include <memory>

namespace xrSim
{
struct ActorWakeSubmitResult
{
    bool ok = false;
    uint64_t requestId = 0;
    std::string reason;
};

struct ActorWakePollResult
{
    bool ok = false;
    bool ready = false;
    std::string reason;
    ActorProviderResult providerResult;
};

class AnthropicActorIntentProvider final : public IActorIntentProvider
{
public:
    explicit AnthropicActorIntentProvider(const AgentProviderConfig& config);

    void SetTransport(IAnthropicTransport* transport);
    void SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport);
    ActorProviderResult Wake(const ActorWakeContext& context) override;
    void Cancel() override;

private:
    AgentProviderConfig m_config;
    std::unique_ptr<IAnthropicTransport> m_ownedTransport;
    IAnthropicTransport* m_transport = nullptr;
};

std::unique_ptr<IActorIntentProvider> CreateLiveActorIntentProvider(const AgentProviderConfig& config);

class AsyncActorProviderQueue
{
public:
    explicit AsyncActorProviderQueue(std::unique_ptr<IActorIntentProvider> provider);
    ~AsyncActorProviderQueue();

    AsyncActorProviderQueue(const AsyncActorProviderQueue&) = delete;
    AsyncActorProviderQueue& operator=(const AsyncActorProviderQueue&) = delete;

    ActorWakeSubmitResult Submit(const ActorWakeContext& context);
    ActorWakePollResult Poll(uint64_t requestId);
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace xrSim
