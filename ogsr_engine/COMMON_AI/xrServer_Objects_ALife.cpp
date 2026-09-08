////////////////////////////////////////////////////////////////////////////
//	Module 		: xrServer_Objects_ALife.cpp
//	Created 	: 19.09.2002
//  Modified 	: 04.06.2003
//	Author		: Oles Shyshkovtsov, Alexander Maksimchuk, Victor Reutskiy and Dmitriy Iassenev
//	Description : Server objects for ALife simulator
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "xrServer_Objects_ALife.h"

#include "../xr_3da/NET_Server_Trash/NET_utils.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "game_base_space.h"
#include "object_broker.h"
#include "restriction_space.h"

#include "character_info.h"

#ifndef XRGAME_EXPORTS
#include "../xr_3da/bone.h"
#include "../xr_3da/defines.h"
LPCSTR GAME_CONFIG = "game.ltx";
#else
#include "../xr_3da/Render.h"
#endif

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeGraphPoint
////////////////////////////////////////////////////////////////////////////

CSE_ALifeGraphPoint::CSE_ALifeGraphPoint(LPCSTR caSection) : CSE_Abstract(caSection)
{
    s_gameid = GAME_DUMMY;

    m_tLocations[0] = 0;
    m_tLocations[1] = 0;
    m_tLocations[2] = 0;
    m_tLocations[3] = 0;
}

CSE_ALifeGraphPoint::~CSE_ALifeGraphPoint() = default;

void CSE_ALifeGraphPoint::__STATE_Read(NET_Packet& tNetPacket, u16)
{
    tNetPacket.r_stringZ(m_caConnectionPointName);

    if (m_wVersion < 33)
        std::ignore = tNetPacket.r_u32();
    else
        tNetPacket.r_stringZ(m_caConnectionLevelName);

    tNetPacket.r_u8(m_tLocations[0]);
    tNetPacket.r_u8(m_tLocations[1]);
    tNetPacket.r_u8(m_tLocations[2]);
    tNetPacket.r_u8(m_tLocations[3]);
}

void CSE_ALifeGraphPoint::__STATE_Write(NET_Packet& tNetPacket)
{
    tNetPacket.w_stringZ(m_caConnectionPointName);
    tNetPacket.w_stringZ(m_caConnectionLevelName);

    tNetPacket.w_u8(m_tLocations[0]);
    tNetPacket.w_u8(m_tLocations[1]);
    tNetPacket.w_u8(m_tLocations[2]);
    tNetPacket.w_u8(m_tLocations[3]);
}

void CSE_ALifeGraphPoint::UPDATE_Read(NET_Packet&) {}
void CSE_ALifeGraphPoint::UPDATE_Write(NET_Packet&) {}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeObject
////////////////////////////////////////////////////////////////////////////

CSE_ALifeObject::CSE_ALifeObject(LPCSTR caSection) : CSE_Abstract(caSection)
{
    m_bOnline = false;
    m_fDistance = 0.0f;
    ID = ALife::_OBJECT_ID(-1);
    m_tGraphID = GameGraph::_GRAPH_ID(-1);
    m_tSpawnID = ALife::_SPAWN_ID(-1);
    m_bDirectControl = true;
    m_bALifeControl = true;
    m_tNodeID = u32(-1);
    m_flags.one();
    m_story_id = INVALID_STORY_ID;
    m_spawn_story_id = INVALID_SPAWN_STORY_ID;
    m_flags.set(flOfflineNoMove, FALSE);
}

#ifdef XRGAME_EXPORTS
CALifeSimulator& CSE_ALifeObject::alife() const { return *XR_ASSERT_VAL(m_alife_simulator != nullptr); }

Fvector CSE_ALifeObject::draw_level_position() const { return (Position()); }
#endif

CSE_ALifeObject::~CSE_ALifeObject() = default;

bool CSE_ALifeObject::move_offline() const { return (!m_flags.test(flOfflineNoMove)); }
void CSE_ALifeObject::used_ai_locations(bool value) { m_flags.set(flUsedAI_Locations, value ? TRUE : FALSE); }
void CSE_ALifeObject::move_offline(bool value) { m_flags.set(flOfflineNoMove, !value ? TRUE : FALSE); }

bool CSE_ALifeObject::visible_for_map() const { return (!!m_flags.test(flVisibleForMap)); }
void CSE_ALifeObject::visible_for_map(bool value) { m_flags.set(flVisibleForMap, value ? TRUE : FALSE); }

void CSE_ALifeObject::__STATE_Write(NET_Packet& tNetPacket)
{
    tNetPacket.w(&m_tGraphID, sizeof(m_tGraphID));
    tNetPacket.w_float(m_fDistance);
    tNetPacket.w_u32(m_bDirectControl);
    tNetPacket.w_u32(m_tNodeID);
    tNetPacket.w_u32(m_flags.get());
    tNetPacket.w_stringZ(m_ini_string);
    tNetPacket.w(&m_story_id, sizeof(m_story_id));
    tNetPacket.w(&m_spawn_story_id, sizeof(m_spawn_story_id));
}

