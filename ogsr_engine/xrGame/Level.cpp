#include "stdafx.h"

#include "Level.h"

#include "../xr_3da/FDemoRecord.h"
#include "../xr_3da/FDemoPlay.h"
#include "../xr_3da/Environment.h"
#include "../xr_3da/IGame_Persistent.h"
#include "ParticlesObject.h"
#include "xrServer.h"
#include "NET_Queue.h"
#include "game_cl_base.h"
#include "entity_alive.h"
#include "HUDManager.h"
#include "ai_space.h"
#include "ai_debug.h"
#include "PHDynamicData.h"
#include "Physics.h"
#include "ShootingObject.h"
#include "Level_Bullet_Manager.h"
#include "script_engine.h"
#include "InfoPortion.h"
#include "patrol_path_storage.h"
#include "date_time.h"
#include "space_restriction_manager.h"
#include "seniority_hierarchy_holder.h"
#include "space_restrictor.h"
#include "client_spawn_manager.h"
#include "ClimableObject.h"
#include "level_graph.h"
#include "mt_config.h"
#include "PHCommander.h"
#include "map_manager.h"
#include "../xr_3da/CameraManager.h"
#include "level_sounds.h"
#include "Car.h"
#include "trade_parameters.h"
#include "clsid_game.h"
#include "MainMenu.h"
#include "../xr_3da/XR_IOConsole.h"
#include "debug_renderer.h"
#include "Actor_Flags.h"
#include "level_changer.h"
#include "player_hud.h"
#include "Actor.h"
#include "PDA.h"

#ifdef DEBUG
#include "level_debug.h"
#include "ai/stalker/ai_stalker.h"
#endif

#include "LevelDebugScript.h"
#include "PhysicObject.h"
#include "UIGameSP.h"
#include "ui/UIPdaWnd.h"
#include "ui/UIBtnHint.h"

extern CAI_Space* g_ai_space;

CPHWorld* ph_world{};

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLevel::CLevel() : IPureClient{Device.GetTimerGlobal()}
{
    g_bDebugEvents = !!strstr(Core.Params, "-debug_ge");

    //	game						= xr_new<game_cl_GameState>();
    game_events = xr_new<NET_Queue_Event>();

    eChangeRP = Engine.Event.handler_attach_locked("LEVEL:ChangeRP", this);
    eDemoPlay = Engine.Event.handler_attach_locked("LEVEL:PlayDEMO", this);
    eChangeTrack = Engine.Event.handler_attach_locked("LEVEL:PlayMusic", this);
    eEnvironment = Engine.Event.handler_attach_locked("LEVEL:Environment", this);
    eEntitySpawn = Engine.Event.handler_attach_locked("LEVEL:spawn", this);

    m_pBulletManager = xr_new<CBulletManager>();
    m_map_manager = xr_new<CMapManager>();

    physics_step_time_callback = (PhysicsStepTimeCallback*)&PhisStepsCallback;
    m_seniority_hierarchy_holder = xr_new<CSeniorityHierarchyHolder>();

    m_level_sound_manager = xr_new<CLevelSoundManager>();
    m_space_restriction_manager = xr_new<CSpaceRestrictionManager>();
    m_client_spawn_manager = xr_new<CClientSpawnManager>();

    m_debug_renderer = xr_new<CDebugRenderer>();
#ifdef DEBUG
    m_level_debug = xr_new<CLevelDebug>();
#endif

    m_ph_commander = xr_new<CPHCommander>();
    m_ph_commander_scripts = xr_new<CPHCommander>();

    g_player_hud = xr_new<player_hud>();
    g_player_hud->load_default();
}

