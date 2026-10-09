#include "xrSim/xrSimZoneRuntime.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>

namespace xrSim
{
namespace
{
AgentScopeKind ZoneRuntimeParseScopeKindString(const std::string& text)
{
    if (text == "zone") return AgentScopeKind::Zone;
    if (text == "faction") return AgentScopeKind::Faction;
    if (text == "ecology") return AgentScopeKind::Ecology;
    if (text == "economy") return AgentScopeKind::Economy;
    if (text == "incident") return AgentScopeKind::Incident;
    if (text == "region") return AgentScopeKind::Region;
    if (text == "squad") return AgentScopeKind::Squad;
    if (text == "mutant_pack") return AgentScopeKind::MutantPack;
    if (text == "named_actor") return AgentScopeKind::NamedActor;
    return AgentScopeKind::Zone;
}

ProviderVerdict ClassifyProviderVerdict(const WorldProviderResult& result)
{
    if (result.ok)
        return ProviderVerdict::Accepted;
    if (result.coast)
    {
        if (result.coastReason == "timeout")
            return ProviderVerdict::Timeout;
        if (result.rateLimited)
            return ProviderVerdict::RateLimited;
        if (result.coastReason == "transport_error")
            return ProviderVerdict::TransportError;
        if (result.coastReason == "cancelled")
            return ProviderVerdict::Cancelled;
        return ProviderVerdict::Coast;
    }
    return ProviderVerdict::CodecRejected;
}

bool IsProviderError(const WorldProviderResult& result)
{
    return !result.ok && result.coast &&
        (result.coastReason == "timeout" || result.coastReason == "transport_error" ||
            result.coastReason == "http_status");
}

PlanOpKind ParseInjectedOpKind(AgentScopeKind scopeKind, const std::string& verb)
{
    if (scopeKind == AgentScopeKind::Squad || scopeKind == AgentScopeKind::MutantPack)
    {
        if (verb == "move")
            return PlanOpKind::Move;
        if (verb == "retreat")
            return PlanOpKind::Retreat;
        if (verb == "resupply")
            return PlanOpKind::Resupply;
    }
    return PlanOpKind::SetObjective;
}
} // namespace

struct ZoneRuntime::Impl
{
    ZoneManifest manifest;
    ZoneState world;
    AuthorityRegistry authority;
    DecisionLedger ledger;
    std::vector<WorldAgentRecord> agents;
    std::unique_ptr<Materializer> materializer;
    std::unique_ptr<WakeScheduler> scheduler;
    ZoneRuntimeConfig config;
    ZoneRuntimeMetrics metrics;

    bool bootstrapped = false;
    bool running = false;
    bool materializationComplete = false;
    uint64_t materializeRetryAtTick = 0;
    std::string lastMaterializeReason;
    uint64_t nextTickMs = 0;
    uint64_t lastNowMs = 0;
    uint64_t simulationTick = 0;
    uint64_t nextRequestId = 1;

    std::vector<IntentEnvelope> injectedIntents;

    struct LiveEventInboxItem
    {
        uint16_t targetId = 0;
        uint16_t sourceId = 0;
        uint32_t damagePermille = 0;
        bool isDeath = false;
    };
    std::deque<LiveEventInboxItem> liveInbox;
    std::mutex inboxMutex;

    const WorldAgentRecord* FindAgent(uint32_t agentId) const
    {
        for (const auto& a : agents)
        {
            if (a.agentId == agentId)
                return &a;
        }
        return nullptr;
    }

    void ClearMaterializationState()
    {
        materializationComplete = false;
        materializeRetryAtTick = 0;
        lastMaterializeReason.clear();
    }

    void MaterializeStep()
    {
        if (!materializer || materializationComplete)
            return;
        if (simulationTick < materializeRetryAtTick)
            return;

        ++metrics.materializeAttempts;
        const MaterializeResult res = materializer->Bootstrap(world, config.spawnBudget);
        metrics.materializedGroups = materializer->Bindings().size();
        if (!res.ok)
        {
            ++metrics.materializeFailures;
            lastMaterializeReason = res.reason;
            materializeRetryAtTick = simulationTick + std::max(1u, config.materializeRetryTicks);
            return;
        }
        materializationComplete = res.complete;
        lastMaterializeReason = res.complete ? "complete" : "partial";
    }

