#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimWorldState.h"

#include <cstdio>
#include <sstream>

namespace xrSim
{
namespace
{
WorldState g_debugWorld;
uint32_t g_nextSeq = 1;
bool g_initialized = false;

void ResetDebugWorld()
{
    g_debugWorld = WorldState{};
    const Handle region = g_debugWorld.CreateRegion("debug_region", 100);
    const Handle species = g_debugWorld.CreateSpecies("blind_dog");
    g_debugWorld.SetPopulation(region, species, 50);
    g_nextSeq = 1;
    g_initialized = true;
}

void EnsureDebugWorld()
{
    if (!g_initialized)
        ResetDebugWorld();
}

WorldState MakeDebugBaseline()
{
    WorldState baseline;
    const Handle region = baseline.CreateRegion("debug_region", 100);
    const Handle species = baseline.CreateSpecies("blind_dog");
    baseline.SetPopulation(region, species, 50);
    return baseline;
}

std::string InjectIntent(const std::string& payload, bool& ok)
{
    char tool[64]{};
    char regionName[64]{};
    char speciesName[64]{};
    int delta = 0;
    if (4 != std::sscanf(payload.c_str(), "%63s %63s %63s %d", tool, regionName, speciesName, &delta))
    {
        ok = false;
        return "usage: ai.inject adjust_population <region> <species> <delta>";
    }

    if (std::string(tool) != "adjust_population")
    {
        ok = false;
        return std::string("unknown intent: ") + tool;
    }

    EnsureDebugWorld();
    const Handle region = g_debugWorld.FindRegionByName(regionName);
    if (!region.IsValid())
    {
        ok = false;
        return std::string("unknown region: ") + regionName;
    }

    const Handle species = g_debugWorld.FindSpeciesByName(speciesName);
    if (!species.IsValid())
    {
        ok = false;
        return std::string("unknown species: ") + speciesName;
    }

    const Result result = g_debugWorld.ApplyAdjustPopulation(g_nextSeq++, region, species, delta, 0);
    ok = result.ok;
    if (!result.ok)
        return result.reason;

    return "accepted applied_delta=" + std::to_string(result.appliedDelta);
}

std::string FormatToolLog()
{
    EnsureDebugWorld();
    const std::vector<ToolRecord>& log = g_debugWorld.ToolLog();
    if (log.empty())
        return "empty";

    std::ostringstream out;
    for (size_t i = 0; i < log.size(); ++i)
    {
        const ToolRecord& record = log[i];
        if (i != 0)
            out << " | ";
        out << "seq=" << record.seq << " day=" << record.gameDay << " tool=" << record.tool
            << " accepted=" << (record.accepted ? 1 : 0) << " requested=" << record.requestedDelta
            << " applied=" << record.appliedDelta;
        if (!record.reason.empty())
            out << " reason=" << record.reason;
    }
    return out.str();
}
} // namespace

std::string HandleBridgeVerb(const std::string& verb, const std::string& payload, bool& ok)
{
    if (verb == "ai.reset")
    {
        ResetDebugWorld();
        ok = true;
        return "xrsim reset";
    }

    if (verb == "ai.status")
    {
        ok = true;
        return std::string("xrsim initialized=") + (g_initialized ? "1" : "0") +
            " log=" + std::to_string(g_debugWorld.ToolLog().size());
    }

    if (verb == "ai.observe")
    {
        EnsureDebugWorld();
        ok = true;
        return g_debugWorld.Digest();
    }

    if (verb == "ai.inject")
        return InjectIntent(payload, ok);

    if (verb == "ai.snapshot")
    {
        EnsureDebugWorld();
        ok = true;
        return g_debugWorld.SaveSnapshot();
    }

    if (verb == "ai.restore")
    {
        Result result = g_debugWorld.LoadSnapshot(payload);
        ok = result.ok;
        if (!result.ok)
            return result.reason;
        g_initialized = true;
        g_nextSeq = uint32_t(g_debugWorld.ToolLog().size() + 1);
        return "xrsim restored log=" + std::to_string(g_debugWorld.ToolLog().size());
    }

    if (verb == "ai.log")
    {
        ok = true;
        return FormatToolLog();
    }

    if (verb == "ai.replay")
    {
        EnsureDebugWorld();
        WorldState replay = MakeDebugBaseline();
        const Result result = replay.ReplayToolLogFrom(g_debugWorld);
        ok = result.ok;
        if (!result.ok)
            return result.reason;
        return "xrsim replay digest=" + replay.Digest();
    }

    ok = false;
    return "unknown ai verb: " + verb;
}
} // namespace xrSim
