#include "xrSim/xrSimWorldState.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace xrSim
{
namespace
{
constexpr uint32_t IndexMask = 0x00ffffffu;
constexpr uint32_t GenerationShift = 24;

Handle MakeHandle(uint32_t index, uint8_t generation)
{
    return Handle{ (uint32_t(generation) << GenerationShift) | (index & IndexMask) };
}

int32_t ClampPopulation(int32_t value, int32_t carryingCapacity)
{
    if (value < 0)
        return 0;
    if (value > carryingCapacity)
        return carryingCapacity;
    return value;
}
} // namespace

Handle Handle::Invalid() { return Handle{}; }

bool Handle::IsValid() const { return value != Invalid().value && Index() < IndexMask; }

uint32_t Handle::Index() const { return value & IndexMask; }

uint32_t Handle::Generation() const { return value >> GenerationShift; }

bool operator==(Handle lhs, Handle rhs) { return lhs.value == rhs.value; }

bool operator!=(Handle lhs, Handle rhs) { return !(lhs == rhs); }

Handle WorldState::CreateRegion(const std::string& name, int32_t carryingCapacity)
{
    Region region;
    region.name = name;
    region.carryingCapacity = carryingCapacity < 0 ? 0 : carryingCapacity;
    m_regions.push_back(region);
    return MakeHandle(uint32_t(m_regions.size() - 1), region.generation);
}

Handle WorldState::CreateSpecies(const std::string& name)
{
    Species species;
    species.name = name;
    m_species.push_back(species);
    return MakeHandle(uint32_t(m_species.size() - 1), species.generation);
}

Handle WorldState::FindRegionByName(const std::string& name) const
{
    for (size_t i = 0; i < m_regions.size(); ++i)
    {
        if (m_regions[i].name == name)
            return MakeHandle(uint32_t(i), m_regions[i].generation);
    }
    return Handle::Invalid();
}

Handle WorldState::FindSpeciesByName(const std::string& name) const
{
    for (size_t i = 0; i < m_species.size(); ++i)
    {
        if (m_species[i].name == name)
            return MakeHandle(uint32_t(i), m_species[i].generation);
    }
    return Handle::Invalid();
}

Result WorldState::SetPopulation(Handle region, Handle species, int32_t count)
{
    const Region* regionRecord = FindRegion(region);
    if (!regionRecord)
        return Result{ false, 0, "unknown region" };
    if (!FindSpecies(species))
        return Result{ false, 0, "unknown species" };

    Cohort* cohort = FindCohort(region, species);
    if (!cohort)
    {
        m_cohorts.push_back(Cohort{ region, species, 0 });
        cohort = &m_cohorts.back();
    }

    cohort->count = ClampPopulation(count, regionRecord->carryingCapacity);
    return Result{ true, 0, "" };
}

Result WorldState::ApplyAdjustPopulation(uint32_t seq, Handle region, Handle species, int32_t delta, uint32_t gameDay)
{
    ToolRecord record;
    record.seq = seq;
    record.gameDay = gameDay;
    record.region = region;
    record.species = species;
    record.requestedDelta = delta;

    const Region* regionRecord = FindRegion(region);
    if (!regionRecord)
    {
        record.reason = "unknown region";
        m_toolLog.push_back(record);
        return Result{ false, 0, record.reason };
    }
    if (!FindSpecies(species))
    {
        record.reason = "unknown species";
        m_toolLog.push_back(record);
        return Result{ false, 0, record.reason };
    }

    Cohort* cohort = FindCohort(region, species);
    if (!cohort)
    {
        m_cohorts.push_back(Cohort{ region, species, 0 });
        cohort = &m_cohorts.back();
    }

    const int32_t rateLimit = std::max<int32_t>(1, regionRecord->carryingCapacity / 10);
    int32_t applied = delta;
    if (applied > rateLimit)
        applied = rateLimit;
    if (applied < -rateLimit)
        applied = -rateLimit;

    const int32_t next = ClampPopulation(cohort->count + applied, regionRecord->carryingCapacity);
    applied = next - cohort->count;
    cohort->count = next;

    record.accepted = true;
    record.appliedDelta = applied;
    m_toolLog.push_back(record);
    return Result{ true, applied, "" };
}

int32_t WorldState::Population(Handle region, Handle species) const
{
    const Cohort* cohort = FindCohort(region, species);
    return cohort ? cohort->count : 0;
}

const std::vector<ToolRecord>& WorldState::ToolLog() const { return m_toolLog; }

std::string WorldState::Digest() const
{
    std::string out = "regions=" + std::to_string(m_regions.size()) + " species=" + std::to_string(m_species.size()) +
        " cohorts=" + std::to_string(m_cohorts.size()) + " log=" + std::to_string(m_toolLog.size()) + " pop{";

    for (size_t i = 0; i < m_cohorts.size(); ++i)
    {
        const Cohort& cohort = m_cohorts[i];
        const Region* region = FindRegion(cohort.region);
        const Species* species = FindSpecies(cohort.species);
        if (i != 0)
            out += ",";
        out += region ? region->name : "?";
        out += ":";
        out += species ? species->name : "?";
        out += "=";
        out += std::to_string(cohort.count);
    }

    out += "}";
    return out;
}

std::string WorldState::SaveSnapshot() const
{
    std::ostringstream out;
    out << "xrsim_snapshot_v1\n";

    out << "regions " << m_regions.size() << "\n";
    for (size_t i = 0; i < m_regions.size(); ++i)
    {
        const Region& region = m_regions[i];
        out << "region " << MakeHandle(uint32_t(i), region.generation).value << " " << region.carryingCapacity << " "
            << region.name << "\n";
    }

    out << "species " << m_species.size() << "\n";
    for (size_t i = 0; i < m_species.size(); ++i)
    {
        const Species& species = m_species[i];
        out << "species " << MakeHandle(uint32_t(i), species.generation).value << " " << species.name << "\n";
    }

    out << "cohorts " << m_cohorts.size() << "\n";
    for (const Cohort& cohort : m_cohorts)
        out << "cohort " << cohort.region.value << " " << cohort.species.value << " " << cohort.count << "\n";

    out << "tools " << m_toolLog.size() << "\n";
    for (const ToolRecord& record : m_toolLog)
    {
        out << "tool " << record.seq << " " << record.gameDay << " " << record.tool << " " << record.region.value << " "
            << record.species.value << " " << (record.accepted ? 1 : 0) << " " << record.requestedDelta << " "
            << record.appliedDelta << " " << (record.reason.empty() ? "-" : record.reason) << "\n";
    }

    out << "end\n";
    return out.str();
}

Result WorldState::LoadSnapshot(const std::string& snapshot)
{
    std::istringstream input(snapshot);
    std::string token;
    input >> token;
    if (token != "xrsim_snapshot_v1")
        return Result{ false, 0, "unsupported snapshot version" };

    WorldState loaded;
    size_t count = 0;

    input >> token >> count;
    if (!input || token != "regions")
        return Result{ false, 0, "missing regions block" };
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t handleValue = 0;
        int32_t carryingCapacity = 0;
        std::string name;
        input >> token >> handleValue >> carryingCapacity >> name;
        const Handle handle{ handleValue };
        if (!input || token != "region" || handle.Index() != loaded.m_regions.size())
            return Result{ false, 0, "invalid region record" };

        Region region;
        region.generation = uint8_t(handle.Generation());
        region.name = name;
        region.carryingCapacity = carryingCapacity < 0 ? 0 : carryingCapacity;
        loaded.m_regions.push_back(region);
    }

    input >> token >> count;
    if (!input || token != "species")
        return Result{ false, 0, "missing species block" };
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t handleValue = 0;
        std::string name;
        input >> token >> handleValue >> name;
        const Handle handle{ handleValue };
        if (!input || token != "species" || handle.Index() != loaded.m_species.size())
            return Result{ false, 0, "invalid species record" };

        Species species;
        species.generation = uint8_t(handle.Generation());
        species.name = name;
        loaded.m_species.push_back(species);
    }

    input >> token >> count;
    if (!input || token != "cohorts")
        return Result{ false, 0, "missing cohorts block" };
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t regionValue = 0;
        uint32_t speciesValue = 0;
        int32_t cohortCount = 0;
        input >> token >> regionValue >> speciesValue >> cohortCount;
        const Handle region{ regionValue };
        const Handle species{ speciesValue };
        if (!input || token != "cohort" || !loaded.FindRegion(region) || !loaded.FindSpecies(species))
            return Result{ false, 0, "invalid cohort record" };

        loaded.m_cohorts.push_back(Cohort{ region, species, cohortCount });
    }

    input >> token >> count;
    if (!input || token != "tools")
        return Result{ false, 0, "missing tools block" };
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t regionValue = 0;
        uint32_t speciesValue = 0;
        int accepted = 0;
        std::string tool;
        std::string reason;
        ToolRecord record;
        input >> token >> record.seq >> record.gameDay >> tool >> regionValue >> speciesValue >> accepted >>
            record.requestedDelta >> record.appliedDelta >> reason;
        record.tool = "adjust_population";
        record.region = Handle{ regionValue };
        record.species = Handle{ speciesValue };
        record.accepted = accepted != 0;
        record.reason = reason == "-" ? "" : reason;
        if (!input || token != "tool" || tool != "adjust_population")
            return Result{ false, 0, "invalid tool record" };
        loaded.m_toolLog.push_back(record);
    }

    input >> token;
    if (!input || token != "end")
        return Result{ false, 0, "missing snapshot end" };

    *this = loaded;
    return Result{ true, 0, "" };
}

