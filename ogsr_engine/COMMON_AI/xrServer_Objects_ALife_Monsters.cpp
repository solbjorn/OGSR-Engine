////////////////////////////////////////////////////////////////////////////
//	Module 		: xrServer_Objects_ALife.h
//	Created 	: 19.09.2002
//  Modified 	: 04.06.2003
//	Author		: Oles Shyshkovtsov, Alexander Maksimchuk, Victor Reutskiy and Dmitriy Iassenev
//	Description : Server objects monsters for ALife simulator
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "xrServer_Objects_ALife_Monsters.h"

#include "xrServer_Objects_ALife_Items.h"
#include "object_broker.h"
#include "alife_human_brain.h"

#include "ai_space.h"
#include "character_info.h"
#include "specific_character.h"

#ifdef XRGAME_EXPORTS
#include "ef_storage.h"
#include "game_base_space.h"
#include "game_graph.h"
#include "alife_simulator.h"
#include "alife_registry_container.h"
#include "ef_primary.h"
#include "string_table.h"
#include "alife_online_offline_group_brain.h"
#include "alife_object_registry.h"
#include "date_time.h"
#include "Level.h"
#endif

#include "../xr_3da/NET_Server_Trash/NET_utils.h"

namespace
{
void setup_location_types_section(GameGraph::TERRAIN_VECTOR& m_vertex_types, CInifile* ini, LPCSTR section)
{
    XR_ASSERT(ini->section_exist(section), "can't open section", section);

    GameGraph::STerrainPlace terrain_mask;
    terrain_mask.tMask.resize(GameGraph::LOCATION_TYPE_COUNT);

    CInifile::Sect& sect = ini->r_section(section);
    for (const auto& I : sect.Data)
    {
        LPCSTR S = I.first.c_str();
        string16 I16;
        u32 N = _GetItemCount(S);

        if (N != GameGraph::LOCATION_TYPE_COUNT)
            continue;

        for (u32 j = 0; j < GameGraph::LOCATION_TYPE_COUNT; ++j)
        {
            const std::string_view val{_GetItem(S, j, I16)};
            const auto res = scn::scan_int<GameGraph::_LOCATION_ID>(val);
            XR_ASSERT(res, res.error().msg(), section, val);

            terrain_mask.tMask[j] = res->value();
        }

        m_vertex_types.push_back(terrain_mask);
    }

    if (!m_vertex_types.empty())
        return;

    for (u32 j = 0; j < GameGraph::LOCATION_TYPE_COUNT; ++j)
        terrain_mask.tMask[j] = 255;

    m_vertex_types.push_back(terrain_mask);
}

void setup_location_types_line(GameGraph::TERRAIN_VECTOR& m_vertex_types, LPCSTR string)
{
    string16 I;
    GameGraph::STerrainPlace terrain_mask;
    terrain_mask.tMask.resize(GameGraph::LOCATION_TYPE_COUNT);

    u32 N = _GetItemCount(string) / GameGraph::LOCATION_TYPE_COUNT * GameGraph::LOCATION_TYPE_COUNT;

    if (!N)
    {
        for (u32 j = 0; j < GameGraph::LOCATION_TYPE_COUNT; ++j)
            terrain_mask.tMask[j] = 255;
        m_vertex_types.push_back(terrain_mask);
        return;
    }

    m_vertex_types.reserve(32);

    for (u32 i = 0; i < N;)
    {
        for (u32 j = 0; j < GameGraph::LOCATION_TYPE_COUNT; ++j, ++i)
        {
            const std::string_view val{_GetItem(string, i, I)};
            const auto res = scn::scan_int<GameGraph::_LOCATION_ID>(val);
            XR_ASSERT(res, res.error().msg(), val);

            terrain_mask.tMask[j] = res->value();
        }

        m_vertex_types.push_back(terrain_mask);
    }
}
} // namespace

void setup_location_types(GameGraph::TERRAIN_VECTOR& m_vertex_types, CInifile* ini, LPCSTR string)
{
    m_vertex_types.clear();
    if (ini->section_exist(string) && ini->line_count(string))
        setup_location_types_section(m_vertex_types, ini, string);
    else
        setup_location_types_line(m_vertex_types, string);
}

//////////////////////////////////////////////////////////////////////////

// возможное отклонение от значения репутации
// заданого в профиле и для конкретного персонажа
#define REPUTATION_DELTA 10
#define RANK_DELTA 10

//////////////////////////////////////////////////////////////////////////

using namespace ALife;

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeTraderAbstract
////////////////////////////////////////////////////////////////////////////

CSE_ALifeTraderAbstract::CSE_ALifeTraderAbstract(LPCSTR caSection)
{
    m_dwMoney = 0;
    if (pSettings->line_exist(caSection, "money"))
        m_dwMoney = pSettings->r_u32(caSection, "money");

    m_fMaxItemMass = pSettings->r_float(caSection, "max_item_mass");
    m_sCharacterProfile._set(READ_IF_EXISTS(pSettings, r_string, caSection, "character_profile", "default"));

#ifdef XRGAME_EXPORTS
    m_community_index = NO_COMMUNITY_INDEX;
    m_rank = NO_RANK;
    m_reputation = NO_REPUTATION;
#endif

    m_trader_flags.zero();
    m_trader_flags.set(eTraderFlagInfiniteAmmo, FALSE);
}

CSE_Abstract* CSE_ALifeTraderAbstract::init() { return base(); }

CSE_ALifeTraderAbstract::~CSE_ALifeTraderAbstract() = default;

void CSE_ALifeTraderAbstract::__STATE_Write(NET_Packet& tNetPacket)
{
    tNetPacket.w_u32(m_dwMoney);

#ifdef XRGAME_EXPORTS
    tNetPacket.w_stringZ(m_SpecificCharacter);
#else
    shared_str s;
    tNetPacket.w_stringZ(s);
#endif

    tNetPacket.w_u32(m_trader_flags.get());
    tNetPacket.w_stringZ(m_sCharacterProfile);

#ifdef XRGAME_EXPORTS
    tNetPacket.w_s32(m_community_index);
    tNetPacket.w_s32(m_rank);
    tNetPacket.w_s32(m_reputation);
#else
    tNetPacket.w_s32(NO_COMMUNITY_INDEX);
    tNetPacket.w_s32(NO_RANK);
    tNetPacket.w_s32(NO_REPUTATION);
#endif

    save_data(m_character_name, tNetPacket);
}