CLevel::~CLevel()
{
    XR_LOG_NOTICE("Destroying level");

    xr_delete(g_player_hud);

    Engine.Event.handler_detach_locked(eEntitySpawn, this);
    Engine.Event.handler_detach_locked(eEnvironment, this);
    Engine.Event.handler_detach_locked(eChangeTrack, this);
    Engine.Event.handler_detach_locked(eDemoPlay, this);
    Engine.Event.handler_detach_locked(eChangeRP, this);

    if (ph_world)
    {
        ph_world->Destroy();
        xr_delete(ph_world);
    }

    // destroy PSs
    for (POIt p_it = m_StaticParticles.begin(); m_StaticParticles.end() != p_it; ++p_it)
        CParticlesObject::Destroy(*p_it);
    m_StaticParticles.clear();

    // Unload sounds
    // unload prefetched sounds
    sound_registry.clear();
    sound_registry_defer.clear();

    xr_delete(m_level_sound_manager);
    xr_delete(m_space_restriction_manager);
    xr_delete(m_seniority_hierarchy_holder);
    xr_delete(m_client_spawn_manager);
    xr_delete(m_debug_renderer);

    xr_delete(game);
    xr_delete(game_events);

    // by Dandy
    // destroy fog of war
    //	xr_delete					(m_pFogOfWar);
    // destroy bullet manager
    xr_delete(m_pBulletManager);
    //-----------------------------------------------------------
    xr_delete(pStatGraphR);
    xr_delete(pStatGraphS);

    //-----------------------------------------------------------
    xr_delete(m_ph_commander);
    xr_delete(m_ph_commander_scripts);
    //-----------------------------------------------------------

    ai().unload();
    //-----------------------------------------------------------
#ifdef DEBUG
    xr_delete(m_level_debug);
#endif
    //-----------------------------------------------------------
    xr_delete(m_map_manager);
    //	xr_delete					(m_pFogOfWarMngr);
    //-----------------------------------------------------------

    // here we clean default trade params
    // because they should be new for each saved/loaded game
    // and I didn't find better place to put this code in
    CTradeParameters::clean();

    for (auto& i : m_debug_render_queue)
    {
        xr_delete(i.second);
    }

    m_debug_render_queue.clear();
}

shared_str CLevel::name() const { return (m_name); }

bool CLevel::PrefetchSound(LPCSTR name)
{
    // preprocess sound name
    string_path tmp;
    strcpy_s(tmp, name);
    xr_strlwr(tmp);

    if (const auto ext = strext(tmp); ext != nullptr)
        *ext = '\0';

    // find in registry
    auto it = sound_registry.find(tmp);
    // if find failed - preload sound
    if (it == sound_registry.end())
    {
        sound_registry[tmp].create(tmp, st_Effect, sg_SourceType);
        return true;
    }

    return false;
}

bool CLevel::PrefetchManySounds(LPCSTR prefix)
{
    bool created = false;
    string_path fn;

    if (xr::sound_exists(fn, prefix))
        created = PrefetchSound(prefix) || created;

    u32 i = 0;
    while (true)
    {
        string256 name;
        sprintf_s(name, "%s%d", prefix, i);

        if (xr::sound_exists(fn, name))
            created = PrefetchSound(name) || created;
        else if (i > 0)
            break;

        i++;
    }

    return created;
}

bool CLevel::PrefetchManySoundsLater(LPCSTR prefix)
{
    std::string s(prefix);
    for (const auto& it : sound_registry_defer)
    {
        if (it == s)
            return false;
    }
    sound_registry_defer.push_back(s);
    return true;
}

tmc::task<void> CLevel::PrefetchDeferredSounds()
{
    while (!sound_registry_defer.empty())
    {
        std::string s = sound_registry_defer.front();
        sound_registry_defer.pop_front();
        if (PrefetchManySounds(s.c_str()))
            break;
    }

    co_return;
}

void CLevel::CancelPrefetchManySounds(LPCSTR prefix)
{
    std::string s(prefix);
    sound_registry_defer.erase(std::remove(sound_registry_defer.begin(), sound_registry_defer.end(), s), sound_registry_defer.end());
}

// Game interface ////////////////////////////////////////////////////

BOOL g_bDebugEvents = FALSE;

tmc::task<void> CLevel::cl_Process_Event(u16 dest, u16 type, NET_Packet& P)
{
    CObject* O = Objects.net_Find(dest);
    if (!O)
    {
#ifdef DEBUG
        Msg("* WARNING: c_EVENT[{}] to [{}]: unknown dest", type, dest);
#endif // DEBUG

        ProcessGameSpawnsDestroy(dest, type);
        co_return;
    }

    CGameObject* GO = smart_cast<CGameObject*>(O);
    if (!GO)
    {
        Msg("! ERROR: c_EVENT[{}] : non-game-object", dest);
        co_return;
    }

    if (type == GE_DESTROY)
        Game().OnDestroy(GO);

    co_await GO->OnEvent(P, type);
}

tmc::task<void> CLevel::ProcessGameEvents()
{
    // Game events
    NET_Packet P;

    m_just_destroyed.clear();

    while (game_events->available())
    {
        u16 ID, dest, type;
        game_events->get(ID, dest, type, P);

        switch (xr::msg{ID})
        {
        case xr::msg::M_SPAWN: {
            u16 dummy16;
            std::ignore = P.r_begin(dummy16);

            co_await cl_Process_Spawn(P);
            break;
        }
        case xr::msg::M_EVENT: co_await cl_Process_Event(dest, type, P); break;
        default: xr::unreachable();
        }
    }

    if (!is_removing_objects())
        Device.add_to_seq_parallel(CallMe::fromMethod<&CLevel::ProcessGameSpawns>(this));
}

