#include "xrSim/xrSimWakeScheduler.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace xrSim
{
struct WakeScheduler::Impl
{
    SchedulerConfig config;
    std::unique_ptr<IWorldIntentProvider> provider;
    SchedulerMetrics metrics;

    uint64_t nextRequestId = 1;
    uint64_t nextDispatchSeq = 1;
    uint64_t nextDrainedSeq = 1;

    struct AgentState
    {
        uint64_t lastCompletedMs = 0;
        uint32_t consecutiveFailures = 0;
        uint64_t circuitOpenUntilMs = 0;
        bool inFlight = false;
    };
    std::unordered_map<uint32_t, AgentState> agentStates;

    struct QueuedJob
    {
        uint64_t requestId = 0;
        WorldWakeContext context;
        WakePriority priority = WakePriority::Heartbeat;
        uint64_t eligibleAtMs = 0;
        uint32_t retryCount = 0;
        std::set<std::string> wakeReasons;
    };
    std::vector<QueuedJob> queue;

    struct ActiveJob
    {
        uint64_t dispatchSeq = 0;
        uint64_t requestId = 0;
        uint32_t agentId = 0;
        uint32_t retryCount = 0;
        WorldWakeContext context;
    };
    std::deque<ActiveJob> workerQueue;
    uint32_t activeWorkers = 0;

    std::unordered_map<uint64_t, SchedulerCompletion> completedMap;
    std::vector<SchedulerCompletion> pendingCompletions;

    bool shutdown = false;
    mutable std::mutex mutex;
    std::condition_variable workerCv;
    std::condition_variable idleCv;
    std::vector<std::thread> workers;

    explicit Impl(const SchedulerConfig& cfg, std::unique_ptr<IWorldIntentProvider> prov)
        : config(cfg), provider(std::move(prov))
    {
        metrics.effectiveRequestsPerMinute = config.requestsPerMinute;
        metrics.maxInflightPerAgent = 1;

        const uint32_t numWorkers = std::max(1u, config.maxConcurrent);
        workers.reserve(numWorkers);
        for (uint32_t i = 0; i < numWorkers; ++i)
        {
            workers.emplace_back([this] { WorkerLoop(); });
        }
    }

    ~Impl()
    {
        Shutdown();
    }

    void Shutdown()
    {
        {
            std::lock_guard lock(mutex);
            if (shutdown)
                return;
            shutdown = true;
            if (provider)
                provider->Cancel();
        }
        workerCv.notify_all();
        idleCv.notify_all();

        for (auto& t : workers)
        {
            if (t.joinable())
                t.join();
        }
        workers.clear();
    }

    void WorkerLoop()
    {
        while (true)
        {
            ActiveJob job;
            {
                std::unique_lock lock(mutex);
                workerCv.wait(lock, [this] { return shutdown || !workerQueue.empty(); });
                if (shutdown && workerQueue.empty())
                    return;

                job = std::move(workerQueue.front());
                workerQueue.pop_front();
                ++activeWorkers;
            }

            WorldProviderResult res;
            if (provider)
                res = provider->Wake(job.context);
            else
            {
                res.ok = false;
                res.coast = true;
                res.error = "no_provider";
                res.coastReason = "no_provider";
            }

            {
                std::lock_guard lock(mutex);
                --activeWorkers;

                SchedulerCompletion comp;
                comp.dispatchSeq = job.dispatchSeq;
                comp.requestId = job.requestId;
                comp.agentId = job.agentId;
                comp.retryCount = job.retryCount;
                comp.context = std::move(job.context);
                comp.result = std::move(res);

                pendingCompletions.push_back(std::move(comp));
                idleCv.notify_all();
            }
        }
    }

    ScheduleWakeResult Schedule(WorldWakeContext context, WakePriority priority, uint64_t nowMs)
    {
        std::lock_guard lock(mutex);
        if (shutdown)
            return ScheduleWakeResult{ false, false, 0, "scheduler_shutdown" };

        const uint32_t agentId = context.agent.agentId;
        if (agentId == 0)
            return ScheduleWakeResult{ false, false, 0, "invalid_agent_id" };

        auto& state = agentStates[agentId];
        if (nowMs < state.circuitOpenUntilMs)
            return ScheduleWakeResult{ false, false, 0, "agent_circuit_open" };

        if (state.lastCompletedMs > 0 && nowMs < state.lastCompletedMs + config.minCooldownMs)
            return ScheduleWakeResult{ false, false, 0, "agent_cooldown" };

        for (auto& existing : queue)
        {
            if (existing.context.agent.agentId == agentId)
            {
                if (uint8_t(priority) < uint8_t(existing.priority))
                    existing.priority = priority;

                existing.context = std::move(context);
                if (!existing.context.wakeReason.empty())
                    existing.wakeReasons.insert(existing.context.wakeReason);

                std::string joinedReasons;
                for (const auto& r : existing.wakeReasons)
                {
                    if (!joinedReasons.empty())
                        joinedReasons += ";";
                    joinedReasons += r;
                }
                existing.context.wakeReason = joinedReasons;

                ++metrics.coalesced;
                return ScheduleWakeResult{ true, true, existing.requestId, "" };
            }
        }

        if (queue.size() >= config.maxQueue)
        {
            ++metrics.queueRejected;
            return ScheduleWakeResult{ false, false, 0, "queue_full" };
        }

        const uint64_t reqId = context.requestId != 0 ? context.requestId : nextRequestId++;
        context.requestId = reqId;

        QueuedJob job;
        job.requestId = reqId;
        job.priority = priority;
        job.eligibleAtMs = nowMs;
        job.retryCount = 0;
        if (!context.wakeReason.empty())
            job.wakeReasons.insert(context.wakeReason);
        job.context = std::move(context);

        queue.push_back(std::move(job));
        ++metrics.scheduled;
        metrics.queueDepth = uint32_t(queue.size());
        if (metrics.queueDepth > metrics.maxQueueDepth)
            metrics.maxQueueDepth = metrics.queueDepth;

        return ScheduleWakeResult{ true, false, reqId, "" };
    }

    void Pump(uint64_t nowMs)
    {
        std::lock_guard lock(mutex);

        std::vector<SchedulerCompletion> completionsToProcess;
        completionsToProcess.swap(pendingCompletions);

        for (auto& comp : completionsToProcess)
        {
            auto& state = agentStates[comp.agentId];
            state.inFlight = false;
            state.lastCompletedMs = nowMs;

            if (!comp.result.ok && comp.result.retryable && comp.retryCount < config.maxRetries)
            {
                uint64_t backoff = config.baseBackoffMs * (1ULL << comp.retryCount);
                const std::string jitterKey =
                    std::to_string(comp.requestId) + ":" + std::to_string(comp.retryCount);
                backoff += (StableLedgerHash64(jitterKey) % 251);

                QueuedJob retryJob;
                retryJob.requestId = comp.requestId;
                retryJob.priority = WakePriority::NearPlayer;
                retryJob.eligibleAtMs = nowMs + backoff;
                retryJob.retryCount = comp.retryCount + 1;
                retryJob.context = std::move(comp.context);
                queue.push_back(std::move(retryJob));
                ++metrics.retries;
            }
            else
            {
                if (comp.result.rateLimited)
                {
                    config.requestsPerMinute = std::max(1u, config.requestsPerMinute / 2);
                    metrics.effectiveRequestsPerMinute = config.requestsPerMinute;
                }

                if (!comp.result.ok)
                {
                    ++state.consecutiveFailures;
                    if (state.consecutiveFailures >= config.circuitFailureThreshold)
                    {
                        state.circuitOpenUntilMs = nowMs + config.circuitOpenMs;
                        ++metrics.circuitOpened;
                    }
                }
                else
                {
                    state.consecutiveFailures = 0;
                }

                completedMap[comp.dispatchSeq] = std::move(comp);
            }
        }

        std::sort(queue.begin(), queue.end(), [](const QueuedJob& a, const QueuedJob& b) {
            if (a.priority != b.priority)
                return uint8_t(a.priority) < uint8_t(b.priority);
            return a.eligibleAtMs < b.eligibleAtMs;
        });

        for (auto it = queue.begin(); it != queue.end();)
        {
            if (it->eligibleAtMs > nowMs)
            {
                ++it;
                continue;
            }

            const uint32_t agentId = it->context.agent.agentId;
            auto& state = agentStates[agentId];
            if (state.inFlight)
            {
                ++it;
                continue;
            }

            if (activeWorkers + workerQueue.size() >= config.maxConcurrent)
                break;

            state.inFlight = true;

            ActiveJob active;
            active.dispatchSeq = nextDispatchSeq++;
            active.requestId = it->requestId;
            active.agentId = agentId;
            active.retryCount = it->retryCount;
            active.context = std::move(it->context);

            workerQueue.push_back(std::move(active));
            ++metrics.dispatched;

            it = queue.erase(it);
        }

        metrics.queueDepth = uint32_t(queue.size());
        metrics.inflight = uint32_t(activeWorkers + workerQueue.size());
        if (metrics.inflight > metrics.maxInflight)
            metrics.maxInflight = metrics.inflight;

        workerCv.notify_all();
    }

    std::vector<SchedulerCompletion> DrainReady()
    {
        std::lock_guard lock(mutex);
        std::vector<SchedulerCompletion> drained;

        while (true)
        {
            auto it = completedMap.find(nextDrainedSeq);
            if (it == completedMap.end())
                break;

            drained.push_back(std::move(it->second));
            completedMap.erase(it);
            ++nextDrainedSeq;
            ++metrics.completed;
        }

        return drained;
    }

    void CancelAll()
    {
        std::lock_guard lock(mutex);
        if (provider)
            provider->Cancel();
        queue.clear();
        for (auto& pair : agentStates)
            pair.second.inFlight = false;
        metrics.queueDepth = 0;
    }

    SchedulerMetrics Metrics() const
    {
        std::lock_guard lock(mutex);
        SchedulerMetrics m = metrics;
        m.queueDepth = uint32_t(queue.size());
        m.inflight = uint32_t(activeWorkers + workerQueue.size());
        return m;
    }

    bool WaitForIdleForTest(uint32_t timeoutMs)
    {
        std::unique_lock lock(mutex);
        return idleCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return queue.empty() && workerQueue.empty() && activeWorkers == 0 && pendingCompletions.empty();
        });
    }
};

WakeScheduler::WakeScheduler(const SchedulerConfig& config, std::unique_ptr<IWorldIntentProvider> provider)
    : m_impl(std::make_unique<Impl>(config, std::move(provider)))
{
}

WakeScheduler::~WakeScheduler() = default;

ScheduleWakeResult WakeScheduler::Schedule(WorldWakeContext context, WakePriority priority, uint64_t nowMs)
{
    return m_impl->Schedule(std::move(context), priority, nowMs);
}

void WakeScheduler::Pump(uint64_t nowMs)
{
    m_impl->Pump(nowMs);
}

std::vector<SchedulerCompletion> WakeScheduler::DrainReady()
{
    return m_impl->DrainReady();
}

void WakeScheduler::CancelAll()
{
    m_impl->CancelAll();
}

void WakeScheduler::Shutdown()
{
    m_impl->Shutdown();
}

SchedulerMetrics WakeScheduler::Metrics() const
{
    return m_impl->Metrics();
}

bool WakeScheduler::WaitForIdleForTest(uint32_t timeoutMs)
{
    return m_impl->WaitForIdleForTest(timeoutMs);
}
} // namespace xrSim