void CSE_ALifeObject::__STATE_Read(NET_Packet& tNetPacket, u16)
{
    if (m_wVersion >= 1)
    {
        if (m_wVersion > 24)
        {
            if (m_wVersion < 83)
                std::ignore = tNetPacket.r_float(); // m_spawn_probability);
        }
        else
        {
            std::ignore = tNetPacket.r_u8();
        }

        if (m_wVersion < 83)
            std::ignore = tNetPacket.r_u32();

        if (m_wVersion < 4)
        {
            u16 wDummy;
            tNetPacket.r_u16(wDummy);
        }

        tNetPacket.r(&m_tGraphID, sizeof(m_tGraphID));
        tNetPacket.r_float(m_fDistance);
    }

    if (m_wVersion >= 4)
    {
        u32 dwDummy;
        tNetPacket.r_u32(dwDummy);
        m_bDirectControl = !!dwDummy;
    }

    if (m_wVersion >= 8)
        tNetPacket.r_u32(m_tNodeID);

    if ((m_wVersion > 22) && (m_wVersion <= 79))
        tNetPacket.r(&m_tSpawnID, sizeof(m_tSpawnID));

    if ((m_wVersion > 23) && (m_wVersion < 84))
    {
        shared_str temp;
        tNetPacket.r_stringZ(temp); // m_spawn_control);
    }

    if (m_wVersion > 49)
    {
        tNetPacket.r_u32(m_flags.flags);
    }

    if (m_wVersion > 57)
    {
        if (m_ini_file)
            xr_delete(m_ini_file);
        tNetPacket.r_stringZ(m_ini_string);
    }

    if (m_wVersion > 61)
        tNetPacket.r(&m_story_id, sizeof(m_story_id));

    if (m_wVersion > 111)
        tNetPacket.r(&m_spawn_story_id, sizeof(m_spawn_story_id));
}

void CSE_ALifeObject::UPDATE_Write(NET_Packet&) {}
void CSE_ALifeObject::UPDATE_Read(NET_Packet&) {}

u32 CSE_ALifeObject::ef_equipment_type() const
{
    string16 cls;
    CLSID2TEXT(m_tClassID, cls);

    XR_PANIC("invalid A-Life equipment type request", cls);
}

u32 CSE_ALifeObject::ef_main_weapon_type() const
{
    string16 cls;
    CLSID2TEXT(m_tClassID, cls);

    XR_PANIC("invalid A-Life main weapon type request", cls);
}

u32 CSE_ALifeObject::ef_weapon_type() const { return 0; }

u32 CSE_ALifeObject::ef_detector_type() const
{
    string16 cls;
    CLSID2TEXT(m_tClassID, cls);

    XR_PANIC("invalid A-Life detector type request", cls);
}

bool CSE_ALifeObject::used_ai_locations() const { return (!!m_flags.is(flUsedAI_Locations)); }

bool CSE_ALifeObject::__can_switch_online() const { return match_configuration() && !!m_flags.is(flSwitchOnline); }

bool CSE_ALifeObject::can_switch_online() const
{
    bool ret = __can_switch_online();

    auto op = ops.find(server_ops::CAN_SWITCH_ONLINE);
    if (op == ops.end())
        return ret;

    return op->second(this, ret);
}

bool CSE_ALifeObject::__can_switch_offline() const { return !match_configuration() || !!m_flags.is(flSwitchOffline); }

bool CSE_ALifeObject::can_switch_offline() const
{
    bool ret = __can_switch_offline();

    auto op = ops.find(server_ops::CAN_SWITCH_OFFLINE);
    if (op == ops.end())
        return ret;

    return op->second(this, ret);
}