    // Shared stage/commit/ledger path for provider completions and injected intents.
    void StageAndRecord(const IntentEnvelope& envelope, RequestLedgerRecord& rec,
        const WorldAgentRecord* refreshAgent, uint64_t nowMs)
    {
        StageIntentResult stagedRes = StageIntent(world, authority, envelope, simulationTick);
        if (stagedRes.ok)
        {
            CommitIntentResult commitRes =
                CommitStagedIntent(world, authority, std::move(stagedRes.staged), simulationTick);
            if (commitRes.ok)
            {
                ++metrics.acceptedIntents;
                rec.providerVerdict = ProviderVerdict::Accepted;
                rec.validationVerdict = "accepted";
                rec.committedRevision = commitRes.committedRevision;
                rec.deltas = commitRes.deltas;
                return;
            }
            if (commitRes.stale)
            {
                ++metrics.staleRejected;
                rec.providerVerdict = ProviderVerdict::StaleRejected;
                rec.validationVerdict = commitRes.reason.empty() ? "stale_revision" : commitRes.reason;
                ScheduleStaleRefresh(refreshAgent, nowMs);
                return;
            }
            ++metrics.validationRejected;
            rec.providerVerdict = ProviderVerdict::ValidationRejected;
            rec.validationVerdict = commitRes.reason;
            return;
        }

        if (stagedRes.reason == "stale_revision")
        {
            ++metrics.staleRejected;
            rec.providerVerdict = ProviderVerdict::StaleRejected;
            rec.validationVerdict = stagedRes.reason;
            ScheduleStaleRefresh(refreshAgent, nowMs);
            return;
        }

        ++metrics.validationRejected;
        rec.providerVerdict = ProviderVerdict::ValidationRejected;
        rec.validationVerdict = stagedRes.reason;
    }

    void ScheduleStaleRefresh(const WorldAgentRecord* agent, uint64_t nowMs)
    {
        if (!agent || !scheduler)
            return;
        ++metrics.freshWakesAfterStale;
        const uint64_t newReq = nextRequestId++;
        const WorldWakeContext freshCtx =
            PrepareWorldWake(newReq, world, *agent, simulationTick, "stale_refresh");
        scheduler->Schedule(freshCtx, WakePriority::InvalidatedPlan, nowMs);
    }

    void ProcessCompletion(SchedulerCompletion& comp, uint64_t nowMs)
    {
        ++metrics.providerRequests;
        metrics.totalLatencyMs += comp.result.latencyMs;
        metrics.inputTokens += comp.result.usage.inputTokens;
        metrics.outputTokens += comp.result.usage.outputTokens;
        metrics.retries += comp.retryCount;
        if (IsProviderError(comp.result))
            ++metrics.providerErrors;

        RequestLedgerRecord rec;
        rec.requestId = comp.requestId;
        rec.agentId = comp.agentId;
        rec.scope = comp.context.agent.scope;
        rec.baseRevision = comp.context.baseRevision;
        rec.wakeReason = comp.context.wakeReason;
        rec.promptHash = StableLedgerHash(comp.context.prompt);
        rec.responseHash = comp.result.responseHash;
        rec.provider = comp.result.provider;
        rec.model = comp.result.model;
        rec.usage = comp.result.usage;
        rec.latencyMs = comp.result.latencyMs;
        rec.retryCount = comp.retryCount;
        rec.providerVerdict = ClassifyProviderVerdict(comp.result);

        if (comp.result.ok)
        {
            rec.codecVerdict = "decoded";
            StageAndRecord(comp.result.intent, rec, &comp.context.agent, nowMs);
        }
        else if (comp.result.coast)
        {
            ++metrics.providerCoasts;
            rec.codecVerdict = comp.result.coastReason.empty() ? comp.result.error : comp.result.coastReason;
            rec.validationVerdict = "coast";
        }
        else
        {
            ++metrics.codecRejected;
            rec.codecVerdict = comp.result.error.empty() ? "codec_rejected" : comp.result.error;
            rec.validationVerdict = "not_staged";
        }

        ledger.AppendRequest(rec);
    }

