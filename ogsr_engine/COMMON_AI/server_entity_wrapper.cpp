////////////////////////////////////////////////////////////////////////////
//	Module 		: server_entity_wrapper.cpp
//	Created 	: 16.10.2004
//  Modified 	: 16.10.2004
//	Author		: Dmitriy Iassenev
//	Description : Server entity wrapper
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "server_entity_wrapper.h"

#include "xrServer_Objects.h"
#include "xrMessages.h"
#include "../../xr_3da/NET_Server_Trash/NET_utils.h"

struct ISE_Abstract;

CServerEntityWrapper::~CServerEntityWrapper() { F_entity_Destroy(m_object); }

void CServerEntityWrapper::save(IWriter& stream)
{
    NET_Packet net_packet;

    // Spawn
    stream.open_chunk(0);

    m_object->Spawn_Write(net_packet, TRUE);
    stream.w_u16(u16(net_packet.B.count));
    stream.w(net_packet.B.data, net_packet.B.count);

    stream.close_chunk();

    // Update
    stream.open_chunk(1);

    net_packet.w_begin(gsl::narrow<u16>(xr::msg::M_UPDATE));
    m_object->UPDATE_Write(net_packet);
    stream.w_u16(u16(net_packet.B.count));
    stream.w(net_packet.B.data, net_packet.B.count);

    stream.close_chunk();
}

void CServerEntityWrapper::load(IReader& stream)
{
    NET_Packet net_packet;
    u16 ID;
    IReader* chunk;

    chunk = stream.open_chunk(0);

    net_packet.B.count = chunk->r_u16();
    chunk->r(net_packet.B.data, net_packet.B.count);

    chunk->close();

    std::ignore = net_packet.r_begin(ID);
    XR_ASSERT(xr::msg{ID} == xr::msg::M_SPAWN, "invalid packet ID");

    string64 s_name;
    net_packet.r_stringZ(s_name);

    m_object = XR_ASSERT_VAL(F_entity_Create(s_name) != nullptr, "failed to create entity", s_name);
    std::ignore = m_object->Spawn_Read(net_packet);

    XR_LOG_TRACE_L1("[SPAWN] {}, ({})", m_object->name_replace(), m_object->ID);

    chunk = stream.open_chunk(1);

    net_packet.B.count = chunk->r_u16();
    chunk->r(net_packet.B.data, net_packet.B.count);

    chunk->close();

    std::ignore = net_packet.r_begin(ID);
    XR_ASSERT(xr::msg{ID} == xr::msg::M_UPDATE, "invalid packet ID", s_name, m_object->ID);

    m_object->UPDATE_Read(net_packet);
}

void CServerEntityWrapper::save_update(IWriter&) {}
void CServerEntityWrapper::load_update(IReader&) {}