void CSE_ALifeTraderAbstract::__STATE_Read(NET_Packet& tNetPacket, u16)
{
    u16 m_wVersion = base()->m_wVersion;
    if (m_wVersion > 19)
    {
        if (m_wVersion < 108)
            XR_ASSERT(tNetPacket.r_u32() == 0, "", m_wVersion);

        if (m_wVersion < 36)
        {
            xr_vector<u16> temp;
            load_data(temp, tNetPacket);
        }

        if (m_wVersion > 62)
            tNetPacket.r_u32(m_dwMoney);

        if ((m_wVersion > 75) && (m_wVersion < 98))
        {
            int tmp;
            tNetPacket.r_s32(tmp);
            if (tmp != -1)
                m_SpecificCharacter = CSpecificCharacter::IndexToId(tmp);
            else
                m_SpecificCharacter._set(nullptr);
        }
        else if (m_wVersion >= 98)
        {
            tNetPacket.r_stringZ(m_SpecificCharacter);
        }

        if (m_wVersion > 77)
            m_trader_flags.assign(tNetPacket.r_u32());

        if (m_wVersion > 81 && m_wVersion < 96)
        {
            int tmp;
            tNetPacket.r_s32(tmp);

            m_sCharacterProfile = CCharacterInfo::IndexToId(tmp);
            XR_ASSERT(xr_strlen(m_sCharacterProfile) > 0, "", m_sCharacterProfile);
        }
        else if (m_wVersion > 95)
        {
            tNetPacket.r_stringZ(m_sCharacterProfile);
        }

        if (m_wVersion > 85)
            tNetPacket.r_s32(m_community_index);

        if (m_wVersion > 86)
        {
            tNetPacket.r_s32(m_rank);
            tNetPacket.r_s32(m_reputation);
        }

        if (m_wVersion > 104)
            load_data(m_character_name, tNetPacket);
    }

#ifdef XRGAME_EXPORTS
    specific_character();
#endif
}

shared_str CSE_ALifeTraderAbstract::specific_character()
{
    if (!m_SpecificCharacter.empty())
        return m_SpecificCharacter;

    CCharacterInfo char_info;
    char_info.Load(character_profile());

    // профиль задан индексом
    if (char_info.data()->m_CharacterId.size())
    {
        set_specific_character(char_info.data()->m_CharacterId);
        return m_SpecificCharacter;
    }
    // профиль задан шаблоном
    //
    // проверяем все информации о персонаже, запоминаем подходящие,
    // а потом делаем случайный выбор
    else
    {
        m_CheckedCharacters.clear();
        m_DefaultCharacters.clear();

        for (int i = 0; i <= CSpecificCharacter::GetMaxIndex(); i++)
        {
            CSpecificCharacter spec_char;
            shared_str id = CSpecificCharacter::IndexToId(i);
            spec_char.Load(id);

            if (spec_char.data()->m_bNoRandom)
                continue;

            bool class_found = false;
            for (std::size_t j = 0; j < spec_char.data()->m_Classes.size(); j++)
            {
                if (char_info.data()->m_Class == spec_char.data()->m_Classes[j])
                {
                    class_found = true;
                    break;
                }
            }
            if (!char_info.data()->m_Class.size() || class_found)
            {
                // запомнить пподходящий персонаж с флажком m_bDefaultForCommunity
                if (spec_char.data()->m_bDefaultForCommunity)
                    m_DefaultCharacters.push_back(id);

                if (char_info.data()->m_Rank == NO_RANK || _abs(spec_char.Rank() - char_info.data()->m_Rank) < RANK_DELTA)
                {
                    if (char_info.data()->m_Reputation == NO_REPUTATION || _abs(spec_char.Reputation() - char_info.data()->m_Reputation) < REPUTATION_DELTA)
                    {
#ifdef XRGAME_EXPORTS
                        int* count{};
                        if (ai().get_alife())
                            count = ai().alife().registry(specific_characters).object(id, true);
                        // если индекс еще не был использован
                        if (!count)
#endif
                            m_CheckedCharacters.push_back(id);
                    }
                }
            }
        }

        XR_ASSERT(!m_DefaultCharacters.empty(), "no default specific character set for class", char_info.data()->m_Class);

#ifdef XRGAME_EXPORTS
        if (m_CheckedCharacters.empty())
            char_info.m_SpecificCharacterId = m_DefaultCharacters[Random.randI(m_DefaultCharacters.size())];
        else
            char_info.m_SpecificCharacterId = m_CheckedCharacters[Random.randI(m_CheckedCharacters.size())];
#else
        char_info.m_SpecificCharacterId = m_DefaultCharacters[Random.randI(m_DefaultCharacters.size())];
#endif

        set_specific_character(char_info.m_SpecificCharacterId);
        return m_SpecificCharacter;
    }
}

void CSE_ALifeTraderAbstract::set_specific_character(shared_str new_spec_char)
{
    XR_ASSERT(!new_spec_char.empty());

#ifdef XRGAME_EXPORTS
    // убрать предыдущий номер из реестра
    if (m_SpecificCharacter.size())
    {
        if (ai().get_alife())
            ai().alife().registry(specific_characters).remove(m_SpecificCharacter, true);
    }
#endif
    m_SpecificCharacter = new_spec_char;

#ifdef XRGAME_EXPORTS
    if (ai().get_alife())
    {
        // запомнить, то что мы использовали индекс
        int a = 1;
        ai().alife().registry(specific_characters).add(m_SpecificCharacter, a, true);
    }
#endif

    CSpecificCharacter selected_char;
    selected_char.Load(m_SpecificCharacter);

    if (selected_char.Visual() && xr_strlen(selected_char.Visual()) > 0)
        XR_ASSERT_VAL(smart_cast<CSE_Visual*>(base()) != nullptr)->set_visual(selected_char.Visual());

#ifdef XRGAME_EXPORTS
    if (NO_COMMUNITY_INDEX == m_community_index)
    {
        m_community_index = selected_char.Community().index();
        CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(base());
        if (creature)
            creature->s_team = selected_char.Community().team();
    }

    //----
    CSE_ALifeMonsterAbstract* monster = smart_cast<CSE_ALifeMonsterAbstract*>(base());
    if (monster && selected_char.terrain_sect().size())
        setup_location_types_section(monster->m_tpaTerrain, pSettings, selected_char.terrain_sect().c_str());
    //----

    if (NO_RANK == m_rank)
        m_rank = selected_char.Rank();

    if (NO_REPUTATION == m_reputation)
        m_reputation = selected_char.Reputation();

    m_character_name = CStringTable().translate(shared_str{selected_char.Name()});
    static constexpr std::string_view gen_name{"GENERATE_NAME_"};

    if (const auto pos = m_character_name.find(gen_name); pos != xr_string::npos)
    {
        // select name and lastname
        const auto subset = m_character_name.substr(pos + xr_strlen(gen_name));

        const auto t1 = xr::format("stalker_names_{}", subset);
        const auto name_cnt = pSettings->r_u32(t1.c_str(), "name_cnt");
        const auto last_name_cnt = pSettings->r_u32(t1.c_str(), "last_name_cnt");

        m_character_name = CStringTable().translate(shared_str{xr::format("name_{}_{}", subset, ::Random.randI(name_cnt))});
        m_character_name += " ";
        m_character_name += CStringTable().translate(shared_str{xr::format("lname_{}_{}", subset, ::Random.randI(last_name_cnt))});
    }

    u32 min_m = selected_char.MoneyDef().min_money;
    u32 max_m = selected_char.MoneyDef().max_money;
    if (min_m != 0 && max_m != 0)
    {
        m_dwMoney = min_m;
        if (min_m != max_m)
            m_dwMoney += ::Random.randI(max_m - min_m);
    }
#else
    // в редакторе специфический профиль оставляем не заполненым
    m_SpecificCharacter = NULL;
#endif
}

