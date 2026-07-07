#include "xrSim/xrSimWorldState.h"
#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimNullAgent.h"

#include <cstdio>
#include <string>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

bool TestAdjustPopulationClampsAndLogs()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");

    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "SetPopulation accepts valid handles");

    result = state.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "positive adjust succeeds") && ok;
    ok = Expect(result.appliedDelta == 10, "positive adjust is rate-limited to 10 percent") && ok;
    ok = Expect(state.Population(region, species) == 60, "positive adjust updates population") && ok;
    ok = Expect(state.ToolLog().size() == 1, "positive adjust appends one log record") && ok;
    ok = Expect(state.ToolLog().back().accepted, "positive adjust log is accepted") && ok;
    ok = Expect(state.ToolLog().back().requestedDelta == 500, "positive adjust records requested delta") && ok;
    ok = Expect(state.ToolLog().back().appliedDelta == 10, "positive adjust records applied delta") && ok;

    result = state.ApplyAdjustPopulation(2, region, species, -500, 7);
    ok = Expect(result.ok, "negative adjust succeeds") && ok;
    ok = Expect(result.appliedDelta == -10, "negative adjust is rate-limited to 10 percent") && ok;
    ok = Expect(state.Population(region, species) == 50, "negative adjust updates population") && ok;
    ok = Expect(state.ToolLog().size() == 2, "negative adjust appends second log record") && ok;
    return ok;
}

bool TestMissingHandleFailsAsValue()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");

    xrSim::Result result = state.ApplyAdjustPopulation(1, region, xrSim::Handle::Invalid(), 5, 1);
    bool ok = Expect(!result.ok, "missing species fails as a value");
    ok = Expect(result.reason.find("species") != std::string::npos, "missing species explains reason") && ok;
    ok = Expect(state.Population(region, species) == 0, "missing species does not mutate population") && ok;
    ok = Expect(state.ToolLog().size() == 1, "missing species is still logged") && ok;
    ok = Expect(!state.ToolLog().back().accepted, "missing species log is rejected") && ok;
    return ok;
}

bool TestDigestIsStable()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 12);
    bool ok = Expect(result.ok, "digest setup accepts valid population");

    ok = Expect(
        state.Digest() == "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=12}",
        "digest is stable and human-readable") && ok;
    return ok;
}

bool TestSnapshotRoundTripPreservesStateAndLog()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "snapshot setup accepts valid population");
    result = state.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "snapshot setup accepts adjustment") && ok;

    const std::string snapshot = state.SaveSnapshot();
    ok = Expect(snapshot.find("xrsim_snapshot_v1") == 0, "snapshot has stable version header") && ok;
    ok = Expect(snapshot.find("tool 1 7 adjust_population 16777216 16777216 1 500 10") != std::string::npos,
        "snapshot records tool log entries") && ok;

    xrSim::WorldState restored;
    result = restored.LoadSnapshot(snapshot);
    ok = Expect(result.ok, "snapshot load succeeds") && ok;
    ok = Expect(restored.Digest() == state.Digest(), "snapshot round trip preserves digest") && ok;
    ok = Expect(restored.ToolLog().size() == 1, "snapshot round trip preserves tool log size") && ok;
    ok = Expect(restored.ToolLog().back().appliedDelta == 10, "snapshot round trip preserves tool log delta") && ok;
    return ok;
}

bool TestSnapshotLoadRejectsBadVersionAsValue()
{
    xrSim::WorldState restored;
    const xrSim::Result result = restored.LoadSnapshot("xrsim_snapshot_v2\n");
    bool ok = Expect(!result.ok, "bad snapshot version fails as a value");
    ok = Expect(result.reason.find("version") != std::string::npos, "bad snapshot version explains reason") && ok;
    ok = Expect(restored.Digest() == "regions=0 species=0 cohorts=0 log=0 pop{}",
        "bad snapshot load does not mutate destination") && ok;
    return ok;
}

bool TestReplayToolLogReproducesRecordedDigest()
{
    xrSim::WorldState baseline;
    const xrSim::Handle region = baseline.CreateRegion("debug_region", 100);
    const xrSim::Handle species = baseline.CreateSpecies("blind_dog");
    xrSim::Result result = baseline.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "replay baseline accepts valid population");

    const std::string baselineSnapshot = baseline.SaveSnapshot();

    xrSim::WorldState recorded;
    result = recorded.LoadSnapshot(baselineSnapshot);
    ok = Expect(result.ok, "replay recorded state loads baseline") && ok;
    result = recorded.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "replay recorded state accepts adjustment") && ok;

    xrSim::WorldState replayed;
    result = replayed.LoadSnapshot(baselineSnapshot);
    ok = Expect(result.ok, "replay target loads baseline") && ok;
    result = replayed.ReplayToolLogFrom(recorded);
    ok = Expect(result.ok, "replay applies recorded log") && ok;
    ok = Expect(replayed.Digest() == recorded.Digest(), "replay reproduces recorded digest") && ok;
    ok = Expect(replayed.ToolLog().size() == recorded.ToolLog().size(), "replay reproduces log size") && ok;
    ok = Expect(replayed.ToolLog().back().appliedDelta == recorded.ToolLog().back().appliedDelta,
        "replay reproduces applied delta") && ok;
    return ok;
}