bool CSE_ALifeObject::can_save() const { return (!!m_flags.is(flCanSave)); }
bool CSE_ALifeObject::interactive() const { return (!!m_flags.is(flInteractive) && !!m_flags.is(flVisibleForAI) && !!m_flags.is(flUsefulForAI)); }
void CSE_ALifeObject::can_switch_online(bool value) { m_flags.set(flSwitchOnline, BOOL(value)); }
void CSE_ALifeObject::can_switch_offline(bool value) { m_flags.set(flSwitchOffline, BOOL(value)); }
void CSE_ALifeObject::interactive(bool value) { m_flags.set(flInteractive, BOOL(value)); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeGroupAbstract
////////////////////////////////////////////////////////////////////////////

CSE_ALifeGroupAbstract::CSE_ALifeGroupAbstract(LPCSTR)
{
    m_tpMembers.clear();
    m_bCreateSpawnPositions = true;
    m_wCount = 1;
    m_tNextBirthTime = 0;
}

CSE_Abstract* CSE_ALifeGroupAbstract::init() { return (base()); }

CSE_ALifeGroupAbstract::~CSE_ALifeGroupAbstract() = default;

void CSE_ALifeGroupAbstract::__STATE_Read(NET_Packet& tNetPacket, u16)
{
    u16 m_wVersion = base()->m_wVersion;
    u32 dwDummy;
    tNetPacket.r_u32(dwDummy);
    m_bCreateSpawnPositions = !!dwDummy;
    tNetPacket.r_u16(m_wCount);
    if (m_wVersion > 19)
        load_data(m_tpMembers, tNetPacket);
}

void CSE_ALifeGroupAbstract::__STATE_Write(NET_Packet& tNetPacket)
{
    tNetPacket.w_u32(m_bCreateSpawnPositions);
    tNetPacket.w_u16(m_wCount);
    save_data(m_tpMembers, tNetPacket);
}

void CSE_ALifeGroupAbstract::UPDATE_Read(NET_Packet& tNetPacket)
{
    u32 dwDummy;
    tNetPacket.r_u32(dwDummy);
    m_bCreateSpawnPositions = !!dwDummy;
}

void CSE_ALifeGroupAbstract::UPDATE_Write(NET_Packet& tNetPacket) { tNetPacket.w_u32(m_bCreateSpawnPositions); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeDynamicObject
////////////////////////////////////////////////////////////////////////////

CSE_ALifeDynamicObject::CSE_ALifeDynamicObject(LPCSTR caSection) : CSE_ALifeObject(caSection)
{
    m_tTimeID = 0;
    m_switch_counter = std::numeric_limits<u64>::max();
}

CSE_ALifeDynamicObject::~CSE_ALifeDynamicObject() {}

void CSE_ALifeDynamicObject::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }
void CSE_ALifeDynamicObject::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::__STATE_Read(tNetPacket, size); }

void CSE_ALifeDynamicObject::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
void CSE_ALifeDynamicObject::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeDynamicObjectVisual
////////////////////////////////////////////////////////////////////////////

CSE_ALifeDynamicObjectVisual::CSE_ALifeDynamicObjectVisual(LPCSTR caSection) : CSE_ALifeDynamicObject(caSection), CSE_Visual()
{
    if (pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));
}

CSE_ALifeDynamicObjectVisual::~CSE_ALifeDynamicObjectVisual() {}

CSE_Visual* CSE_ALifeDynamicObjectVisual::visual() { return (this); }

void CSE_ALifeDynamicObjectVisual::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    visual_write(tNetPacket);
}

void CSE_ALifeDynamicObjectVisual::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    if (m_wVersion > 31)
        visual_read(tNetPacket, m_wVersion);
}

void CSE_ALifeDynamicObjectVisual::UPDATE_Write(NET_Packet& tNetPacket) { inherited1::UPDATE_Write(tNetPacket); }
void CSE_ALifeDynamicObjectVisual::UPDATE_Read(NET_Packet& tNetPacket) { inherited1::UPDATE_Read(tNetPacket); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifePHSkeletonObject
////////////////////////////////////////////////////////////////////////////

CSE_ALifePHSkeletonObject::CSE_ALifePHSkeletonObject(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection), CSE_PHSkeleton(caSection)
{
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
}

CSE_ALifePHSkeletonObject::~CSE_ALifePHSkeletonObject() {}

void CSE_ALifePHSkeletonObject::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    if (m_wVersion >= 64)
        inherited2::__STATE_Read(tNetPacket, size);
}

void CSE_ALifePHSkeletonObject::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
}

void CSE_ALifePHSkeletonObject::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited2::load(tNetPacket);
}
void CSE_ALifePHSkeletonObject::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

void CSE_ALifePHSkeletonObject::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
}

bool CSE_ALifePHSkeletonObject::can_save() const { return CSE_PHSkeleton::need_save(); }
bool CSE_ALifePHSkeletonObject::used_ai_locations() const { return false; }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeSpaceRestrictor
////////////////////////////////////////////////////////////////////////////

CSE_ALifeSpaceRestrictor::CSE_ALifeSpaceRestrictor(LPCSTR caSection) : CSE_ALifeDynamicObject(caSection)
{
    m_flags.set(flUseSwitches, FALSE);
    m_space_restrictor_type = RestrictionSpace::eDefaultRestrictorTypeNone;
    m_flags.set(flUsedAI_Locations, FALSE);
    m_spawn_flags.set(flSpawnDestroyOnSpawn, FALSE);
    m_flags.set(flCheckForSeparator, TRUE);
}

CSE_ALifeSpaceRestrictor::~CSE_ALifeSpaceRestrictor() {}

bool CSE_ALifeSpaceRestrictor::__can_switch_offline() const { return false; }
bool CSE_ALifeSpaceRestrictor::used_ai_locations() const { return (false); }
ISE_Shape* CSE_ALifeSpaceRestrictor::shape() { return (this); }

void CSE_ALifeSpaceRestrictor::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    cform_read(tNetPacket);
    if (m_wVersion > 74)
        m_space_restrictor_type = tNetPacket.r_u8();
}

void CSE_ALifeSpaceRestrictor::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    cform_write(tNetPacket);
    tNetPacket.w_u8(m_space_restrictor_type);
}

