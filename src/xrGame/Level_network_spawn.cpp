#include "pch_script.h"
#include "xrServer_Objects_ALife_All.h"
#include "Level.h"
#include "game_cl_base.h"
#include "NET_Queue.h"
#include "ai_space.h"
#include "xrAICore/Navigation/game_level_cross_table.h"
#include "xrAICore/Navigation/level_graph.h"
#include "client_spawn_manager.h"
#include "xrEngine/xr_object.h"
#include "xrEngine/IGame_Persistent.h"
#include "xrNetServer/NET_Messages.h"

void CLevel::cl_Process_Spawn(NET_Packet& P)
{
    // Begin analysis
    shared_str s_name;
    P.r_stringZ(s_name);

    // Create DC (xrSE)
    CSE_Abstract* E = F_entity_Create(s_name.c_str());
    R_ASSERT2(E, s_name.c_str());

    E->Spawn_Read(P);
    if (E->s_flags.is(M_SPAWN_UPDATE))
        E->UPDATE_Read(P);

    if (!E->match_configuration())
    {
        F_entity_Destroy(E);
        return;
    }
    //-------------------------------------------------
    //.	Msg ("M_SPAWN - %s[%d][%x] - %d %d", *s_name,  E->ID, E,E->ID_Parent, Device.dwFrame);
    //-------------------------------------------------
    // force object to be local for server client
    if (OnServer())
    {
        E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE);
    };

    /*
    game_spawn_queue.push_back(E);
    if (g_bDebugEvents)		ProcessGameSpawns();
    /*/
    g_sv_Spawn(E);

    F_entity_Destroy(E);
    //*/
};

void CLevel::g_cl_Spawn(LPCSTR name, u8 rp, u16 flags, Fvector pos)
{
    ZoneScoped;

    // Create
    CSE_Abstract* E = F_entity_Create(name);
    VERIFY(E);

    // Fill
    E->s_name = name;
    E->set_name_replace("");
    //.	E->s_gameid			=	u8(GameID());
    E->s_RP = rp;
    E->ID = 0xffff;
    E->ID_Parent = 0xffff;
    E->ID_Phantom = 0xffff;
    E->s_flags.assign(flags);
    E->RespawnTime = 0;
    E->o_Position = pos;

    // Send
    NET_Packet P;
    E->Spawn_Write(P, TRUE);
    Send(P, net_flags(TRUE));

    // Destroy
    F_entity_Destroy(E);
}

bool CLevel::AgentBridgeSpawnObjectNearCurrentEntity(
    pcstr section, u32 ordinal, u32 count, float radius, u16& id, xr_string& reason)
{
    id = u16(-1);

    if (!section || !pSettings || !pSettings->section_exist(section))
    {
        reason = "invalid section: ";
        reason += section ? section : "<null>";
        return false;
    }

    if (!pSettings->line_exist(section, "class"))
    {
        reason = "spawn section has no class: ";
        reason += section;
        return false;
    }

    if (!CurrentEntity())
    {
        reason = "current entity unavailable";
        return false;
    }

    if (!Server)
    {
        reason = "server unavailable";
        return false;
    }

    if (!ai().get_level_graph())
    {
        reason = "level graph unavailable";
        return false;
    }

    const Fvector actorPosition = CurrentEntity()->Position();
    if (!ai().level_graph().valid_vertex_position(actorPosition))
    {
        reason = "current entity is outside the level graph";
        return false;
    }

    const u32 actorVertex = ai().level_graph().vertex_id(actorPosition);
    if (!ai().level_graph().valid_vertex_id(actorVertex))
    {
        reason = "current entity has invalid level vertex";
        return false;
    }

    const float angle = 6.28318530718f * float(ordinal) / float(count ? count : 1);
    Fvector desiredPosition = actorPosition;
    desiredPosition.x += _cos(angle) * radius;
    desiredPosition.z += _sin(angle) * radius;

    const u32 spawnVertex = ai().level_graph().vertex(actorVertex, desiredPosition);

    if (!ai().level_graph().valid_vertex_id(spawnVertex))
    {
        reason = "no valid nearby level vertex";
        return false;
    }

    const Fvector spawnPosition = ai().level_graph().vertex_position(spawnVertex);
    CSE_Abstract* temporary = F_entity_Create(section, true);
    if (!temporary)
    {
        reason = "spawn object class is not registered";
        return false;
    }

    CSE_ALifeDynamicObject* dynamicObject = smart_cast<CSE_ALifeDynamicObject*>(temporary);
    if (dynamicObject && ai().get_level_graph())
    {
        dynamicObject->m_tNodeID = spawnVertex;
        if (ai().get_game_graph() && ai().get_cross_table())
            dynamicObject->m_tGraphID = ai().cross_table().vertex(spawnVertex).game_vertex_id();
    }

    temporary->s_name = section;
    temporary->set_name_replace(section);
    temporary->o_Position = spawnPosition;
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

    u16 message = 0;
    packet.r_begin(message);
    if (message != M_SPAWN)
    {
        reason = "spawn packet was not M_SPAWN";
        return false;
    }

    CSE_Abstract* spawned = Server->Process_spawn(packet, clientID);
    if (!spawned)
    {
        reason = "server spawn failed";
        return false;
    }

    id = spawned->ID;
    return true;
}

