#include "xrSim/xrSimActorProvider.h"

#include "xrSim/xrSimAnthropicTransport.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

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

struct AsyncActorProviderQueue::Impl
{
    struct Job
    {
        uint64_t requestId = 0;
        ActorWakeContext context;
    };

    struct Request
    {
        enum class State
        {
            Pending,
            Ready,
            Failed,
        };

        State state = State::Pending;
        std::string reason;
        ActorProviderResult result;
    };

    explicit Impl(std::unique_ptr<IActorIntentProvider> providerValue) : provider(std::move(providerValue))
    {
        if (provider)
            worker = std::thread([this] { WorkerLoop(); });
        else
            shutdown = true;
    }

    void WorkerLoop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock lock{mutex};
                wake.wait(lock, [this] { return shutdown || !jobs.empty(); });
                if (shutdown && jobs.empty())
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }

            ActorProviderResult result = provider->Wake(job.context);

            std::lock_guard guard{mutex};
            const auto request = requests.find(job.requestId);
            if (request != requests.end() && request->second.state == Request::State::Pending)
            {
                request->second.state = Request::State::Ready;
                request->second.result = std::move(result);
            }
        }
    }

    std::unique_ptr<IActorIntentProvider> provider;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::unordered_map<uint64_t, Request> requests;
    uint64_t nextRequestId = 1;
    bool shutdown = false;
};

AsyncActorProviderQueue::AsyncActorProviderQueue(std::unique_ptr<IActorIntentProvider> provider)
    : m_impl(std::make_unique<Impl>(std::move(provider)))
{
}

AsyncActorProviderQueue::~AsyncActorProviderQueue() { Shutdown(); }

ActorWakeSubmitResult AsyncActorProviderQueue::Submit(const ActorWakeContext& context)
{
    ActorWakeSubmitResult result;
    if (!m_impl)
    {
        result.reason = "queue_shutdown";
        return result;
    }

    {
        std::lock_guard guard{m_impl->mutex};
        if (m_impl->shutdown)
        {
            result.reason = "queue_shutdown";
            return result;
        }

        result.requestId = m_impl->nextRequestId++;
        m_impl->requests.emplace(result.requestId, Impl::Request{});
        m_impl->jobs.push_back(Impl::Job{result.requestId, context});
    }
    m_impl->wake.notify_one();
    result.ok = true;
    return result;
}

ActorWakePollResult AsyncActorProviderQueue::Poll(uint64_t requestId)
{
    ActorWakePollResult result;
    if (!m_impl)
    {
        result.reason = "queue_shutdown";
        return result;
    }

    std::lock_guard guard{m_impl->mutex};
    const auto request = m_impl->requests.find(requestId);
    if (request == m_impl->requests.end())
    {
        result.reason = "unknown_request";
        return result;
    }

    if (request->second.state == Impl::Request::State::Failed)
    {
        result.ready = true;
        result.reason = request->second.reason;
        m_impl->requests.erase(request);
        return result;
    }

    result.ok = true;
    result.ready = request->second.state == Impl::Request::State::Ready;
    if (result.ready)
    {
        result.providerResult = std::move(request->second.result);
        m_impl->requests.erase(request);
    }
    return result;
}

void AsyncActorProviderQueue::Shutdown()
{
    if (!m_impl)
        return;

    {
        std::lock_guard guard{m_impl->mutex};
        if (m_impl->shutdown)
        {
            if (!m_impl->worker.joinable())
                return;
        }
        else
        {
            m_impl->shutdown = true;
            m_impl->jobs.clear();
            for (auto& [requestId, request] : m_impl->requests)
            {
                (void)requestId;
                if (request.state == Impl::Request::State::Pending)
                {
                    request.state = Impl::Request::State::Failed;
                    request.reason = "queue_shutdown";
                }
            }
        }
    }

    if (m_impl->provider)
        m_impl->provider->Cancel();
    m_impl->wake.notify_all();
    if (m_impl->worker.joinable())
        m_impl->worker.join();
}
} // namespace xrSim
