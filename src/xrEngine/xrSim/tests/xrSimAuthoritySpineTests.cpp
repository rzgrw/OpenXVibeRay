#include "xrSim/xrSimZoneManifest.h"
#include "xrSim/xrSimZoneState.h"
#include "xrSim/xrSimAuthority.h"
#include "xrSim/xrSimIntentCommit.h"
#include "xrSim/xrSimDecisionLedger.h"
#include "xrSim/xrSimWorldAgents.h"
#include "xrSim/xrSimWorldProvider.h"
#include "xrSim/xrSimWakeScheduler.h"
#include "xrSim/xrSimMaterialization.h"
#include "xrSim/xrSimZoneRuntime.h"
#include "xrSim/xrSimAgentProvider.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

const char* kManifest = R"(xrsim_zone_manifest_v1
scenario=garbage_gate1
save=radik zagirov - to garbage
level=l02_garbage
seed=14072026
origin=player
anchor=stalker_camp,3500,0,0
anchor=bandit_camp,-3500,0,0
anchor=scrapyard_center,0,0,4500
faction=stalker,101
faction=bandit,102
group=stalker_squad,squad,stalker,sim_default_stalker_0,3,stalker_camp,201
group=bandit_squad,squad,bandit,sim_default_bandit_0,3,bandit_camp,202
group=garbage_dogs,mutant_pack,ecology,dog_weak,4,scrapyard_center,203
resource=scrap_cache,scrap,scrapyard_center,40,5
route=stalker_to_scrap,stalker_camp,scrapyard_center
agent=100,zone,1,zone_director,zone_director_v1
agent=101,faction,1,stalker_faction,stalker_faction_v1
agent=102,faction,2,bandit_faction,bandit_faction_v1
agent=103,ecology,1,garbage_ecology,ecology_v1
agent=104,economy,1,garbage_economy,economy_v1
agent=105,region,1,garbage_region,region_v1
agent=201,squad,1,stalker_squad,squad_v1
agent=202,squad,2,bandit_squad,squad_v1
agent=203,mutant_pack,3,garbage_dogs,pack_v1
)";

bool TestManifestBootstrapsDeterministically()
{
    const xrSim::ZoneManifestParseResult parsed = xrSim::ParseZoneManifest(kManifest);
    if (!Expect(parsed.ok, "valid manifest parses"))
        return false;
    xrSim::ZoneState first;
    xrSim::ZoneState second;
    const xrSim::ZoneResult a = first.Bootstrap(parsed.manifest);
    const xrSim::ZoneResult b = second.Bootstrap(parsed.manifest);
    return Expect(a.ok && b.ok, "manifest bootstraps") &&
        Expect(first.Revision() == 1, "bootstrap creates revision one") &&
        Expect(first.Digest() == second.Digest(), "same manifest produces same digest") &&
        Expect(first.FindGroupByName("bandit_squad").IsValid(), "group has stable handle") &&
        Expect(first.FindLocationByName("scrapyard_center").IsValid(), "anchor has stable handle");
}

bool TestManifestRejectsUnknownAndDuplicateRecords()
{
    return Expect(!xrSim::ParseZoneManifest("xrsim_zone_manifest_v1\nunknown=x\n").ok,
               "unknown key rejected") &&
        Expect(!xrSim::ParseZoneManifest(
            "xrsim_zone_manifest_v1\nscenario=x\nscenario=y\n").ok,
            "duplicate singleton rejected");
}

bool TestAuthorityTransferRejectsLegacyAfterCommit()
{
    xrSim::AuthorityRegistry registry;
    const xrSim::ScopeId scope{ xrSim::AgentScopeKind::Squad, 7 };
    bool ok = Expect(registry.Register(scope, 201, 1).ok, "scope registers in observe");
    ok = Expect(registry.BeginShadow(scope, 2).ok, "observe enters shadow") && ok;
    ok = Expect(registry.BeginTransfer(scope, 3).ok, "shadow enters transfer") && ok;
    ok = Expect(registry.CommitTransfer(scope, 3).ok, "matching transfer commits") && ok;
    ok = Expect(registry.CanAuthor(scope, xrSim::ObjectiveAuthor::Ai, 201).ok,
             "owner AI can author") && ok;
    const xrSim::AuthorityResult legacy =
        registry.CanAuthor(scope, xrSim::ObjectiveAuthor::Legacy, 0);
    return Expect(!legacy.ok && legacy.reason == "legacy_author_rejected",
        "legacy objective is rejected after transfer");
}

