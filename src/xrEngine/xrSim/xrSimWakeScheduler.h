#pragma once

#include "xrSim/xrSimWorldProvider.h"

#include <memory>
#include <vector>

namespace xrSim
{
enum class WakePriority : uint8_t
{
    NearPlayer,
    InvalidatedPlan,
    Strategic,
    Heartbeat
};

struct SchedulerConfig
{
    uint32_t maxConcurrent = 3;
    uint32_t maxQueue = 64;
    uint32_t requestsPerMinute = 20;
    uint32_t minCooldownMs = 5000;
    uint32_t maxRetries = 2;
    uint32_t baseBackoffMs = 1000;
    uint32_t circuitFailureThreshold = 3;
    uint32_t circuitOpenMs = 30000;
};

struct ScheduleWakeResult
{
    bool ok = false;
    bool coalesced = false;
    uint64_t requestId = 0;
    std::string reason;
};

struct SchedulerCompletion
{
    uint64_t dispatchSeq = 0;
    uint64_t requestId = 0;
    uint32_t agentId = 0;
    uint32_t retryCount = 0;
    WorldWakeContext context;
    WorldProviderResult result;
};

struct SchedulerMetrics
{
    uint64_t scheduled = 0;
    uint64_t dispatched = 0;
    uint64_t completed = 0;
    uint64_t coalesced = 0;
    uint64_t queueRejected = 0;
    uint64_t retries = 0;
    uint64_t circuitOpened = 0;
    uint32_t queueDepth = 0;
    uint32_t inflight = 0;
    uint32_t maxQueueDepth = 0;
    uint32_t maxInflight = 0;
    uint32_t maxInflightPerAgent = 0;
    uint32_t effectiveRequestsPerMinute = 20;
};

class WakeScheduler
{
public:
    WakeScheduler(const SchedulerConfig& config, std::unique_ptr<IWorldIntentProvider> provider);
    ~WakeScheduler();

    ScheduleWakeResult Schedule(WorldWakeContext context, WakePriority priority, uint64_t nowMs);
    void Pump(uint64_t nowMs);
    std::vector<SchedulerCompletion> DrainReady();
    void CancelAll();
    void Shutdown();
    SchedulerMetrics Metrics() const;
    bool WaitForIdleForTest(uint32_t timeoutMs);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace xrSim