    void ProcessInjectedIntent(IntentEnvelope envelope, uint64_t nowMs)
    {
        ++metrics.injectedIntents;
        // An injection without an explicit base revision targets the revision current at
        // the tick it is processed, so several sequential bridge injections do not go stale.
        if (envelope.baseRevision == 0)
            envelope.baseRevision = world.Revision();
        const std::string text = FormatWorldIntentEnvelope(envelope);

        RequestLedgerRecord rec;
        rec.requestId = envelope.requestId;
        rec.agentId = envelope.agentId;
        rec.scope = envelope.scope;
        rec.baseRevision = envelope.baseRevision;
        rec.wakeReason = envelope.wakeReason;
        rec.promptHash = StableLedgerHash(text);
        rec.responseHash = rec.promptHash;
        rec.provider = "bridge_inject";
        rec.model = "deterministic";
        rec.codecVerdict = "injected";
        StageAndRecord(envelope, rec, nullptr, nowMs);
        ledger.AppendRequest(rec);
    }
};

ZoneRuntime::ZoneRuntime() : m_impl(std::make_unique<Impl>()) {}

ZoneRuntime::~ZoneRuntime()
{
    // Workers must be joined before the world/ledger they reference disappears.
    DetachLive(false);
}

ZoneRuntimeResult ZoneRuntime::BootstrapManifest(const ZoneManifest& manifest)
{
    if (IsAttached())
        DetachLive(true);

    m_impl->world = ZoneState();
    const ZoneResult zoneRes = m_impl->world.Bootstrap(manifest);
    if (!zoneRes.ok)
    {
        m_impl->bootstrapped = false;
        return ZoneRuntimeResult{ false, "world_bootstrap_failed: " + zoneRes.reason };
    }

    m_impl->manifest = manifest;
    m_impl->authority = AuthorityRegistry();
    for (const auto& a : manifest.agents)
    {
        const ScopeId scope{ ZoneRuntimeParseScopeKindString(a.scopeKind), a.scopeId };
        m_impl->authority.Register(scope, a.agentId, m_impl->world.Revision());
    }
    m_impl->agents = CreateWorldAgents(manifest);
    m_impl->ledger = DecisionLedger();
    m_impl->injectedIntents.clear();
    m_impl->nextTickMs = 0;
    m_impl->simulationTick = 0;
    m_impl->nextRequestId = 1;
    m_impl->ClearMaterializationState();
    m_impl->bootstrapped = true;
    m_impl->running = false;
    return ZoneRuntimeResult{ true, "" };
}

ZoneRuntimeResult ZoneRuntime::AttachLive(
    IAgentZoneLiveAdapter& adapter, const ZoneRuntimeConfig& config, std::unique_ptr<IWorldIntentProvider> provider)
{
    if (!m_impl->bootstrapped)
        return ZoneRuntimeResult{ false, "runtime_not_bootstrapped" };
    if (IsAttached())
        return ZoneRuntimeResult{ false, "live_adapter_already_attached" };

    m_impl->config = config;
    m_impl->materializer = std::make_unique<Materializer>(adapter);
    m_impl->scheduler = std::make_unique<WakeScheduler>(config.scheduler, std::move(provider));
    m_impl->ClearMaterializationState();
    return ZoneRuntimeResult{ true, "" };
}

ZoneRuntimeResult ZoneRuntime::Bootstrap(
    const ZoneManifest& manifest,
    IAgentZoneLiveAdapter& adapter,
    const ZoneRuntimeConfig& config,
    std::unique_ptr<IWorldIntentProvider> provider)
{
    const ZoneRuntimeResult boot = BootstrapManifest(manifest);
    if (!boot.ok)
        return boot;
    const ZoneRuntimeResult attach = AttachLive(adapter, config, std::move(provider));
    if (!attach.ok)
        return attach;
    return Start();
}

ZoneRuntimeResult ZoneRuntime::Start()
{
    if (!m_impl->bootstrapped)
        return ZoneRuntimeResult{ false, "runtime_not_bootstrapped" };
    if (!IsAttached())
        return ZoneRuntimeResult{ false, "live_adapter_not_attached" };
    m_impl->running = true;
    return ZoneRuntimeResult{ true, "" };
}

void ZoneRuntime::Stop()
{
    m_impl->running = false;
}

void ZoneRuntime::DetachLive(bool releaseObjects)
{
    m_impl->running = false;
    if (m_impl->scheduler)
        m_impl->scheduler->CancelAll();
    if (m_impl->materializer && releaseObjects)
        m_impl->materializer->ReleaseAll();
    m_impl->materializer.reset();
    m_impl->scheduler.reset();
    m_impl->ClearMaterializationState();
}

void ZoneRuntime::Reset()
{
    DetachLive(true);
    m_impl->bootstrapped = false;
    m_impl->manifest = ZoneManifest();
    m_impl->world = ZoneState();
    m_impl->authority = AuthorityRegistry();
    m_impl->ledger = DecisionLedger();
    m_impl->agents.clear();
    m_impl->injectedIntents.clear();
    m_impl->metrics = ZoneRuntimeMetrics();
    m_impl->nextTickMs = 0;
    m_impl->simulationTick = 0;
    m_impl->nextRequestId = 1;
    {
        std::lock_guard lock(m_impl->inboxMutex);
        m_impl->liveInbox.clear();
    }
}

void ZoneRuntime::NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille)
{
    std::lock_guard lock(m_impl->inboxMutex);
    if (m_impl->liveInbox.size() >= 256)
    {
        ++m_impl->metrics.liveEventsDropped;
        return;
    }
    Impl::LiveEventInboxItem item;
    item.targetId = targetId;
    item.sourceId = sourceId;
    item.damagePermille = damagePermille;
    item.isDeath = false;
    m_impl->liveInbox.push_back(item);
}

