#include "xrSim/xrSimWorldProvider.h"

#include <chrono>
#include <thread>

namespace xrSim
{
AnthropicWorldIntentProvider::AnthropicWorldIntentProvider(const AgentProviderConfig& config) : m_config(config) {}

void AnthropicWorldIntentProvider::SetTransport(IAnthropicTransport* transport)
{
    m_ownedTransport.reset();
    m_transport = transport;
}

void AnthropicWorldIntentProvider::SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport)
{
    m_ownedTransport = std::move(transport);
    m_transport = m_ownedTransport.get();
}

WorldProviderResult AnthropicWorldIntentProvider::Wake(const WorldWakeContext& context)
{
    WorldProviderResult result;
    result.provider = m_config.provider;
    result.model = m_config.model;

    if (!m_config.enabled)
    {
        result.ok = false;
        result.coast = true;
        result.coastReason = "provider_disabled";
        result.error = "provider_disabled";
        return result;
    }
    if (m_config.apiKey.empty())
    {
        result.ok = false;
        result.coast = true;
        result.coastReason = "missing_api_key";
        result.error = "missing_api_key";
        return result;
    }
    if (!m_transport)
    {
        result.ok = false;
        result.coast = true;
        result.coastReason = "network_adapter_not_linked";
        result.error = "network_adapter_not_linked";
        return result;
    }

    const AnthropicMessagesRequest request =
        BuildAnthropicMessagesRequestForPrompt(m_config, context.prompt, 1024);
    const AnthropicTransportResult transportResult = m_transport->Send(m_config, request);
    result.latencyMs = transportResult.latencyMs;

    if (!transportResult.ok)
    {
        result.ok = false;
        result.coast = true;
        result.retryable = true;
        result.coastReason = "transport_error";
        result.error = transportResult.error;
        return result;
    }

    if (transportResult.status != 200)
    {
        result.ok = false;
        result.coast = true;
        result.error = "http_status_" + std::to_string(transportResult.status);
        result.coastReason = "http_status";
        if (transportResult.status == 429)
        {
            result.retryable = true;
            result.rateLimited = true;
        }
        else if (transportResult.status >= 500 && transportResult.status <= 504)
        {
            result.retryable = true;
        }
        return result;
    }

    const AnthropicTextResult decoded = DecodeAnthropicMessagesText(transportResult.body);
    if (!decoded.ok)
    {
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:" + decoded.error;
        return result;
    }

    result.usage.inputTokens = decoded.inputTokens;
    result.usage.outputTokens = decoded.outputTokens;
    result.responseHash = StableLedgerHash(decoded.text);

    const IntentParseResult parsed = ParseWorldIntentEnvelope(decoded.text);
    if (!parsed.ok)
    {
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:" + parsed.reason;
        return result;
    }

    if (parsed.envelope.requestId != context.requestId || parsed.envelope.agentId != context.agent.agentId ||
        parsed.envelope.scope != context.agent.scope || parsed.envelope.baseRevision != context.baseRevision)
    {
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:context_identity_mismatch";
        return result;
    }

    result.ok = true;
    result.intent = parsed.envelope;
    return result;
}

void AnthropicWorldIntentProvider::Cancel()
{
    if (m_transport)
        m_transport->Cancel();
}

GeminiWorldIntentProvider::GeminiWorldIntentProvider(const AgentProviderConfig& config) : m_config(config) {}

void GeminiWorldIntentProvider::SetTransport(IGeminiTransport* transport)
{
    m_ownedTransport.reset();
    m_transport = transport;
}

void GeminiWorldIntentProvider::SetOwnedTransport(std::unique_ptr<IGeminiTransport> transport)
{
    m_ownedTransport = std::move(transport);
    m_transport = m_ownedTransport.get();
}