bool TestAuthorityTransferIsRevisionChecked()
{
    xrSim::AuthorityRegistry registry;
    const xrSim::ScopeId scope{ xrSim::AgentScopeKind::MutantPack, 9 };
    registry.Register(scope, 203, 1);
    registry.BeginShadow(scope, 2);
    registry.BeginTransfer(scope, 3);
    const xrSim::AuthorityResult result = registry.CommitTransfer(scope, 4);
    return Expect(!result.ok && result.reason == "transfer_revision_mismatch",
        "transfer cannot commit against another revision") &&
        Expect(registry.Find(scope)->mode == xrSim::AuthorityMode::TransferPending,
            "failed transfer does not change owner mode");
}

const char* kIntent = R"(xrsim_world_intent_v1
request_id=17
agent_id=202
scope=squad,2
base_revision=3
horizon_ticks=600
priority=80
wake_reason=player_trespass
rationale=defend the western approach
op=set_objective,bandit_squad,hold,bandit_camp,900
)";

bool TestIntentCodecIsStrictAndRoundTrips()
{
    const xrSim::IntentParseResult parsed = xrSim::ParseWorldIntentEnvelope(kIntent);
    return Expect(parsed.ok, "valid world intent parses") &&
        Expect(parsed.envelope.operations.size() == 1, "one operation decoded") &&
        Expect(xrSim::ParseWorldIntentEnvelope(
            std::string(kIntent) + "unknown=value\n").reason == "unknown_record",
            "unknown record rejected") &&
        Expect(xrSim::ParseWorldIntentEnvelope(
            xrSim::FormatWorldIntentEnvelope(parsed.envelope)).ok,
            "formatted envelope reparses");
}

bool TestIntentCommitRejectsStaleAndMixedPlansAtomically()
{
    const xrSim::ZoneManifestParseResult manifest = xrSim::ParseZoneManifest(kManifest);
    xrSim::ZoneState world;
    world.Bootstrap(manifest.manifest);
    world.SetRevision(3);
    xrSim::AuthorityRegistry authority;
    const xrSim::ScopeId scope{ xrSim::AgentScopeKind::Squad, 2 };
    authority.Register(scope, 202, 1);
    authority.BeginShadow(scope, 2);
    authority.BeginTransfer(scope, 3);
    authority.CommitTransfer(scope, 3);

    xrSim::IntentEnvelope stale = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    stale.baseRevision = 2;
    const std::string before = world.Digest();
    const xrSim::StageIntentResult staleResult = xrSim::StageIntent(world, authority, stale, 300);
    if (!Expect(!staleResult.ok && staleResult.reason == "stale_revision", "stale rejected"))
        return false;

    xrSim::IntentEnvelope mixed = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    mixed.operations.push_back({ xrSim::PlanOpKind::SetSupply, "missing_resource", "", "", 4, 0 });
    const xrSim::StageIntentResult mixedResult = xrSim::StageIntent(world, authority, mixed, 300);
    return Expect(!mixedResult.ok, "invalid dependent operation rejects envelope") &&
        Expect(world.Digest() == before, "failed stage leaves canonical state untouched");
}

bool TestScopedProjectionDoesNotExposeOtherAgentBeliefs()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    xrSim::WorldAgentRecord bandits;
    bandits.agentId = 202;
    bandits.scope = { xrSim::AgentScopeKind::Squad, 2 };
    bandits.name = "bandit_squad_mind";
    bandits.beliefs.push_back({ "player_intent", "hostile", 60 });
    xrSim::WorldAgentRecord stalkers;
    stalkers.agentId = 201;
    stalkers.scope = { xrSim::AgentScopeKind::Squad, 1 };
    stalkers.name = "stalker_squad_mind";
    stalkers.beliefs.push_back({ "player_intent", "friendly", 40 });

    const std::string projection = xrSim::BuildScopedProjection(world, bandits);
    return Expect(projection.find("hostile") != std::string::npos,
               "owner sees its belief") &&
        Expect(projection.find("friendly") == std::string::npos,
            "owner does not see another private belief");
}