void ZoneRuntime::NotifyDeath(uint16_t targetId, uint16_t sourceId)
{
    std::lock_guard lock(m_impl->inboxMutex);
    if (m_impl->liveInbox.size() >= 256)
    {
        ++m_impl->metrics.liveEventsDropped;
        return;
    }
    Impl::LiveEventInboxItem item;
    item.targetId = targetId;
    item.sourceId = sourceId;
    item.damagePermille = 1000;
    item.isDeath = true;
    m_impl->liveInbox.push_back(item);
}

bool ZoneRuntime::IsBootstrapped() const { return m_impl->bootstrapped; }
bool ZoneRuntime::IsAttached() const { return m_impl->materializer && m_impl->scheduler; }
bool ZoneRuntime::IsRunning() const { return m_impl->running; }
const ZoneManifest& ZoneRuntime::Manifest() const { return m_impl->manifest; }
const ZoneState& ZoneRuntime::World() const { return m_impl->world; }
const AuthorityRegistry& ZoneRuntime::Authority() const { return m_impl->authority; }
const DecisionLedger& ZoneRuntime::Ledger() const { return m_impl->ledger; }
const std::vector<WorldAgentRecord>& ZoneRuntime::Agents() const { return m_impl->agents; }
const ZoneRuntimeMetrics& ZoneRuntime::Metrics() const { return m_impl->metrics; }
const Materializer* ZoneRuntime::LiveMaterializer() const { return m_impl->materializer.get(); }
WakeScheduler& ZoneRuntime::Scheduler() { return *m_impl->scheduler; }

ScheduleWakeResult ZoneRuntime::WakeAgent(uint32_t agentId, const std::string& reason, WakePriority priority)
{
    if (!m_impl->bootstrapped)
        return ScheduleWakeResult{ false, false, 0, "not_bootstrapped" };
    if (!m_impl->scheduler)
        return ScheduleWakeResult{ false, false, 0, "live_adapter_not_attached" };
    const WorldAgentRecord* agent = m_impl->FindAgent(agentId);
    if (!agent)
        return ScheduleWakeResult{ false, false, 0, "unknown_agent" };

    const uint64_t reqId = m_impl->nextRequestId++;
    const WorldWakeContext ctx = PrepareWorldWake(reqId, m_impl->world, *agent, m_impl->simulationTick, reason);
    return m_impl->scheduler->Schedule(ctx, priority, m_impl->lastNowMs);
}