bool TestReplayDetectsDivergentBaseline()
{
    xrSim::WorldState baseline;
    const xrSim::Handle region = baseline.CreateRegion("debug_region", 100);
    const xrSim::Handle species = baseline.CreateSpecies("blind_dog");
    xrSim::Result result = baseline.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "divergent replay baseline accepts population");

    xrSim::WorldState recorded;
    result = recorded.LoadSnapshot(baseline.SaveSnapshot());
    ok = Expect(result.ok, "divergent replay recorded loads baseline") && ok;
    result = recorded.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "divergent replay recorded accepts adjustment") && ok;

    xrSim::WorldState divergent;
    result = divergent.LoadSnapshot(baseline.SaveSnapshot());
    ok = Expect(result.ok, "divergent replay target loads baseline") && ok;
    result = divergent.SetPopulation(region, species, 95);
    ok = Expect(result.ok, "divergent replay target mutates baseline") && ok;
    result = divergent.ReplayToolLogFrom(recorded);
    ok = Expect(!result.ok, "divergent replay fails as a value") && ok;
    ok = Expect(result.reason.find("replay mismatch") != std::string::npos, "divergent replay explains mismatch") && ok;
    return ok;
}

bool TestNullAgentWakeAppliesDeterministicIntent()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "null agent setup accepts population");

    xrSim::NullAgentRuntime runtime;
    result = runtime.Wake(state, 3);
    ok = Expect(result.ok, "null agent wake succeeds") && ok;
    ok = Expect(result.appliedDelta == 5, "null agent wake applies deterministic delta") && ok;
    ok = Expect(state.Population(region, species) == 55, "null agent wake mutates population") && ok;
    ok = Expect(state.ToolLog().size() == 1, "null agent wake records tool log") && ok;
    ok = Expect(state.ToolLog().back().seq == 1, "null agent wake starts sequence at one") && ok;
    ok = Expect(state.ToolLog().back().gameDay == 3, "null agent wake records game day") && ok;
    ok = Expect(runtime.WakeCount() == 1, "null agent wake count increments") && ok;
    return ok;
}

bool TestBridgeDebugVerbs()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "ai.reset succeeds");
    ok = Expect(out.find("xrsim reset") != std::string::npos, "ai.reset reports reset") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}",
        "ai.observe reports deterministic debug digest") && ok;

    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 500", verbOk);
    ok = Expect(verbOk, "ai.inject succeeds") && ok;
    ok = Expect(out == "accepted applied_delta=10", "ai.inject reports clamped delta") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe after inject succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=60}",
        "ai.observe after inject reports updated digest") && ok;

    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region snork 5", verbOk);
    ok = Expect(!verbOk, "ai.inject unknown species fails") && ok;
    ok = Expect(out.find("unknown species") != std::string::npos, "ai.inject unknown species explains failure") && ok;
    return ok;
}

bool TestBridgeSnapshotVerbs()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "bridge snapshot setup reset succeeds");

    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 500", verbOk);
    ok = Expect(verbOk, "bridge snapshot setup inject succeeds") && ok;

    const std::string snapshot = xrSim::HandleBridgeVerb("ai.snapshot", "", verbOk);
    ok = Expect(verbOk, "ai.snapshot succeeds") && ok;
    ok = Expect(snapshot.find("xrsim_snapshot_v1") == 0, "ai.snapshot returns a versioned snapshot") && ok;

    out = xrSim::HandleBridgeVerb("ai.log", "", verbOk);
    ok = Expect(verbOk, "ai.log succeeds") && ok;
    ok = Expect(out == "seq=1 day=0 tool=adjust_population accepted=1 requested=500 applied=10",
        "ai.log returns deterministic tool log") && ok;

    out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    ok = Expect(verbOk, "bridge snapshot reset before restore succeeds") && ok;
    out = xrSim::HandleBridgeVerb("ai.restore", snapshot, verbOk);
    ok = Expect(verbOk, "ai.restore succeeds") && ok;
    ok = Expect(out == "xrsim restored log=1", "ai.restore reports restored log size") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe after restore succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=60}",
        "ai.restore recreates observed digest") && ok;
    return ok;
}

bool TestBridgeReplayVerb()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "bridge replay setup reset succeeds");
    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 500", verbOk);
    ok = Expect(verbOk, "bridge replay setup inject succeeds") && ok;

    out = xrSim::HandleBridgeVerb("ai.replay", "", verbOk);
    ok = Expect(verbOk, "ai.replay succeeds") && ok;
    ok = Expect(out == "xrsim replay digest=regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=60}",
        "ai.replay reports reproduced digest") && ok;
    return ok;
}

bool TestBridgeNullAgentWakeVerb()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "bridge null-agent setup reset succeeds");

    out = xrSim::HandleBridgeVerb("ai.wake", "", verbOk);
    ok = Expect(verbOk, "ai.wake succeeds") && ok;
    ok = Expect(out == "xrsim wake applied_delta=5 wakes=1", "ai.wake reports deterministic wake") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe after wake succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=55}",
        "ai.wake updates observed digest") && ok;
    return ok;
}
} // namespace

int main()
{
    bool ok = true;
    ok = TestAdjustPopulationClampsAndLogs() && ok;
    ok = TestMissingHandleFailsAsValue() && ok;
    ok = TestDigestIsStable() && ok;
    ok = TestSnapshotRoundTripPreservesStateAndLog() && ok;
    ok = TestSnapshotLoadRejectsBadVersionAsValue() && ok;
    ok = TestReplayToolLogReproducesRecordedDigest() && ok;
    ok = TestReplayDetectsDivergentBaseline() && ok;
    ok = TestNullAgentWakeAppliesDeterministicIntent() && ok;
    ok = TestBridgeDebugVerbs() && ok;
    ok = TestBridgeSnapshotVerbs() && ok;
    ok = TestBridgeReplayVerb() && ok;
    ok = TestBridgeNullAgentWakeVerb() && ok;
    return ok ? 0 : 1;
}
