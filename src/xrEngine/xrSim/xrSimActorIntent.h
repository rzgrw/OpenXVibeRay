#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ActorAction
{
    std::string verb;
    std::string target;
    std::string arg0;
    std::string arg1;
    int32_t amount = 0;
};

struct ActorIntentPlan
{
    bool coast = false;
    std::string goal;
    std::string stance;
    uint32_t durationMs = 0;
    std::vector<ActorAction> actions;
    std::vector<std::string> memories;
};

struct ActorIntentParseResult
{
    bool ok = false;
    ActorIntentPlan plan;
    std::string reason;
};

ActorIntentParseResult ParseActorIntentPlan(const std::string& text);
std::string FormatActorIntentPlan(const ActorIntentPlan& plan);
} // namespace xrSim
