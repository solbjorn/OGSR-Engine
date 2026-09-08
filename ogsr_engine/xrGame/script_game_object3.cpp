////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_script3.cpp
//	Created 	: 17.11.2004
//  Modified 	: 17.11.2004
//	Author		: Dmitriy Iassenev
//	Description : Script game object class script export
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "script_game_object.h"

#include "ai_space.h"
#include "script_engine.h"
#include "cover_evaluators.h"
#include "cover_point.h"
#include "cover_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "stalker_animation_manager.h"
#include "stalker_planner.h"
#include "Weapon.h"
#include "Inventory.h"
#include "CustomZone.h"
#include "patrol_path_manager.h"
#include "object_handler_planner.h"
#include "object_handler_space.h"
#include "memory_manager.h"
#include "visual_memory_manager.h"
#include "sound_memory_manager.h"
#include "hit_memory_manager.h"
#include "sight_manager.h"
#include "stalker_movement_manager.h"
#include "movement_manager_space.h"
#include "detail_path_manager_space.h"
#include "level_debug.h"
#include "ai/monsters/basemonster/base_monster.h"
#include "trade_parameters.h"
#include "sound_player.h"
#include "stalker_decision_space.h"
#include "Actor.h"
#include "ActorCondition.h"

#include "CustomOutfit.h"
#include "InventoryBox.h"
#include "trade.h"

namespace MemorySpace
{
struct CVisibleObject;
struct CSoundObject;
struct CHitObject;
} // namespace MemorySpace

const CCoverPoint* CScriptGameObject::best_cover(const Fvector& position, const Fvector& enemy_position, f32 radius, f32 min_enemy_distance,
                                                 f32 max_enemy_distance)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    stalker->m_ce_best->setup(enemy_position, min_enemy_distance, max_enemy_distance, 0.0f);

    return ai().cover_manager().best_cover(position, radius, *stalker->m_ce_best);
}

const CCoverPoint* CScriptGameObject::best_cover(const Fvector& position, const Fvector& enemy_position, f32 radius, f32 min_enemy_distance,
                                                 f32 max_enemy_distance, sol::function callback)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    xr_vector<const CCoverPoint*> covers;

    stalker->m_ce_best->setup(enemy_position, min_enemy_distance, max_enemy_distance, 0.0f, [&](auto point) -> bool {
        covers.push_back(point);
        return true;
    });

    ai().cover_manager().best_cover(position, radius, *stalker->m_ce_best);

    for (auto p : covers | std::views::reverse)
    {
        if (callback(p))
            return p;
    }

    return nullptr;
}

const CCoverPoint* CScriptGameObject::safe_cover(const Fvector& position, f32 radius, f32 min_distance)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    stalker->m_ce_safe->setup(min_distance);

    return ai().cover_manager().best_cover(position, radius, *stalker->m_ce_safe);
}

const CCoverPoint* CScriptGameObject::safe_cover(const Fvector& position, f32 radius, f32 min_distance, sol::function callback)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    xr_vector<const CCoverPoint*> covers;

    stalker->m_ce_safe->setup(min_distance, [&](auto point) -> bool {
        covers.push_back(point);
        return true;
    });

    ai().cover_manager().best_cover(position, radius, *stalker->m_ce_safe);

    for (auto p : covers | std::views::reverse)
    {
        if (callback(p))
            return p;
    }

    return nullptr;
}

const CCoverPoint* CScriptGameObject::ambush_cover(const Fvector& position, const Fvector& enemy_position, f32 radius, f32 min_distance)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    stalker->m_ce_ambush->setup(position, enemy_position, min_distance);

    return ai().cover_manager().best_cover(position, radius, *stalker->m_ce_ambush);
}