bool TestObservedConsequenceUpdatesMemoryWithoutInventingFact()
{
    xrSim::WorldAgentRecord agent;
    agent.agentId = 202;
    agent.scope = { xrSim::AgentScopeKind::Squad, 2 };
    xrSim::WorldEvent event{ 1, 500, "player_damaged_group", {}, {}, 5, "live_observation" };
    xrSim::ApplyObservedConsequence(agent, event);
    return Expect(agent.episodes.size() == 1, "observed event enters episodic memory") &&
        Expect(agent.memorySummary.find("player_damaged_group") != std::string::npos,
            "bounded summary reflects encounter");
}

bool TestLedgerHashesPayloadAndNeverPrintsKey()
{
    xrSim::DecisionLedger ledger;
    xrSim::RequestLedgerRecord record;
    record.requestId = 17;
    record.agentId = 202;
    record.promptHash = xrSim::StableLedgerHash("prompt");
    record.responseHash = xrSim::StableLedgerHash("response");
    record.provider = "anthropic";
    record.model = "claude-sonnet-5";
    ledger.AppendRequest(record);
    const std::string dump = ledger.Dump();
    return Expect(dump.find("prompt") == std::string::npos, "raw prompt absent") &&
        Expect(dump.find("sk-ant") == std::string::npos, "secret prefix absent") &&
        Expect(dump.find("claude-sonnet-5") != std::string::npos, "model metadata present");
}

class WorldFakeTransport final : public xrSim::IAnthropicTransport
{
public:
    xrSim::AnthropicTransportResult next;
    xrSim::AnthropicMessagesRequest captured;
    xrSim::AnthropicTransportResult Send(const xrSim::AgentProviderConfig&,
        const xrSim::AnthropicMessagesRequest& request) override
    {
        captured = request;
        return next;
    }
};

class WorldFakeGeminiTransport final : public xrSim::IGeminiTransport
{
public:
    xrSim::GeminiTransportResult next;
    xrSim::GeminiGenerateContentRequest captured;
    xrSim::GeminiTransportResult Send(const xrSim::AgentProviderConfig&,
        const xrSim::GeminiGenerateContentRequest& request) override
    {
        captured = request;
        return next;
    }
};

bool TestWorldProviderDecodesIntentAndUsage()
{
    xrSim::AgentProviderConfig config;
    config.apiKey = "in-memory-test-key";
    WorldFakeTransport transport;
    transport.next.ok = true;
    transport.next.status = 200;
    transport.next.latencyMs = 41;
    transport.next.body = R"({"content":[{"type":"text","text":"xrsim_world_intent_v1\nrequest_id=17\nagent_id=202\nscope=squad,2\nbase_revision=3\nhorizon_ticks=600\npriority=80\nwake_reason=player_trespass\nrationale=hold\nop=set_objective,bandit_squad,hold,bandit_camp,900\n"}],"usage":{"input_tokens":321,"output_tokens":87}})";
    xrSim::AnthropicWorldIntentProvider provider(config);
    provider.SetTransport(&transport);
    xrSim::WorldWakeContext context;
    context.requestId = 17;
    context.baseRevision = 3;
    context.agent.agentId = 202;
    context.agent.scope = { xrSim::AgentScopeKind::Squad, 2 };
    context.prompt = "bounded prompt";
    const xrSim::WorldProviderResult result = provider.Wake(context);
    return Expect(result.ok && !result.coast, "world response accepted") &&
        Expect(result.usage.inputTokens == 321 && result.usage.outputTokens == 87,
            "usage decoded") &&
        Expect(result.intent.requestId == 17, "intent request identity preserved") &&
        Expect(transport.captured.body.find("in-memory-test-key") == std::string::npos,
            "key absent from request body");
}

bool TestWorldProviderClassifiesRetryableFailuresAsValues()
{
    xrSim::AgentProviderConfig config;
    config.apiKey = "in-memory-test-key";
    WorldFakeTransport transport;
    transport.next.ok = true;
    transport.next.status = 429;
    xrSim::AnthropicWorldIntentProvider provider(config);
    provider.SetTransport(&transport);
    const xrSim::WorldProviderResult result = provider.Wake({});
    return Expect(!result.ok && result.coast, "rate limit coasts") &&
        Expect(result.retryable && result.rateLimited, "rate limit classified") &&
        Expect(result.error == "http_status_429", "failure has stable reason");
}