WorldProviderResult GeminiWorldIntentProvider::Wake(const WorldWakeContext& context)
{
    WorldProviderResult result;
    result.provider = m_config.provider;
    result.model = m_config.model;

    if (!m_config.enabled)
    {
        result.ok = false;
        result.coast = true;
        result.coastReason = "provider_disabled";
        result.error = "provider_disabled";
        return result;
    }
    if (m_config.apiKey.empty())
    {
        result.ok = false;
        result.coast = true;
        result.coastReason = "missing_api_key";
        result.error = "missing_api_key";
        return result;
    }
    if (!m_transport)
    {
        result.ok = false;
        result.coast = true;
        result.coastReason = "network_adapter_not_linked";
        result.error = "network_adapter_not_linked";
        return result;
    }

    const GeminiGenerateContentRequest request =
        BuildGeminiGenerateContentRequestForPrompt(m_config, context.prompt, 1024);
    const GeminiTransportResult transportResult = m_transport->Send(m_config, request);
    result.latencyMs = transportResult.latencyMs;

    if (!transportResult.ok)
    {
        result.ok = false;
        result.coast = true;
        result.retryable = true;
        result.coastReason = "transport_error";
        result.error = transportResult.error;
        return result;
    }

    if (transportResult.status != 200)
    {
        result.ok = false;
        result.coast = true;
        result.error = "http_status_" + std::to_string(transportResult.status);
        result.coastReason = "http_status";
        if (transportResult.status == 429)
        {
            result.retryable = true;
            result.rateLimited = true;
        }
        else if (transportResult.status >= 500 && transportResult.status <= 504)
        {
            result.retryable = true;
        }
        return result;
    }

    const GeminiTextResult decoded = DecodeGeminiGenerateContentText(transportResult.body);
    if (!decoded.ok)
    {
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:" + decoded.error;
        return result;
    }

    result.usage.inputTokens = decoded.inputTokens;
    result.usage.outputTokens = decoded.outputTokens;
    result.responseHash = StableLedgerHash(decoded.text);

    const IntentParseResult parsed = ParseWorldIntentEnvelope(decoded.text);
    if (!parsed.ok)
    {
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:" + parsed.reason;
        return result;
    }

    if (parsed.envelope.requestId != context.requestId || parsed.envelope.agentId != context.agent.agentId ||
        parsed.envelope.scope != context.agent.scope || parsed.envelope.baseRevision != context.baseRevision)
    {
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:context_identity_mismatch";
        return result;
    }

    result.ok = true;
    result.intent = parsed.envelope;
    return result;
}

void GeminiWorldIntentProvider::Cancel()
{
    if (m_transport)
        m_transport->Cancel();
}

RecordedWorldIntentProvider::RecordedWorldIntentProvider(std::vector<WorldProviderResult> script)
    : m_script(std::move(script))
{
}

WorldProviderResult RecordedWorldIntentProvider::Wake(const WorldWakeContext& context)
{
    (void)context;
    if (m_cursor >= m_script.size())
    {
        WorldProviderResult result;
        result.ok = false;
        result.error = "recorded_provider_exhausted";
        return result;
    }
    return m_script[m_cursor++];
}

FaultWorldIntentProvider::FaultWorldIntentProvider(InjectedProviderFault fault) : m_fault(fault) {}

WorldProviderResult FaultWorldIntentProvider::Wake(const WorldWakeContext& context)
{
    (void)context;
    WorldProviderResult result;
    if (m_cancelled || m_fault == InjectedProviderFault::Cancelled)
    {
        result.ok = false;
        result.coast = true;
        result.error = "cancelled";
        result.coastReason = "cancelled";
        return result;
    }

    switch (m_fault)
    {
    case InjectedProviderFault::Timeout:
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        result.ok = false;
        result.coast = true;
        result.retryable = true;
        result.error = "timeout";
        result.coastReason = "timeout";
        return result;
    case InjectedProviderFault::RateLimit:
        result.ok = false;
        result.coast = true;
        result.retryable = true;
        result.rateLimited = true;
        result.error = "http_status_429";
        result.coastReason = "http_status";
        return result;
    case InjectedProviderFault::TransportError:
        result.ok = false;
        result.coast = true;
        result.retryable = true;
        result.error = "transport_error";
        result.coastReason = "transport_error";
        return result;
    case InjectedProviderFault::Malformed:
        result.ok = false;
        result.coast = false;
        result.retryable = false;
        result.error = "codec_rejected:malformed";
        return result;
    case InjectedProviderFault::None:
    case InjectedProviderFault::Cancelled:
        break;
    }

    result.ok = true;
    return result;
}

void FaultWorldIntentProvider::Cancel()
{
    m_cancelled = true;
}

std::unique_ptr<IWorldIntentProvider> CreateLiveWorldIntentProvider(const AgentProviderConfig& config)
{
    if (config.provider == "gemini" || config.provider == "google")
    {
        auto provider = std::make_unique<GeminiWorldIntentProvider>(config);
        provider->SetOwnedTransport(CreateGeminiHttpTransport());
        return provider;
    }

    auto provider = std::make_unique<AnthropicWorldIntentProvider>(config);
    if (config.provider == "anthropic")
        provider->SetOwnedTransport(CreateAnthropicHttpTransport());
    return provider;
}
} // namespace xrSim