const CCoverPoint* CScriptGameObject::ambush_cover(const Fvector& position, const Fvector& enemy_position, f32 radius, f32 min_distance, sol::function callback)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    xr_vector<const CCoverPoint*> covers;

    stalker->m_ce_ambush->setup(position, enemy_position, min_distance, [&](auto point) -> bool {
        covers.push_back(point);
        return true;
    });

    ai().cover_manager().best_cover(position, radius, *stalker->m_ce_ambush);

    for (auto p : covers | std::views::reverse)
    {
        if (callback(p))
            return p;
    }

    return nullptr;
}

const xr_deque<MemorySpace::CVisibleObject>& CScriptGameObject::memory_visible_objects() const
{
    CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
    if (monster == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot access class member memory_visible_objects!");
        R_ASSERT(monster != nullptr);
    }

    return monster->memory().visual().objects();
}

const xr_deque<MemorySpace::CSoundObject>& CScriptGameObject::memory_sound_objects() const
{
    CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
    if (monster == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot access class member memory_sound_objects!");
        R_ASSERT(monster != nullptr);
    }

    return monster->memory().sound().objects();
}

const xr_deque<MemorySpace::CHitObject>& CScriptGameObject::memory_hit_objects() const
{
    CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
    if (monster == nullptr)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot access class member memory_hit_objects!");
        R_ASSERT(monster != nullptr);
    }

    return monster->memory().hit().objects();
}

void CScriptGameObject::ChangeTeam(u8 team, u8 squad, u8 group)
{
    CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
    if (!custom_monster)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CCustomMonster: cannot access class member ChangeTeam!");
    else
        custom_monster->ChangeTeam(team, squad, group);
}

CScriptGameObject* CScriptGameObject::GetEnemy() const
{
    CCustomMonster* l_tpCustomMonster = smart_cast<CCustomMonster*>(&object());
    if (l_tpCustomMonster)
    {
        if (l_tpCustomMonster->GetCurrentEnemy() && !l_tpCustomMonster->GetCurrentEnemy()->getDestroy())
            return (l_tpCustomMonster->GetCurrentEnemy()->lua_game_object());

        return nullptr;
    }
    else
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member GetEnemy!");
        return nullptr;
    }
}

CScriptGameObject* CScriptGameObject::GetCorpse() const
{
    CCustomMonster* l_tpCustomMonster = smart_cast<CCustomMonster*>(&object());
    if (l_tpCustomMonster)
    {
        if (l_tpCustomMonster->GetCurrentCorpse() && !l_tpCustomMonster->GetCurrentCorpse()->getDestroy())
            return (l_tpCustomMonster->GetCurrentCorpse()->lua_game_object());

        return nullptr;
    }
    else
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member GetCorpse!");
        return nullptr;
    }
}

bool CScriptGameObject::CheckTypeVisibility(const char* section_name)
{
    CCustomMonster* l_tpCustomMonster = smart_cast<CCustomMonster*>(&object());
    if (l_tpCustomMonster)
        return (l_tpCustomMonster->CheckTypeVisibility(section_name));
    else
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member CheckTypeVisibility!");
        return (false);
    }
}

CScriptGameObject* CScriptGameObject::GetCurrentWeapon() const
{
    CAI_Stalker* l_tpStalker = smart_cast<CAI_Stalker*>(&object());
    if (!l_tpStalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member GetCurrentWeapon!");
        return nullptr;
    }

    CGameObject* current_weapon = l_tpStalker->GetCurrentWeapon();
    return current_weapon ? current_weapon->lua_game_object() : nullptr;
}

CScriptGameObject* CScriptGameObject::GetCurrentOutfit() const
{
    CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
    if (!inventoryOwner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member GetCurrentOutfit!");
        return nullptr;
    }

    CGameObject* current_equipment = inventoryOwner->GetCurrentOutfit() ? &inventoryOwner->GetCurrentOutfit()->object() : nullptr;
    return current_equipment ? current_equipment->lua_game_object() : nullptr;
}

