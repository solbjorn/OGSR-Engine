////////////////////////////////////////////////////////////////////////////
//	Module 		: xrServer_Object_Base.cpp
//	Created 	: 19.09.2002
//  Modified 	: 16.07.2004
//	Author		: Oles Shyshkovtsov, Alexander Maksimchuk, Victor Reutskiy and Dmitriy Iassenev
//	Description : Server base object
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "xrServer_Objects.h"

#include "xrMessages.h"
#include "game_base_space.h"
#include "script_value_container_impl.h"
#include "clsid_game.h"

#include "object_factory.h"

namespace
{
constexpr LPCSTR script_section = "script";
constexpr LPCSTR current_version = "current_server_entity_version";
} // namespace

u16 script_server_object_version()
{
    static bool initialized = false;
    static u16 script_version = 0;
    if (!initialized)
    {
        initialized = true;
        if (!pSettings->section_exist(script_section) || !pSettings->line_exist(script_section, current_version))
            script_version = 0;
        script_version = pSettings->r_u16(script_section, current_version);
    }
    return (script_version);
}

////////////////////////////////////////////////////////////////////////////
// CPureServerObject
////////////////////////////////////////////////////////////////////////////

void CPureServerObject::save(IWriter&) {}
void CPureServerObject::load(IReader&) {}

void CPureServerObject::load(NET_Packet&) {}
void CPureServerObject::save(NET_Packet&) {}

////////////////////////////////////////////////////////////////////////////
// CSE_Abstract
////////////////////////////////////////////////////////////////////////////

CSE_Abstract::CSE_Abstract(LPCSTR caSection)
{
    m_editor_flags.zero();
    RespawnTime = 0;
    net_Ready = FALSE;
    ID = 0xffff;
    ID_Parent = 0xffff;
    ID_Phantom = 0xffff;
    s_gameid = 0;
    s_RP = 0xFE; // Use supplied coords
    s_flags.assign(0);
    s_name._set(caSection);
    o_Angle.set(0.f, 0.f, 0.f);
    o_Position.set(0.f, 0.f, 0.f);
    m_bALifeControl = false;
    m_wVersion = 0;
    m_script_version = 0;
    m_tClassID = TEXT2CLSID(pSettings->r_string(caSection, "class"));

    m_spawn_flags.zero();
    m_spawn_flags.set(flSpawnEnabled, TRUE);
    m_spawn_flags.set(flSpawnOnSurgeOnly, TRUE);
    m_spawn_flags.set(flSpawnSingleItemOnly, TRUE);
    m_spawn_flags.set(flSpawnIfDestroyedOnly, TRUE);
    m_spawn_flags.set(flSpawnInfiniteCount, TRUE);

    if (pSettings->line_exist(caSection, "custom_data"))
    {
        string_path file_name;
        std::ignore = FS.update_path(file_name, "$game_config$", pSettings->r_string(caSection, "custom_data"));

        if (!FS.exist(file_name))
        {
            XR_LOG_ERROR("Can't open config file {}", file_name);
        }
        else
        {
            const auto reader = absl::WrapUnique(FS.r_open(file_name));
            reader->skip_bom(file_name);

            const xr_string temp{static_cast<gsl::czstring>(reader->pointer()), gsl::narrow_cast<size_t>(reader->elapsed())};
            m_ini_string._set(temp.c_str());
        }
    }

    m_script_clsid = object_factory().script_clsid(m_tClassID);
}

CSE_Abstract::~CSE_Abstract()
{
    xr_free(s_name_replace);
    xr_delete(m_ini_file);
}

CSE_Visual* CSE_Abstract::visual() { return nullptr; }
ISE_Shape* CSE_Abstract::shape() { return nullptr; }
CSE_Motion* CSE_Abstract::motion() { return nullptr; }

CInifile& CSE_Abstract::spawn_ini()
{
    if (!m_ini_file)
    {
        IReader r((void*)m_ini_string.c_str(), m_ini_string.size());
        m_ini_file = xr_new<CInifile>(&r, FS.get_path("$game_config$")->m_Path);
    }

    return (*m_ini_file);
}

void CSE_Abstract::STATE_Write(NET_Packet& tNetPacket)
{
    __STATE_Write(tNetPacket);

    auto op = ops.find(server_ops::STATE_WRITE);
    if (op == ops.end())
        return;

    op->second(this, tNetPacket);
}

void CSE_Abstract::Spawn_Write(NET_Packet& tNetPacket, BOOL bLocal)
{
    // generic
    tNetPacket.w_begin(gsl::narrow<u16>(xr::msg::M_SPAWN));
    tNetPacket.w_stringZ(s_name);
    tNetPacket.w_stringZ(s_name_replace ? s_name_replace : "");
    tNetPacket.w_u8(s_gameid);
    tNetPacket.w_u8(s_RP);
    tNetPacket.w_vec3(o_Position);
    tNetPacket.w_vec3(o_Angle);
    tNetPacket.w_u16(RespawnTime);
    tNetPacket.w_u16(ID);
    tNetPacket.w_u16(ID_Parent);
    tNetPacket.w_u16(ID_Phantom);

    s_flags.set(M_SPAWN_VERSION, TRUE);
    if (bLocal)
        tNetPacket.w_u16(u16(s_flags.flags | M_SPAWN_OBJECT_LOCAL));
    else
        tNetPacket.w_u16(u16(s_flags.flags & ~(M_SPAWN_OBJECT_LOCAL | M_SPAWN_OBJECT_ASPLAYER)));

    tNetPacket.w_u16(SPAWN_VERSION);

    tNetPacket.w_u16(script_server_object_version());

    // client object custom data serialization SAVE
    u16 client_data_size = (u16)client_data.size(); // не может быть больше 256 байт
    tNetPacket.w_u16(client_data_size);

    if (client_data_size > 0)
        tNetPacket.w(client_data.data(), client_data_size);

    tNetPacket.w(&m_tSpawnID, sizeof(m_tSpawnID));

    // write specific data
    u32 position = tNetPacket.w_tell();
    tNetPacket.w_u16(0);
    STATE_Write(tNetPacket);

    const auto size = tNetPacket.w_tell() - position;
    XR_ASSERT(m_tClassID == CLSID_SPECTATOR || size > sizeof(u16), "failed to save object", name_replace(), m_tClassID, size);

    tNetPacket.w_seek(position, &size, sizeof(u16));
}