void CSE_ALifeTraderAbstract::set_character_profile(shared_str new_profile) { m_sCharacterProfile = new_profile; }
shared_str CSE_ALifeTraderAbstract::character_profile() { return m_sCharacterProfile; }

#ifdef XRGAME_EXPORTS
// для работы с relation system
u16 CSE_ALifeTraderAbstract::object_id() const { return base()->ID; }

CHARACTER_COMMUNITY_INDEX CSE_ALifeTraderAbstract::Community() const { return m_community_index; }
gsl::czstring CSE_ALifeTraderAbstract::CommunityName() const { return CHARACTER_COMMUNITY::IndexToId(m_community_index).c_str(); }

CHARACTER_RANK_VALUE CSE_ALifeTraderAbstract::Rank()
{
    specific_character();
    return m_rank;
}

void CSE_ALifeTraderAbstract::SetRank(CHARACTER_RANK_VALUE val)
{
    specific_character();
    m_rank = val;
}

CHARACTER_REPUTATION_VALUE CSE_ALifeTraderAbstract::Reputation()
{
    specific_character();
    return m_reputation;
}
#endif

void CSE_ALifeTraderAbstract::UPDATE_Write(NET_Packet&) {}
void CSE_ALifeTraderAbstract::UPDATE_Read(NET_Packet&) {}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeTrader
////////////////////////////////////////////////////////////////////////////

CSE_ALifeTrader::CSE_ALifeTrader(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection), CSE_ALifeTraderAbstract(caSection)
{
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));
}

CSE_ALifeTrader::~CSE_ALifeTrader() = default;

#ifdef DEBUG
bool CSE_ALifeTrader::match_configuration() const { return (!strstr(Core.Params, "-designer")); }
#endif

CSE_Abstract* CSE_ALifeTrader::init()
{
    std::ignore = inherited1::init();
    std::ignore = inherited2::init();

    return base();
}

CSE_Abstract* CSE_ALifeTrader::base() { return (inherited1::base()); }
const CSE_Abstract* CSE_ALifeTrader::base() const { return (inherited1::base()); }

void CSE_ALifeTrader::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
}

void CSE_ALifeTrader::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    inherited2::__STATE_Read(tNetPacket, size);

    if ((m_wVersion > 35) && (m_wVersion < 118))
        std::ignore = tNetPacket.r_u32();

    if ((m_wVersion > 29) && (m_wVersion < 118))
    {
        u32 l_dwCount = tNetPacket.r_u32();

        for (int i = 0; i < (int)l_dwCount; ++i)
        {
            shared_str temp;
            tNetPacket.r_stringZ(temp);
            std::ignore = tNetPacket.r_u32();

            for (int i = 0, n = tNetPacket.r_u32(); i < n; ++i)
            {
                tNetPacket.r_stringZ(temp);
                std::ignore = tNetPacket.r_u32();
                std::ignore = tNetPacket.r_u32();
            }
        }
    }

    if ((m_wVersion > 30) && (m_wVersion < 118))
    {
        u32 count = tNetPacket.r_u32();
        shared_str temp;

        for (u32 i = 0; i < count; ++i)
        {
            tNetPacket.r_stringZ(temp);
            std::ignore = tNetPacket.r_u32();
            std::ignore = tNetPacket.r_float();
            std::ignore = tNetPacket.r_float();
        }
    }
}

void CSE_ALifeTrader::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

void CSE_ALifeTrader::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
}

bool CSE_ALifeTrader::interactive() const { return (false); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeCustomZone
////////////////////////////////////////////////////////////////////////////

CSE_ALifeCustomZone::CSE_ALifeCustomZone(LPCSTR caSection) : CSE_ALifeSpaceRestrictor(caSection)
{
    m_owner_id = u32(-1);

    if (pSettings->line_exist(caSection, "hit_type"))
        m_tHitType = ALife::g_tfString2HitType(pSettings->r_string(caSection, "hit_type"));
    else
        m_tHitType = ALife::eHitTypeMax;

    m_enabled_time = 0;
    m_disabled_time = 0;
    m_start_time_shift = 0;
}

CSE_ALifeCustomZone::~CSE_ALifeCustomZone() = default;

void CSE_ALifeCustomZone::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::__STATE_Read(tNetPacket, size);

    float tmp;
    tNetPacket.r_float(tmp /*m_maxPower*/);

    if (m_wVersion < 113)
    {
        std::ignore = tNetPacket.r_float();
        std::ignore = tNetPacket.r_u32();
    }

    if ((m_wVersion > 66) && (m_wVersion < 118))
        std::ignore = tNetPacket.r_u32();

    if (m_wVersion > 102)
        tNetPacket.r_u32(m_owner_id);

    if (m_wVersion > 105)
    {
        tNetPacket.r_u32(m_enabled_time);
        tNetPacket.r_u32(m_disabled_time);
    }

    if (m_wVersion > 106)
        tNetPacket.r_u32(m_start_time_shift);
}

void CSE_ALifeCustomZone::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited::__STATE_Write(tNetPacket);
    tNetPacket.w_float(0.0 /*m_maxPower*/);
    tNetPacket.w_u32(m_owner_id);
    tNetPacket.w_u32(m_enabled_time);
    tNetPacket.w_u32(m_disabled_time);
    tNetPacket.w_u32(m_start_time_shift);
}

void CSE_ALifeCustomZone::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeCustomZone::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeAnomalousZone
////////////////////////////////////////////////////////////////////////////

CSE_ALifeAnomalousZone::CSE_ALifeAnomalousZone(LPCSTR caSection) : CSE_ALifeCustomZone(caSection)
{
    m_offline_interactive_radius = 30.f;
    m_artefact_spawn_count = 32;
    m_spawn_flags.set(flSpawnDestroyOnSpawn, TRUE);
}

CSE_Abstract* CSE_ALifeAnomalousZone::init()
{
    std::ignore = inherited::init();
    return base();
}

