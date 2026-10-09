#pragma once

#include "xrSim/xrSimWorldState.h"
#include "xrSim/xrSimZoneManifest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ZoneResult
{
    bool ok = false;
    std::string reason;
    uint64_t revision = 0;
};

struct FixedPosition
{
    int32_t xCm = 0;
    int32_t yCm = 0;
    int32_t zCm = 0;
    uint32_t levelVertex = 0xffffffffu;
};

enum class GroupKind : uint8_t
{
    Squad,
    MutantPack
};

enum class ObjectiveKind : uint8_t
{
    Hold,
    Move,
    Retreat,
    Resupply,
    Hunt,
    Avoid,
    Reinforce
};

enum class ObjectiveAuthor : uint8_t
{
    Legacy,
    Ai,
    Executor
};

enum class ObjectiveSubjectKind : uint8_t
{
    Scope,
    Faction,
    Group,
    NamedActor
};

struct FactionRecord
{
    Handle id;
    std::string name;
    uint32_t agentId = 0;
};

struct LocationRecord
{
    Handle id;
    std::string name;
    FixedPosition position;
    Handle ownerFaction;
};

struct RouteRecord
{
    Handle id;
    std::string name;
    Handle from;
    Handle to;
    bool open = true;
};

struct GroupRecord
{
    Handle id;
    std::string name;
    GroupKind kind = GroupKind::Squad;
    Handle faction;
    std::string section;
    uint32_t initialCount = 0;
    uint32_t aliveCount = 0;
    Handle location;
    uint32_t agentId = 0;
    bool active = true;
};

struct ResourceRecord
{
    Handle id;
    std::string name;
    std::string kind;
    Handle location;
    int32_t quantity = 0;
    int32_t minimum = 0;
    int32_t supply = 0;
    int32_t demand = 0;
    int32_t maximum = 1000;
};

struct RelationRecord
{
    Handle first;
    Handle second;
    int16_t score = 0;
};

struct ObjectiveRecord
{
    uint64_t planId = 0;
    uint32_t scopeKind = 0;
    uint32_t scopeId = 0;
    ObjectiveSubjectKind subjectKind = ObjectiveSubjectKind::Group;
    Handle subject;
    std::string subjectName;
    ObjectiveKind kind = ObjectiveKind::Hold;
    Handle target;
    uint64_t expiresAtTick = 0;
    ObjectiveAuthor author = ObjectiveAuthor::Legacy;
    bool active = false;
};

struct IncidentRecord
{
    uint64_t id = 0;
    Handle location;
    std::string kind;
    int32_t magnitude = 0;
    bool pending = true;
};

struct SalientActorRecord
{
    Handle id;
    Handle parentGroup;
    uint16_t lastLiveObjectId = 0xffffu;
    bool alive = true;
};

struct WorldEvent
{
    uint64_t seq = 0;
    uint64_t tick = 0;
    std::string kind;
    Handle subject;
    Handle object;
    int32_t amount = 0;
    std::string source;
};

class ZoneState
{
public:
    ZoneResult Bootstrap(const ZoneManifest& manifest);
    uint64_t Revision() const;
    Handle FindFactionByName(const std::string& name) const;
    Handle FindLocationByName(const std::string& name) const;
    Handle FindGroupByName(const std::string& name) const;
    Handle FindResourceByName(const std::string& name) const;
    Handle FindRouteByName(const std::string& name) const;
    const FactionRecord* FindFaction(Handle id) const;
    const LocationRecord* FindLocation(Handle id) const;
    LocationRecord* FindLocation(Handle id);
    const GroupRecord* FindGroup(Handle id) const;
    GroupRecord* FindGroup(Handle id);
    const ResourceRecord* FindResource(Handle id) const;
    ResourceRecord* FindResource(Handle id);
    const RouteRecord* FindRoute(Handle id) const;
    RouteRecord* FindRoute(Handle id);
    ZoneResult AddGroup(const GroupRecord& group);
    ZoneResult DeactivateGroup(Handle group);
    ZoneResult SetRelation(Handle first, Handle second, int16_t score);
    ZoneResult AppendIncident(const IncidentRecord& incident);
    ZoneResult PromoteSalientActor(Handle group, uint16_t liveObjectId);
    const std::vector<ObjectiveRecord>& Objectives() const;
    std::vector<ObjectiveRecord>& MutableObjectives();
    const std::vector<WorldEvent>& Events() const;
    void AppendEvent(const WorldEvent& event);
    void SetRevision(uint64_t revision);
    std::string Digest() const;
    std::string SaveSnapshot() const;

private:
    uint64_t m_revision = 0;
    uint64_t m_nextEventSeq = 1;
    std::vector<FactionRecord> m_factions;
    std::vector<LocationRecord> m_locations;
    std::vector<RouteRecord> m_routes;
    std::vector<GroupRecord> m_groups;
    std::vector<ResourceRecord> m_resources;
    std::vector<RelationRecord> m_relations;
    std::vector<ObjectiveRecord> m_objectives;
    std::vector<IncidentRecord> m_incidents;
    std::vector<SalientActorRecord> m_salientActors;
    std::vector<WorldEvent> m_events;
};
} // namespace xrSim
