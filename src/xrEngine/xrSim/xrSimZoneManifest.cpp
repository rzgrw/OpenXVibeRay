#include "xrSim/xrSimZoneManifest.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>

namespace xrSim
{
namespace
{
constexpr size_t MaxNameLength = 96;
constexpr size_t MaxAnchors = 64;
constexpr size_t MaxFactions = 64;
constexpr size_t MaxGroups = 32;
constexpr size_t MaxResources = 64;
constexpr size_t MaxRoutes = 64;
constexpr size_t MaxAgents = 32;
constexpr uint32_t MaxGroupCount = 8;

bool ParseInt32(const std::string& text, int32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    if (parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
        return false;
    value = int32_t(parsed);
    return true;
}

bool ParseUint32(const std::string& text, uint32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    if (parsed > std::numeric_limits<uint32_t>::max())
        return false;
    value = uint32_t(parsed);
    return true;
}

std::vector<std::string> SplitCsv(const std::string& line)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : line)
    {
        if (ch == ',')
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current += ch;
        }
    }
    parts.push_back(current);
    return parts;
}

bool IsValidUtf8AndNoControl(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 && c != '\t' && c != '\r' && c != '\n')
            return false;
        if (c == 0x7F)
            return false;
        // Basic UTF-8 lead byte check
        if (c >= 0x80)
        {
            size_t extra = 0;
            if ((c & 0xE0) == 0xC0)
                extra = 1;
            else if ((c & 0xF0) == 0xE0)
                extra = 2;
            else if ((c & 0xF8) == 0xF0)
                extra = 3;
            else
                return false;

            if (i + extra >= text.size())
                return false;
            for (size_t j = 0; j < extra; ++j)
            {
                const unsigned char follow = static_cast<unsigned char>(text[++i]);
                if ((follow & 0xC0) != 0x80)
                    return false;
            }
        }
    }
    return true;
}

bool IsValidCharterKey(const std::string& key)
{
    return key == "zone_director_v1" || key == "stalker_faction_v1" || key == "bandit_faction_v1" ||
        key == "ecology_v1" || key == "economy_v1" || key == "region_v1" || key == "squad_v1" || key == "pack_v1";
}

bool IsValidScopeKind(const std::string& kind)
{
    return kind == "zone" || kind == "faction" || kind == "ecology" || kind == "economy" ||
        kind == "region" || kind == "squad" || kind == "mutant_pack";
}
} // namespace