bool TestGeminiWorldProviderDecodesIntentAndUsage()
{
    xrSim::AgentProviderConfig config;
    config.provider = "gemini";
    config.model = "gemini-3.8-flash";
    config.apiKey = "in-memory-gemini-key";
    WorldFakeGeminiTransport transport;
    transport.next.ok = true;
    transport.next.status = 200;
    transport.next.latencyMs = 38;
    transport.next.body = R"({"candidates":[{"content":{"parts":[{"text":"xrsim_world_intent_v1\nrequest_id=17\nagent_id=202\nscope=squad,2\nbase_revision=3\nhorizon_ticks=600\npriority=80\nwake_reason=player_trespass\nrationale=hold\nop=set_objective,bandit_squad,hold,bandit_camp,900\n"}]}}],"usageMetadata":{"promptTokenCount":305,"candidatesTokenCount":72}})";
    xrSim::GeminiWorldIntentProvider provider(config);
    provider.SetTransport(&transport);
    xrSim::WorldWakeContext context;
    context.requestId = 17;
    context.baseRevision = 3;
    context.agent.agentId = 202;
    context.agent.scope = { xrSim::AgentScopeKind::Squad, 2 };
    context.prompt = "gemini bounded prompt";
    const xrSim::WorldProviderResult result = provider.Wake(context);
    return Expect(result.ok && !result.coast, "gemini world response accepted") &&
        Expect(result.usage.inputTokens == 305 && result.usage.outputTokens == 72,
            "gemini usage decoded") &&
        Expect(result.intent.requestId == 17, "gemini intent request identity preserved") &&
        Expect(result.provider == "gemini", "gemini provider stamped") &&
        Expect(result.model == "gemini-3.8-flash", "gemini-3.8-flash model stamped") &&
        Expect(transport.captured.body.find("in-memory-gemini-key") == std::string::npos,
            "gemini key absent from request body");
}

bool TestFaultWorldIntentProviderGeneratesConfiguredFaults()
{
    xrSim::FaultWorldIntentProvider timeoutProvider(xrSim::InjectedProviderFault::Timeout);
    const xrSim::WorldProviderResult timeoutResult = timeoutProvider.Wake({});
    bool ok = Expect(!timeoutResult.ok && timeoutResult.coast, "timeout coasts");
    ok = Expect(timeoutResult.retryable, "timeout retryable") && ok;
    ok = Expect(timeoutResult.error == "timeout", "timeout error code") && ok;

    xrSim::FaultWorldIntentProvider rateLimitProvider(xrSim::InjectedProviderFault::RateLimit);
    const xrSim::WorldProviderResult rateResult = rateLimitProvider.Wake({});
    ok = Expect(rateResult.rateLimited && rateResult.retryable, "rate limit set") && ok;
    return ok;
}

bool TestSchedulerCoalescesAndBoundsOneInflightPerAgent()
{
    std::vector<xrSim::WorldProviderResult> script(2);
    script[0].ok = true;
    script[1].ok = true;
    auto provider = std::make_unique<xrSim::RecordedWorldIntentProvider>(script);
    xrSim::WakeScheduler scheduler({}, std::move(provider));
    xrSim::WorldWakeContext first;
    first.agent.agentId = 202;
    first.wakeReason = "heartbeat";
    xrSim::WorldWakeContext urgent = first;
    urgent.wakeReason = "player_trespass";
    const xrSim::ScheduleWakeResult a =
        scheduler.Schedule(first, xrSim::WakePriority::Heartbeat, 1000);
    const xrSim::ScheduleWakeResult b =
        scheduler.Schedule(urgent, xrSim::WakePriority::NearPlayer, 1001);
    scheduler.Pump(1001);
    scheduler.WaitForIdleForTest(2000);
    const xrSim::SchedulerMetrics metrics = scheduler.Metrics();
    return Expect(a.ok && b.ok, "compatible wakes accepted") &&
        Expect(metrics.coalesced == 1, "second wake coalesced") &&
        Expect(metrics.maxInflightPerAgent == 1, "one request per agent");
}

