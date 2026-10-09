#pragma once

#include "xrSim/xrSimMaterialization.h"
#include "xrSim/xrSimWakeScheduler.h"

#include <memory>

namespace xrSim
{
enum class ZoneProviderMode : uint8_t
{
    Live,
    Recorded,
    Fault
};

struct ZoneRuntimeConfig
{
    uint32_t tickMs = 250;
    uint32_t observationTicks = 1;
    uint32_t heartbeatTicks = 240;
    uint32_t commandBudget = 2;
    uint32_t spawnBudget = 1;
    uint32_t calmReacquireMs = 5000;
    ZoneProviderMode providerMode = ZoneProviderMode::Live;
    SchedulerConfig scheduler;
};

struct ZoneRuntimeMetrics
{
    uint64_t frameCalls = 0;
    uint64_t simulationTicks = 0;
    uint64_t providerWaitOnFrameUs = 0;
    uint64_t acceptedIntents = 0;
    uint64_t providerCoasts = 0;
    uint64_t codecRejected = 0;
    uint64_t validationRejected = 0;
    uint64_t staleRejected = 0;
    uint64_t freshWakesAfterStale = 0;
    uint64_t legacyObjectiveRejected = 0;
    uint64_t observedEvents = 0;
    uint64_t providerRequests = 0;
    uint64_t inputTokens = 0;
    uint64_t outputTokens = 0;
    uint64_t providerErrors = 0;
    uint64_t retries = 0;
    uint64_t totalLatencyMs = 0;
    uint32_t maxQueueDepth = 0;
    uint32_t maxInflight = 0;
    uint32_t liveEventQueueDepth = 0;
    uint64_t liveEventsDropped = 0;
};

struct ZoneRuntimeResult
{
    bool ok = false;
    std::string reason;
};

class ZoneRuntime
{
public:
    ZoneRuntime();
    ~ZoneRuntime();

    ZoneRuntimeResult Bootstrap(const ZoneManifest& manifest, IAgentZoneLiveAdapter& adapter,
        const ZoneRuntimeConfig& config, std::unique_ptr<IWorldIntentProvider> provider);
    ZoneRuntimeResult Start();
    void OnFrame(uint64_t nowMs, uint64_t frameNumber);
    void Reset();
    std::string HandleBridgeVerb(const std::string& verb, const std::string& payload, bool& ok);

    void NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille);
    void NotifyDeath(uint16_t targetId, uint16_t sourceId);

    const ZoneState& World() const;
    const AuthorityRegistry& Authority() const;
    const DecisionLedger& Ledger() const;
    const std::vector<WorldAgentRecord>& Agents() const;
    const ZoneRuntimeMetrics& Metrics() const;
    WakeScheduler& Scheduler();

    void TransferAllForTest();
    void WakeAgentForTest(uint32_t agentId, const std::string& reason,
        WakePriority priority, uint64_t nowMs);
    void SetWorldRevisionForTest(uint64_t revision);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace xrSim