Result WorldState::ReplayToolLogFrom(const WorldState& recorded)
{
    for (const ToolRecord& expected : recorded.ToolLog())
    {
        if (std::string(expected.tool) != "adjust_population")
            return Result{ false, 0, "unsupported replay tool" };

        const Result actual =
            ApplyAdjustPopulation(expected.seq, expected.region, expected.species, expected.requestedDelta, expected.gameDay);
        if (actual.ok != expected.accepted || actual.appliedDelta != expected.appliedDelta)
            return Result{ false, actual.appliedDelta, "replay mismatch" };
    }

    if (Digest() != recorded.Digest())
        return Result{ false, 0, "replay mismatch digest" };

    return Result{ true, 0, "" };
}

const WorldState::Region* WorldState::FindRegion(Handle handle) const
{
    if (!handle.IsValid() || handle.Index() >= m_regions.size())
        return nullptr;
    const Region& region = m_regions[handle.Index()];
    return region.generation == handle.Generation() ? &region : nullptr;
}

const WorldState::Species* WorldState::FindSpecies(Handle handle) const
{
    if (!handle.IsValid() || handle.Index() >= m_species.size())
        return nullptr;
    const Species& species = m_species[handle.Index()];
    return species.generation == handle.Generation() ? &species : nullptr;
}

WorldState::Cohort* WorldState::FindCohort(Handle region, Handle species)
{
    for (Cohort& cohort : m_cohorts)
    {
        if (cohort.region == region && cohort.species == species)
            return &cohort;
    }
    return nullptr;
}

const WorldState::Cohort* WorldState::FindCohort(Handle region, Handle species) const
{
    for (const Cohort& cohort : m_cohorts)
    {
        if (cohort.region == region && cohort.species == species)
            return &cohort;
    }
    return nullptr;
}
} // namespace xrSim
