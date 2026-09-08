////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_object_registry.cpp
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife object registry
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "alife_object_registry.h"

#include "../xr_3da/NET_Server_Trash/NET_utils.h"
#include "ai_debug.h"

#include "game_object_space.h"
#include "script_game_object.h"
#include "Actor.h"

CALifeObjectRegistry::CALifeObjectRegistry() = default;

CALifeObjectRegistry::~CALifeObjectRegistry()
{
    m_object_ids.clear();

    OBJECT_REGISTRY::iterator I = m_objects.begin();
    OBJECT_REGISTRY::iterator E = m_objects.end();
    for (; I != E; ++I)
    {
        // hack, should be revisited in case of not intended behaviour
        (*I).second->on_unregister();
        xr_delete((*I).second);
    }
    m_objects.clear();
}

void CALifeObjectRegistry::save(IWriter& memory_stream, CSE_ALifeDynamicObject* object, u32& object_count)
{
    ++object_count;

    NET_Packet tNetPacket;
    // Spawn
    object->Spawn_Write(tNetPacket, TRUE);
    memory_stream.w_u16(u16(tNetPacket.B.count));
    memory_stream.w(tNetPacket.B.data, tNetPacket.B.count);

    // Update
    tNetPacket.w_begin(gsl::narrow<u16>(xr::msg::M_UPDATE));
    object->UPDATE_Write(tNetPacket);

    memory_stream.w_u16(u16(tNetPacket.B.count));
    memory_stream.w(tNetPacket.B.data, tNetPacket.B.count);

    ALife::OBJECT_VECTOR::const_iterator I = object->children.begin();
    ALife::OBJECT_VECTOR::const_iterator E = object->children.end();
    for (; I != E; ++I)
    {
        CSE_ALifeDynamicObject* child = this->object(*I, true);
        if (!child)
            continue;

        if (!child->can_save())
            continue;

        save(memory_stream, child, object_count);
    }
}

void CALifeObjectRegistry::save(IWriter& memory_stream)
{
    XR_LOG_NOTICE("Saving objects...");

    // alpet: колбек перед сохранением всех объектов 18.10.2014 :)
    if (g_actor)
        g_actor->callback(GameObject::eBeforeSave)();

    memory_stream.open_chunk(OBJECT_CHUNK_DATA);
    u32 position = memory_stream.tell();
    memory_stream.w_u32(u32(-1));

    u32 object_count = 0;
    OBJECT_REGISTRY::iterator I = m_objects.begin();
    OBJECT_REGISTRY::iterator E = m_objects.end();
    for (; I != E; ++I)
    {
        if (!(*I).second->can_save())
            continue;

        if ((*I).second->ID_Parent != 0xffff)
            continue;

        save(memory_stream, (*I).second, object_count);
    }

    u32 last_position = memory_stream.tell();
    memory_stream.seek(position);
    memory_stream.w_u32(object_count);
    memory_stream.seek(last_position);

    memory_stream.close_chunk();

    Msg("* {} objects are successfully saved", object_count);

    // Real Wolf: колбек после сохранения всех объектов. 01.08.2014.
    if (g_actor)
        g_actor->callback(GameObject::ePostSave)();
}

CSE_ALifeDynamicObject* CALifeObjectRegistry::get_object(IReader& file_stream)
{
    NET_Packet tNetPacket;
    u16 u_id;
    // Spawn
    tNetPacket.B.count = file_stream.r_u16();
    file_stream.r(tNetPacket.B.data, tNetPacket.B.count);

    std::ignore = tNetPacket.r_begin(u_id);
    XR_ASSERT(xr::msg{u_id} == xr::msg::M_SPAWN, "invalid packet ID");

    string64 s_name;
    tNetPacket.r_stringZ(s_name);

#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
        Msg("Loading object {}", s_name);
#endif

    // create entity
    CSE_Abstract* tpSE_Abstract = F_entity_Create(s_name);
    R_ASSERT2(tpSE_Abstract, "Can't create entity.");
    CSE_ALifeDynamicObject* tpALifeDynamicObject = smart_cast<CSE_ALifeDynamicObject*>(tpSE_Abstract);
    R_ASSERT2(tpALifeDynamicObject, "Non-ALife object in the saved game!");
    std::ignore = tpALifeDynamicObject->Spawn_Read(tNetPacket);

    // Update
    tNetPacket.B.count = file_stream.r_u16();
    file_stream.r(tNetPacket.B.data, tNetPacket.B.count);

    std::ignore = tNetPacket.r_begin(u_id);
    XR_ASSERT(xr::msg{u_id} == xr::msg::M_UPDATE, "invalid packet ID");
    tpALifeDynamicObject->UPDATE_Read(tNetPacket);

    return tpALifeDynamicObject;
}

void CALifeObjectRegistry::load(IReader& file_stream)
{
    XR_LOG_NOTICE("Loading objects...");
    XR_ASSERT(file_stream.find_chunk(OBJECT_CHUNK_DATA), "can't find chunk", OBJECT_CHUNK_DATA);

    m_objects.clear();
    m_object_ids.clear();

    u32 count = file_stream.r_u32();
    for (u32 i = 0; i < count; i++)
        add(get_object(file_stream));

    Msg("* {} objects are successfully loaded", count);
}