float CScriptGameObject::GetCurrentOutfitProtection(int hit_type)
{
    CInventoryOwner* inventoryOwner = smart_cast<CInventoryOwner*>(&object());
    if (!inventoryOwner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member GetCurrentOutfitProtection!");
        return (0);
    }
    CGameObject* current_equipment = &inventoryOwner->GetCurrentOutfit()->object();
    CCustomOutfit* o = smart_cast<CCustomOutfit*>(current_equipment);
    if (!o)
        return 0.0f;

    return 1.0f - o->GetDefHitTypeProtection(ALife::EHitType(hit_type));
}

CScriptGameObject* CScriptGameObject::GetFood() const
{
    CAI_Stalker* l_tpStalker = smart_cast<CAI_Stalker*>(&object());
    if (!l_tpStalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member GetFood!");
        return nullptr;
    }

    CGameObject* food = l_tpStalker->GetFood() ? &l_tpStalker->GetFood()->object() : nullptr;
    return food ? food->lua_game_object() : nullptr;
}

CScriptGameObject* CScriptGameObject::GetMedikit() const
{
    CAI_Stalker* l_tpStalker = smart_cast<CAI_Stalker*>(&object());
    if (!l_tpStalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member GetCurrentWeapon!");
        return nullptr;
    }

    CGameObject* medikit = l_tpStalker->GetMedikit() ? &l_tpStalker->GetMedikit()->object() : nullptr;
    return medikit ? medikit->lua_game_object() : nullptr;
}

LPCSTR CScriptGameObject::GetPatrolPathName()
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        CScriptEntity* script_monster = smart_cast<CScriptEntity*>(&object());
        if (!script_monster)
        {
            Msg("!![{}] cannot access class member GetPatrolPathName! Object: [{}]", std::source_location::current().function_name(), this->cName());
            return ("");
        }
        else
        {
            return (script_monster->GetPatrolPathName());
        }
    }
    else
    {
        return stalker->movement().patrol().path_name().c_str();
    }
}

void CScriptGameObject::add_animation(gsl::czstring animation, bool hand_usage, bool use_movement_controller)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot access class member add_animation!");
        return;
    }

    stalker->animation().add_script_animation(animation, hand_usage, use_movement_controller);
}

void CScriptGameObject::clear_animations()
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot access class member clear_animations!");
        return;
    }
    stalker->animation().clear_script_animations();
}

int CScriptGameObject::animation_count() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CGameObject : cannot access class member clear_animations!");
        return (-1);
    }
    return ((int)stalker->animation().script_animations().size());
}

Flags32 CScriptGameObject::get_actor_relation_flags() const { return XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr)->m_actor_relation_flags; }

void CScriptGameObject::set_actor_relation_flags(Flags32 flags)
{
    XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr)->m_actor_relation_flags = flags;
}

void CScriptGameObject::set_patrol_path(LPCSTR path_name, const PatrolPathManager::EPatrolStartType patrol_start_type,
                                        const PatrolPathManager::EPatrolRouteType patrol_route_type, bool random)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().patrol().set_path(shared_str{path_name}, patrol_start_type, patrol_route_type, random);
}

void CScriptGameObject::set_dest_level_vertex_id(u32 level_vertex_id)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member set_dest_level_vertex_id!");
    else
    {
        if (!ai().level_graph().valid_vertex_id(level_vertex_id))
        {
#ifdef DEBUG
            ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : invalid vertex id being setup by action {}!",
                                            stalker->brain().CStalkerPlanner::current_action().m_action_name);
#endif

            return;
        }

        XR_ASSERT(stalker->movement().restrictions().accessible(level_vertex_id), "", stalker->cName(), level_vertex_id);
        stalker->movement().set_level_dest_vertex(level_vertex_id);
    }
}

CHARACTER_RANK_VALUE CScriptGameObject::GetRank()
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member GetRank!");
        return (CHARACTER_RANK_VALUE(0));
    }
    else
    {
        return (stalker->Rank());
    }
}

