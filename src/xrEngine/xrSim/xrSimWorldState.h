#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct Handle
{
    uint32_t value = 0xffffffffu;

    static Handle Invalid();

    bool IsValid() const;
    uint32_t Index() const;
    uint32_t Generation() const;
};

bool operator==(Handle lhs, Handle rhs);
bool operator!=(Handle lhs, Handle rhs);

struct Result
{
    bool ok = false;
    int32_t appliedDelta = 0;
    std::string reason;
};

struct ToolRecord
{
    uint32_t seq = 0;
    uint32_t gameDay = 0;
    const char* tool = "adjust_population";
    Handle region;
    Handle species;
    bool accepted = false;
    int32_t requestedDelta = 0;
    int32_t appliedDelta = 0;
    std::string reason;
};

class WorldState
{
public:
    Handle CreateRegion(const std::string& name, int32_t carryingCapacity);
    Handle CreateSpecies(const std::string& name);

    Result SetPopulation(Handle region, Handle species, int32_t count);
    Result ApplyAdjustPopulation(uint32_t seq, Handle region, Handle species, int32_t delta, uint32_t gameDay);

    int32_t Population(Handle region, Handle species) const;
    const std::vector<ToolRecord>& ToolLog() const;
    std::string Digest() const;

private:
    struct Region
    {
        uint8_t generation = 1;
        std::string name;
        int32_t carryingCapacity = 0;
    };

    struct Species
    {
        uint8_t generation = 1;
        std::string name;
    };

    struct Cohort
    {
        Handle region;
        Handle species;
        int32_t count = 0;
    };

    const Region* FindRegion(Handle handle) const;
    const Species* FindSpecies(Handle handle) const;
    Cohort* FindCohort(Handle region, Handle species);
    const Cohort* FindCohort(Handle region, Handle species) const;

    std::vector<Region> m_regions;
    std::vector<Species> m_species;
    std::vector<Cohort> m_cohorts;
    std::vector<ToolRecord> m_toolLog;
};
} // namespace xrSim
