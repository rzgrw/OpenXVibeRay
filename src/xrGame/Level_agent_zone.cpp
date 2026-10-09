#include "StdAfx.h"
#include "Level.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "ai_space.h"
#include "xrAICore/Navigation/game_graph.h"
#include "xrAICore/Navigation/level_graph.h"
#include "xrAICore/Navigation/game_level_cross_table.h"
#include "xrScriptEngine/script_engine.hpp"
#include "script_entity.h"
#include "script_entity_action.h"
#include "script_movement_action.h"
#include "Actor.h"
#include "CameraFirstEye.h"
#include "script_game_object.h"
#include "ai/monsters/basemonster/base_monster.h"
#include "CharacterPhysicsSupport.h"

xrSim::ReadPositionResult CLevel::CapturePlayerOrigin()
{
    if (!CurrentEntity())
        return { false, "current_entity_unavailable", {} };

    const Fvector pos = CurrentEntity()->Position();
    xrSim::FixedPosition fixed;
    fixed.xCm = static_cast<int32_t>(pos.x * 100.f);
    fixed.yCm = static_cast<int32_t>(pos.y * 100.f);
    fixed.zCm = static_cast<int32_t>(pos.z * 100.f);

    if (ai().get_level_graph() && ai().level_graph().valid_vertex_position(pos))
        fixed.levelVertex = ai().level_graph().vertex_id(pos);
    else
        fixed.levelVertex = 0xffffffffu;

    return { true, "", fixed };
}

xrSim::LiveAdapterResult CLevel::ValidateSection(const std::string& section)
{
    if (!pSettings || !pSettings->section_exist(section.c_str()) ||
        !pSettings->line_exist(section.c_str(), "class"))
    {
        return { false, "invalid_section: " + section };
    }
    return { true, "" };
}

xrSim::SpawnGroupResult CLevel::SpawnGroup(const xrSim::LiveSpawnRequest& request)
{
    xrSim::SpawnGroupResult result;
    if (!Server || !ai().get_level_graph())
    {
        result.reason = "server_or_level_graph_unavailable";
        return result;
    }

    const Fvector anchorPos{
        request.position.xCm / 100.f,
        request.position.yCm / 100.f,
        request.position.zCm / 100.f
    };

    u32 baseVertex = 0xffffffffu;
    if (ai().level_graph().valid_vertex_position(anchorPos))
        baseVertex = ai().level_graph().vertex_id(anchorPos);

    if (!ai().level_graph().valid_vertex_id(baseVertex))
    {
        if (CurrentEntity() && ai().level_graph().valid_vertex_position(CurrentEntity()->Position()))
            baseVertex = ai().level_graph().vertex_id(CurrentEntity()->Position());
    }

    if (!ai().level_graph().valid_vertex_id(baseVertex))
    {
        result.reason = "no_valid_level_vertex_for_spawn";
        return result;
    }

    result.objectIds.reserve(request.count);
    const float radius = 5.0f;

    for (u32 i = 0; i < request.count; ++i)
    {
        const float angle = 6.28318530718f * float(i) / float(request.count ? request.count : 1);
        Fvector desiredPos = anchorPos;
        desiredPos.x += _cos(angle) * radius;
        desiredPos.z += _sin(angle) * radius;

        const u32 spawnVertex = ai().level_graph().vertex(baseVertex, desiredPos);
        const Fvector spawnPos = ai().level_graph().valid_vertex_id(spawnVertex)
            ? ai().level_graph().vertex_position(spawnVertex)
            : anchorPos;

        CSE_Abstract* temporary = F_entity_Create(request.section.c_str(), true);
        if (!temporary)
        {
            result.reason = "spawn_entity_create_failed";
            return result;
        }

        CSE_ALifeDynamicObject* dynObj = smart_cast<CSE_ALifeDynamicObject*>(temporary);
        if (dynObj && ai().get_level_graph())
        {
            dynObj->m_tNodeID = spawnVertex;
            if (ai().get_game_graph() && ai().get_cross_table())
                dynObj->m_tGraphID = ai().cross_table().vertex(spawnVertex).game_vertex_id();
        }

        temporary->s_name = request.section.c_str();
        temporary->set_name_replace(request.section.c_str());
        temporary->o_Position = spawnPos;
        temporary->s_RP = 0xff;
        temporary->ID = 0xffff;
        temporary->ID_Parent = 0xffff;
        temporary->ID_Phantom = 0xffff;
        temporary->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
        temporary->RespawnTime = 0;

        NET_Packet packet;
        temporary->Spawn_Write(packet, TRUE);
        F_entity_Destroy(temporary);

        ClientID clientID;
        clientID.set(0xffff);

        u16 msg = 0;
        packet.r_begin(msg);
        CSE_Abstract* spawned = Server->Process_spawn(packet, clientID);
        if (!spawned)
        {
            result.reason = "process_spawn_failed";
            return result;
        }

        const u16 spawnedId = spawned->ID;
        result.objectIds.push_back(spawnedId);

        if (strstr(request.section.c_str(), "stalker") || strstr(request.section.c_str(), "duty") ||
            strstr(request.section.c_str(), "bandit") || strstr(request.section.c_str(), "monolith") ||
            strstr(request.section.c_str(), "soldier") || strstr(request.section.c_str(), "freedom"))
        {
            pcstr weaponSect = "wpn_abakan";
            pcstr ammoSect = "ammo_5.45x39_fmj";
            if (strstr(request.section.c_str(), "duty"))
            {
                weaponSect = "wpn_groza";
                ammoSect = "ammo_9x39_pab9";
            }
            else if (strstr(request.section.c_str(), "bandit"))
            {
                weaponSect = "wpn_ak74u";
                ammoSect = "ammo_5.45x39_fmj";
            }
            else if (strstr(request.section.c_str(), "monolith"))
            {
                weaponSect = "wpn_val";
                ammoSect = "ammo_9x39_pab9";
            }

            auto spawnChild = [&](pcstr itemSect) {
                CSE_Abstract* item = F_entity_Create(itemSect, true);
                if (item)
                {
                    item->s_name = itemSect;
                    item->set_name_replace(itemSect);
                    item->ID = 0xffff;
                    item->ID_Parent = spawnedId;
                    item->ID_Phantom = 0xffff;
                    item->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
                    item->RespawnTime = 0;
                    NET_Packet itemP;
                    item->Spawn_Write(itemP, TRUE);
                    F_entity_Destroy(item);
                    u16 m = 0;
                    itemP.r_begin(m);
                    Server->Process_spawn(itemP, clientID);
                }
            };

            spawnChild(weaponSect);
            spawnChild(ammoSect);
            spawnChild(ammoSect);
            spawnChild(ammoSect);
        }
    }

    result.ok = true;
    return result;
}