CSE_Abstract* CSE_ALifeAnomalousZone::base() { return (inherited::base()); }
const CSE_Abstract* CSE_ALifeAnomalousZone::base() const { return (inherited::base()); }

CSE_ALifeAnomalousZone::~CSE_ALifeAnomalousZone() = default;

u32 CSE_ALifeAnomalousZone::ef_anomaly_type() const { return (pSettings->r_u32(name(), "ef_anomaly_type")); }
u32 CSE_ALifeAnomalousZone::ef_weapon_type() const { return (pSettings->r_u32(name(), "ef_weapon_type")); }
u32 CSE_ALifeAnomalousZone::ef_creature_type() const { return (inherited::ef_weapon_type()); }

void CSE_ALifeAnomalousZone::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::__STATE_Read(tNetPacket, size);

    if (m_wVersion > 21)
    {
        tNetPacket.r_float(m_offline_interactive_radius);
        if (m_wVersion < 113)
        {
            std::ignore = tNetPacket.r_float();

            shared_str temp;
            for (u16 i = 0, n = tNetPacket.r_u16(); i < n; ++i)
            {
                tNetPacket.r_stringZ(temp);

                if (m_wVersion > 26)
                    std::ignore = tNetPacket.r_float();
                else
                    std::ignore = tNetPacket.r_u32();
            }
        }
    }

    if (m_wVersion > 25)
    {
        tNetPacket.r_u16(m_artefact_spawn_count);
        tNetPacket.r_u32(m_artefact_position_offset);
    }

    if ((m_wVersion < 67) && (m_wVersion > 27))
        std::ignore = tNetPacket.r_u32();

    if ((m_wVersion > 38) && (m_wVersion < 113))
        std::ignore = tNetPacket.r_float();

    if ((m_wVersion > 78) && (m_wVersion < 113))
    {
        std::ignore = tNetPacket.r_float();
        std::ignore = tNetPacket.r_float();
        std::ignore = tNetPacket.r_float();
    }

    if (m_wVersion == 102)
    {
        // fuck
        u32 dummy;
        tNetPacket.r_u32(dummy);
    }
}

void CSE_ALifeAnomalousZone::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited::__STATE_Write(tNetPacket);
    tNetPacket.w_float(m_offline_interactive_radius);
    tNetPacket.w_u16(m_artefact_spawn_count);
    tNetPacket.w_u32(m_artefact_position_offset);
}

void CSE_ALifeAnomalousZone::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeAnomalousZone::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

//////////////////////////////////////////////////////////////////////////
// SE_ALifeTorridZone
//////////////////////////////////////////////////////////////////////////

CSE_ALifeTorridZone::CSE_ALifeTorridZone(LPCSTR caSection) : CSE_ALifeCustomZone(caSection), CSE_Motion() {}
CSE_ALifeTorridZone::~CSE_ALifeTorridZone() = default;

CSE_Motion* CSE_ALifeTorridZone::motion() { return (this); }

void CSE_ALifeTorridZone::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    CSE_Motion::motion_read(tNetPacket);
    set_editor_flag(flMotionChange);
}

void CSE_ALifeTorridZone::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    CSE_Motion::motion_write(tNetPacket);
}

void CSE_ALifeTorridZone::UPDATE_Read(NET_Packet& tNetPacket) { inherited1::UPDATE_Read(tNetPacket); }
void CSE_ALifeTorridZone::UPDATE_Write(NET_Packet& tNetPacket) { inherited1::UPDATE_Write(tNetPacket); }

//////////////////////////////////////////////////////////////////////////
// CSE_ALifeZoneVisual
//////////////////////////////////////////////////////////////////////////

CSE_ALifeZoneVisual::CSE_ALifeZoneVisual(LPCSTR caSection) : CSE_ALifeAnomalousZone(caSection), CSE_Visual(caSection)
{
    if (pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));
}

CSE_ALifeZoneVisual::~CSE_ALifeZoneVisual() = default;

CSE_Visual* CSE_ALifeZoneVisual::visual() { return static_cast<CSE_Visual*>(this); }

void CSE_ALifeZoneVisual::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    visual_read(tNetPacket, m_wVersion);
    tNetPacket.r_stringZ(startup_animation);
    tNetPacket.r_stringZ(attack_animation);
}

void CSE_ALifeZoneVisual::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    visual_write(tNetPacket);
    tNetPacket.w_stringZ(startup_animation);
    tNetPacket.w_stringZ(attack_animation);
}

void CSE_ALifeZoneVisual::UPDATE_Read(NET_Packet& tNetPacket) { inherited1::UPDATE_Read(tNetPacket); }
void CSE_ALifeZoneVisual::UPDATE_Write(NET_Packet& tNetPacket) { inherited1::UPDATE_Write(tNetPacket); }

//-------------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////
// CSE_ALifeCreatureAbstract
////////////////////////////////////////////////////////////////////////////

CSE_ALifeCreatureAbstract::CSE_ALifeCreatureAbstract(LPCSTR caSection) : CSE_ALifeDynamicObjectVisual(caSection)
{
    s_team = s_squad = s_group = 0;
    o_model = 0.f;
    o_torso.pitch = 0.f;
    o_torso.yaw = 0.f;
    o_torso.roll = 0.f;
    fHealth = 1;
    m_bDeathIsProcessed = false;
    m_fAccuracy = 25.f;
    m_fIntelligence = 25.f;
    m_fMorale = 100.f;
    m_ef_creature_type = pSettings->r_u32(caSection, "ef_creature_type");
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, caSection, "ef_weapon_type", u32(-1));
    m_ef_detector_type = READ_IF_EXISTS(pSettings, r_u32, caSection, "ef_detector_type", u32(-1));
    m_killer_id = ALife::_OBJECT_ID(-1);
    m_game_death_time = 0;
    flags = 0;
}

CSE_ALifeCreatureAbstract::~CSE_ALifeCreatureAbstract() = default;

#ifdef DEBUG
bool CSE_ALifeCreatureAbstract::match_configuration() const { return (!strstr(Core.Params, "-designer")); }
#endif

u32 CSE_ALifeCreatureAbstract::ef_creature_type() const { return (m_ef_creature_type); }
u32 CSE_ALifeCreatureAbstract::ef_weapon_type() const { return XR_ASSERT_VAL(m_ef_weapon_type != std::numeric_limits<u32>::max()); }
u32 CSE_ALifeCreatureAbstract::ef_detector_type() const { return XR_ASSERT_VAL(m_ef_detector_type != std::numeric_limits<u32>::max()); }

#ifdef XRGAME_EXPORTS
void CSE_ALifeCreatureAbstract::__on_death(CSE_Abstract*)
{
    XR_ASSERT(m_game_death_time == 0);
    m_game_death_time = Level().GetGameTime();

    fHealth = -1.0f;
}