void CSE_ALifeSpaceRestrictor::UPDATE_Read(NET_Packet& tNetPacket) { inherited1::UPDATE_Read(tNetPacket); }
void CSE_ALifeSpaceRestrictor::UPDATE_Write(NET_Packet& tNetPacket) { inherited1::UPDATE_Write(tNetPacket); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeLevelChanger
////////////////////////////////////////////////////////////////////////////
CSE_ALifeLevelChanger::CSE_ALifeLevelChanger(LPCSTR caSection) : CSE_ALifeSpaceRestrictor(caSection)
{
    m_tNextGraphID = GameGraph::_GRAPH_ID(-1);
    m_dwNextNodeID = u32(-1);
    m_tNextPosition.set(0.f, 0.f, 0.f);
    m_tAngles.set(0.f, 0.f, 0.f);
    m_SilentMode = 0;
}

CSE_ALifeLevelChanger::~CSE_ALifeLevelChanger() = default;

void CSE_ALifeLevelChanger::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::__STATE_Read(tNetPacket, size);

    if (m_wVersion < 34)
    {
        std::ignore = tNetPacket.r_u32();
        std::ignore = tNetPacket.r_u32();
    }
    else
    {
        tNetPacket.r(&m_tNextGraphID, sizeof(m_tNextGraphID));
        tNetPacket.r_u32(m_dwNextNodeID);
        tNetPacket.r_float(m_tNextPosition.x);
        tNetPacket.r_float(m_tNextPosition.y);
        tNetPacket.r_float(m_tNextPosition.z);

        if (m_wVersion <= 53)
            m_tAngles.set(0.f, tNetPacket.r_float(), 0.f);
        else
            tNetPacket.r_vec3(m_tAngles);
    }

    tNetPacket.r_stringZ(m_caLevelToChange);
    tNetPacket.r_stringZ(m_caLevelPointToChange);

    if (m_wVersion > 116)
        m_SilentMode = tNetPacket.r_u8();
}

void CSE_ALifeLevelChanger::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited::__STATE_Write(tNetPacket);
    tNetPacket.w(&m_tNextGraphID, sizeof(m_tNextGraphID));
    tNetPacket.w_u32(m_dwNextNodeID);
    tNetPacket.w_float(m_tNextPosition.x);
    tNetPacket.w_float(m_tNextPosition.y);
    tNetPacket.w_float(m_tNextPosition.z);
    tNetPacket.w_vec3(m_tAngles);
    tNetPacket.w_stringZ(m_caLevelToChange);
    tNetPacket.w_stringZ(m_caLevelPointToChange);
    tNetPacket.w_u8(m_SilentMode);
}

void CSE_ALifeLevelChanger::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeLevelChanger::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeObjectPhysic
////////////////////////////////////////////////////////////////////////////

CSE_ALifeObjectPhysic::CSE_ALifeObjectPhysic(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection), CSE_PHSkeleton(caSection)
{
    type = epotSkeleton;
    mass = 10.f;

    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));

    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
    m_flags.set(flUsedAI_Locations, FALSE);
}

CSE_ALifeObjectPhysic::~CSE_ALifeObjectPhysic() = default;

void CSE_ALifeObjectPhysic::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    if (m_wVersion >= 14)
    {
        if (m_wVersion >= 16)
        {
            inherited1::__STATE_Read(tNetPacket, size);
            if (m_wVersion < 32)
                visual_read(tNetPacket, m_wVersion);
        }
        else
        {
            CSE_ALifeObject::__STATE_Read(tNetPacket, size);
            visual_read(tNetPacket, m_wVersion);
        }
    }

    if (m_wVersion >= 64)
        inherited2::__STATE_Read(tNetPacket, size);

    tNetPacket.r_u32(type);
    tNetPacket.r_float(mass);

    if (m_wVersion > 9)
        tNetPacket.r_stringZ(fixed_bones);

    if (m_wVersion < 65 && m_wVersion > 28)
        tNetPacket.r_stringZ(startup_animation);

    if (m_wVersion < 64)
    {
        if (m_wVersion > 39) // > 39
            tNetPacket.r_u8(_flags.flags);

        if (m_wVersion > 56)
            tNetPacket.r_u16(source_id);

        if (m_wVersion > 60 && _flags.test(flSavedData))
        {
            data_load(tNetPacket);
        }
    }
    set_editor_flag(flVisualAnimationChange);
}

void CSE_ALifeObjectPhysic::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
    tNetPacket.w_u32(type);
    tNetPacket.w_float(mass);
    tNetPacket.w_stringZ(fixed_bones);
}

void CSE_ALifeObjectPhysic::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
}

void CSE_ALifeObjectPhysic::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

void CSE_ALifeObjectPhysic::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited2::load(tNetPacket);
}

bool CSE_ALifeObjectPhysic::used_ai_locations() const { return (false); }

