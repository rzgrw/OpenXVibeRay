#include "xrSim/xrSimActorProvider.h"

#include "xrSim/xrSimAnthropicTransport.h"

namespace xrSim
{
namespace
{
ActorProviderResult CoastActor(
    const AgentProviderConfig& config, const std::string& reason, const std::string& error = {})
{
    ActorProviderResult result;
    result.ok = true;
    result.coast = true;
    result.provider = config.provider;
    result.model = config.model;
    result.error = error;
    result.coastReason = reason;
    result.plan.coast = true;
    return result;
}

ActorProviderResult FailActor(
    const AgentProviderConfig& config, const std::string& error)
{
    ActorProviderResult result;
    result.provider = config.provider;
    result.model = config.model;
    result.error = error;
    return result;
}
} // namespace

AnthropicActorIntentProvider::AnthropicActorIntentProvider(const AgentProviderConfig& config) : m_config(config) {}

void AnthropicActorIntentProvider::SetTransport(IAnthropicTransport* transport)
{
    m_ownedTransport.reset();
    m_transport = transport;
}

void AnthropicActorIntentProvider::SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport)
{
    m_ownedTransport = std::move(transport);
    m_transport = m_ownedTransport.get();
}

ActorProviderResult AnthropicActorIntentProvider::Wake(const ActorWakeContext& context)
{
    if (!m_config.enabled)
        return CoastActor(m_config, "provider_disabled");
    if (m_config.provider != "anthropic")
        return CoastActor(m_config, "unsupported_provider");
    if (m_config.apiKey.empty())
        return CoastActor(m_config, "missing_api_key");
    if (!m_transport)
        return CoastActor(m_config, "network_adapter_not_linked");

    const AnthropicMessagesRequest request =
        BuildAnthropicMessagesRequestForPrompt(m_config, context.prompt, 1024);
    const AnthropicTransportResult transportResult = m_transport->Send(m_config, request);
    if (!transportResult.ok)
        return CoastActor(m_config, "transport_error", transportResult.error);
    if (transportResult.status != 200)
        return CoastActor(m_config, "http_status", std::to_string(transportResult.status));

    const AnthropicTextResult decoded = DecodeAnthropicMessagesText(transportResult.body);
    if (!decoded.ok)
        return FailActor(m_config, decoded.error);

    const ActorIntentParseResult parsed = ParseActorIntentPlan(decoded.text);
    if (!parsed.ok)
        return FailActor(m_config, parsed.reason);

    ActorProviderResult result;
    result.ok = true;
    result.coast = parsed.plan.coast;
    result.provider = m_config.provider;
    result.model = m_config.model;
    result.plan = parsed.plan;
    return result;
}

void AnthropicActorIntentProvider::Cancel()
{
    if (m_transport)
        m_transport->Cancel();
}

std::unique_ptr<IActorIntentProvider> CreateLiveActorIntentProvider(const AgentProviderConfig& config)
{
    auto provider = std::make_unique<AnthropicActorIntentProvider>(config);
    if (config.provider == "anthropic")
        provider->SetOwnedTransport(CreateAnthropicHttpTransport());
    return provider;
}
} // namespace xrSim