void CSE_Abstract::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    __STATE_Read(tNetPacket, size);

    auto op = ops.find(server_ops::STATE_READ);
    if (op == ops.end())
        return;

    op->second(this, tNetPacket, size);
}

BOOL CSE_Abstract::Spawn_Read(NET_Packet& tNetPacket)
{
    u16 dummy16;
    std::ignore = tNetPacket.r_begin(dummy16);
    XR_ASSERT(xr::msg{dummy16} == xr::msg::M_SPAWN);

    // generic
    tNetPacket.r_stringZ(s_name);

    string256 temp;
    tNetPacket.r_stringZ(temp);
    set_name_replace(temp);
    tNetPacket.r_u8(s_gameid);
    tNetPacket.r_u8(s_RP);
    tNetPacket.r_vec3(o_Position);
    tNetPacket.r_vec3(o_Angle);
    tNetPacket.r_u16(RespawnTime);
    tNetPacket.r_u16(ID);
    tNetPacket.r_u16(ID_Parent);
    tNetPacket.r_u16(ID_Phantom);

    tNetPacket.r_u16(s_flags.flags);

    // dangerous!!!!!!!!!
    if (s_flags.is(M_SPAWN_VERSION))
        tNetPacket.r_u16(m_wVersion);

    if (0 == m_wVersion)
    {
        tNetPacket.r_pos -= sizeof(u16);
        m_wVersion = 0;
        return FALSE;
    }

    if (m_wVersion > 69)
        m_script_version = tNetPacket.r_u16();

    // read specific data

    // client object custom data serialization LOAD
    if (m_wVersion > 70)
    {
        u16 client_data_size = (m_wVersion > 93) ? tNetPacket.r_u16() : tNetPacket.r_u8(); // не может быть больше 256 байт
        if (client_data_size > 0)
        {
            client_data.resize(client_data_size);
            tNetPacket.r(client_data.data(), client_data_size);
        }
        else
        {
            client_data.clear();
        }
    }
    else
    {
        client_data.clear();
    }

    if (m_wVersion > 79)
        tNetPacket.r(&m_tSpawnID, sizeof(m_tSpawnID));

    if (m_wVersion < 112)
    {
        if (m_wVersion > 82)
            std::ignore = tNetPacket.r_float(); // m_spawn_probability);

        if (m_wVersion > 83)
        {
            std::ignore = tNetPacket.r_u32(); // m_spawn_flags.assign(tNetPacket.r_u32());

            xr_string temp;
            tNetPacket.r_stringZ(temp); // tNetPacket.r_stringZ(m_spawn_control);

            std::ignore = tNetPacket.r_u32(); // m_max_spawn_count);
            // this stuff we do not need even in case of uncomment
            std::ignore = tNetPacket.r_u32(); // m_spawn_count);
            std::ignore = tNetPacket.r_u64(); // m_last_spawn_time);
        }

        if (m_wVersion > 84)
        {
            std::ignore = tNetPacket.r_u64(); // m_min_spawn_interval);
            std::ignore = tNetPacket.r_u64(); // m_max_spawn_interval);
        }
    }

    u16 size;
    tNetPacket.r_u16(size);
    XR_ASSERT(m_tClassID == CLSID_SPECTATOR || size > sizeof(u16), "failed to read object", name_replace(), m_tClassID, size);

    STATE_Read(tNetPacket, size);

    return TRUE;
}

void CSE_Abstract::load(NET_Packet& tNetPacket)
{
    CPureServerObject::load(tNetPacket);

    u16 client_data_size = (m_wVersion > 93) ? tNetPacket.r_u16() : tNetPacket.r_u8();
    if (client_data_size > 0)
    {
        client_data.resize(client_data_size);
        tNetPacket.r(client_data.data(), client_data_size);
    }
    else
    {
        if (!client_data.empty())
            XR_LOG_DEBUG("Client data is cleared for [{}][{}]", ID, name_replace());

        client_data.clear();
    }
}

CSE_Abstract* CSE_Abstract::base() { return (this); }
const CSE_Abstract* CSE_Abstract::base() const { return (this); }

CSE_Abstract* CSE_Abstract::init() { return (this); }

gsl::czstring CSE_Abstract::name() const { return s_name.c_str(); }
gsl::czstring CSE_Abstract::name_replace() const { return s_name_replace; }

Fvector& CSE_Abstract::position() { return (o_Position); }
Fvector& CSE_Abstract::angle() { return (o_Angle); }
Flags16& CSE_Abstract::flags() { return (s_flags); }

bool CSE_Abstract::validate() { return (true); }