tmc::task<void> CLevel::OnFrame()
{
    m_feel_deny.update();

    psDeviceFlags.set(rsDisableObjectsAsCrows, false);

    // commit events from bullet manager from prev-frame
    Device.Statistic->TEST0.Begin();
    BulletManager().CommitEvents();
    Device.Statistic->TEST0.End();

    Device.Statistic->netClient1.Begin();
    co_await ClientReceive();
    Device.Statistic->netClient1.End();

    co_await ProcessGameEvents();

    if XR_RELEASE_CONSTEXPR (g_mt_config.test(mtMap))
        Device.add_to_seq_parallel(CallMe::fromMethod<&CMapManager::Update>(m_map_manager));
    else
        co_await MapManager().Update();

    // Inherited update
    co_await inherited::OnFrame();

    if (!xr::editor() || !xr::editor()->script_time())
        g_pGamePersistent->Environment().SetGameTime(GetEnvironmentGameDayTimeSec(), game->GetEnvironmentGameTimeFactor());

    m_ph_commander->update();
    m_ph_commander_scripts->update();

    // просчитать полет пуль
    Device.Statistic->TEST0.Begin();
    co_await BulletManager().CommitRenderSet();
    Device.Statistic->TEST0.End();

    // update static sounds
    if XR_RELEASE_CONSTEXPR (g_mt_config.test(mtLevelSounds))
        Device.add_to_seq_parallel(CallMe::fromMethod<&CLevelSoundManager::Update>(m_level_sound_manager));
    else
        co_await m_level_sound_manager->Update();

    if (!sound_registry_defer.empty())
        Device.add_to_seq_parallel(CallMe::fromMethod<&CLevel::PrefetchDeferredSounds>(this));

    // defer LUA-GC-STEP
    if XR_RELEASE_CONSTEXPR (g_mt_config.test(mtLUA_GC))
        Device.add_to_seq_parallel(CallMe::fromMethod<&CLevel::script_gc>(this));
    else
        co_await script_gc();

    //-----------------------------------------------------
    if (pStatGraphR)
    {
        static float fRPC_Mult = 10.0f;
        static float fRPS_Mult = 1.0f;

        pStatGraphR->AppendItem(float(m_dwRPC) * fRPC_Mult, 0xffff0000, 1);
        pStatGraphR->AppendItem(float(m_dwRPS) * fRPS_Mult, 0xff00ff00, 0);
    }
}

int psLUA_GCSTEP = 100; // 10
int psLUA_GCTIMEOUT = 1000;
u32 ps_lua_gc_method = 1;

tmc::task<void> CLevel::script_gc()
{
    XR_TRACY_ZONE_SCOPED();

    auto& lua = ai().script_engine().lua();

    switch (ps_lua_gc_method)
    {
    case 0: co_return;
#ifdef LUA_GCTIMEOUT
    case 2:
        if (lua_gc(lua.lua_state(), LUA_GCTIMEOUT, psLUA_GCTIMEOUT) >= 0)
            break;

        // LUA_GCTIMEOUT is unsupported, fallback to LUA_GCSTEP
        [[fallthrough]];
#endif
    default: ps_lua_gc_method = 1; [[fallthrough]];
    case 1: lua.step_gc(psLUA_GCSTEP); break;
    }

    // If script_gc() is performed once a frame, we don't need automatic GC
    lua.stop_gc();
}