bool CSE_ALifeObjectPhysic::can_save() const { return CSE_PHSkeleton::need_save(); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeObjectHangingLamp
////////////////////////////////////////////////////////////////////////////

CSE_ALifeObjectHangingLamp::CSE_ALifeObjectHangingLamp(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection), CSE_PHSkeleton(caSection)
{
    flags.assign(flTypeSpot | flR1 | flR2);

    range = 10.f;
    color = 0xffffffff;
    brightness = 1.f;
    m_health = 100.f;
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);

    m_virtual_size = 0.1f;
    m_ambient_radius = 10.f;
    m_ambient_power = 0.1f;
    spot_cone_angle = deg2rad(120.f);
    glow_radius = 0.7f;
}

CSE_ALifeObjectHangingLamp::~CSE_ALifeObjectHangingLamp() {}

void CSE_ALifeObjectHangingLamp::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    if (m_wVersion > 20)
        inherited1::__STATE_Read(tNetPacket, size);
    if (m_wVersion >= 69)
        inherited2::__STATE_Read(tNetPacket, size);
    if (m_wVersion < 32)
        visual_read(tNetPacket, m_wVersion);

    if (m_wVersion < 49)
    {
        shared_str s_tmp;
        float f_tmp;
        // model
        tNetPacket.r_u32(color);
        tNetPacket.r_stringZ(color_animator);
        tNetPacket.r_stringZ(s_tmp);
        tNetPacket.r_stringZ(s_tmp);
        tNetPacket.r_float(range);
        tNetPacket.r_angle8(f_tmp);
        if (m_wVersion > 10)
            tNetPacket.r_float(brightness);
        if (m_wVersion > 11)
            tNetPacket.r_u16(flags.flags);
        if (m_wVersion > 12)
            tNetPacket.r_float(f_tmp);
        if (m_wVersion > 17)
            tNetPacket.r_stringZ(startup_animation);

        set_editor_flag(flVisualAnimationChange);

        if (m_wVersion > 42)
        {
            tNetPacket.r_stringZ(s_tmp);
            tNetPacket.r_float(f_tmp);
        }

        if (m_wVersion > 43)
        {
            tNetPacket.r_stringZ(fixed_bones);
        }

        if (m_wVersion > 44)
        {
            tNetPacket.r_float(m_health);
        }
    }
    else
    {
        // model
        tNetPacket.r_u32(color);
        tNetPacket.r_float(brightness);
        tNetPacket.r_stringZ(color_animator);
        tNetPacket.r_float(range);
        tNetPacket.r_u16(flags.flags);
        tNetPacket.r_stringZ(startup_animation);
        set_editor_flag(flVisualAnimationChange);
        tNetPacket.r_stringZ(fixed_bones);
        tNetPacket.r_float(m_health);
    }
    if (m_wVersion > 55)
    {
        tNetPacket.r_float(m_virtual_size);
        tNetPacket.r_float(m_ambient_radius);
        tNetPacket.r_float(m_ambient_power);
        tNetPacket.r_stringZ(m_ambient_texture);
        tNetPacket.r_stringZ(light_texture);
        tNetPacket.r_stringZ(light_main_bone);
        tNetPacket.r_float(spot_cone_angle);
        tNetPacket.r_stringZ(glow_texture);
        tNetPacket.r_float(glow_radius);
    }
    if (m_wVersion > 96)
    {
        tNetPacket.r_stringZ(light_ambient_bone);
    }
    else
    {
        light_ambient_bone = light_main_bone;
    }

    if (tNetPacket.r_eof())
        return;

    tNetPacket.r_float(m_speed);
    tNetPacket.r_float(m_amount);
    tNetPacket.r_float(m_smap_jitter);
}

void CSE_ALifeObjectHangingLamp::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);

    // model
    tNetPacket.w_u32(color);
    tNetPacket.w_float(brightness);
    tNetPacket.w_stringZ(color_animator);
    tNetPacket.w_float(range);
    tNetPacket.w_u16(flags.flags);
    tNetPacket.w_stringZ(startup_animation);
    tNetPacket.w_stringZ(fixed_bones);
    tNetPacket.w_float(m_health);
    tNetPacket.w_float(m_virtual_size);
    tNetPacket.w_float(m_ambient_radius);
    tNetPacket.w_float(m_ambient_power);
    tNetPacket.w_stringZ(m_ambient_texture);

    tNetPacket.w_stringZ(light_texture);
    tNetPacket.w_stringZ(light_main_bone);
    tNetPacket.w_float(spot_cone_angle);
    tNetPacket.w_stringZ(glow_texture);
    tNetPacket.w_float(glow_radius);

    tNetPacket.w_stringZ(light_ambient_bone);

    tNetPacket.w_float(m_speed);
    tNetPacket.w_float(m_amount);
    tNetPacket.w_float(m_smap_jitter);
}

void CSE_ALifeObjectHangingLamp::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
}

void CSE_ALifeObjectHangingLamp::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

void CSE_ALifeObjectHangingLamp::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited2::load(tNetPacket);
}