void CScriptGameObject::set_desired_position()
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_desired_position(nullptr);
}

void CScriptGameObject::set_desired_position(const Fvector* desired_position)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    }
    else
    {
        XR_ASSERT(desired_position == nullptr || stalker->movement().restrictions().accessible(*desired_position), "", stalker->cName());
        stalker->movement().set_desired_position(desired_position);
    }
}

void CScriptGameObject::set_desired_direction()
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_desired_direction(nullptr);
}

void CScriptGameObject::set_desired_direction(const Fvector* desired_direction)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_desired_direction(desired_direction);
}

void CScriptGameObject::set_body_state(MonsterSpace::EBodyState body_state)
{
    XR_ASSERT(body_state == MonsterSpace::eBodyStateStand || body_state == MonsterSpace::eBodyStateCrouch);

    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_body_state(body_state);
}

void CScriptGameObject::set_movement_type(MonsterSpace::EMovementType movement_type)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_movement_type(movement_type);
}

void CScriptGameObject::set_mental_state(MonsterSpace::EMentalState mental_state)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    }
    else
    {
#ifdef DEBUG
        if (mental_state != eMentalStateDanger)
        {
            if (stalker->brain().current_action_id() == StalkerDecisionSpace::eWorldOperatorCombatPlanner)
            {
                ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : set_mental_state is used during universal combat!");
                return;
            }
        }
#endif // DEBUG

        stalker->movement().set_mental_state(mental_state);
    }
}

void CScriptGameObject::set_path_type(MovementManager::EPathType path_type)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_path_type(path_type);
}

void CScriptGameObject::set_detail_path_type(DetailPathManager::EDetailPathType detail_path_type)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement!");
    else
        stalker->movement().set_detail_path_type(detail_path_type);
}

MonsterSpace::EBodyState CScriptGameObject::body_state() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        // KD: для боевки
        CActor* actor = smart_cast<CActor*>(&object());
        if (!actor)
        {
            ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member body_state!");
            return (MonsterSpace::eBodyStateStand);
        }

        return actor->is_crouch() ? MonsterSpace::eBodyStateCrouch : MonsterSpace::eBodyStateStand;
    }

    return (stalker->movement().body_state());
}

MonsterSpace::EBodyState CScriptGameObject::target_body_state() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member body_state!");
        return (MonsterSpace::eBodyStateStand);
    }

    return (stalker->movement().target_body_state());
}

MonsterSpace::EMovementType CScriptGameObject::movement_type() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member movement_type!");
        return (MonsterSpace::eMovementTypeStand);
    }

    return (stalker->movement().movement_type());
}

MonsterSpace::EMovementType CScriptGameObject::target_movement_type() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member target_movement_type!");
        return (MonsterSpace::eMovementTypeStand);
    }

    return (stalker->movement().target_movement_type());
}

MonsterSpace::EMentalState CScriptGameObject::mental_state() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member mental_state!");
        return (MonsterSpace::eMentalStateDanger);
    }

    return (stalker->movement().mental_state());
}

MonsterSpace::EMentalState CScriptGameObject::target_mental_state() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member mental_state!");
        return (MonsterSpace::eMentalStateDanger);
    }

    return (stalker->movement().target_mental_state());
}

MovementManager::EPathType CScriptGameObject::path_type() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member path_type!");
        return (MovementManager::ePathTypeNoPath);
    }

    return (stalker->movement().path_type());
}

DetailPathManager::EDetailPathType CScriptGameObject::detail_path_type() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member detail_path_type!");
        return (DetailPathManager::eDetailPathTypeSmooth);
    }

    return (DetailPathManager::eDetailPathTypeSmooth);
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, const Fvector* vector3d, u32)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(sight_type, vector3d);
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, bool torso_look, bool path)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(sight_type, torso_look, path);
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, const Fvector& vector3d, bool torso_look = false)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(sight_type, vector3d, torso_look);
}