void CSE_ALifeCreatureAbstract::on_death(CSE_Abstract* killer)
{
    __on_death(killer);

    auto op = ops.find(server_ops::ON_DEATH);
    if (op == ops.end())
        return;

    op->second(this, killer);
}
#endif // XRGAME_EXPORTS

void CSE_ALifeCreatureAbstract::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited::__STATE_Write(tNetPacket);
    tNetPacket.w_u8(s_team);
    tNetPacket.w_u8(s_squad);
    tNetPacket.w_u8(s_group);
    tNetPacket.w_float(fHealth);
#ifdef DSH_MOD
    xr_vector<ALife::_OBJECT_ID> tmp;
    save_data(tmp, tNetPacket);
    save_data(tmp, tNetPacket);
#else
    save_data(m_dynamic_out_restrictions, tNetPacket);
    save_data(m_dynamic_in_restrictions, tNetPacket);
#endif
    tNetPacket.w(&m_killer_id, sizeof(m_killer_id));
    tNetPacket.w(&m_game_death_time, sizeof(m_game_death_time));
}

void CSE_ALifeCreatureAbstract::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::__STATE_Read(tNetPacket, size);
    tNetPacket.r_u8(s_team);
    tNetPacket.r_u8(s_squad);
    tNetPacket.r_u8(s_group);
    if (m_wVersion > 18)
        tNetPacket.r_float(fHealth);

    if (m_wVersion < 115)
        fHealth /= 100.0f;

    if (m_wVersion < 32)
        visual_read(tNetPacket, m_wVersion);
    o_model = o_torso.yaw;

    if (m_wVersion > 87)
    {
#ifdef DSH_MOD
        xr_vector<ALife::_OBJECT_ID> tmp1, tmp2;
        load_data(tmp1, tNetPacket);
        load_data(tmp2, tNetPacket);
#else
        load_data(m_dynamic_out_restrictions, tNetPacket);
        load_data(m_dynamic_in_restrictions, tNetPacket);
#endif
    }
    if (m_wVersion > 94)
        tNetPacket.r(&m_killer_id, sizeof(m_killer_id));

    o_torso.pitch = o_Angle.x;
    o_torso.yaw = o_Angle.y;

    if (m_wVersion > 115)
        tNetPacket.r(&m_game_death_time, sizeof(m_game_death_time));
}

void CSE_ALifeCreatureAbstract::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);

    tNetPacket.w_float(fHealth);

    tNetPacket.w_u32(timestamp);
    tNetPacket.w_u8(flags);
    tNetPacket.w_vec3(o_Position);
    tNetPacket.w_float /*w_angle8*/ (o_model);
    tNetPacket.w_float /*w_angle8*/ (o_torso.yaw);
    tNetPacket.w_float /*w_angle8*/ (o_torso.pitch);
    tNetPacket.w_float /*w_angle8*/ (o_torso.roll);
    tNetPacket.w_u8(s_team);
    tNetPacket.w_u8(s_squad);
    tNetPacket.w_u8(s_group);
}

void CSE_ALifeCreatureAbstract::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);

    tNetPacket.r_float(fHealth);

    tNetPacket.r_u32(timestamp);
    tNetPacket.r_u8(flags);
    tNetPacket.r_vec3(o_Position);
    tNetPacket.r_float /*r_angle8*/ (o_model);
    tNetPacket.r_float /*r_angle8*/ (o_torso.yaw);
    tNetPacket.r_float /*r_angle8*/ (o_torso.pitch);
    tNetPacket.r_float /*r_angle8*/ (o_torso.roll);

    tNetPacket.r_u8(s_team);
    tNetPacket.r_u8(s_squad);
    tNetPacket.r_u8(s_group);
}

u8 CSE_ALifeCreatureAbstract::g_team() { return s_team; }
u8 CSE_ALifeCreatureAbstract::g_squad() { return s_squad; }
u8 CSE_ALifeCreatureAbstract::g_group() { return s_group; }

bool CSE_ALifeCreatureAbstract::used_ai_locations() const { return (true); }
bool CSE_ALifeCreatureAbstract::__can_switch_online() const { return inherited::__can_switch_online(); }
bool CSE_ALifeCreatureAbstract::__can_switch_offline() const { return inherited::__can_switch_offline() && g_Health() > 0.f; }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeMonsterAbstract
////////////////////////////////////////////////////////////////////////////

CSE_ALifeMonsterAbstract::CSE_ALifeMonsterAbstract(LPCSTR caSection) : CSE_ALifeCreatureAbstract{caSection}, CSE_ALifeSchedulable{caSection}
{
    m_group_id = 0xffff;
    m_tNextGraphID = m_tGraphID;
    m_tPrevGraphID = m_tGraphID;
    m_fCurSpeed = 0.0f;
    m_fDistanceFromPoint = 0.0f;
    m_fDistanceToPoint = 0.0f;

    m_fMaxHealthValue = pSettings->r_float(caSection, "MaxHealthValue");
    if (pSettings->line_exist(caSection, "hit_power"))
    {
        m_fHitPower = pSettings->r_float(caSection, "hit_power");
        m_tHitType = ALife::g_tfString2HitType(pSettings->r_string(caSection, "hit_type"));
    }
    else
    {
        m_fHitPower = 0;
        m_tHitType = ALife::eHitTypeMax;
    }

    {
        string64 S;
        m_fpImmunityFactors.resize(ALife::eHitTypeMax);
        auto B = m_fpImmunityFactors.begin(), I = B;
        auto E = m_fpImmunityFactors.end();

        LPCSTR imm_section = caSection;
        if (pSettings->line_exist(caSection, "immunities_sect"))
            imm_section = pSettings->r_string(caSection, "immunities_sect");
        for (; I != E; ++I)
            *I = READ_IF_EXISTS(pSettings, r_float, imm_section, strcat(strcpy(S, ALife::g_cafHitType2String(ALife::EHitType(I - B))), "_immunity"), 1.f);
    }

    if (pSettings->line_exist(caSection, "retreat_threshold"))
        m_fRetreatThreshold = pSettings->r_float(caSection, "retreat_threshold");
    else
        m_fRetreatThreshold = 0.2f;

    m_fEyeRange = pSettings->r_float(caSection, "eye_range");

    m_fGoingSpeed = pSettings->r_float(caSection, "going_speed");
    m_fCurrentLevelGoingSpeed = READ_IF_EXISTS(pSettings, r_float, caSection, "current_level_going_speed", m_fGoingSpeed);

    setup_location_types(m_tpaTerrain, pSettings, pSettings->r_string(caSection, "terrain"));

    m_tpBestDetector = this;

    m_smart_terrain_id = 0xffff;
    m_task_reached = false;

    m_rank = (pSettings->line_exist(caSection, "rank")) ? pSettings->r_s32(caSection, "rank") : 0;

#ifdef XRGAME_EXPORTS
    m_stay_after_death_time_interval = generate_time(1, 1, 1, pSettings->r_u32("monsters_common", "stay_after_death_time_interval"), 0, 0);
#endif // XRGAME_EXPORTS
}