tmc::task<void> CLevel::OnRender()
{
    Render->BeforeWorldRender(); //--#SM+#-- +SecondVP+
    co_await inherited::OnRender();
    Game().OnRender();

    // отрисовать трассы пуль
    BulletManager().Render();

    auto pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
    auto pActor = smart_cast<CActor*>(Level().CurrentEntity());
    CPda* Pda = pActor ? pActor->GetPDA() : nullptr;
    const bool need_pda_render = Pda && Pda->Is3DPDA() && psActorFlags.test(AF_3D_PDA) && pGameSP->PdaMenu->IsShown();
    Render->AfterWorldRender(need_pda_render);

    if (need_pda_render)
    {
        HUD().RenderUI();

        if (g_btnHint)
            co_await g_btnHint->OnRender();

        co_await GetUICursor()->OnRender();
        draw_wnds_rects();

        Fvector2 cursor_pos = GetUICursor()->GetCursorPosition();
        Fvector2 cursor_pos_dif{cursor_pos};
        cursor_pos_dif.sub(pGameSP->PdaMenu->last_cursor_pos);
        pGameSP->PdaMenu->last_cursor_pos.set(cursor_pos);
        pGameSP->PdaMenu->MouseMovement(cursor_pos_dif.x, cursor_pos_dif.y);

        Render->AfterUIRender();
    }

    HUD().RenderUI();

    if (g_btnHint)
        co_await g_btnHint->OnRender();

    co_await GetUICursor()->OnRender();
    draw_wnds_rects();

#ifndef DEBUG
    if (psActorFlags.test(AF_ZONES_DBG))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            auto _O = Level().Objects.o_get_by_iterator(I);
            auto space_restrictor = smart_cast<CSpaceRestrictor*>(_O);
            if (space_restrictor)
                space_restrictor->OnRender();

            auto level_changer = smart_cast<CLevelChanger*>(_O);
            if (level_changer)
                level_changer->OnRender();

            auto climable = smart_cast<CClimableObject*>(_O);
            if (climable)
                climable->OnRender();
        }
    }

    if (psActorFlags.test(AF_VERTEX_DBG))
    {
        if (ai().get_level_graph())
            ai().level_graph().render();
    }

    if (dbg_net_Draw_Flags.test(1 << 11)) // draw skeleton
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            CObject* _O = Level().Objects.o_get_by_iterator(I);

            CGameObject* pGO = smart_cast<CGameObject*>(_O);
            if (pGO /*&& pGO != Level().CurrentViewEntity()*/ && !pGO->H_Parent())
            {
                if (pGO->Position().distance_to_sqr(Device.vCameraPosition) < 400.0f)
                {
                    // CPhysicObject* physic_object = smart_cast<CPhysicObject*>(_O);
                    // if (physic_object)
                    //     physic_object->OnRender();

                    pGO->dbg_DrawSkeleton();
                }
            }
        }
    }
#else
    ph_world->OnRender();

    if (ai().get_level_graph())
        ai().level_graph().render();

#ifdef DEBUG_PRECISE_PATH
    test_precise_path();
#endif

    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(Level().CurrentEntity());
    if (stalker)
        stalker->OnRender();

    if (bDebug)
    {
        ObjectSpace.dbgRender();

        //---------------------------------------------------------------------
        HUD().Font().pFontStat->OutSet(170, 630);
        HUD().Font().pFontStat->SetHeight(16.0f);
        HUD().Font().pFontStat->SetColor(0xffff0000);

        if (Server)
            HUD().Font().pFontStat->OutNext("Client Objects:      [{}]", Server->GetEntitiesNum());

        HUD().Font().pFontStat->OutNext("Server Objects:      [{}]", Objects.o_count());
        HUD().Font().pFontStat->OutNext("Interpolation Steps: [{}]", Level().GetInterpolationSteps());
        HUD().Font().pFontStat->SetHeight(8.0f);
        //---------------------------------------------------------------------
    }

    ScriptDebugRender();

    if (psActorFlags.test(AF_ZONES_DBG))
    {
        DBG().draw_object_info();
        DBG().draw_text();
        DBG().draw_level_info();
    }
#endif

    debug_renderer().render();

#ifdef DEBUG
    if (psAI_Flags.is(aiVision))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            CObject* object = Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
            if (!stalker)
                continue;
            stalker->dbg_draw_vision();
        }
    }

    if (psAI_Flags.test(aiDrawVisibilityRays))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            CObject* object = Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
            if (!stalker)
                continue;

            stalker->dbg_draw_visibility_rays();
        }
    }
#endif
}

tmc::task<void> CLevel::OnEvent(CEvent* E, u64 P1, u64)
{
    if (E == eEntitySpawn)
    {
        const auto res = scn::scan_value<xr_string>(std::string_view{reinterpret_cast<gsl::czstring>(P1)});
        R_ASSERT(res, res.error().msg());

        Level().g_cl_Spawn(res->value().c_str(), 0xff, M_SPAWN_OBJECT_LOCAL, Fvector3{});
    }
    else if (E == eDemoPlay && P1)
    {
        Cameras().AddCamEffector(xr_new<CDemoPlay>(xr::format("{}.xrdemo", reinterpret_cast<gsl::czstring>(P1)).c_str(), 1.3f, 0u));
    }

    co_return;
}

void CLevel::PhisStepsCallback(u32, u32) {}

ALife::_TIME_ID CLevel::GetGameTime()
{
    return (game->GetGameTime());
    //	return			(Server->game->GetGameTime());
}

ALife::_TIME_ID CLevel::GetEnvironmentGameTime()
{
    return (game->GetEnvironmentGameTime());
    //	return			(Server->game->GetGameTime());
}