void CScriptGameObject::set_sight(SightManager::ESightType sight_type, const Fvector* vector3d)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(sight_type, vector3d);
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(&object_to_look->object());
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look, bool torso_look)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(&object_to_look->object(), torso_look);
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look, bool torso_look, bool fire_object)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(&object_to_look->object(), torso_look, fire_object);
}

void CScriptGameObject::set_sight(CScriptGameObject* object_to_look, bool torso_look, bool fire_object, bool no_pitch)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(CSightAction(&object_to_look->object(), torso_look, fire_object, no_pitch));
}

void CScriptGameObject::set_sight(const MemorySpace::CMemoryInfo* memory_object, bool torso_look)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CSightManager : cannot access class member set_sight!");
    else
        stalker->sight().setup(memory_object, torso_look);
}

// CAI_Stalker
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

u32 CScriptGameObject::GetInventoryObjectCount() const
{
    CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
    if (l_tpInventoryOwner)
        return (l_tpInventoryOwner->inventory().dwfGetObjectCount());
    else
    {
        // Real Wolf. 03.08.2014.
        if (auto box = smart_cast<IInventoryBox*>(&object()))
            return box->GetSize();

        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member obj_count!");
        return (0);
    }
}

CScriptGameObject* CScriptGameObject::GetActiveItem()
{
    CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
    if (l_tpInventoryOwner)
    {
        if (l_tpInventoryOwner->inventory().ActiveItem())
            return (l_tpInventoryOwner->inventory().ActiveItem()->object().lua_game_object());

        return nullptr;
    }
    else
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member active_item!");
        return nullptr;
    }
}

CScriptGameObject* CScriptGameObject::GetObjectByName(LPCSTR caObjectName) const
{
    CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
    if (l_tpInventoryOwner)
    {
        CInventoryItem* l_tpInventoryItem = l_tpInventoryOwner->inventory().GetItemFromInventory(caObjectName);
        CGameObject* l_tpGameObject = smart_cast<CGameObject*>(l_tpInventoryItem);
        if (!l_tpGameObject)
            return nullptr;

        return l_tpGameObject->lua_game_object();
    }
    else
    {
        // Real Wolf. 03.08.2014.
        if (auto box = smart_cast<IInventoryBox*>(&object()))
            return box->GetObjectByName(caObjectName);

        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member object!");
        return nullptr;
    }
}

CScriptGameObject* CScriptGameObject::GetObjectByIndex(int iIndex) const
{
    CInventoryOwner* l_tpInventoryOwner = smart_cast<CInventoryOwner*>(&object());
    if (l_tpInventoryOwner)
    {
        CInventoryItem* l_tpInventoryItem = l_tpInventoryOwner->inventory().tpfGetObjectByIndex(iIndex);
        CGameObject* l_tpGameObject = smart_cast<CGameObject*>(l_tpInventoryItem);
        if (!l_tpGameObject)
            return nullptr;

        return (l_tpGameObject->lua_game_object());
    }
    else
    {
        // Real Wolf. 03.08.2014.
        if (auto box = smart_cast<IInventoryBox*>(&object()))
            return box->GetObjectByIndex(iIndex);

        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member object!");
        return nullptr;
    }
}

void CScriptGameObject::EnableAnomaly() { XR_ASSERT_VAL(smart_cast<CCustomZone*>(&object()) != nullptr)->ZoneEnable(); }
void CScriptGameObject::DisableAnomaly(bool keep_update) { XR_ASSERT_VAL(smart_cast<CCustomZone*>(&object()) != nullptr)->ZoneDisable(keep_update); }

float CScriptGameObject::GetAnomalyPower() { return XR_ASSERT_VAL(smart_cast<CCustomZone*>(&object()) != nullptr)->GetMaxPower(); }
void CScriptGameObject::SetAnomalyPower(float p) { XR_ASSERT_VAL(smart_cast<CCustomZone*>(&object()) != nullptr)->SetMaxPower(p); }