CSE_ALifeMonsterAbstract::~CSE_ALifeMonsterAbstract() { xr_delete(m_brain); }

CALifeMonsterBrain* CSE_ALifeMonsterAbstract::create_brain() { return (xr_new<CALifeMonsterBrain>(this)); }

CSE_Abstract* CSE_ALifeMonsterAbstract::init()
{
    std::ignore = inherited1::init();
    std::ignore = inherited2::init();

    if (spawn_ini().section_exist("alife") && spawn_ini().line_exist("alife", "terrain"))
        setup_location_types(m_tpaTerrain, &spawn_ini(), spawn_ini().r_string("alife", "terrain"));

    m_brain = create_brain();

    return base();
}

CSE_Abstract* CSE_ALifeMonsterAbstract::base() { return (inherited1::base()); }

u32 CSE_ALifeMonsterAbstract::ef_creature_type() const { return (inherited1::ef_creature_type()); }
u32 CSE_ALifeMonsterAbstract::ef_weapon_type() const { return (inherited1::ef_weapon_type()); }
u32 CSE_ALifeMonsterAbstract::ef_detector_type() const { return (inherited1::ef_detector_type()); }

const CSE_Abstract* CSE_ALifeMonsterAbstract::base() const { return (inherited1::base()); }

void CSE_ALifeMonsterAbstract::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    tNetPacket.w_stringZ(m_out_space_restrictors);
    tNetPacket.w_stringZ(m_in_space_restrictors);
    tNetPacket.w(&m_smart_terrain_id, sizeof(m_smart_terrain_id));
    tNetPacket.w(&m_task_reached, sizeof(m_task_reached));
}

void CSE_ALifeMonsterAbstract::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    if (m_wVersion > 72)
    {
        tNetPacket.r_stringZ(m_out_space_restrictors);
        if (m_wVersion > 73)
            tNetPacket.r_stringZ(m_in_space_restrictors);
    }

    if (m_wVersion > 111)
        tNetPacket.r(&m_smart_terrain_id, sizeof(m_smart_terrain_id));

    if (m_wVersion > 113)
        tNetPacket.r(&m_task_reached, sizeof(m_task_reached));
}

void CSE_ALifeMonsterAbstract::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);

    tNetPacket.w(&m_tNextGraphID, sizeof(m_tNextGraphID));
    tNetPacket.w(&m_tPrevGraphID, sizeof(m_tPrevGraphID));
    tNetPacket.w(&m_fDistanceFromPoint, sizeof(m_fDistanceFromPoint));
    tNetPacket.w(&m_fDistanceToPoint, sizeof(m_fDistanceToPoint));
}

void CSE_ALifeMonsterAbstract::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);

    tNetPacket.r(&m_tNextGraphID, sizeof(m_tNextGraphID));
    tNetPacket.r(&m_tPrevGraphID, sizeof(m_tPrevGraphID));
    tNetPacket.r(&m_fDistanceFromPoint, sizeof(m_fDistanceFromPoint));
    tNetPacket.r(&m_fDistanceToPoint, sizeof(m_fDistanceToPoint));
}

bool CSE_ALifeMonsterAbstract::need_update(CSE_ALifeDynamicObject* object) { return (CSE_ALifeSchedulable::need_update(object) && (fHealth > EPS_L)); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeCreatureActor
////////////////////////////////////////////////////////////////////////////

CSE_ALifeCreatureActor::CSE_ALifeCreatureActor(LPCSTR caSection)
    : CSE_ALifeCreatureAbstract(caSection), CSE_ALifeTraderAbstract(caSection), CSE_PHSkeleton(caSection)
{
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));

    m_u16NumItems = 0;
    fRadiation = 0.f;
    accel.set(0.f, 0.f, 0.f);
    velocity.set(0.f, 0.f, 0.f);
    m_holderID = u16(-1);
}

CSE_ALifeCreatureActor::~CSE_ALifeCreatureActor() = default;

#ifdef DEBUG
bool CSE_ALifeCreatureActor::match_configuration() const { return (true); }
#endif

CSE_Abstract* CSE_ALifeCreatureActor::init()
{
    std::ignore = inherited1::init();
    std::ignore = inherited2::init();

    return inherited1::base();
}

CSE_Abstract* CSE_ALifeCreatureActor::base() { return (inherited1::base()); }
const CSE_Abstract* CSE_ALifeCreatureActor::base() const { return (inherited1::base()); }

void CSE_ALifeCreatureActor::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    if (m_wVersion < 21)
    {
        CSE_ALifeDynamicObject::__STATE_Read(tNetPacket, size);
        tNetPacket.r_u8(s_team);
        tNetPacket.r_u8(s_squad);
        tNetPacket.r_u8(s_group);
        if (m_wVersion > 18)
            tNetPacket.r_float(fHealth);
        if (m_wVersion >= 3)
            visual_read(tNetPacket, m_wVersion);
    }
    else
    {
        inherited1::__STATE_Read(tNetPacket, size);
        inherited2::__STATE_Read(tNetPacket, size);
        if (m_wVersion < 32)
            visual_read(tNetPacket, m_wVersion);
    }
    if (m_wVersion > 91)
    {
        inherited3::__STATE_Read(tNetPacket, size);
    }
    if (m_wVersion > 88)
    {
        m_holderID = tNetPacket.r_u16();
    }
}

void CSE_ALifeCreatureActor::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
    inherited3::__STATE_Write(tNetPacket);
    tNetPacket.w_u16(m_holderID);
}

void CSE_ALifeCreatureActor::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited3::load(tNetPacket);
    m_holderID = tNetPacket.r_u16();
}

void CSE_ALifeCreatureActor::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
    tNetPacket.r_u16(mstate);
    tNetPacket.r_sdir(accel);
    tNetPacket.r_sdir(velocity);
    tNetPacket.r_float(fRadiation);
    tNetPacket.r_u8(weapon);
    ////////////////////////////////////////////////////
    tNetPacket.r_u16(m_u16NumItems);

    if (!m_u16NumItems)
        return;

    if (m_u16NumItems == 1)
    {
        tNetPacket.r_u8(*((u8*)&(m_AliveState.enabled)));

        tNetPacket.r_vec3(m_AliveState.angular_vel);
        tNetPacket.r_vec3(m_AliveState.linear_vel);

        tNetPacket.r_vec3(m_AliveState.force);
        tNetPacket.r_vec3(m_AliveState.torque);

        tNetPacket.r_vec3(m_AliveState.position);

        tNetPacket.r_float(m_AliveState.quaternion.x);
        tNetPacket.r_float(m_AliveState.quaternion.y);
        tNetPacket.r_float(m_AliveState.quaternion.z);
        tNetPacket.r_float(m_AliveState.quaternion.w);

        return;
    }

    ////////////// Import dead body ////////////////////
    XR_LOG_WARNING("A mi ni hera tut ne chitaem (m_u16NumItems == {})", m_u16NumItems);

    m_BoneDataSize = tNetPacket.r_u8();
    u32 BodyDataSize = 24 + m_BoneDataSize * m_u16NumItems;
    tNetPacket.r(m_DeadBodyData, BodyDataSize);
}