#ifdef DEBUG
extern Flags32 psAI_Flags;
extern float debug_on_frame_gather_stats_frequency;
#include "ai_debug.h"
#endif // DEBUG

void CLevel::g_sv_Spawn(CSE_Abstract* E)
{
    ZoneScoped;

//	CTimer		T(false);

#ifdef DEBUG
//	Msg					("* CLIENT: Spawn: %s, ID=%d", *E->s_name, E->ID);
#endif

    // Optimization for single-player only	- minimize traffic between client and server
    if (GameID() == eGameIDSingle)
        psNET_Flags.set(NETFLAG_MINIMIZEUPDATES, TRUE);
    else
        psNET_Flags.set(NETFLAG_MINIMIZEUPDATES, FALSE);

    // Client spawn
    //	T.Start		();
    IGameObject* O = Objects.Create(E->s_name.c_str());
// Msg				("--spawn--CREATE: %f ms",1000.f*T.GetAsync());

//	T.Start		();
    if (0 == O || (!O->net_Spawn(E)))
    {
        O->net_Destroy();
        if (!GEnv.isDedicatedServer)
            client_spawn_manager().clear(O->ID());
        Objects.Destroy(O);
        Msg("! Failed to spawn entity '%s'", E->s_name.c_str());
    }
    else
    {
        if (!GEnv.isDedicatedServer)
            client_spawn_manager().callback(O);
        // Msg			("--spawn--SPAWN: %f ms",1000.f*T.GetAsync());

        if ((E->s_flags.is(M_SPAWN_OBJECT_LOCAL)) && (E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER)))
        {
            if (IsDemoPlayStarted())
            {
                if (E->s_flags.is(M_SPAWN_OBJECT_PHANTOM))
                {
                    SetControlEntity(O);
                    SetEntity(O); // do not switch !!!
                    SetDemoSpectator(O);
                }
            }
            else
            {
                if (CurrentEntity() != NULL)
                {
                    CGameObject* pGO = smart_cast<CGameObject*>(CurrentEntity());
                    if (pGO)
                        pGO->On_B_NotCurrentEntity();
                }
                SetControlEntity(O);
                SetEntity(O); // do not switch !!!
            }
        }

        if (0xffff != E->ID_Parent)
        {
            /*
            // Generate ownership-event
            NET_Packet			GEN;
            GEN.w_begin			(M_EVENT);
            GEN.w_u32			(E->m_dwSpawnTime);//-NET_Latency);
            GEN.w_u16			(GE_OWNERSHIP_TAKE);
            GEN.w_u16			(E->ID_Parent);
            GEN.w_u16			(u16(O->ID()));
            game_events->insert	(GEN);
            /*/
            NET_Packet GEN;
            GEN.write_start();
            GEN.read_start();
            GEN.w_u16(u16(O->ID()));
            cl_Process_Event(E->ID_Parent, GE_OWNERSHIP_TAKE, GEN);
            //*/
        }
    }

    /*if (E->s_flags.is(M_SPAWN_UPDATE)) {
		NET_Packet				temp;
		temp.B.count			= 0;
		E->UPDATE_Write			(temp);
		if (temp.B.count > 0)
		{
			temp.r_seek				(0);
			O->net_Import			(temp);
		}
		}*/ //:(

    //---------------------------------------------------------
    Game().OnSpawn(O);
}

CSE_Abstract* CLevel::spawn_item(
    LPCSTR section, const Fvector& position, u32 level_vertex_id, u16 parent_id, bool return_item)
{
    ZoneScoped;

    CSE_Abstract* abstract = F_entity_Create(section);
    R_ASSERT3(abstract, "Cannot find item with section", section);
    CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(abstract);
    if (dynamic_object && ai().get_level_graph())
    {
        dynamic_object->m_tNodeID = level_vertex_id;
        if (ai().level_graph().valid_vertex_id(level_vertex_id) && ai().get_game_graph() && ai().get_cross_table())
            dynamic_object->m_tGraphID = ai().cross_table().vertex(level_vertex_id).game_vertex_id();
    }

    //оружие спавним с полным магазинои
    CSE_ALifeItemWeapon* weapon = smart_cast<CSE_ALifeItemWeapon*>(abstract);
    if (weapon)
        weapon->a_elapsed = weapon->get_ammo_magsize();

    // Fill
    abstract->s_name = section;
    abstract->set_name_replace(section);
    //.	abstract->s_gameid		= u8(GameID());
    abstract->o_Position = position;
    abstract->s_RP = 0xff;
    abstract->ID = 0xffff;
    abstract->ID_Parent = parent_id;
    abstract->ID_Phantom = 0xffff;
    abstract->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
    abstract->RespawnTime = 0;

    if (!return_item)
    {
        NET_Packet P;
        abstract->Spawn_Write(P, TRUE);
        Send(P, net_flags(TRUE));
        F_entity_Destroy(abstract);
        return (0);
    }
    else
        return (abstract);
}

void CLevel::ProcessGameSpawns()
{
    ZoneScoped;

    while (!game_spawn_queue.empty())
    {
        CSE_Abstract* E = game_spawn_queue.front();

        g_sv_Spawn(E);

        F_entity_Destroy(E);

        game_spawn_queue.pop_front();
    }
}