xrSim::LiveAdapterResult CLevel::DestroyObjects(const std::vector<uint16_t>& objectIds)
{
    if (!Server)
        return { false, "server_unavailable" };

    for (const uint16_t id : objectIds)
    {
        CSE_Abstract* obj = Server->ID_to_entity(id);
        if (obj)
            Server->entity_Destroy(obj);
    }
    return { true, "" };
}

xrSim::LiveAdapterResult CLevel::ApplyObjective(
    const xrSim::LiveBinding& binding, const xrSim::LiveObjectiveCommand& command)
{
    Fvector target;
    target.set(command.target.xCm / 100.f, command.target.yCm / 100.f, command.target.zCm / 100.f);

    for (const uint16_t id : binding.objectIds)
    {
        IGameObject* netObj = Objects.net_Find(id);
        if (!netObj)
            continue;

        CGameObject* gameObj = smart_cast<CGameObject*>(netObj);
        CScriptEntity* scriptEntity = smart_cast<CScriptEntity*>(netObj);
        if (!scriptEntity)
            continue;

        CBaseMonster* monster = smart_cast<CBaseMonster*>(netObj);
        if (monster)
            monster->set_custom_panic_threshold(0.0f);

        if (scriptEntity->GetCurrentEnemy())
        {
            if (scriptEntity->GetScriptControl() &&
                xr_strcmp(scriptEntity->GetScriptControlName(), "xrSim") == 0)
            {
                scriptEntity->ClearActionQueue();
                scriptEntity->SetScriptControl(false, shared_str("xrSim"));
            }
            continue;
        }

        scriptEntity->SetScriptControl(true, shared_str("xrSim"));
        scriptEntity->ClearActionQueue();
        CScriptEntityAction action;

        if (smart_cast<CBaseMonster*>(gameObj))
        {
            CScriptMovementAction movement(MonsterSpace::eMA_WalkFwd, &target, 2.f);
            action.SetAction(movement);
        }
        else
        {
            CScriptMovementAction movement(MonsterSpace::eBodyStateStand,
                MonsterSpace::eMovementTypeWalk, DetailPathManager::eDetailPathTypeSmooth,
                &target, 1.f);
            action.SetAction(movement);
        }
        scriptEntity->AddAction(&action, true);
    }

    return { true, "" };
}