ZoneRuntimeResult ZoneRuntime::InjectIntent(IntentEnvelope envelope)
{
    if (!m_impl->bootstrapped)
        return ZoneRuntimeResult{ false, "not_bootstrapped" };
    const WorldAgentRecord* agent = m_impl->FindAgent(envelope.agentId);
    if (!agent)
        return ZoneRuntimeResult{ false, "unknown_agent" };
    if (envelope.operations.empty())
        return ZoneRuntimeResult{ false, "no_operations" };
    if (m_impl->injectedIntents.size() >= 64)
        return ZoneRuntimeResult{ false, "inject_queue_full" };

    envelope.scope = agent->scope;
    if (envelope.requestId == 0)
        envelope.requestId = m_impl->nextRequestId++;
    if (envelope.wakeReason.empty())
        envelope.wakeReason = "bridge_inject";
    m_impl->injectedIntents.push_back(std::move(envelope));
    return ZoneRuntimeResult{ true, std::to_string(m_impl->injectedIntents.back().requestId) };
}

void ZoneRuntime::TransferAllForTest()
{
    m_impl->world.SetRevision(3);
    for (const auto& a : m_impl->agents)
    {
        m_impl->authority.BeginShadow(a.scope, 2);
        m_impl->authority.BeginTransfer(a.scope, 3);
        m_impl->authority.CommitTransfer(a.scope, 3);
    }
}

void ZoneRuntime::WakeAgentForTest(
    uint32_t agentId, const std::string& reason, WakePriority priority, uint64_t nowMs)
{
    if (!m_impl->scheduler)
        return;
    const WorldAgentRecord* agent = m_impl->FindAgent(agentId);
    if (!agent)
        return;
    const uint64_t reqId = m_impl->nextRequestId++;
    const WorldWakeContext ctx = PrepareWorldWake(reqId, m_impl->world, *agent, m_impl->simulationTick, reason);
    m_impl->scheduler->Schedule(ctx, priority, nowMs);
}

void ZoneRuntime::SetWorldRevisionForTest(uint64_t revision)
{
    m_impl->world.SetRevision(revision);
}

void ZoneRuntime::OnFrame(uint64_t nowMs, uint64_t frameNumber)
{
    (void)frameNumber;
    ++m_impl->metrics.frameCalls;
    m_impl->lastNowMs = nowMs;

    if (!m_impl->running || !m_impl->bootstrapped)
        return;

    if (m_impl->scheduler)
        m_impl->scheduler->Pump(nowMs);

    const uint64_t tickMs = m_impl->config.tickMs ? m_impl->config.tickMs : 250;
    if (m_impl->nextTickMs == 0)
        m_impl->nextTickMs = (nowMs / tickMs) * tickMs + tickMs;

    if (nowMs < m_impl->nextTickMs)
        return;

    m_impl->nextTickMs += tickMs;
    ++m_impl->metrics.simulationTicks;
    ++m_impl->simulationTick;

    // 0. Materialize the manifest groups (budgeted: at most spawnBudget groups per tick)
    m_impl->MaterializeStep();

    // 1. Drain live inbox
    std::deque<Impl::LiveEventInboxItem> inboxItems;
    {
        std::lock_guard lock(m_impl->inboxMutex);
        inboxItems.swap(m_impl->liveInbox);
    }

    for (const auto& item : inboxItems)
    {
        WorldEvent ev;
        ev.tick = m_impl->simulationTick;
        ev.kind = item.isDeath ? "live_death" : "live_hit";
        ev.amount = int32_t(item.damagePermille);
        ev.source = "live_observation";
        m_impl->world.AppendEvent(ev);
        ++m_impl->metrics.observedEvents;
    }

    // 2. Poll observations from materializer
    if (m_impl->materializer)
    {
        ObservationBatch batch = m_impl->materializer->PollObservations(m_impl->world, m_impl->simulationTick);
        for (const auto& ev : batch.events)
        {
            m_impl->world.AppendEvent(ev);
            ++m_impl->metrics.observedEvents;
        }
    }

    // 3. Expire objectives
    for (auto& obj : m_impl->world.MutableObjectives())
    {
        if (obj.active && obj.expiresAtTick > 0 && m_impl->simulationTick >= obj.expiresAtTick)
            obj.active = false;
    }

    // 4. Pump scheduler
    if (m_impl->scheduler)
        m_impl->scheduler->Pump(nowMs);

    // 5. Drain completed responses (dispatch order) and injected intents, commit valid intents
    if (m_impl->scheduler)
    {
        std::vector<SchedulerCompletion> ready = m_impl->scheduler->DrainReady();
        for (auto& comp : ready)
            m_impl->ProcessCompletion(comp, nowMs);

        const SchedulerMetrics sm = m_impl->scheduler->Metrics();
        m_impl->metrics.maxQueueDepth = std::max(m_impl->metrics.maxQueueDepth, sm.maxQueueDepth);
        m_impl->metrics.maxInflight = std::max(m_impl->metrics.maxInflight, sm.maxInflight);
    }
    if (!m_impl->injectedIntents.empty())
    {
        std::vector<IntentEnvelope> injected;
        injected.swap(m_impl->injectedIntents);
        for (const auto& envelope : injected)
            m_impl->ProcessInjectedIntent(envelope, nowMs);
    }

    // 6. Materialize committed objectives
    if (m_impl->materializer)
    {
        const uint64_t calmTicks =
            std::max<uint64_t>(1, m_impl->config.calmReacquireMs / (m_impl->config.tickMs ? m_impl->config.tickMs : 250));
        const MaterializeResult applied = m_impl->materializer->ApplyCommittedState(
            m_impl->world, m_impl->config.commandBudget, m_impl->simulationTick, calmTicks);
        m_impl->metrics.liveCommandsApplied += applied.applied;
    }
}