u8 CLevel::GetDayTime()
{
    u32 dummy32;
    u32 hours;
    GetGameDateTime(dummy32, dummy32, dummy32, hours, dummy32, dummy32, dummy32);
    VERIFY(hours < 256);
    return u8(hours);
}

float CLevel::GetGameDayTimeSec() { return (float(s64(GetGameTime() % (24 * 60 * 60 * 1000))) / 1000.f); }

u32 CLevel::GetGameDayTimeMS() { return (u32(s64(GetGameTime() % (24 * 60 * 60 * 1000)))); }

float CLevel::GetEnvironmentGameDayTimeSec() { return (float(s64(GetEnvironmentGameTime() % (24 * 60 * 60 * 1000))) / 1000.f); }

void CLevel::ScriptDebugRender()
{
    if (!m_debug_render_queue.size())
        return;

    bool hasVisibleObj = false;

    xr_map<u16, DBG_ScriptObject*>::iterator it = m_debug_render_queue.begin();
    xr_map<u16, DBG_ScriptObject*>::iterator it_e = m_debug_render_queue.end();
    for (; it != it_e; ++it)
    {
        DBG_ScriptObject* obj = (*it).second;
        if (obj->m_visible)
        {
            obj->Render();
            hasVisibleObj = true;
        }
    }

    // demonized: fix of showing console window when there are no visible gizmos
    if (hasVisibleObj)
        DRender->OnFrameEnd();
}

void CLevel::GetGameDateTime(u32& year, u32& month, u32& day, u32& hours, u32& mins, u32& secs, u32& milisecs)
{
    split_time(GetGameTime(), year, month, day, hours, mins, secs, milisecs);
}

float CLevel::GetGameTimeFactor()
{
    return (game->GetGameTimeFactor());
    //	return			(Server->game->GetGameTimeFactor());
}

void CLevel::SetGameTimeFactor(const float fTimeFactor)
{
    game->SetGameTimeFactor(fTimeFactor);
    //	Server->game->SetGameTimeFactor(fTimeFactor);
}

void CLevel::SetGameTimeFactor(ALife::_TIME_ID GameTime, const float fTimeFactor)
{
    game->SetGameTimeFactor(GameTime, fTimeFactor);
    //	Server->game->SetGameTimeFactor(fTimeFactor);
}

void CLevel::SetEnvironmentGameTimeFactor(u64 const& GameTime, float const& fTimeFactor)
{
    if (!game)
        return;

    game->SetEnvironmentGameTimeFactor(GameTime, fTimeFactor);
}

void CLevel::GetGameTimeForShaders(u32& hours, u32& minutes, u32& seconds, u32& milliseconds)
{
    u32 unused;
    split_time(GetGameTime(), unused, unused, unused, hours, minutes, seconds, milliseconds);
}

void CLevel::OnSessionTerminate(LPCSTR reason) { MainMenu()->OnSessionTerminate(reason); }

void CLevel::OnDestroyObject(u16 id) { m_just_destroyed.push_back(id); }

void CLevel::OnChangeCurrentWeather(const char* sect)
{
    if (on_change_weather_callback.empty())
        return;

    if (sol::function func; ai().script_engine().function(on_change_weather_callback.c_str(), func))
        func(sect);
}

u32 GameID() { return Game().Type(); }

GlobalFeelTouch::GlobalFeelTouch() = default;
GlobalFeelTouch::~GlobalFeelTouch() = default;

struct delete_predicate_by_time final
{
    bool operator()(Feel::Touch::DenyTouch const& left, DWORD const expire_time) const
    {
        if (left.Expire <= expire_time)
            return true;
        return false;
    }
};

struct objects_ptrs_equal final
{
    bool operator()(Feel::Touch::DenyTouch const& left, CObject const* const right) const
    {
        if (left.O == right)
            return true;
        return false;
    }
};

void GlobalFeelTouch::update()
{
    // we ignore P and R arguments, we need just delete evaled denied objects...
    xr_vector<Feel::Touch::DenyTouch>::iterator new_end =
        std::remove_if(feel_touch_disable.begin(), feel_touch_disable.end(), std::bind(delete_predicate_by_time(), std::placeholders::_1, Device.dwTimeGlobal));
    feel_touch_disable.erase(new_end, feel_touch_disable.end());
}

bool GlobalFeelTouch::is_object_denied(CObject const* O)
{
    /*Fvector temp_vector;
    feel_touch_update(temp_vector, 0.f);*/
    if (std::find_if(feel_touch_disable.begin(), feel_touch_disable.end(), std::bind(objects_ptrs_equal(), std::placeholders::_1, O)) ==
        feel_touch_disable.end())
    {
        return false;
    }
    return true;
}
