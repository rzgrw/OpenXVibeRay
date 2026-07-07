#include "xrSim/xrSimWorldState.h"

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
} // namespace

int main()
{
    bool ok = true;
    ok = TestAdjustPopulationClampsAndLogs() && ok;
    ok = TestMissingHandleFailsAsValue() && ok;
    ok = TestDigestIsStable() && ok;
    return ok ? 0 : 1;
}