ZoneManifestParseResult ParseZoneManifest(const std::string& text)
{
    ZoneManifestParseResult result;

    if (!IsValidUtf8AndNoControl(text))
    {
        result.reason = "invalid UTF-8 or control bytes in manifest";
        return result;
    }

    std::istringstream stream(text);
    std::string line;

    bool sawHeader = false;
    bool sawScenario = false;
    bool sawSave = false;
    bool sawLevel = false;
    bool sawSeed = false;
    bool sawOrigin = false;

    std::set<std::string> anchorNames;
    std::set<std::string> factionNames;
    std::set<std::string> groupNames;
    std::set<std::string> resourceNames;
    std::set<std::string> routeNames;
    std::set<uint32_t> agentIds;
    std::set<std::pair<std::string, uint32_t>> agentScopes;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        if (!sawHeader)
        {
            if (line != "xrsim_zone_manifest_v1")
            {
                result.reason = "unsupported manifest header: " + line;
                return result;
            }
            sawHeader = true;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            result.reason = "malformed manifest line: " + line;
            return result;
        }

        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "scenario")
        {
            if (sawScenario)
            {
                result.reason = "duplicate singleton: scenario";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid scenario name length";
                return result;
            }
            result.manifest.scenario = value;
            sawScenario = true;
        }
        else if (key == "save")
        {
            if (sawSave)
            {
                result.reason = "duplicate singleton: save";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid save name length";
                return result;
            }
            result.manifest.saveName = value;
            sawSave = true;
        }
        else if (key == "level")
        {
            if (sawLevel)
            {
                result.reason = "duplicate singleton: level";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid level name length";
                return result;
            }
            result.manifest.levelName = value;
            sawLevel = true;
        }
        else if (key == "seed")
        {
            if (sawSeed)
            {
                result.reason = "duplicate singleton: seed";
                return result;
            }
            if (!ParseUint32(value, result.manifest.seed))
            {
                result.reason = "invalid seed value: " + value;
                return result;
            }
            sawSeed = true;
        }
        else if (key == "origin")
        {
            if (sawOrigin)
            {
                result.reason = "duplicate singleton: origin";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid origin name length";
                return result;
            }
            result.manifest.origin = value;
            sawOrigin = true;
        }
        else if (key == "anchor")
        {
            if (result.manifest.anchors.size() >= MaxAnchors)
            {
                result.reason = "anchor limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 4)
            {
                result.reason = "invalid anchor format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !anchorNames.insert(name).second)
            {
                result.reason = "duplicate or invalid anchor name: " + name;
                return result;
            }
            ManifestAnchor anchor;
            anchor.name = name;
            if (!ParseInt32(parts[1], anchor.xCm) || !ParseInt32(parts[2], anchor.yCm) ||
                !ParseInt32(parts[3], anchor.zCm))
            {
                result.reason = "invalid anchor coordinates: " + value;
                return result;
            }
            result.manifest.anchors.push_back(anchor);
        }
        else if (key == "faction")
        {
            if (result.manifest.factions.size() >= MaxFactions)
            {
                result.reason = "faction limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 2)
            {
                result.reason = "invalid faction format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !factionNames.insert(name).second)
            {
                result.reason = "duplicate or invalid faction name: " + name;
                return result;
            }
            ManifestFaction faction;
            faction.name = name;
            if (!ParseUint32(parts[1], faction.agentId) || faction.agentId == 0)
            {
                result.reason = "invalid faction agent id: " + parts[1];
                return result;
            }
            result.manifest.factions.push_back(faction);
        }
        else if (key == "group")
        {
            if (result.manifest.groups.size() >= MaxGroups)
            {
                result.reason = "group limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 7)
            {
                result.reason = "invalid group format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !groupNames.insert(name).second)
            {
                result.reason = "duplicate or invalid group name: " + name;
                return result;
            }
            ManifestGroup group;
            group.name = name;
            group.kind = parts[1];
            group.ownerDomain = parts[2];
            group.section = parts[3];
            group.anchor = parts[5];
            if (group.kind != "squad" && group.kind != "mutant_pack")
            {
                result.reason = "unknown group kind: " + group.kind;
                return result;
            }
            if (group.section.empty() || group.section.size() > MaxNameLength)
            {
                result.reason = "invalid group section: " + group.section;
                return result;
            }
            if (!ParseUint32(parts[4], group.count) || group.count == 0 || group.count > MaxGroupCount)
            {
                result.reason = "invalid group count: " + parts[4];
                return result;
            }
            if (!ParseUint32(parts[6], group.agentId) || group.agentId == 0)
            {
                result.reason = "invalid group agent id: " + parts[6];
                return result;
            }
            result.manifest.groups.push_back(group);
        }
        else if (key == "resource")
        {
            if (result.manifest.resources.size() >= MaxResources)
            {
                result.reason = "resource limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 5)
            {
                result.reason = "invalid resource format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !resourceNames.insert(name).second)
            {
                result.reason = "duplicate or invalid resource name: " + name;
                return result;
            }
            ManifestResource resource;
            resource.name = name;
            resource.kind = parts[1];
            resource.anchor = parts[2];
            if (!ParseInt32(parts[3], resource.quantity) || !ParseInt32(parts[4], resource.minimum))
            {
                result.reason = "invalid resource quantity/minimum: " + value;
                return result;
            }
            result.manifest.resources.push_back(resource);
        }
        else if (key == "route")
        {
            if (result.manifest.routes.size() >= MaxRoutes)
            {
                result.reason = "route limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 3)
            {
                result.reason = "invalid route format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !routeNames.insert(name).second)
            {
                result.reason = "duplicate or invalid route name: " + name;
                return result;
            }
            ManifestRoute route;
            route.name = name;
            route.from = parts[1];
            route.to = parts[2];
            result.manifest.routes.push_back(route);
        }
        else if (key == "agent")
        {
            if (result.manifest.agents.size() >= MaxAgents)
            {
                result.reason = "agent limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 5)
            {
                result.reason = "invalid agent format: " + value;
                return result;
            }
            ManifestAgent agent;
            if (!ParseUint32(parts[0], agent.agentId) || agent.agentId == 0)
            {
                result.reason = "invalid agent id: " + parts[0];
                return result;
            }
            if (!agentIds.insert(agent.agentId).second)
            {
                result.reason = "duplicate agent id: " + parts[0];
                return result;
            }
            agent.scopeKind = parts[1];
            if (!IsValidScopeKind(agent.scopeKind))
            {
                result.reason = "unknown scope kind: " + agent.scopeKind;
                return result;
            }
            if (!ParseUint32(parts[2], agent.scopeId))
            {
                result.reason = "invalid scope id: " + parts[2];
                return result;
            }
            if (!agentScopes.insert({ agent.scopeKind, agent.scopeId }).second)
            {
                result.reason = "duplicate agent scope: " + agent.scopeKind + "," + parts[2];
                return result;
            }
            agent.name = parts[3];
            agent.charterKey = parts[4];
            if (!IsValidCharterKey(agent.charterKey))
            {
                result.reason = "unknown charter key: " + agent.charterKey;
                return result;
            }
            result.manifest.agents.push_back(agent);
        }
        else
        {
            result.reason = "unknown key: " + key;
            return result;
        }
    }

    if (!sawHeader)
    {
        result.reason = "missing manifest header";
        return result;
    }
    if (!sawScenario || !sawSave || !sawLevel || !sawSeed || !sawOrigin)
    {
        result.reason = "missing required singleton fields";
        return result;
    }

    // Cross-reference integrity validation
    for (const auto& group : result.manifest.groups)
    {
        if (anchorNames.find(group.anchor) == anchorNames.end())
        {
            result.reason = "group references undefined anchor: " + group.anchor;
            return result;
        }
        if (group.kind == "squad")
        {
            if (factionNames.find(group.ownerDomain) == factionNames.end())
            {
                result.reason = "squad references undefined faction: " + group.ownerDomain;
                return result;
            }
        }
        else if (group.kind == "mutant_pack")
        {
            if (group.ownerDomain != "ecology")
            {
                result.reason = "mutant_pack owner domain must be 'ecology': " + group.ownerDomain;
                return result;
            }
        }
    }

    for (const auto& resource : result.manifest.resources)
    {
        if (anchorNames.find(resource.anchor) == anchorNames.end())
        {
            result.reason = "resource references undefined anchor: " + resource.anchor;
            return result;
        }
    }

    for (const auto& route : result.manifest.routes)
    {
        if (anchorNames.find(route.from) == anchorNames.end())
        {
            result.reason = "route references undefined from anchor: " + route.from;
            return result;
        }
        if (anchorNames.find(route.to) == anchorNames.end())
        {
            result.reason = "route references undefined to anchor: " + route.to;
            return result;
        }
        if (route.from == route.to)
        {
            result.reason = "route from and to cannot be identical: " + route.from;
            return result;
        }
    }

    result.ok = true;
    return result;
}
} // namespace xrSim