void CSE_ALifeCreatureActor::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
    tNetPacket.w_u16(mstate);
    tNetPacket.w_sdir(accel);
    tNetPacket.w_sdir(velocity);
    tNetPacket.w_float(fRadiation);
    tNetPacket.w_u8(weapon);
    ////////////////////////////////////////////////////
    tNetPacket.w_u16(m_u16NumItems);
    if (!m_u16NumItems)
        return;

    if (m_u16NumItems == 1)
    {
        tNetPacket.w_u8(m_AliveState.enabled);

        tNetPacket.w_vec3(m_AliveState.angular_vel);
        tNetPacket.w_vec3(m_AliveState.linear_vel);

        tNetPacket.w_vec3(m_AliveState.force);
        tNetPacket.w_vec3(m_AliveState.torque);

        tNetPacket.w_vec3(m_AliveState.position);

        tNetPacket.w_float(m_AliveState.quaternion.x);
        tNetPacket.w_float(m_AliveState.quaternion.y);
        tNetPacket.w_float(m_AliveState.quaternion.z);
        tNetPacket.w_float(m_AliveState.quaternion.w);

        return;
    }

    ////////////// Export dead body ////////////////////
    {
        tNetPacket.w_u8(m_BoneDataSize);
        u32 BodyDataSize = 24 + m_BoneDataSize * m_u16NumItems;
        tNetPacket.w(m_DeadBodyData, BodyDataSize);
    }
}

#ifdef XRGAME_EXPORTS
void CSE_ALifeCreatureActor::spawn_supplies()
{
    inherited1::spawn_supplies();
    inherited2::spawn_supplies();
}
#endif

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeCreatureCrow
////////////////////////////////////////////////////////////////////////////

CSE_ALifeCreatureCrow::CSE_ALifeCreatureCrow(LPCSTR caSection) : CSE_ALifeCreatureAbstract(caSection)
{
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));
    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
}

CSE_ALifeCreatureCrow::~CSE_ALifeCreatureCrow() = default;

void CSE_ALifeCreatureCrow::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    if (m_wVersion > 20)
    {
        inherited::__STATE_Read(tNetPacket, size);
        if (m_wVersion < 32)
            visual_read(tNetPacket, m_wVersion);
    }
}

void CSE_ALifeCreatureCrow::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }

void CSE_ALifeCreatureCrow::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeCreatureCrow::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

bool CSE_ALifeCreatureCrow::used_ai_locations() const { return (false); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeCreaturePhantom
////////////////////////////////////////////////////////////////////////////

CSE_ALifeCreaturePhantom::CSE_ALifeCreaturePhantom(LPCSTR caSection) : CSE_ALifeCreatureAbstract(caSection)
{
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));

    m_flags.set(flUseSwitches, FALSE);
    m_flags.set(flSwitchOffline, FALSE);
}

CSE_ALifeCreaturePhantom::~CSE_ALifeCreaturePhantom() = default;

void CSE_ALifeCreaturePhantom::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::__STATE_Read(tNetPacket, size); }
void CSE_ALifeCreaturePhantom::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }

void CSE_ALifeCreaturePhantom::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeCreaturePhantom::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

bool CSE_ALifeCreaturePhantom::used_ai_locations() const { return (false); }

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeMonsterZombie
////////////////////////////////////////////////////////////////////////////

CSE_ALifeMonsterZombie::CSE_ALifeMonsterZombie(LPCSTR caSection) : CSE_ALifeMonsterAbstract(caSection)
{
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));

    // personal charactersitics
    fEyeFov = 120;
    fEyeRange = 30;
    fHealth = 200;
    fMinSpeed = 1.5;
    fMaxSpeed = 1.75;
    fAttackSpeed = 2.0;
    fMaxPursuitRadius = 100;
    fMaxHomeRadius = 30;
    // attack
    fHitPower = 20.0;
    u16HitInterval = 1000;
    fAttackDistance = 1.0f;
    fAttackAngle = 15;
}

CSE_ALifeMonsterZombie::~CSE_ALifeMonsterZombie() = default;

void CSE_ALifeMonsterZombie::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    // inherited properties
    inherited::__STATE_Read(tNetPacket, size);

    // personal characteristics
    tNetPacket.r_float(fEyeFov);
    tNetPacket.r_float(fEyeRange);

    if (m_wVersion <= 5)
        tNetPacket.r_float(fHealth);

    tNetPacket.r_float(fMinSpeed);
    tNetPacket.r_float(fMaxSpeed);
    tNetPacket.r_float(fAttackSpeed);
    tNetPacket.r_float(fMaxPursuitRadius);
    tNetPacket.r_float(fMaxHomeRadius);

    // attack
    tNetPacket.r_float(fHitPower);
    tNetPacket.r_u16(u16HitInterval);
    tNetPacket.r_float(fAttackDistance);
    tNetPacket.r_float(fAttackAngle);
}

void CSE_ALifeMonsterZombie::__STATE_Write(NET_Packet& tNetPacket)
{
    // inherited properties
    inherited::__STATE_Write(tNetPacket);

    // personal characteristics
    tNetPacket.w_float(fEyeFov);
    tNetPacket.w_float(fEyeRange);
    tNetPacket.w_float(fMinSpeed);
    tNetPacket.w_float(fMaxSpeed);
    tNetPacket.w_float(fAttackSpeed);
    tNetPacket.w_float(fMaxPursuitRadius);
    tNetPacket.w_float(fMaxHomeRadius);

    // attack
    tNetPacket.w_float(fHitPower);
    tNetPacket.w_u16(u16HitInterval);
    tNetPacket.w_float(fAttackDistance);
    tNetPacket.w_float(fAttackAngle);
}

void CSE_ALifeMonsterZombie::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeMonsterZombie::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

//////////////////////////////////////////////////////////////////////////
// CSE_ALifeMonsterBase
//////////////////////////////////////////////////////////////////////////

CSE_ALifeMonsterBase::CSE_ALifeMonsterBase(LPCSTR caSection) : CSE_ALifeMonsterAbstract(caSection), CSE_PHSkeleton(caSection)
{
    set_visual(pSettings->r_string(caSection, "visual"));
    m_spec_object_id = 0xffff;
}