xrSim::ObserveGroupResult CLevel::ObserveGroup(const xrSim::LiveBinding& binding)
{
    xrSim::ObserveGroupResult result;
    result.ok = true;

    uint32_t aliveCount = 0;
    bool inCombat = false;
    Fvector centroid{};
    float totalHealth = 0.f;

    for (const uint16_t id : binding.objectIds)
    {
        IGameObject* netObj = Objects.net_Find(id);
        if (!netObj)
            continue;

        CEntityAlive* aliveObj = smart_cast<CEntityAlive*>(netObj);
        if (aliveObj && aliveObj->g_Alive())
        {
            ++aliveCount;
            centroid.add(aliveObj->Position());
            totalHealth += aliveObj->conditions().GetHealth();

            CScriptEntity* se = smart_cast<CScriptEntity*>(aliveObj);
            if (se && se->GetCurrentEnemy())
                inCombat = true;
        }
    }

    result.aliveCount = aliveCount;
    result.inCombat = inCombat;
    if (aliveCount > 0)
    {
        centroid.div(float(aliveCount));
        result.centroid.xCm = static_cast<int32_t>(centroid.x * 100.f);
        result.centroid.yCm = static_cast<int32_t>(centroid.y * 100.f);
        result.centroid.zCm = static_cast<int32_t>(centroid.z * 100.f);
        result.healthPermille = static_cast<uint32_t>(clampr((totalHealth / float(aliveCount)) * 1000.f, 0.f, 1000.f));
    }

    return result;
}

xrSim::LiveAdapterResult CLevel::ReleaseObjective(const xrSim::LiveBinding& binding)
{
    for (const uint16_t id : binding.objectIds)
    {
        IGameObject* netObj = Objects.net_Find(id);
        if (!netObj)
            continue;

        CScriptEntity* scriptEntity = smart_cast<CScriptEntity*>(netObj);
        if (scriptEntity && scriptEntity->GetScriptControl() &&
            xr_strcmp(scriptEntity->GetScriptControlName(), "xrSim") == 0)
        {
            scriptEntity->ClearActionQueue();
            scriptEntity->SetScriptControl(false, shared_str("xrSim"));
        }
    }
    return { true, "" };
}

xrSim::LiveAdapterResult CLevel::SetPlayerAt(const xrSim::FixedPosition& position)
{
    CActor* actor = smart_cast<CActor*>(CurrentEntity());
    if (!actor)
        return { false, "actor_unavailable" };

    Fmatrix F = actor->XFORM();
    F.c.set(position.xCm / 100.f, position.yCm / 100.f, position.zCm / 100.f);

    if (ai().get_level_graph() && ai().level_graph().valid_vertex_position(F.c))
    {
        const u32 vertex = ai().level_graph().vertex_id(F.c);
        if (ai().level_graph().valid_vertex_id(vertex))
            F.c = ai().level_graph().vertex_position(vertex);
    }

    actor->ForceTransform(F);
    return { true, "" };
}

xrSim::LiveAdapterResult CLevel::FacePlayerAt(const xrSim::LiveBinding& binding)
{
    CActor* actor = smart_cast<CActor*>(CurrentEntity());
    if (!actor)
        return { false, "actor_unavailable" };

    for (const uint16_t id : binding.objectIds)
    {
        IGameObject* netObj = Objects.net_Find(id);
        if (!netObj)
            continue;

        CEntityAlive* alive = smart_cast<CEntityAlive*>(netObj);
        if (alive && alive->g_Alive())
        {
            const Fvector dir = Fvector().sub(alive->Position(), actor->Position());
            const float yaw = atan2f(-dir.x, dir.z);
            if (actor->cam_Active())
                actor->cam_Active()->Set(yaw, 0, 0);
            return { true, "" };
        }
    }
    return { false, "no_living_target_to_face" };
}

xrSim::LiveAdapterResult CLevel::DamageGroupFromPlayer(
    const xrSim::LiveBinding& binding, uint32_t damagePermille)
{
    CActor* actor = smart_cast<CActor*>(CurrentEntity());
    if (!actor)
        return { false, "actor_unavailable" };

    for (const uint16_t id : binding.objectIds)
    {
        IGameObject* netObj = Objects.net_Find(id);
        if (!netObj)
            continue;

        CEntityAlive* alive = smart_cast<CEntityAlive*>(netObj);
        if (alive && alive->g_Alive())
        {
            const float maxHealth = alive->conditions().GetMaxHealth();
            const float damageFraction = clampr(float(damagePermille) / 1000.f, 0.001f, 0.1f);
            const float hitPower = maxHealth * damageFraction;

            SHit HDS;
            HDS.power = hitPower;
            HDS.dir.set(0, 0, 1);
            HDS.who = actor;
            HDS.boneID = 0;
            HDS.p_in_bone_space.set(0, 0, 0);
            HDS.impulse = 5.f;
            HDS.hit_type = ALife::eHitTypeWound;
            alive->Hit(&HDS);
            return { true, "" };
        }
    }
    return { false, "no_living_target_to_damage" };
}