bool CScriptGameObject::weapon_strapped() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member weapon_strapped!");
        return (false);
    }
    return (stalker->weapon_strapped());
}

bool CScriptGameObject::weapon_unstrapped() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member weapon_unstrapped!");
        return (false);
    }
    return (stalker->weapon_unstrapped());
}

bool CScriptGameObject::path_completed() const
{
    CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
    if (!monster)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member path_completed!");
        return (false);
    }
    return (monster->movement().path_completed());
}

void CScriptGameObject::patrol_path_make_inactual()
{
    CCustomMonster* monster = smart_cast<CCustomMonster*>(&object());
    if (!monster)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member patrol_path_make_inactual!");
        return;
    }
    monster->movement().patrol().make_inactual();
}

Fvector CScriptGameObject::head_orientation() const
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot access class member head_orientation!");
        return (Fvector().set(flt_max, flt_max, flt_max));
    }

    const SRotation& r = stalker->movement().head_orientation().current;
    return (Fvector().setHP(-r.yaw, -r.pitch));
}

void CScriptGameObject::info_add([[maybe_unused]] LPCSTR text)
{
#ifdef DEBUG
    DBG().object_info(&object(), this).add_item(text, D3DCOLOR_XRGB(255, 0, 0), 0);
#endif
}

void CScriptGameObject::info_clear()
{
#ifdef DEBUG
    DBG().object_info(&object(), this).clear();
#endif
}

void CScriptGameObject::jump(const Fvector& position, float factor)
{
    CBaseMonster* monster = smart_cast<CBaseMonster*>(&object());
    if (!monster)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject : cannot process jump for not a monster!");
        return;
    }

    monster->jump(position, factor);
}

void CScriptGameObject::make_object_visible_somewhen(CScriptGameObject* object)
{
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&this->object());
    if (!stalker)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CAI_Stalker : cannot access class member make_object_visible_somewhen!");
        return;
    }

    CEntityAlive* entity_alive = smart_cast<CEntityAlive*>(&(object->object()));
    if (!entity_alive)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CEntityAlive : cannot access class member make_object_visible_somewhen!");
        return;
    }

    stalker->memory().make_object_visible_somewhen(entity_alive);
}

void CScriptGameObject::sell_condition(CInifile* ini_file, gsl::czstring section)
{
    CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member sell_condition!");
        return;
    }

    inventory_owner->trade_parameters().process(CTradeParameters::action_sell(nullptr), *ini_file, shared_str{section});
}

void CScriptGameObject::sell_condition(f32 friend_factor, f32 enemy_factor)
{
    CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member sell_condition!");
        return;
    }

    inventory_owner->trade_parameters().default_factors(CTradeParameters::action_sell(nullptr), CTradeFactors(friend_factor, enemy_factor));
}

void CScriptGameObject::buy_condition(CInifile* ini_file, gsl::czstring section)
{
    CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member buy_condition!");
        return;
    }

    inventory_owner->trade_parameters().process(CTradeParameters::action_buy(nullptr), *ini_file, shared_str{section});
}

void CScriptGameObject::buy_condition(f32 friend_factor, f32 enemy_factor)
{
    CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member buy_condition!");
        return;
    }

    inventory_owner->trade_parameters().default_factors(CTradeParameters::action_buy(nullptr), CTradeFactors(friend_factor, enemy_factor));
}

void CScriptGameObject::show_condition(CInifile* ini_file, gsl::czstring section)
{
    CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member show_condition!");
        return;
    }

    inventory_owner->trade_parameters().process(CTradeParameters::action_show(nullptr), *ini_file, shared_str{section});
}

void CScriptGameObject::buy_supplies(CInifile* ini_file, gsl::czstring section)
{
    CInventoryOwner* inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CInventoryOwner : cannot access class member buy_condition!");
        return;
    }

    inventory_owner->buy_supplies(*ini_file, section);
}

