////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_simulator_script.cpp
//	Created 	: 25.12.2002
//  Modified 	: 13.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator script export
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "alife_simulator.h"

#include "GameObject.h"
#include "ai_space.h"
#include "alife_object_registry.h"
#include "alife_story_registry.h"
#include "script_engine.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "restriction_space.h"
#include "alife_graph_registry.h"
#include "alife_spawn_registry.h"
#include "alife_registry_container.h"
#include "xrServer.h"
#include "Level.h"

namespace detail
{
bool object_exists_in_alife_registry(u32 id)
{
    if (ai().get_alife())
    {
        return ai().alife().objects().object((ALife::_OBJECT_ID)id, true) != nullptr;
    }
    return false;
}
} // namespace detail

STORY_PAIRS story_ids;
SPAWN_STORY_PAIRS spawn_story_ids;

static CALifeSimulator* alife() { return (const_cast<CALifeSimulator*>(ai().get_alife())); }

static CSE_ALifeDynamicObject* alife_object(const CALifeSimulator* self, ALife::_OBJECT_ID object_id)
{
    VERIFY(self);
    return (self->objects().object(object_id, true));
}

static bool valid_object_id(const CALifeSimulator* self, ALife::_OBJECT_ID object_id)
{
    VERIFY(self);
    return (object_id != 0xffff);
}

static CSE_ALifeDynamicObject* alife_object(const CALifeSimulator* self, LPCSTR name)
{
    VERIFY(self);
    return self->objects().object_by_name(name);
}

static CSE_ALifeDynamicObject* alife_object(const CALifeSimulator* self, ALife::_OBJECT_ID id, bool no_assert)
{
    VERIFY(self);
    return (self->objects().object(id, no_assert));
}

static const CALifeObjectRegistry::OBJECT_REGISTRY& alife_objects(const CALifeSimulator* self)
{
    VERIFY(self);
    return self->objects().objects();
}

static CSE_ALifeDynamicObject* alife_story_object(const CALifeSimulator* self, ALife::_STORY_ID id) { return (self->story_objects().object(id, true)); }

template <typename _id_type>
static void generate_story_ids(STORY_PAIRS& result, _id_type INVALID_ID, LPCSTR section_name, LPCSTR INVALID_ID_STRING, LPCSTR invalid_id_description,
                               LPCSTR invalid_id_redefinition, LPCSTR duplicated_id_description)
{
    result.clear();

    CInifile* Ini = pGameIni;

    LPCSTR N, V;
    u32 k;
    shared_str temp;
    LPCSTR section = section_name;
    R_ASSERT(Ini->section_exist(section));

    for (k = 0; Ini->r_line(section, k, &N, &V); ++k)
    {
        temp = Ini->r_string_wb(section, N);

        R_ASSERT3(!std::string_view{temp}.contains(' '), invalid_id_description, temp.c_str());
        R_ASSERT2(std::is_neq(xr_strcmp(temp, INVALID_ID_STRING)), invalid_id_redefinition);

        const auto res = scn::scan_int<u32>(N);
        R_ASSERT(res, res.error().msg());

        XR_ASSERT(result.try_emplace(temp, res->value()).second, duplicated_id_description, section, N, temp);
    }

    result.try_emplace(shared_str{INVALID_ID_STRING}, INVALID_ID);
}

static void add_in_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->add_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeIn);
}

static void add_out_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->add_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeOut);
}

static void remove_in_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->remove_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeIn);
}

static void remove_in_restrictions(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster)
{
    alife->remove_all_restrictions(monster->ID, RestrictionSpace::eRestrictorTypeIn);
}

static void remove_out_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->remove_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeOut);
}

static void remove_out_restrictions(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster)
{
    alife->remove_all_restrictions(monster->ID, RestrictionSpace::eRestrictorTypeOut);
}

static CSE_ALifeDynamicObject* CALifeSimulator__create(CALifeSimulator* self, ALife::_SPAWN_ID spawn_id)
{
    const CALifeSpawnRegistry::SPAWN_GRAPH::CVertex* vertex =
        XR_ASSERT_VAL(ai().alife().spawns().spawns().vertex(spawn_id) != nullptr, "invalid spawn ID", spawn_id);
    auto spawn = XR_ASSERT_VAL(smart_cast<CSE_ALifeDynamicObject*>(&vertex->data()->object()) != nullptr, "", spawn_id);

    CSE_ALifeDynamicObject* object;
    self->create(object, spawn, spawn_id);

    return object;
}