#define VIS_RADIUS 0.25f

void CSE_ALifeObjectHangingLamp::on_render(CDUInterface* du, ISE_AbstractLEOwner* owner, bool bSelected, const Fmatrix& parent, int priority, bool strictB2F)
{
    inherited1::on_render(du, owner, bSelected, parent, priority, strictB2F);
    if ((1 == priority) && (false == strictB2F))
    {
        u32 clr = bSelected ? 0x00FFFFFF : 0x00FFFF00;
        Fmatrix main_xform, ambient_xform;
        owner->get_bone_xform(light_main_bone.c_str(), main_xform);
        main_xform.mulA_43(parent);

        if (flags.is(flPointAmbient))
        {
            owner->get_bone_xform(light_ambient_bone.c_str(), ambient_xform);
            ambient_xform.mulA_43(parent);
        }
        if (bSelected)
        {
            if (flags.is(flTypeSpot))
            {
                du->DrawSpotLight(main_xform.c, main_xform.k, range, spot_cone_angle, clr);
            }
            else
            {
                du->DrawLineSphere(main_xform.c, range, clr, true);
            }
            if (flags.is(flPointAmbient))
                du->DrawLineSphere(ambient_xform.c, m_ambient_radius, clr, true);
        }
        du->DrawPointLight(main_xform.c, VIS_RADIUS, clr);
        if (flags.is(flPointAmbient))
            du->DrawPointLight(ambient_xform.c, VIS_RADIUS, clr);
    }
}

bool CSE_ALifeObjectHangingLamp::used_ai_locations() const { return (false); }

bool CSE_ALifeObjectHangingLamp::validate()
{
    if (flags.test(flR1) || flags.test(flR2))
        return (true);

    XR_LOG_ERROR("{}: render type is not set properly!", name_replace());

    return (false);
}

bool CSE_ALifeObjectHangingLamp::match_configuration() const
{
    XR_ASSERT(flags.test(flR1) || flags.test(flR2), "no renderer type set for hanging lamp", name_replace());

#ifdef XRGAME_EXPORTS
    return flags.test(flR2);
#else
    return (true);
#endif
}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeObjectSearchlight
////////////////////////////////////////////////////////////////////////////

CSE_ALifeObjectProjector::CSE_ALifeObjectProjector(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection)
{
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
}

CSE_ALifeObjectProjector::~CSE_ALifeObjectProjector() = default;

void CSE_ALifeObjectProjector::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::__STATE_Read(tNetPacket, size); }
void CSE_ALifeObjectProjector::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }

void CSE_ALifeObjectProjector::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeObjectProjector::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

bool CSE_ALifeObjectProjector::used_ai_locations() const { return (false); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeSchedulable
////////////////////////////////////////////////////////////////////////////

CSE_ALifeSchedulable::CSE_ALifeSchedulable(LPCSTR) {}
CSE_ALifeSchedulable::~CSE_ALifeSchedulable() = default;

bool CSE_ALifeSchedulable::need_update(CSE_ALifeDynamicObject* object)
{
    return !object || (object->m_bDirectControl && object->used_ai_locations() && !object->m_bOnline);
}

CSE_Abstract* CSE_ALifeSchedulable::init() { return (base()); }

u32 CSE_ALifeSchedulable::ef_creature_type() const
{
    string16 cls;
    CLSID2TEXT(base()->m_tClassID, cls);

    XR_PANIC("invalid A-Life creature type request", cls);
}

u32 CSE_ALifeSchedulable::ef_anomaly_type() const
{
    string16 cls;
    CLSID2TEXT(base()->m_tClassID, cls);

    XR_PANIC("invalid A-Life anomaly type request", cls);
}

u32 CSE_ALifeSchedulable::ef_weapon_type() const
{
    string16 cls;
    CLSID2TEXT(base()->m_tClassID, cls);

    XR_PANIC("invalid A-Life weapon type request", cls);
}

u32 CSE_ALifeSchedulable::ef_detector_type() const
{
    string16 cls;
    CLSID2TEXT(base()->m_tClassID, cls);

    XR_PANIC("invalid A-Life detector type request", cls);
}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeHelicopter
////////////////////////////////////////////////////////////////////////////

CSE_ALifeHelicopter::CSE_ALifeHelicopter(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection), CSE_Motion(), CSE_PHSkeleton(caSection)
{
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
    m_flags.set(flInteractive, FALSE);
}

CSE_ALifeHelicopter::~CSE_ALifeHelicopter() = default;

CSE_Motion* CSE_ALifeHelicopter::motion() { return (this); }

void CSE_ALifeHelicopter::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    CSE_Motion::motion_read(tNetPacket);
    if (m_wVersion >= 69)
        inherited3::__STATE_Read(tNetPacket, size);
    tNetPacket.r_stringZ(startup_animation);
    tNetPacket.r_stringZ(engine_sound);

    set_editor_flag(flVisualAnimationChange | flMotionChange);
}