void sell_condition(CInifile* ini_file, gsl::czstring section)
{
    default_trade_parameters().process(CTradeParameters::action_sell(nullptr), *ini_file, shared_str{section});
}

void sell_condition(f32 friend_factor, f32 enemy_factor)
{
    default_trade_parameters().default_factors(CTradeParameters::action_sell(nullptr), CTradeFactors(friend_factor, enemy_factor));
}

void buy_condition(CInifile* ini_file, gsl::czstring section)
{
    default_trade_parameters().process(CTradeParameters::action_buy(nullptr), *ini_file, shared_str{section});
}

void buy_condition(f32 friend_factor, f32 enemy_factor)
{
    default_trade_parameters().default_factors(CTradeParameters::action_buy(nullptr), CTradeFactors(friend_factor, enemy_factor));
}

void show_condition(CInifile* ini_file, gsl::czstring section)
{
    default_trade_parameters().process(CTradeParameters::action_show(nullptr), *ini_file, shared_str{section});
}

LPCSTR CScriptGameObject::sound_prefix() const
{
    CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
    if (!custom_monster)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CCustomMonster : cannot access class member sound_prefix!");
        return nullptr;
    }

    return custom_monster->sound().sound_prefix().c_str();
}

void CScriptGameObject::sound_prefix(LPCSTR sound_prefix)
{
    CCustomMonster* custom_monster = smart_cast<CCustomMonster*>(&object());
    if (!custom_monster)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CCustomMonster : cannot access class member sound_prefix!");
        return;
    }

    custom_monster->sound().sound_prefix(shared_str{sound_prefix});
}

void CScriptGameObject::SetMaxWeight(float _weight)
{
    CActor* e = smart_cast<CActor*>(&object());
    if (!e)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CActor : cannot access class member SetMaxWeight!");
        return;
    }
    e->inventory().SetMaxWeight(_weight);
}

void CScriptGameObject::SetMaxWalkWeight(float _weight)
{
    CActor* e = smart_cast<CActor*>(&object());
    if (!e)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CActor : cannot access class member SetMaxWalkWeight!");
        return;
    }
    e->conditions().SetMaxWalkWeight(_weight);
}

float CScriptGameObject::GetMaxWeight() const
{
    auto e = smart_cast<CInventoryOwner*>(&object());
    if (!e)
    {
        XR_LOG_ERROR("Not an inventory owner object");
        return 0;
    }

    return e->MaxCarryWeight();
}

float CScriptGameObject::GetMaxWalkWeight() const
{
    auto e = smart_cast<CActor*>(&object());
    if (!e)
    {
        XR_LOG_ERROR("Not an actor object");
        return 0;
    }

    float max_w = e->conditions().m_MaxWalkWeight + e->ArtefactsAddWeight();
    auto outfit = e->GetOutfit();
    if (outfit)
        max_w += outfit->m_additional_weight;

    return max_w;
}

float CScriptGameObject::GetInventoryWeight() const
{
    auto e = smart_cast<CInventoryOwner*>(&object());
    if (!e)
    {
        XR_LOG_ERROR("Not an inventory owner object");
        return 0;
    }

    return e->GetCarryWeight();
}

u32 CScriptGameObject::CalcItemPrice(CScriptGameObject* item, bool b_buying) const
{
    auto inventory_owner = smart_cast<CInventoryOwner*>(&object());
    if (!inventory_owner)
    {
        XR_LOG_ERROR("Not an inventory owner object");
        return 0;
    }

    auto inventory_item = smart_cast<CInventoryItem*>(&item->object());
    if (!inventory_item)
    {
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CScriptGameObject::CalcItemPrice non-CInventoryItem object !!!");
        return 0;
    }

    bool set_partner = false;
    if (inventory_owner->GetTrade()->pPartner.base != Actor())
    {
        inventory_owner->GetTrade()->SetPartner(Actor());
        set_partner = true;
    }
    const u32 item_price = inventory_owner->GetTrade()->GetItemPrice(inventory_item, b_buying);
    if (set_partner)
    {
        inventory_owner->GetTrade()->RemovePartner();
    }
    return item_price;
}

