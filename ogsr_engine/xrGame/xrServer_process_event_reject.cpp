#include "stdafx.h"

#include "xrServer.h"

#include "xrServer_Objects.h"

bool xrServer::Process_event_reject(NET_Packet& P, const u16 id_parent, const u16 id_entity, bool send_message)
{
    // Parse message
    CSE_Abstract* e_parent = game->get_entity_from_eid(id_parent);
    CSE_Abstract* e_entity = game->get_entity_from_eid(id_entity);

#ifdef DEBUG
    Msg("sv reject. id_parent {} id_entity {} [{}]", ent_name_safe(id_parent), ent_name_safe(id_entity), Device.dwFrame);
#endif

    if (!e_parent)
    {
        XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "No parent object! ID {}", id_parent);
        return false;
    }

    if (!e_entity)
    {
        XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "No entity object! ID {}", id_entity);
        return false;
    }

    game->OnDetach(id_parent, id_entity);

    if (0xffff == e_entity->ID_Parent)
    {
        XR_LOG_DYNAMIC_DEBUG(xr::level::Error, "Can't detach independent object. entity[{}][{}], parent[{}][{}], section[{}]", e_entity->name_replace(),
                             id_entity, e_parent->name_replace(), id_parent, e_entity->s_name);
        return false;
    }

    // Rebuild parentness
    if (e_entity->ID_Parent != id_parent)
        // it can't be !!!
        XR_LOG_DYNAMIC_DEBUG(xr::level::Error, "e_entity->ID_Parent = [{}]  parent = [{}][{}]  entity_id = [{}]  frame = [{}]", e_entity->ID_Parent, id_parent,
                             e_parent->name_replace(), id_entity, Device.dwFrame);

    auto& children = e_parent->children;
    const auto child = std::find(children.begin(), children.end(), id_entity);
    if (child == children.end())
    {
        XR_LOG_DYNAMIC_DEBUG(xr::level::Error, "SV: can't find children [{}] of parent [{}]", id_entity, id_parent);
        return false;
    }

    e_entity->ID_Parent = 0xffff;

    children.erase(child);

    // Signal to everyone (including sender)
    if (send_message)
    {
        DWORD MODE = net_flags(TRUE, TRUE, FALSE, TRUE);
        SendBroadcast(BroadcastCID, P, MODE);
    }

    return true;
}