bool TestSchedulerCallSiteNeverWaitsForSlowProvider()
{
    auto provider = std::make_unique<xrSim::FaultWorldIntentProvider>(
        xrSim::InjectedProviderFault::Timeout);
    xrSim::WakeScheduler scheduler({}, std::move(provider));
    xrSim::WorldWakeContext context;
    context.agent.agentId = 202;
    const auto start = std::chrono::steady_clock::now();
    scheduler.Schedule(context, xrSim::WakePriority::NearPlayer, 1000);
    scheduler.Pump(1000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    scheduler.Shutdown();
    return Expect(elapsed < 20, "schedule and pump never wait on provider");
}

bool TestSchedulerOpensCircuitAfterThreeFailures()
{
    auto provider = std::make_unique<xrSim::FaultWorldIntentProvider>(
        xrSim::InjectedProviderFault::TransportError);
    xrSim::SchedulerConfig config;
    config.minCooldownMs = 0;
    config.maxRetries = 0;
    xrSim::WakeScheduler scheduler(config, std::move(provider));
    for (uint64_t i = 0; i < 3; ++i)
    {
        xrSim::WorldWakeContext context;
        context.agent.agentId = 202;
        scheduler.Schedule(context, xrSim::WakePriority::NearPlayer, 1000 + i);
        scheduler.Pump(1000 + i);
        scheduler.WaitForIdleForTest(2000);
        scheduler.Pump(1000 + i);
        scheduler.DrainReady();
    }
    xrSim::WorldWakeContext blockedContext;
    blockedContext.agent.agentId = 202;
    const xrSim::ScheduleWakeResult blocked = scheduler.Schedule(
        blockedContext, xrSim::WakePriority::Heartbeat, 1004);
    return Expect(!blocked.ok && blocked.reason == "agent_circuit_open",
        "three failures open per-agent circuit");
}

class FakeLiveAdapter final : public xrSim::IAgentZoneLiveAdapter
{
public:
    uint32_t spawnCalls = 0;
    uint32_t objectiveCalls = 0;
    uint32_t releaseCalls = 0;
    bool inCombat = false;
    xrSim::ReadPositionResult CapturePlayerOrigin() override
    {
        return { true, "", { 0, 0, 0, 0xffffffffu } };
    }
    xrSim::LiveAdapterResult ValidateSection(const std::string&) override
    {
        return { true, "" };
    }
    xrSim::SpawnGroupResult SpawnGroup(const xrSim::LiveSpawnRequest& request) override
    {
        ++spawnCalls;
        xrSim::SpawnGroupResult result;
        result.ok = true;
        for (uint32_t i = 0; i < request.count; ++i)
            result.objectIds.push_back(static_cast<uint16_t>(100 + spawnCalls * 10 + i));
        return result;
    }
    xrSim::LiveAdapterResult DestroyObjects(const std::vector<uint16_t>&) override
    {
        return { true, "" };
    }
    std::vector<uint16_t> lastObjectiveIds;
    xrSim::LiveAdapterResult ApplyObjective(
        const xrSim::LiveBinding& binding, const xrSim::LiveObjectiveCommand&) override
    {
        ++objectiveCalls;
        lastObjectiveIds = binding.objectIds;
        return { true, "" };
    }
    xrSim::LiveAdapterResult FacePlayerAt(const xrSim::LiveBinding&) override
    {
        return { true, "" };
    }
    xrSim::ObserveGroupResult ObserveGroup(const xrSim::LiveBinding& binding) override
    {
        return { true, "", static_cast<uint32_t>(binding.objectIds.size()), inCombat, {}, 100 };
    }
    xrSim::LiveAdapterResult ReleaseObjective(const xrSim::LiveBinding&) override
    {
        ++releaseCalls;
        return { true, "" };
    }
    xrSim::LiveAdapterResult SetPlayerAt(const xrSim::FixedPosition&) override
    {
        return { true, "" };
    }
    xrSim::LiveAdapterResult DamageGroupFromPlayer(const xrSim::LiveBinding&, uint32_t) override
    {
        return { true, "" };
    }
};

bool TestMaterializerNeverDuplicatesStableGroup()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    FakeLiveAdapter adapter;
    xrSim::Materializer materializer(adapter);
    const xrSim::MaterializeResult first = materializer.Bootstrap(world, 2);
    const xrSim::MaterializeResult second = materializer.Bootstrap(world, 2);
    const xrSim::MaterializeResult third = materializer.Bootstrap(world, 2);
    return Expect(first.ok && second.ok, "bootstrap calls succeed") &&
        Expect(third.ok && third.complete, "all groups eventually bind") &&
        Expect(adapter.spawnCalls == 3, "three manifest groups spawned once") &&
        Expect(materializer.Bindings().size() == 3, "stable groups never duplicate");
}

