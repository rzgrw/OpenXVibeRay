#include "xrSim/xrSimZoneState.h"

#include <sstream>

namespace xrSim
{
namespace
{
constexpr uint32_t ZoneIndexMask = 0x00ffffffu;
constexpr uint32_t ZoneGenerationShift = 24;

Handle MakeZoneHandle(uint32_t index, uint8_t generation = 1)
{
    return Handle{ (uint32_t(generation) << ZoneGenerationShift) | (index & ZoneIndexMask) };
}
} // namespace

ZoneResult ZoneState::Bootstrap(const ZoneManifest& manifest)
{
    ZoneState staged;

    for (size_t i = 0; i < manifest.factions.size(); ++i)
    {
        const auto& mf = manifest.factions[i];
        FactionRecord f;
        f.id = MakeZoneHandle(uint32_t(i), 1);
        f.name = mf.name;
        f.agentId = mf.agentId;
        staged.m_factions.push_back(f);
    }

    for (size_t i = 0; i < manifest.anchors.size(); ++i)
    {
        const auto& ma = manifest.anchors[i];
        LocationRecord loc;
        loc.id = MakeZoneHandle(uint32_t(i), 1);
        loc.name = ma.name;
        loc.position.xCm = ma.xCm;
        loc.position.yCm = ma.yCm;
        loc.position.zCm = ma.zCm;
        loc.position.levelVertex = 0xffffffffu;
        loc.ownerFaction = Handle::Invalid();
        staged.m_locations.push_back(loc);
    }

    for (size_t i = 0; i < manifest.routes.size(); ++i)
    {
        const auto& mr = manifest.routes[i];
        const Handle from = staged.FindLocationByName(mr.from);
        const Handle to = staged.FindLocationByName(mr.to);
        if (!from.IsValid() || !to.IsValid())
            return ZoneResult{ false, "route references undefined anchor: " + mr.name, 0 };

        RouteRecord route;
        route.id = MakeZoneHandle(uint32_t(i), 1);
        route.name = mr.name;
        route.from = from;
        route.to = to;
        route.open = true;
        staged.m_routes.push_back(route);
    }

    for (size_t i = 0; i < manifest.resources.size(); ++i)
    {
        const auto& mres = manifest.resources[i];
        const Handle loc = staged.FindLocationByName(mres.anchor);
        if (!loc.IsValid())
            return ZoneResult{ false, "resource references undefined anchor: " + mres.anchor, 0 };

        ResourceRecord res;
        res.id = MakeZoneHandle(uint32_t(i), 1);
        res.name = mres.name;
        res.kind = mres.kind;
        res.location = loc;
        res.quantity = mres.quantity;
        res.minimum = mres.minimum;
        res.supply = 0;
        res.demand = 0;
        res.maximum = 1000;
        staged.m_resources.push_back(res);
    }

    for (size_t i = 0; i < manifest.groups.size(); ++i)
    {
        const auto& mg = manifest.groups[i];
        const Handle loc = staged.FindLocationByName(mg.anchor);
        if (!loc.IsValid())
            return ZoneResult{ false, "group references undefined anchor: " + mg.anchor, 0 };

        Handle faction = Handle::Invalid();
        GroupKind kind = GroupKind::Squad;
        if (mg.kind == "squad")
        {
            kind = GroupKind::Squad;
            faction = staged.FindFactionByName(mg.ownerDomain);
            if (!faction.IsValid())
                return ZoneResult{ false, "squad references undefined faction: " + mg.ownerDomain, 0 };
        }
        else if (mg.kind == "mutant_pack")
        {
            kind = GroupKind::MutantPack;
            faction = Handle::Invalid();
        }
        else
        {
            return ZoneResult{ false, "unknown group kind: " + mg.kind, 0 };
        }

        GroupRecord grp;
        grp.id = MakeZoneHandle(uint32_t(i), 1);
        grp.name = mg.name;
        grp.kind = kind;
        grp.faction = faction;
        grp.section = mg.section;
        grp.initialCount = mg.count;
        grp.aliveCount = mg.count;
        grp.location = loc;
        grp.agentId = mg.agentId;
        grp.active = true;
        staged.m_groups.push_back(grp);
    }

    staged.m_revision = 1;
    *this = std::move(staged);
    return ZoneResult{ true, "", m_revision };
}

uint64_t ZoneState::Revision() const
{
    return m_revision;
}

Handle ZoneState::FindFactionByName(const std::string& name) const
{
    for (const auto& f : m_factions)
    {
        if (f.name == name)
            return f.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindLocationByName(const std::string& name) const
{
    for (const auto& l : m_locations)
    {
        if (l.name == name)
            return l.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindGroupByName(const std::string& name) const
{
    for (const auto& g : m_groups)
    {
        if (g.name == name)
            return g.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindResourceByName(const std::string& name) const
{
    for (const auto& r : m_resources)
    {
        if (r.name == name)
            return r.id;
    }
    return Handle::Invalid();
}

Handle ZoneState::FindRouteByName(const std::string& name) const
{
    for (const auto& r : m_routes)
    {
        if (r.name == name)
            return r.id;
    }
    return Handle::Invalid();
}

const FactionRecord* ZoneState::FindFaction(Handle id) const
{
    for (const auto& f : m_factions)
    {
        if (f.id == id)
            return &f;
    }
    return nullptr;
}

const LocationRecord* ZoneState::FindLocation(Handle id) const
{
    for (const auto& l : m_locations)
    {
        if (l.id == id)
            return &l;
    }
    return nullptr;
}

LocationRecord* ZoneState::FindLocation(Handle id)
{
    for (auto& l : m_locations)
    {
        if (l.id == id)
            return &l;
    }
    return nullptr;
}

const GroupRecord* ZoneState::FindGroup(Handle id) const
{
    for (const auto& g : m_groups)
    {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

GroupRecord* ZoneState::FindGroup(Handle id)
{
    for (auto& g : m_groups)
    {
        if (g.id == id)
            return &g;
    }
    return nullptr;
}

const ResourceRecord* ZoneState::FindResource(Handle id) const
{
    for (const auto& r : m_resources)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

ResourceRecord* ZoneState::FindResource(Handle id)
{
    for (auto& r : m_resources)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

const RouteRecord* ZoneState::FindRoute(Handle id) const
{
    for (const auto& r : m_routes)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

RouteRecord* ZoneState::FindRoute(Handle id)
{
    for (auto& r : m_routes)
    {
        if (r.id == id)
            return &r;
    }
    return nullptr;
}

ZoneResult ZoneState::AddGroup(const GroupRecord& group)
{
    GroupRecord g = group;
    g.id = MakeZoneHandle(uint32_t(m_groups.size()), 1);
    m_groups.push_back(g);
    return ZoneResult{ true, "", m_revision };
}

ZoneResult ZoneState::DeactivateGroup(Handle group)
{
    GroupRecord* g = FindGroup(group);
    if (!g)
        return ZoneResult{ false, "group not found", m_revision };
    g->active = false;
    return ZoneResult{ true, "", m_revision };
}

ZoneResult ZoneState::SetRelation(Handle first, Handle second, int16_t score)
{
    for (auto& r : m_relations)
    {
        if ((r.first == first && r.second == second) || (r.first == second && r.second == first))
        {
            r.score = score;
            return ZoneResult{ true, "", m_revision };
        }
    }
    m_relations.push_back(RelationRecord{ first, second, score });
    return ZoneResult{ true, "", m_revision };
}

ZoneResult ZoneState::AppendIncident(const IncidentRecord& incident)
{
    m_incidents.push_back(incident);
    return ZoneResult{ true, "", m_revision };
}

ZoneResult ZoneState::PromoteSalientActor(Handle group, uint16_t liveObjectId)
{
    SalientActorRecord actor;
    actor.id = MakeZoneHandle(uint32_t(m_salientActors.size()), 1);
    actor.parentGroup = group;
    actor.lastLiveObjectId = liveObjectId;
    actor.alive = true;
    m_salientActors.push_back(actor);
    return ZoneResult{ true, "", m_revision };
}

const std::vector<ObjectiveRecord>& ZoneState::Objectives() const
{
    return m_objectives;
}

std::vector<ObjectiveRecord>& ZoneState::MutableObjectives()
{
    return m_objectives;
}

const std::vector<WorldEvent>& ZoneState::Events() const
{
    return m_events;
}

void ZoneState::AppendEvent(const WorldEvent& event)
{
    WorldEvent e = event;
    e.seq = m_nextEventSeq++;
    m_events.push_back(e);
}

void ZoneState::SetRevision(uint64_t revision)
{
    m_revision = revision;
}

std::string ZoneState::Digest() const
{
    std::ostringstream out;
    out << "rev=" << m_revision
        << " factions=" << m_factions.size()
        << " locations=" << m_locations.size()
        << " routes=" << m_routes.size()
        << " groups=" << m_groups.size()
        << " resources=" << m_resources.size();

    for (const auto& f : m_factions)
        out << " | f:" << f.name;

    for (const auto& loc : m_locations)
        out << " | loc:" << loc.name << "@" << loc.position.xCm << "," << loc.position.yCm << "," << loc.position.zCm;

    for (const auto& g : m_groups)
        out << " | grp:" << g.name << ":" << g.aliveCount << "/" << g.initialCount;

    for (const auto& r : m_resources)
        out << " | res:" << r.name << ":" << r.quantity;

    return out.str();
}

std::string ZoneState::SaveSnapshot() const
{
    std::ostringstream out;
    out << "xrsim_zone_snapshot_v1\n";
    out << "revision " << m_revision << "\n";
    out << "factions " << m_factions.size() << "\n";
    for (const auto& f : m_factions)
        out << "faction " << f.id.value << " " << f.agentId << " " << f.name << "\n";
    out << "locations " << m_locations.size() << "\n";
    for (const auto& l : m_locations)
        out << "location " << l.id.value << " " << l.position.xCm << " " << l.position.yCm << " "
            << l.position.zCm << " " << l.name << "\n";
    out << "groups " << m_groups.size() << "\n";
    for (const auto& g : m_groups)
        out << "group " << g.id.value << " " << uint32_t(g.kind) << " " << g.faction.value << " "
            << g.location.value << " " << g.aliveCount << " " << g.initialCount << " " << g.name << "\n";
    out << "resources " << m_resources.size() << "\n";
    for (const auto& r : m_resources)
        out << "resource " << r.id.value << " " << r.location.value << " " << r.quantity << " " << r.name << "\n";
    out << "end\n";
    return out.str();
}
} // namespace xrSim