static CSE_Abstract* CALifeSimulator__spawn_item(CALifeSimulator* self, LPCSTR section, const Fvector& position, u32 level_vertex_id,
                                                 GameGraph::_GRAPH_ID game_vertex_id)
{
    return XR_ASSERT_VAL(self != nullptr)->spawn_item(section, position, level_vertex_id, game_vertex_id, ALife::_OBJECT_ID(-1));
}

static CSE_Abstract* CALifeSimulator__spawn_item2(CALifeSimulator* self, LPCSTR section, const Fvector& position, u32 level_vertex_id,
                                                  GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent)
{
    if (id_parent == ALife::_OBJECT_ID(-1))
        return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent));

    CSE_ALifeDynamicObject* object = ai().alife().objects().object(id_parent, true);
    if (!object)
    {
        Msg("! invalid parent id [{}] specified", id_parent);
        return nullptr;
    }

    if (!object->m_bOnline)
        return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent));

    NET_Packet packet;
    packet.w_begin(gsl::narrow<u16>(xr::msg::M_SPAWN));
    packet.w_stringZ(section);

    CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent, false);
    item->Spawn_Write(packet, FALSE);
    self->server().FreeID(item->ID, 0);
    F_entity_Destroy(item);

    ClientID clientID;
    clientID.set(0xffff);

    u16 dummy;
    std::ignore = packet.r_begin(dummy);
    XR_ASSERT(xr::msg{dummy} == xr::msg::M_SPAWN);

    return self->server().Process_spawn(packet, clientID);
}

static CSE_Abstract* CALifeSimulator__spawn_ammo(CALifeSimulator* self, LPCSTR section, const Fvector& position, u32 level_vertex_id,
                                                 GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent, int ammo_to_spawn)
{
    CSE_ALifeDynamicObject* object{};

    if (id_parent != ALife::_OBJECT_ID(-1))
    {
        object = ai().alife().objects().object(id_parent, true);
        if (!object)
        {
            Msg("! invalid parent id [{}] specified", id_parent);
            return nullptr;
        }
    }

    if (!object || !object->m_bOnline)
    {
        CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent);
        CSE_ALifeItemAmmo* ammo = XR_ASSERT_VAL(smart_cast<CSE_ALifeItemAmmo*>(item) != nullptr);

        ammo->a_elapsed = XR_ASSERT_VAL(ammo_to_spawn <= ammo->m_boxSize);

        return item;
    }

    NET_Packet packet;
    packet.w_begin(gsl::narrow<u16>(xr::msg::M_SPAWN));
    packet.w_stringZ(section);

    CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent, false);
    CSE_ALifeItemAmmo* ammo = XR_ASSERT_VAL(smart_cast<CSE_ALifeItemAmmo*>(item) != nullptr);

    ammo->a_elapsed = XR_ASSERT_VAL(ammo_to_spawn <= ammo->m_boxSize);

    item->Spawn_Write(packet, FALSE);
    self->server().FreeID(item->ID, 0);
    F_entity_Destroy(item);

    ClientID clientID;
    clientID.set(0xffff);

    u16 dummy;
    std::ignore = packet.r_begin(dummy);
    XR_ASSERT(xr::msg{dummy} == xr::msg::M_SPAWN);

    return self->server().Process_spawn(packet, clientID);
}

namespace
{
void CALifeSimulator__release(CALifeSimulator* self, CSE_Abstract* object, bool)
{
    XR_ASSERT(self != nullptr);

    auto alife_object = XR_ASSERT_VAL(smart_cast<CSE_ALifeObject*>(object) != nullptr);
    if (!alife_object->m_bOnline)
    {
        self->release(object, true);
        return;
    }

    if (auto pGameObject = smart_cast<CGameObject*>(Level().Objects.net_Find(alife_object->ID)); pGameObject != nullptr)
    {
        pGameObject->DestroyObject();
        return;
    }

    // awful hack, for stohe only
    NET_Packet packet;
    packet.w_begin(gsl::narrow<u16>(xr::msg::M_EVENT));
    packet.w_u32(Level().timeServer());
    packet.w_u16(GE_DESTROY);
    packet.w_u16(object->ID);
    Level().Send(packet, net_flags(TRUE, TRUE));
}

inline void CALifeSimulator__release(CALifeSimulator* self, CSE_Abstract* object) { CALifeSimulator__release(self, object, false); }
} // namespace