std::string ZoneRuntime::HandleBridgeVerb(
    const std::string& verb, const std::string& payload, bool& ok)
{
    ok = true;
    if (verb == "agent.zone.bootstrap")
    {
        if (payload.empty() || payload.size() > 1024)
        {
            ok = false;
            return "invalid_manifest_path";
        }
        std::ifstream file(payload);
        if (!file.is_open())
        {
            ok = false;
            return "cannot_open_manifest: " + payload;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        const ZoneManifestParseResult parsed = ParseZoneManifest(ss.str());
        if (!parsed.ok)
        {
            ok = false;
            return "manifest_parse_failed: " + parsed.reason;
        }

        const ZoneRuntimeResult boot = BootstrapManifest(parsed.manifest);
        if (!boot.ok)
        {
            ok = false;
            return "bootstrap_failed: " + boot.reason;
        }

        std::ostringstream out;
        out << "scenario=" << parsed.manifest.scenario << " level=" << parsed.manifest.levelName
            << " revision=" << m_impl->world.Revision() << " groups=" << parsed.manifest.groups.size()
            << " agents=" << m_impl->agents.size();
        return out.str();
    }
    if (verb == "agent.zone.start")
    {
        if (!m_impl->bootstrapped)
        {
            ok = false;
            return "not_bootstrapped";
        }
        const ZoneRuntimeResult started = Start();
        if (!started.ok)
        {
            ok = false;
            return started.reason;
        }
        return "running";
    }
    if (verb == "agent.zone.shadow" || verb == "agent.zone.shadow all")
    {
        if (!m_impl->bootstrapped)
        {
            ok = false;
            return "not_bootstrapped";
        }
        if (verb == "agent.zone.shadow" && payload != "all")
        {
            ok = false;
            return "usage: agent.zone.shadow all";
        }
        const uint64_t revision = m_impl->world.Revision() + 1;
        uint32_t shadowed = 0;
        uint32_t rejected = 0;
        for (const auto& a : m_impl->agents)
        {
            if (m_impl->authority.BeginShadow(a.scope, revision).ok)
                ++shadowed;
            else
                ++rejected;
        }
        m_impl->world.SetRevision(revision);
        std::ostringstream out;
        out << "shadowed=" << shadowed << " rejected=" << rejected << " revision=" << revision;
        return out.str();
    }
    if (verb == "agent.zone.transfer" || verb == "agent.zone.transfer all")
    {
        if (!m_impl->bootstrapped)
        {
            ok = false;
            return "not_bootstrapped";
        }
        if (verb == "agent.zone.transfer" && payload != "all")
        {
            ok = false;
            return "usage: agent.zone.transfer all";
        }
        const uint64_t revision = m_impl->world.Revision() + 1;
        uint32_t transferred = 0;
        uint32_t rejected = 0;
        for (const auto& a : m_impl->agents)
        {
            const AuthorityResult begun = m_impl->authority.BeginTransfer(a.scope, revision);
            if (begun.ok && m_impl->authority.CommitTransfer(a.scope, revision).ok)
                ++transferred;
            else
                ++rejected;
        }
        m_impl->world.SetRevision(revision);
        std::ostringstream out;
        out << "transferred=" << transferred << " rejected=" << rejected << " revision=" << revision;
        return out.str();
    }
    if (verb == "agent.zone.agents")
    {
        std::ostringstream out;
        for (size_t i = 0; i < m_impl->agents.size(); ++i)
        {
            const auto& a = m_impl->agents[i];
            if (i != 0)
                out << "\n";
            out << "id=" << a.agentId << " scope=" << uint32_t(a.scope.kind) << ":" << a.scope.id
                << " name=" << a.name << " charter_v=" << a.charterVersion;
            const AuthorityRecord* rec = m_impl->authority.Find(a.scope);
            if (rec)
                out << " authority=" << uint32_t(rec->mode);
        }
        return out.str();
    }
    if (verb == "agent.zone.ledger")
    {
        std::istringstream in(payload);
        uint64_t afterSeq = 0;
        uint32_t limit = 50;
        in >> afterSeq >> limit;
        if (limit < 1 || limit > 256)
            limit = 50;
        return m_impl->ledger.DumpPage(afterSeq, limit);
    }
    if (verb == "agent.zone.wake")
    {
        std::istringstream in(payload);
        uint32_t agentId = 0;
        std::string reason;
        if (!(in >> agentId >> reason))
        {
            ok = false;
            return "usage: agent.zone.wake <agent_id> <reason>";
        }
        const ScheduleWakeResult res = WakeAgent(agentId, reason, WakePriority::NearPlayer);
        if (!res.ok)
        {
            ok = false;
            return "wake_rejected: " + res.reason;
        }
        std::ostringstream out;
        out << "request=" << res.requestId << " coalesced=" << (res.coalesced ? 1 : 0);
        return out.str();
    }
    if (verb == "agent.zone.intent")
    {
        // agent.zone.intent <agent_id> <subject> <verb> <target> [ttl_ticks]
        std::istringstream in(payload);
        uint32_t agentId = 0;
        std::string subject;
        std::string opVerb;
        std::string target;
        uint64_t ttlTicks = 600;
        if (!(in >> agentId >> subject >> opVerb >> target))
        {
            ok = false;
            return "usage: agent.zone.intent <agent_id> <subject> <verb> <target> [ttl_ticks]";
        }
        in >> ttlTicks;
        if (ttlTicks < 2 || ttlTicks > 36000)
        {
            ok = false;
            return "invalid_ttl_ticks";
        }
        const WorldAgentRecord* agent = m_impl->FindAgent(agentId);
        if (!agent)
        {
            ok = false;
            return "unknown_agent";
        }

        IntentEnvelope envelope;
        envelope.agentId = agentId;
        envelope.scope = agent->scope;
        envelope.baseRevision = 0; // resolved at the processing tick
        envelope.horizonTicks = uint32_t(ttlTicks);
        envelope.priority = 50;
        envelope.wakeReason = "bridge_inject";
        envelope.rationaleSummary = "bridge injected deterministic intent";
        PlanOp op;
        op.kind = ParseInjectedOpKind(agent->scope.kind, opVerb);
        op.subject = subject;
        op.verb = opVerb;
        op.target = target;
        op.expiresAtTick = m_impl->simulationTick + ttlTicks;
        envelope.operations.push_back(op);

        const ZoneRuntimeResult injected = InjectIntent(std::move(envelope));
        if (!injected.ok)
        {
            ok = false;
            return "intent_rejected: " + injected.reason;
        }
        return "queued request=" + injected.reason;
    }
    if (verb == "agent.zone.status")
    {
        uint32_t groups = 0;
        for (uint32_t i = 0;; ++i)
        {
            if (!m_impl->world.FindGroup(Handle{ (1u << 24) | i }))
                break;
            ++groups;
        }
        SchedulerMetrics sm;
        if (m_impl->scheduler)
            sm = m_impl->scheduler->Metrics();
        std::ostringstream out;
        out << "running=" << (m_impl->running ? 1 : 0)
            << " bootstrapped=" << (m_impl->bootstrapped ? 1 : 0)
            << " attached=" << (IsAttached() ? 1 : 0)
            << " tick=" << m_impl->simulationTick
            << " revision=" << m_impl->world.Revision()
            << " agents=" << m_impl->agents.size()
            << " groups=" << groups
            << " bound=" << (m_impl->materializer ? m_impl->materializer->Bindings().size() : 0)
            << " materialized=" << (m_impl->materializationComplete ? 1 : 0)
            << " inflight=" << sm.inflight
            << " queue=" << sm.queueDepth
            << " ledger=" << m_impl->ledger.Requests().size()
            << " last_materialize=" << (m_impl->lastMaterializeReason.empty() ? "none" : m_impl->lastMaterializeReason);
        return out.str();
    }
    if (verb == "agent.zone.bindings")
    {
        if (!m_impl->materializer)
        {
            ok = false;
            return "live_adapter_not_attached";
        }
        std::ostringstream out;
        const auto& bindings = m_impl->materializer->Bindings();
        out << "bindings=" << bindings.size();
        for (const auto& b : bindings)
        {
            const GroupRecord* grp = m_impl->world.FindGroup(b.group);
            out << "\ngroup=" << (grp ? grp->name : std::string("?")) << " handle=" << b.group.value << " ids=";
            for (size_t i = 0; i < b.objectIds.size(); ++i)
            {
                if (i != 0)
                    out << ",";
                out << b.objectIds[i];
            }
            out << " plan=" << b.activePlanId << " combat=" << (b.tacticalCombat ? 1 : 0)
                << " bound_rev=" << b.boundRevision;
        }
        return out.str();
    }
    if (verb == "agent.zone.authority")
        return m_impl->authority.DumpAudit();
    if (verb == "agent.zone.snapshot")
        return m_impl->world.SaveSnapshot();
    if (verb == "agent.zone.metrics")
    {
        SchedulerMetrics sm;
        if (m_impl->scheduler)
            sm = m_impl->scheduler->Metrics();
        const auto& m = m_impl->metrics;
        std::ostringstream out;
        out << "frame_calls=" << m.frameCalls
            << " simulation_ticks=" << m.simulationTicks
            << " provider_wait_frame_us=0"
            << " provider_requests=" << m.providerRequests
            << " accepted_intents=" << m.acceptedIntents
            << " provider_coasts=" << m.providerCoasts
            << " provider_errors=" << m.providerErrors
            << " codec_rejected=" << m.codecRejected
            << " validation_rejected=" << m.validationRejected
            << " stale_rejected=" << m.staleRejected
            << " fresh_wakes_after_stale=" << m.freshWakesAfterStale
            << " injected_intents=" << m.injectedIntents
            << " retries=" << m.retries
            << " input_tokens=" << m.inputTokens
            << " output_tokens=" << m.outputTokens
            << " total_latency_ms=" << m.totalLatencyMs
            << " observed_events=" << m.observedEvents
            << " live_events_dropped=" << m.liveEventsDropped
            << " materialize_attempts=" << m.materializeAttempts
            << " materialize_failures=" << m.materializeFailures
            << " materialized_groups=" << m.materializedGroups
            << " live_commands_applied=" << m.liveCommandsApplied
            << " scheduled=" << sm.scheduled
            << " dispatched=" << sm.dispatched
            << " completed=" << sm.completed
            << " coalesced=" << sm.coalesced
            << " queue_rejected=" << sm.queueRejected
            << " circuit_opened=" << sm.circuitOpened
            << " queue_depth=" << sm.queueDepth
            << " inflight=" << sm.inflight
            << " max_queue_depth=" << std::max(m.maxQueueDepth, sm.maxQueueDepth)
            << " max_inflight=" << std::max(m.maxInflight, sm.maxInflight)
            << " effective_rpm=" << sm.effectiveRequestsPerMinute;
        return out.str();
    }
    if (verb == "agent.zone.stop")
    {
        Stop();
        return "stopped";
    }
    if (verb == "agent.zone.reset")
    {
        uint32_t cancelled = 0;
        if (m_impl->scheduler)
        {
            const SchedulerMetrics sm = m_impl->scheduler->Metrics();
            cancelled = sm.queueDepth + sm.inflight;
        }
        Reset();
        return "reset cancelled=" + std::to_string(cancelled);
    }

    ok = false;
    return "unknown_zone_verb: " + verb;
}
} // namespace xrSim