bool TestMaterializerFeedsObservedAliveCountBackAsEvent()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    FakeLiveAdapter adapter;
    xrSim::Materializer materializer(adapter);
    while (!materializer.Bootstrap(world, 2).complete) {}
    const xrSim::ObservationBatch batch = materializer.PollObservations(world, 50);
    return Expect(batch.ok, "observation poll succeeds") &&
        Expect(!batch.events.empty(), "live results return as observed events") &&
        Expect(batch.events.front().source == "live_observation", "outcome source is explicit");
}

bool TestMaterializerHandsOffCombatAndReacquiresWhenCalm()
{
    xrSim::ZoneState world;
    world.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest);
    FakeLiveAdapter adapter;
    xrSim::Materializer materializer(adapter);
    while (!materializer.Bootstrap(world, 2).complete) {}

    xrSim::ObjectiveRecord objective;
    objective.planId = 7;
    objective.subjectKind = xrSim::ObjectiveSubjectKind::Group;
    objective.subject = world.FindGroupByName("bandit_squad");
    objective.subjectName = "bandit_squad";
    objective.kind = xrSim::ObjectiveKind::Move;
    objective.target = world.FindLocationByName("scrapyard_center");
    objective.expiresAtTick = 1000;
    objective.author = xrSim::ObjectiveAuthor::Ai;
    objective.active = true;
    world.MutableObjectives().push_back(objective);

    materializer.ApplyCommittedState(world, 2, 10, 4);
    if (!Expect(adapter.objectiveCalls == 1, "calm group receives the committed objective"))
        return false;

    adapter.inCombat = true;
    xrSim::ObservationBatch entered = materializer.PollObservations(world, 11);
    materializer.ApplyCommittedState(world, 2, 11, 4);
    bool sawEnter = false;
    for (const auto& ev : entered.events)
        sawEnter = sawEnter || ev.kind == "combat_entered";
    // The fake adapter reports combat for every bound group, so all three are released.
    if (!Expect(sawEnter && adapter.releaseCalls == 3, "combat entry releases the objective to the live AI"))
        return false;
    if (!Expect(adapter.objectiveCalls == 1, "no objective is re-applied while fighting"))
        return false;

    adapter.inCombat = false;
    materializer.PollObservations(world, 12);
    materializer.ApplyCommittedState(world, 2, 13, 4);
    if (!Expect(adapter.objectiveCalls == 1, "objective is not reacquired before the calm window"))
        return false;
    materializer.ApplyCommittedState(world, 2, 16, 4);
    return Expect(adapter.objectiveCalls == 2, "same committed plan is reacquired once calm");
}

bool TestZoneRuntimeCommitsOnlyAtTickBoundary()
{
    FakeLiveAdapter adapter;
    xrSim::WorldProviderResult response;
    response.ok = true;
    response.intent = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    auto provider = std::make_unique<xrSim::RecordedWorldIntentProvider>(
        std::vector<xrSim::WorldProviderResult>{ response });
    xrSim::ZoneRuntime runtime;
    xrSim::ZoneRuntimeConfig config;
    config.tickMs = 250;
    config.providerMode = xrSim::ZoneProviderMode::Recorded;
    const xrSim::ZoneManifest manifest = xrSim::ParseZoneManifest(kManifest).manifest;
    if (!Expect(runtime.Bootstrap(manifest, adapter, config, std::move(provider)).ok,
            "runtime bootstraps"))
        return false;
    runtime.TransferAllForTest();
    runtime.WakeAgentForTest(202, "player_trespass", xrSim::WakePriority::NearPlayer, 1000);
    runtime.OnFrame(1100, 10);
    const uint64_t before = runtime.World().Revision();
    runtime.Scheduler().WaitForIdleForTest(2000);
    runtime.OnFrame(1249, 11);
    if (!Expect(runtime.World().Revision() == before, "no mid-tick commit"))
        return false;
    runtime.OnFrame(1250, 12);
    return Expect(runtime.World().Revision() == before + 1,
        "ready response commits at boundary");
}