static void CALifeSimulator__assign_story_id(CALifeSimulator* self, ALife::_OBJECT_ID _id, ALife::_STORY_ID _story_id)
{
    auto obj = XR_ASSERT_VAL(ai().alife().objects().object(_id) != nullptr, "", _id);

#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
        Msg("[LSS] Assigning story_id for object [{}][{}][{}][{:#x}]", obj->name_replace(), obj->s_name, obj->ID, obj);
#endif

    if (!(ai().alife().story_objects().object(_story_id, true)))
    {
        obj->m_story_id = _story_id;
        self->add_story_object(obj, _story_id);
    }
    else
    {
        XR_LOG_ERROR("Specified story ID {} is already used", _story_id);
    }
}

static void CALifeSimulator__use_ai_locations(CALifeSimulator*, CSE_Abstract* object, bool _use)
{
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
        Msg("[LSS] Assigning use of ai locations for object [{}][{}][{}][{:#x}]", object->name_replace(), object->s_name, object->ID,
            smart_cast<void*>(object));
#endif

    CSE_ALifeDynamicObject* obj = ai().alife().objects().object(object->ID);
    R_ASSERT(obj);

    obj->used_ai_locations(_use);
}

static void CALifeSimulator__teleport_object(CALifeSimulator* self, u16 ID, Fvector _pos, u32 _lvid, GameGraph::_GRAPH_ID _gvid)
{
    self->teleport_object(ID, _gvid, _lvid, _pos);
}

namespace
{
void wrong_CALifeSimulator__teleport_object(CALifeSimulator*, gsl::czstring name, Fvector, u32, GameGraph::_GRAPH_ID, u16)
{
    static constexpr std::string_view pattern{"teleport_object(id, position, lvid, gvid)"};
    XR_PANIC("invalid arguments", name, pattern);
}

[[nodiscard]] u32 get_level_id(CALifeSimulator* self) { return self->graph().level().level_id(); }

[[nodiscard]] u32 get_level_id_by_name(CALifeSimulator*, gsl::czstring level_name)
{
    const GameGraph::SLevel* level = ai().game_graph().header().level(level_name, true);
    if (level != nullptr)
        return level->id();

    return std::numeric_limits<u32>::max();
}

[[nodiscard]] std::string_view get_level_name(const CALifeSimulator*, GameGraph::_LEVEL_ID level_id)
{
    return ai().game_graph().header().level(level_id).name();
}

[[nodiscard]] CSE_ALifeCreatureActor* get_actor(const CALifeSimulator* self) { return XR_ASSERT_VAL(self != nullptr)->graph().actor(); }

[[nodiscard]] KNOWN_INFO_VECTOR* registry(const CALifeSimulator* self, ALife::_OBJECT_ID id)
{
    return XR_ASSERT_VAL(self != nullptr)->registry(info_portions).object(id, true);
}

class CFindByIDPred final
{
public:
    constexpr explicit CFindByIDPred(gsl::czstring element_to_find) { element._set(element_to_find); }

    [[nodiscard]] bool operator()(const INFO_DATA& data) const { return data.info_id == element; }

private:
    shared_str element;
};
} // namespace

static bool has_info(const CALifeSimulator* self, const ALife::_OBJECT_ID& id, LPCSTR info_id)
{
    const KNOWN_INFO_VECTOR* known_info = registry(self, id);
    if (!known_info)
        return (false);

    if (std::find_if(known_info->begin(), known_info->end(), CFindByIDPred(info_id)) == known_info->end())
        return (false);

    return (true);
}

static bool dont_has_info(const CALifeSimulator* self, const ALife::_OBJECT_ID& id, LPCSTR info_id)
{
    // absurdly, but only because of scriptwriters needs
    return !has_info(XR_ASSERT_VAL(self != nullptr), id, info_id);
}

static LPCSTR get_save_name(CALifeSimulator* sim)
{
    // alpet: обертка предотвращает вылет, при обращении к свойству на ранней стадии  инициализации
    LPCSTR result{};
    if (sim)
        result = sim->save_name(FALSE);

    return result ? result : "NULL";
}

static LPCSTR get_loaded_save(CALifeSimulator* sim)
{
    LPCSTR result{};
    if (sim)
        result = sim->save_name(TRUE);

    return result ? result : "NULL";
}

static bool sim_is_unloading(CALifeSimulator* sim) { return sim->is_unloading(); }