void CSE_ALifeHelicopter::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    CSE_Motion::motion_write(tNetPacket);
    inherited3::__STATE_Write(tNetPacket);
    tNetPacket.w_stringZ(startup_animation);
    tNetPacket.w_stringZ(engine_sound);
}

void CSE_ALifeHelicopter::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited3::UPDATE_Read(tNetPacket);
}

void CSE_ALifeHelicopter::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited3::UPDATE_Write(tNetPacket);
}

void CSE_ALifeHelicopter::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited3::load(tNetPacket);
}

bool CSE_ALifeHelicopter::can_save() const { return CSE_PHSkeleton::need_save(); }

bool CSE_ALifeHelicopter::used_ai_locations() const { return (false); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeCar
////////////////////////////////////////////////////////////////////////////
CSE_ALifeCar::CSE_ALifeCar(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection), CSE_PHSkeleton(caSection)
{
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
    health = 1.0f;
}

CSE_ALifeCar::~CSE_ALifeCar() {}

void CSE_ALifeCar::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);

    if (m_wVersion > 65)
        inherited2::__STATE_Read(tNetPacket, size);

    if ((m_wVersion > 52) && (m_wVersion < 55))
        std::ignore = tNetPacket.r_float();

    if (m_wVersion > 92)
        health = tNetPacket.r_float();
    if (health > 1.0f)
        health /= 100.0f;
}

void CSE_ALifeCar::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
    tNetPacket.w_float(health);
}

void CSE_ALifeCar::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
}

void CSE_ALifeCar::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

bool CSE_ALifeCar::used_ai_locations() const { return (false); }

bool CSE_ALifeCar::can_save() const { return CSE_PHSkeleton::need_save(); }

void CSE_ALifeCar::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited2::load(tNetPacket); // CSE_PHSkeleton::load
}

void CSE_ALifeCar::data_load(NET_Packet& tNetPacket)
{
    inherited2::data_load(tNetPacket);

    tNetPacket.r_vec3(o_Position);
    tNetPacket.r_vec3(o_Angle);

    door_states.clear();
    u16 doors_number = tNetPacket.r_u16();

    for (u16 i = 0; i < doors_number; ++i)
    {
        SDoorState ds;
        ds.read(tNetPacket);
        door_states.push_back(ds);
    }

    wheel_states.clear();
    u16 wheels_number = tNetPacket.r_u16();

    for (u16 i = 0; i < wheels_number; ++i)
    {
        SWheelState ws;
        ws.read(tNetPacket);
        wheel_states.push_back(ws);
    }

    health = tNetPacket.r_float();
}

void CSE_ALifeCar::data_save(NET_Packet& tNetPacket)
{
    inherited2::data_save(tNetPacket);

    tNetPacket.w_vec3(o_Position);
    tNetPacket.w_vec3(o_Angle);

    {
        tNetPacket.w_u16(u16(door_states.size()));
        xr_vector<SDoorState>::iterator i = door_states.begin(), e = door_states.end();
        for (; e != i; ++i)
        {
            i->write(tNetPacket);
        }
    }

    {
        tNetPacket.w_u16(u16(wheel_states.size()));
        xr_vector<SWheelState>::iterator i = wheel_states.begin(), e = wheel_states.end();
        for (; e != i; ++i)
        {
            i->write(tNetPacket);
        }
    }

    tNetPacket.w_float(health);
}

void CSE_ALifeCar::SDoorState::read(NET_Packet& P)
{
    open_state = P.r_u8();
    health = P.r_float();
}

void CSE_ALifeCar::SDoorState::write(NET_Packet& P)
{
    P.w_u8(open_state);
    P.w_float(health);
}

void CSE_ALifeCar::SWheelState::read(NET_Packet& P) { health = P.r_float(); }
void CSE_ALifeCar::SWheelState::write(NET_Packet& P) { P.w_float(health); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeObjectBreakable
////////////////////////////////////////////////////////////////////////////

CSE_ALifeObjectBreakable::CSE_ALifeObjectBreakable(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection)
{
    m_health = 1.f;
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
}

CSE_ALifeObjectBreakable::~CSE_ALifeObjectBreakable() = default;

void CSE_ALifeObjectBreakable::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::__STATE_Read(tNetPacket, size);
    tNetPacket.r_float(m_health);
}

void CSE_ALifeObjectBreakable::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited::__STATE_Write(tNetPacket);
    tNetPacket.w_float(m_health);
}

void CSE_ALifeObjectBreakable::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeObjectBreakable::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

bool CSE_ALifeObjectBreakable::used_ai_locations() const { return (false); }

bool CSE_ALifeObjectBreakable::__can_switch_offline() const { return false; }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeObjectClimable
////////////////////////////////////////////////////////////////////////////

CSE_ALifeObjectClimable::CSE_ALifeObjectClimable(LPCSTR caSection) : CSE_Shape(), CSE_ALifeDynamicObject(caSection)
{
    // m_health					= 100.f;
    // m_flags.set					(flUseSwitches,FALSE);
    // m_flags.set					(flSwitchOffline,FALSE);
}