bool TestZoneRuntimeStaleCompletionSchedulesFreshWake()
{
    FakeLiveAdapter adapter;
    xrSim::WorldProviderResult response;
    response.ok = true;
    response.intent = xrSim::ParseWorldIntentEnvelope(kIntent).envelope;
    auto provider = std::make_unique<xrSim::RecordedWorldIntentProvider>(
        std::vector<xrSim::WorldProviderResult>{ response });
    xrSim::ZoneRuntime runtime;
    xrSim::ZoneRuntimeConfig config;
    config.scheduler.minCooldownMs = 0;
    runtime.Bootstrap(xrSim::ParseZoneManifest(kManifest).manifest, adapter, config,
        std::move(provider));
    runtime.TransferAllForTest();
    runtime.SetWorldRevisionForTest(response.intent.baseRevision + 1);
    runtime.WakeAgentForTest(202, "stale_test", xrSim::WakePriority::InvalidatedPlan, 1000);
    runtime.OnFrame(1250, 20);
    runtime.Scheduler().WaitForIdleForTest(2000);
    runtime.OnFrame(1500, 21);
    return Expect(runtime.Metrics().staleRejected == 1, "stale result recorded") &&
        Expect(runtime.Metrics().freshWakesAfterStale == 1, "fresh revision wake queued");
}

bool TestBridgeVerbsDriveLiveRuntimeWithoutKey()
{
    FakeLiveAdapter adapter;
    xrSim::ZoneRuntime runtime;
    bool ok = true;

    std::string out = runtime.HandleBridgeVerb("agent.zone.bootstrap", "tools/ai_garbage_gate1.xrsim", ok);
    if (!Expect(ok && out.find("groups=3") != std::string::npos, "bridge bootstrap parses the Gate 1 manifest"))
        return false;
    out = runtime.HandleBridgeVerb("agent.zone.start", "", ok);
    if (!Expect(!ok && out == "live_adapter_not_attached", "start without a live adapter fails as a value"))
        return false;
    out = runtime.HandleBridgeVerb("agent.zone.wake", "201 early", ok);
    if (!Expect(!ok && out == "wake_rejected: live_adapter_not_attached", "wake before attach is rejected"))
        return false;

    xrSim::AgentProviderConfig providerConfig;
    providerConfig.apiKey.clear();
    xrSim::ZoneRuntimeConfig config;
    config.scheduler.minCooldownMs = 0;
    if (!Expect(runtime.AttachLive(adapter, config, xrSim::CreateLiveWorldIntentProvider(providerConfig)).ok,
            "live adapter attaches to a bootstrapped runtime"))
        return false;
    out = runtime.HandleBridgeVerb("agent.zone.start", "", ok);
    if (!Expect(ok && out == "running", "start runs once attached"))
        return false;
    out = runtime.HandleBridgeVerb("agent.zone.shadow", "all", ok);
    if (!Expect(ok && out.find("shadowed=9") != std::string::npos, "shadow uses the split verb/payload form"))
        return false;
    out = runtime.HandleBridgeVerb("agent.zone.transfer", "all", ok);
    if (!Expect(ok && out.find("transferred=9") != std::string::npos, "transfer uses the split verb/payload form"))
        return false;

    uint64_t now = 1000;
    for (uint64_t frame = 0; frame < 5; ++frame)
    {
        now += 250;
        runtime.OnFrame(now, frame);
    }
    if (!Expect(adapter.spawnCalls == 3 && runtime.LiveMaterializer() &&
            runtime.LiveMaterializer()->Bindings().size() == 3,
            "manifest groups materialize one group per tick"))
        return false;

    out = runtime.HandleBridgeVerb("agent.zone.wake", "201 debug_wake", ok);
    if (!Expect(ok && out.rfind("request=", 0) == 0, "wake schedules through the runtime scheduler"))
        return false;
    now += 250;
    runtime.OnFrame(now, 10);
    runtime.Scheduler().WaitForIdleForTest(500);
    now += 250;
    runtime.OnFrame(now, 11);
    if (!Expect(runtime.Ledger().Requests().size() == 1 &&
            runtime.Ledger().Requests().front().providerVerdict == xrSim::ProviderVerdict::Coast &&
            runtime.Ledger().Requests().front().codecVerdict == "missing_api_key",
            "keyless wake coasts and is recorded in the ledger"))
        return false;
    if (!Expect(runtime.Metrics().providerCoasts == 1, "coast is counted"))
        return false;

    out = runtime.HandleBridgeVerb("agent.zone.intent", "201 stalker_squad move scrapyard_center 100", ok);
    if (!Expect(ok && out.rfind("queued request=", 0) == 0, "deterministic intent is queued"))
        return false;
    const uint64_t before = runtime.World().Revision();
    now += 250;
    runtime.OnFrame(now, 12);
    if (!Expect(runtime.World().Revision() == before + 1 && runtime.Metrics().acceptedIntents == 1,
            "injected intent commits at the tick boundary"))
        return false;
    if (!Expect(adapter.objectiveCalls == 1 && adapter.lastObjectiveIds.size() == 3,
            "committed objective reaches the bound live group"))
        return false;
    if (!Expect(runtime.Ledger().Requests().back().provider == "bridge_inject",
            "injected intent is attributed in the ledger"))
        return false;

    out = runtime.HandleBridgeVerb("agent.zone.status", "", ok);
    if (!Expect(ok && out.find("bound=3") != std::string::npos && out.find("materialized=1") != std::string::npos,
            "status reports live bindings"))
        return false;
    out = runtime.HandleBridgeVerb("agent.zone.ledger", "", ok);
    if (!Expect(ok && out.find("done=1") != std::string::npos, "ledger page is terminated"))
        return false;

    runtime.DetachLive(false);
    out = runtime.HandleBridgeVerb("agent.zone.start", "", ok);
    return Expect(!ok && out == "live_adapter_not_attached", "detached runtime refuses to run");
}