void CALifeSimulator::script_register(sol::state_view& lua)
{
    lua.new_usertype<CALifeSimulator>(
        "alife_simulator", sol::no_constructor, "valid_object_id", &valid_object_id, "level_id",
        sol::overload(&get_level_id_by_name, sol::resolve<u32(CALifeSimulator*)>(&get_level_id)), "level_name", &get_level_name, "objects", &alife_objects,
        "object",
        sol::overload(sol::resolve<CSE_ALifeDynamicObject*(const CALifeSimulator*, ALife::_OBJECT_ID, bool)>(&alife_object),
                      sol::resolve<CSE_ALifeDynamicObject*(const CALifeSimulator*, ALife::_OBJECT_ID)>(&alife_object),
                      sol::resolve<CSE_ALifeDynamicObject*(const CALifeSimulator*, LPCSTR)>(&alife_object)),
        "is_unloading", &sim_is_unloading, "story_object", sol::resolve<CSE_ALifeDynamicObject*(const CALifeSimulator*, ALife::_STORY_ID)>(alife_story_object),
        "set_switch_online", sol::resolve<void(ALife::_OBJECT_ID, bool)>(&CALifeSimulator::set_switch_online), "set_switch_offline",
        sol::resolve<void(ALife::_OBJECT_ID, bool)>(&CALifeSimulator::set_switch_offline), "set_interactive",
        sol::resolve<void(ALife::_OBJECT_ID, bool)>(&CALifeSimulator::set_interactive), "add_in_restriction", &add_in_restriction, "add_out_restriction",
        &add_out_restriction, "remove_in_restriction", &remove_in_restriction, "remove_in_restrictions", &remove_in_restrictions, "remove_out_restriction",
        &remove_out_restriction, "remove_out_restrictions", &remove_out_restrictions, "remove_all_restrictions", &CALifeSimulator::remove_all_restrictions,
        "create", sol::overload(&CALifeSimulator__spawn_item2, &CALifeSimulator__spawn_item, CALifeSimulator__create), "create_ammo",
        &CALifeSimulator__spawn_ammo, "release",
        sol::overload(sol::resolve<void(CALifeSimulator*, CSE_Abstract*, bool)>(&CALifeSimulator__release),
                      sol::resolve<void(CALifeSimulator*, CSE_Abstract*)>(&CALifeSimulator__release)),
        "spawn_id",
        sol::overload([](CALifeSimulator* self, ALife::_SPAWN_STORY_ID spawn_story_id) { return self->spawns().spawn_id(spawn_story_id); },
                      [](CALifeSimulator* self, const char* obj_name) { return self->spawns().spawn_id(obj_name); }),
        "actor", &get_actor, "has_info", &has_info, "dont_has_info", &dont_has_info, "switch_distance", &CALifeSimulator::switch_distance,
        "set_switch_distance", &CALifeSimulator::set_switch_distance, "teleport_object",
        sol::overload(&wrong_CALifeSimulator__teleport_object, &CALifeSimulator__teleport_object), "assign_story_id", &CALifeSimulator__assign_story_id,
        "use_ai_locations", &CALifeSimulator__use_ai_locations, "save_name", sol::property(&get_save_name), "loaded_save_name",
        sol::property(&get_loaded_save));

    lua.set_function("alife", &alife);

    sol::table target;

    if (story_ids.empty())
        generate_story_ids(story_ids, INVALID_STORY_ID, "story_ids", "INVALID_STORY_ID", "Invalid story id description (contains spaces)!",
                           "INVALID_STORY_ID redifinition!", "Duplicated story id description");

    target = lua.create_table(story_ids.size(), 0);

    for (const auto& pair : story_ids)
        target.set(std::string_view{pair.first}, pair.second);

    xr::sol_new_enum(lua, "story_ids", target);

    if (spawn_story_ids.empty())
        generate_story_ids(spawn_story_ids, INVALID_SPAWN_STORY_ID, "spawn_story_ids", "INVALID_SPAWN_STORY_ID",
                           "Invalid spawn story id description (contains spaces)!", "INVALID_SPAWN_STORY_ID redifinition!",
                           "Duplicated spawn story id description");

    target = lua.create_table(spawn_story_ids.size(), 0);

    for (const auto& pair : spawn_story_ids)
        target.set(std::string_view{pair.first}, pair.second);

    xr::sol_new_enum(lua, "spawn_story_ids", target);
}