CSE_ALifeMonsterBase::~CSE_ALifeMonsterBase() = default;

void CSE_ALifeMonsterBase::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);

    if (m_wVersion >= 68)
        inherited2::__STATE_Read(tNetPacket, size);

    if (m_wVersion >= 109)
        tNetPacket.r_u16(m_spec_object_id);
}

void CSE_ALifeMonsterBase::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);

    tNetPacket.w_u16(m_spec_object_id);
}

void CSE_ALifeMonsterBase::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
}

void CSE_ALifeMonsterBase::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

void CSE_ALifeMonsterBase::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited2::load(tNetPacket);
}

//////////////////////////////////////////////////////////////////////////
// CSE_ALifePsyDogPhantom
//////////////////////////////////////////////////////////////////////////

CSE_ALifePsyDogPhantom::CSE_ALifePsyDogPhantom(LPCSTR caSection) : CSE_ALifeMonsterBase(caSection) {}
CSE_ALifePsyDogPhantom::~CSE_ALifePsyDogPhantom() = default;

void CSE_ALifePsyDogPhantom::__STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::__STATE_Read(tNetPacket, size); }
void CSE_ALifePsyDogPhantom::__STATE_Write(NET_Packet& tNetPacket) { inherited::__STATE_Write(tNetPacket); }

void CSE_ALifePsyDogPhantom::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifePsyDogPhantom::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }

//////////////////////////////////////////////////////////////////////////
// CSE_ALifeHumanAbstract
//////////////////////////////////////////////////////////////////////////

CSE_ALifeHumanAbstract::CSE_ALifeHumanAbstract(LPCSTR caSection) : CSE_ALifeTraderAbstract{caSection}, CSE_ALifeMonsterAbstract{caSection} {}
CSE_ALifeHumanAbstract::~CSE_ALifeHumanAbstract() = default;

CALifeMonsterBrain* CSE_ALifeHumanAbstract::create_brain()
{
    m_brain = xr_new<CALifeHumanBrain>(this);
    return m_brain;
}

CSE_Abstract* CSE_ALifeHumanAbstract::init()
{
    std::ignore = inherited1::init();
    std::ignore = inherited2::init();

    return base();
}

CSE_Abstract* CSE_ALifeHumanAbstract::base() { return (inherited2::base()); }
const CSE_Abstract* CSE_ALifeHumanAbstract::base() const { return (inherited2::base()); }

void CSE_ALifeHumanAbstract::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
    brain().on_state_write(tNetPacket);
}

void CSE_ALifeHumanAbstract::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);
    inherited2::__STATE_Read(tNetPacket, size);
    brain().on_state_read(tNetPacket);
    if ((m_wVersion >= 110) && (m_wVersion < 112))
        tNetPacket.r(&m_smart_terrain_id, sizeof(m_smart_terrain_id));
}

void CSE_ALifeHumanAbstract::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
}

void CSE_ALifeHumanAbstract::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);

    if (m_wVersion < 110)
    {
        std::ignore = tNetPacket.r_u32();
        std::ignore = tNetPacket.r_u32();
        std::ignore = tNetPacket.r_u32();
    }
}

//////////////////////////////////////////////////////////////////////////
// CSE_ALifeHumanStalker
//////////////////////////////////////////////////////////////////////////

CSE_ALifeHumanStalker::CSE_ALifeHumanStalker(LPCSTR caSection) : CSE_ALifeHumanAbstract{caSection}, CSE_PHSkeleton{caSection}
{
    m_trader_flags.set(eTraderFlagInfiniteAmmo, TRUE);
    m_start_dialog._set("");
}

CSE_ALifeHumanStalker::~CSE_ALifeHumanStalker() = default;

void CSE_ALifeHumanStalker::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);
    inherited2::__STATE_Write(tNetPacket);
}

void CSE_ALifeHumanStalker::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);

    if (m_wVersion > 67)
        inherited2::__STATE_Read(tNetPacket, size);

    if ((m_wVersion > 90) && (m_wVersion < 111))
        std::ignore = tNetPacket.r_u8();
}

void CSE_ALifeHumanStalker::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);
    tNetPacket.w_stringZ(m_start_dialog);
}

void CSE_ALifeHumanStalker::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);
    tNetPacket.r_stringZ(m_start_dialog);
}

void CSE_ALifeHumanStalker::load(NET_Packet& tNetPacket)
{
    inherited1::load(tNetPacket);
    inherited2::load(tNetPacket);
}

//////////////////////////////////////////////////////////////////////////
// CSE_ALifeOnlineOfflineGroup
//////////////////////////////////////////////////////////////////////////

CSE_ALifeOnlineOfflineGroup::CSE_ALifeOnlineOfflineGroup(LPCSTR caSection) : CSE_ALifeDynamicObject(caSection), CSE_ALifeSchedulable(caSection) {}

CSE_ALifeOnlineOfflineGroup::~CSE_ALifeOnlineOfflineGroup()
{
#ifdef XRGAME_EXPORTS
    xr_delete(m_brain);
#endif
}

CSE_Abstract* CSE_ALifeOnlineOfflineGroup::base() { return (this); }
const CSE_Abstract* CSE_ALifeOnlineOfflineGroup::base() const { return (this); }

CSE_Abstract* CSE_ALifeOnlineOfflineGroup::init()
{
    std::ignore = inherited1::init();
    std::ignore = inherited2::init();

#ifdef XRGAME_EXPORTS
    m_brain = xr_new<CALifeOnlineOfflineGroupBrain>(this);
#endif

    XR_ASSERT(m_members.size() == 0, "", m_members);
    m_flags.set(flUsedAI_Locations, FALSE);

    return this;
}

void CSE_ALifeOnlineOfflineGroup::__STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::__STATE_Write(tNetPacket);

    tNetPacket.w_u32(m_members.size());

    MEMBERS::iterator I = m_members.begin();
    MEMBERS::iterator E = m_members.end();
    for (; I != E; ++I)
        save_data((*I).first, tNetPacket);
}

void CSE_ALifeOnlineOfflineGroup::__STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::__STATE_Read(tNetPacket, size);

    u32 container_size = tNetPacket.r_u32();
    for (u32 i = 0; i < container_size; ++i)
    {
        MEMBERS::key_type first;
        load_data(first, tNetPacket);
        m_members.emplace(first, nullptr);
    }
}

void CSE_ALifeOnlineOfflineGroup::UPDATE_Write(NET_Packet& tNetPacket) { inherited1::UPDATE_Write(tNetPacket); }
void CSE_ALifeOnlineOfflineGroup::UPDATE_Read(NET_Packet& tNetPacket) { inherited1::UPDATE_Read(tNetPacket); }