float CScriptGameObject::GetShapeRadius() const
{
    if (!g_pGameLevel)
    {
        XR_LOG_ERROR("Game level doesn't exist");
        return 0.0;
    }

    auto obj = smart_cast<CSpaceRestrictor*>(&object());
    if (!obj)
    {
        XR_LOG_ERROR("Not a space restrictor object");
        return 0.0;
    }

    return obj->Radius();
}

u16 CScriptGameObject::GetAmmoBoxCurr() const
{
    if (!g_pGameLevel)
    {
        XR_LOG_ERROR("Game level doesn't exist");
        return 0;
    }

    auto obj = smart_cast<CWeaponAmmo*>(&object());
    if (!obj)
    {
        XR_LOG_ERROR("Not a weapon ammo object");
        return 0;
    }

    return obj->m_boxCurr;
}

u16 CScriptGameObject::GetAmmoBoxSize() const
{
    if (!g_pGameLevel)
    {
        XR_LOG_ERROR("Game level doesn't exist");
        return 0;
    }

    auto obj = smart_cast<CWeaponAmmo*>(&object());
    if (!obj)
    {
        XR_LOG_ERROR("Not a weapon ammo object");
        return 0;
    }

    return obj->m_boxSize;
}

void CScriptGameObject::SetAmmoBoxSize(u16 size)
{
    if (!g_pGameLevel)
    {
        XR_LOG_ERROR("Game level doesn't exist");
        return;
    }

    auto obj = smart_cast<CWeaponAmmo*>(&object());
    if (!obj)
    {
        XR_LOG_ERROR("Not a weapon ammo object");
        return;
    }

    obj->m_boxSize = size;
}

void CScriptGameObject::SetAmmoBoxCurr(u16 curr)
{
    if (!g_pGameLevel)
    {
        XR_LOG_ERROR("Game level doesn't exist");
        return;
    }

    auto obj = smart_cast<CWeaponAmmo*>(&object());
    if (!obj)
    {
        XR_LOG_ERROR("Not a weapon ammo object");
        return;
    }

    obj->m_boxCurr = curr;
}

const char* CScriptGameObject::GetVisualName() const
{
    if (!g_pGameLevel)
    {
        XR_LOG_ERROR("Game level doesn't exist");
        return "";
    }

    return object().cNameVisual().c_str();
}

const CCoverPoint* CScriptGameObject::angle_cover(const Fvector& position, f32 radius, const Fvector& enemy_position, f32 min_enemy_distance,
                                                  f32 max_enemy_distance, u32 enemy_vertex_id)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    stalker->m_ce_angle->setup(enemy_position, min_enemy_distance, max_enemy_distance, enemy_vertex_id);

    return ai().cover_manager().best_cover(position, radius, *stalker->m_ce_angle);
}

const CCoverPoint* CScriptGameObject::angle_cover(const Fvector& position, f32 radius, const Fvector& enemy_position, f32 min_enemy_distance,
                                                  f32 max_enemy_distance, u32 enemy_vertex_id, sol::function callback)
{
    auto stalker = XR_ASSERT_VAL(smart_cast<CAI_Stalker*>(&object()) != nullptr, "not a stalker", object().cName());
    xr_vector<const CCoverPoint*> covers;

    stalker->m_ce_angle->setup(enemy_position, min_enemy_distance, max_enemy_distance, enemy_vertex_id, [&](auto point) -> bool {
        covers.push_back(point);
        return true;
    });

    ai().cover_manager().best_cover(position, radius, *stalker->m_ce_angle);

    for (auto p : covers | std::views::reverse)
    {
        if (callback(p))
            return p;
    }

    return nullptr;
}
