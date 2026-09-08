////////////////////////////////////////////////////////////////////////////
//	Module 		: script_binder.cpp
//	Created 	: 26.03.2004
//  Modified 	: 26.03.2004
//	Author		: Dmitriy Iassenev
//	Description : Script objects binder
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "script_binder.h"

#include "ai_space.h"
#include "script_engine.h"
#include "xrServer_Objects_ALife.h"
#include "script_binder_object.h"
#include "script_game_object.h"
#include "GameObject.h"
#include "Level.h"

CScriptBinder::CScriptBinder() { init(); }
CScriptBinder::~CScriptBinder() { VERIFY(!m_object); }

void CScriptBinder::init() { m_object = nullptr; }

void CScriptBinder::clear()
{
    xr_delete(m_object);
    init();
}

void CScriptBinder::reinit()
{
    if (m_object)
        m_object->reinit();
}

void CScriptBinder::Load(LPCSTR) {}

void CScriptBinder::reload(LPCSTR section)
{
    VERIFY(!m_object);

    if (!pSettings->line_exist(section, "script_binding"))
        return;

    auto script_func_name = pSettings->r_string(section, "script_binding");
    sol::function lua_function;
    if (!ai().script_engine().function(script_func_name, lua_function))
    {
        Msg("!![CScriptBinder::reload] function [{}] not loaded!", script_func_name);
        return;
    }

    auto game_object = smart_cast<CGameObject*>(this);
    if (!game_object) // Объекта нет - значит тут делать нечего.
    {
        XR_LOG_ERROR("Not a game object");
        return;
    }

    lua_function(game_object->lua_game_object());

    if (m_object)
    {
        m_object->reload(section);
    }
}

tmc::task<bool> CScriptBinder::net_Spawn(CSE_Abstract* DC)
{
    CSE_Abstract* abstract = (CSE_Abstract*)DC;

    if (m_object == nullptr)
        co_return true;

    auto object = smart_cast<CSE_ALifeObject*>(abstract);
    if (object != nullptr)
        co_return co_await m_object->net_Spawn(object);

    co_return true;
}

tmc::task<void> CScriptBinder::net_Destroy()
{
    if (m_object != nullptr)
    {
#ifdef DEBUG
        Msg("* Core object {} is UNbinded from the script object",
            smart_cast<CGameObject*>(this) != nullptr ? std::string_view{smart_cast<CGameObject*>(this)->cName()} : std::string_view{});
#endif // DEBUG

        co_await m_object->net_Destroy();
    }

    xr_delete(m_object);
}

void CScriptBinder::set_object(CScriptBinderObject* object)
{
    VERIFY2(!m_object, "Cannot bind to the object twice!");

#ifdef DEBUG
    Msg("* Core object {} is binded with the script object",
        smart_cast<CGameObject*>(this) != nullptr ? std::string_view{smart_cast<CGameObject*>(this)->cName()} : std::string_view{});
#endif // DEBUG

    m_object = object;
}

tmc::task<void> CScriptBinder::shedule_Update(u32 time_delta)
{
    if (m_object != nullptr)
        co_await m_object->shedule_Update(time_delta);
}

void CScriptBinder::save(NET_Packet& output_packet)
{
    if (m_object)
    {
        m_object->save(&output_packet);
    }
}

void CScriptBinder::load(IReader& input_packet)
{
    if (m_object)
    {
        m_object->load(&input_packet);
    }
}

BOOL CScriptBinder::net_SaveRelevant()
{
    if (m_object)
    {
        return m_object->net_SaveRelevant();
    }
    return FALSE;
}

void CScriptBinder::net_Relcase(CObject* object)
{
    if (!m_object)
        return;

    CGameObject* game_object = smart_cast<CGameObject*>(object);
    if (game_object)
    {
        m_object->net_Relcase(game_object->lua_game_object());
    }
}