CSE_ALifeObjectClimable::~CSE_ALifeObjectClimable() = default;

ISE_Shape* CSE_ALifeObjectClimable::shape() { return (this); }

void CSE_ALifeObjectClimable::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    // inherited1::__STATE_Read		(tNetPacket,size);
    if (m_wVersion == 99)
        CSE_ALifeObject::__STATE_Read(tNetPacket, size);
    if (m_wVersion > 99)
        inherited2::__STATE_Read(tNetPacket, size);
    cform_read(tNetPacket);
}

void CSE_ALifeObjectClimable::__STATE_Write(NET_Packet& tNetPacket)
{
    // inherited1::__STATE_Write		(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
    cform_write(tNetPacket);
}

void CSE_ALifeObjectClimable::UPDATE_Read(NET_Packet&) {}
void CSE_ALifeObjectClimable::UPDATE_Write(NET_Packet&) {}

bool CSE_ALifeObjectClimable::used_ai_locations() const { return false; }
bool CSE_ALifeObjectClimable::__can_switch_offline() const { return false; }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeMountedWeapon
////////////////////////////////////////////////////////////////////////////

CSE_ALifeMountedWeapon::CSE_ALifeMountedWeapon(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection) {}
CSE_ALifeMountedWeapon::~CSE_ALifeMountedWeapon() = default;

void CSE_ALifeMountedWeapon::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::__STATE_Read(tNetPacket, size); }
void CSE_ALifeMountedWeapon::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }

void CSE_ALifeMountedWeapon::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeMountedWeapon::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

CSE_ALifeStationaryMgun::CSE_ALifeStationaryMgun(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection) {}
CSE_ALifeStationaryMgun::~CSE_ALifeStationaryMgun() = default;

void CSE_ALifeStationaryMgun::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);
    m_bWorking = !!tNetPacket.r_u8();
    load_data(m_destEnemyDir, tNetPacket);
}

void CSE_ALifeStationaryMgun::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);
    tNetPacket.w_u8(m_bWorking ? 1 : 0);
    save_data(m_destEnemyDir, tNetPacket);
}

void CSE_ALifeStationaryMgun::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::__STATE_Read(tNetPacket, size); }
void CSE_ALifeStationaryMgun::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeSmartZone
////////////////////////////////////////////////////////////////////////////

CSE_ALifeSmartZone::CSE_ALifeSmartZone(LPCSTR caSection) : CSE_ALifeSpaceRestrictor(caSection), CSE_ALifeSchedulable(caSection) {}
CSE_ALifeSmartZone::~CSE_ALifeSmartZone() = default;

CSE_Abstract* CSE_ALifeSmartZone::base() { return this; }
const CSE_Abstract* CSE_ALifeSmartZone::base() const { return this; }

CSE_Abstract* CSE_ALifeSmartZone::init()
{
    std::ignore = inherited1::init();
    std::ignore = inherited2::init();

    return this;
}

void CSE_ALifeSmartZone::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited1::__STATE_Read(tNetPacket, size); }
void CSE_ALifeSmartZone::__STATE_Write(NET_Packet& tNetPacket) { inherited1::__STATE_Write(tNetPacket); }

void CSE_ALifeSmartZone::UPDATE_Read(NET_Packet& tNetPacket) { inherited1::UPDATE_Read(tNetPacket); }
void CSE_ALifeSmartZone::UPDATE_Write(NET_Packet& tNetPacket) { inherited1::UPDATE_Write(tNetPacket); }

bool CSE_ALifeSmartZone::enabled(CSE_ALifeMonsterAbstract* object) const
{
    auto op = ops.find(server_ops::ENABLED);
    if (op == ops.end())
        return false;

    return op->second(this, object);
}

void CSE_ALifeSmartZone::register_npc(CSE_ALifeMonsterAbstract* object)
{
    auto op = ops.find(server_ops::REGISTER_NPC);
    if (op == ops.end())
        return;

    op->second(this, object);
}

float CSE_ALifeSmartZone::suitable(CSE_ALifeMonsterAbstract* object) const
{
    auto op = ops.find(server_ops::SUITABLE);
    if (op == ops.end())
        return 0.f;

    return op->second(this, object);
}

CALifeSmartTerrainTask* CSE_ALifeSmartZone::task(CSE_ALifeMonsterAbstract* object)
{
    auto op = ops.find(server_ops::TASK);
    if (op == ops.end())
        return nullptr;

    return op->second(this, object);
}

void CSE_ALifeSmartZone::unregister_npc(CSE_ALifeMonsterAbstract* object)
{
    auto op = ops.find(server_ops::UNREGISTER_NPC);
    if (op == ops.end())
        return;

    op->second(this, object);
}

void CSE_ALifeSmartZone::update()
{
    auto op = ops.find(server_ops::UPDATE);
    if (op == ops.end())
        return;

    XR_TRACY_ZONE_SCOPED();

    op->second(this);
}