bool TestFileManifestCreatesEveryGateOneMind()
{
    std::ifstream input("tools/ai_garbage_gate1.xrsim");
    if (!Expect(input.good(), "Gate 1 manifest file opens"))
        return false;
    std::ostringstream text;
    text << input.rdbuf();
    const xrSim::ZoneManifestParseResult parsed = xrSim::ParseZoneManifest(text.str());
    if (!Expect(parsed.ok, "file manifest parses"))
        return false;
    const std::vector<xrSim::WorldAgentRecord> agents = xrSim::CreateWorldAgents(parsed.manifest);
    return Expect(agents.size() == 9, "all Gate 1 minds created") &&
        Expect(agents.front().agentId == 100, "director order stable") &&
        Expect(agents.back().scope.kind == xrSim::AgentScopeKind::MutantPack,
            "pack mind present") &&
        Expect(!agents.back().charter.empty(), "versioned charter resolved") &&
        Expect(parsed.manifest.groups.size() == 3, "three participating groups fixed");
}
} // namespace

int main()
{
    bool ok = true;
    ok = TestManifestBootstrapsDeterministically() && ok;
    ok = TestManifestRejectsUnknownAndDuplicateRecords() && ok;
    ok = TestAuthorityTransferRejectsLegacyAfterCommit() && ok;
    ok = TestAuthorityTransferIsRevisionChecked() && ok;
    ok = TestIntentCodecIsStrictAndRoundTrips() && ok;
    ok = TestIntentCommitRejectsStaleAndMixedPlansAtomically() && ok;
    ok = TestScopedProjectionDoesNotExposeOtherAgentBeliefs() && ok;
    ok = TestObservedConsequenceUpdatesMemoryWithoutInventingFact() && ok;
    ok = TestLedgerHashesPayloadAndNeverPrintsKey() && ok;
    ok = TestWorldProviderDecodesIntentAndUsage() && ok;
    ok = TestWorldProviderClassifiesRetryableFailuresAsValues() && ok;
    ok = TestGeminiWorldProviderDecodesIntentAndUsage() && ok;
    ok = TestFaultWorldIntentProviderGeneratesConfiguredFaults() && ok;
    ok = TestSchedulerCoalescesAndBoundsOneInflightPerAgent() && ok;
    ok = TestSchedulerCallSiteNeverWaitsForSlowProvider() && ok;
    ok = TestSchedulerOpensCircuitAfterThreeFailures() && ok;
    ok = TestMaterializerNeverDuplicatesStableGroup() && ok;
    ok = TestMaterializerFeedsObservedAliveCountBackAsEvent() && ok;
    ok = TestMaterializerHandsOffCombatAndReacquiresWhenCalm() && ok;
    ok = TestZoneRuntimeCommitsOnlyAtTickBoundary() && ok;
    ok = TestZoneRuntimeStaleCompletionSchedulesFreshWake() && ok;
    ok = TestBridgeVerbsDriveLiveRuntimeWithoutKey() && ok;
    ok = TestFileManifestCreatesEveryGateOneMind() && ok;
    return ok ? 0 : 1;
}
