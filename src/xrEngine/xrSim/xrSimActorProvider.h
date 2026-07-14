#pragma once

#include "xrSim/xrSimActorRuntime.h"
#include "xrSim/xrSimAgentProvider.h"

#include <memory>

namespace xrSim
{
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
} // namespace xrSim
