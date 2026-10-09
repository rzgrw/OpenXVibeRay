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
    uint32_t materializeRetryTicks = 8;
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
    uint64_t materializeAttempts = 0;
    uint64_t materializeFailures = 0;
    uint64_t materializedGroups = 0;
    uint64_t liveCommandsApplied = 0;
    uint64_t injectedIntents = 0;
};

struct ZoneRuntimeResult
{
    bool ok = false;
    std::string reason;
};

// Orchestrates one Zone scenario: canonical ZoneState, authority registry,
// persistent world agents, the off-frame wake scheduler and the live
// materializer. Lifecycle:
//   BootstrapManifest  - pure world setup (no live adapter, no provider)
//   AttachLive         - bind the live adapter and the intent provider
//   Start/Stop         - gate the 250 ms simulation tick
//   DetachLive         - drop the live adapter (level unload / shutdown)
//   Reset              - back to the empty, un-bootstrapped state
// Bootstrap(manifest, adapter, config, provider) does all three setup steps
// at once and is what the unit tests use.
class ZoneRuntime
{
public:
    ZoneRuntime();
    ~ZoneRuntime();

    ZoneRuntimeResult BootstrapManifest(const ZoneManifest& manifest);
    ZoneRuntimeResult AttachLive(IAgentZoneLiveAdapter& adapter, const ZoneRuntimeConfig& config,
        std::unique_ptr<IWorldIntentProvider> provider);
    ZoneRuntimeResult Bootstrap(const ZoneManifest& manifest, IAgentZoneLiveAdapter& adapter,
        const ZoneRuntimeConfig& config, std::unique_ptr<IWorldIntentProvider> provider);
    ZoneRuntimeResult Start();
    void Stop();
    // releaseObjects=true releases objectives and destroys the spawned objects
    // through the adapter first. false drops the bindings without touching the
    // adapter (the level is already gone).
    void DetachLive(bool releaseObjects);
    void OnFrame(uint64_t nowMs, uint64_t frameNumber);
    void Reset();
    std::string HandleBridgeVerb(const std::string& verb, const std::string& payload, bool& ok);

    ScheduleWakeResult WakeAgent(uint32_t agentId, const std::string& reason, WakePriority priority);
    // Queues a locally authored intent for the next tick boundary. It runs
    // through the same stage/commit/ledger path as a provider response and is
    // recorded with provider "bridge_inject" (debug / deterministic preflight).
    ZoneRuntimeResult InjectIntent(IntentEnvelope envelope);

    void NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille);
    void NotifyDeath(uint16_t targetId, uint16_t sourceId);

    bool IsBootstrapped() const;
    bool IsAttached() const;
    bool IsRunning() const;
    const ZoneManifest& Manifest() const;
    const ZoneState& World() const;
    const AuthorityRegistry& Authority() const;
    const DecisionLedger& Ledger() const;
    const std::vector<WorldAgentRecord>& Agents() const;
    const ZoneRuntimeMetrics& Metrics() const;
    const Materializer* LiveMaterializer() const;
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
