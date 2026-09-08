#include "stdafx.h"

#include "Level.h"

#include "Level_Bullet_Manager.h"
#include "xrServer.h"
#include "xrMessages.h"
#include "game_cl_base.h"
#include "PHCommander.h"
#include "NET_Queue.h"
#include "MainMenu.h"
#include "space_restriction_manager.h"
#include "ai_space.h"
#include "script_engine.h"
#include "stalker_animation_data_storage.h"
#include "client_spawn_manager.h"
#include "seniority_hierarchy_holder.h"
#include "LevelDebugScript.h"
#include "UIGameCustom.h"

#include "xrServer_Objects.h"

#include "sleep.h"

#ifdef DEBUG
void show_animation_stats();
#endif // DEBUG

tmc::task<void> CLevel::remove_objects()
{
    m_is_removing_objects = true;
    BOOL b_stored = psDeviceFlags.test(rsDisableObjectsAsCrows);

    xr::render_memory_usage usage;
    Device.m_pRender->ResourcesGetMemoryUsage(usage);

    XR_LOG_NOTICE("ObjectResources unload...");
    Msg("~ ObjectResources - base: {}, {} Kb", usage.c_base, usage.m_base / 1024);
    Msg("~ ObjectResources - lmap: {}, {} Kb", usage.c_lmaps, usage.m_lmaps / 1024);
    Msg("~ ObjectResources - Lua: {} Kb", usage.lua / 1024);

    co_await Game().reset_ui();

    VERIFY(Server != nullptr);
    Server->SLS_Clear(); // generate GE_DESTROY for all game objects

    snd_Events.clear();

    // process destroy queue
    for (int i = 0; i < 6; ++i)
    {
        // ugly hack for checks that update is twice on frame
        // we need it since we do updates for checking network messages
        ++(Device.dwFrame);
        psDeviceFlags.set(rsDisableObjectsAsCrows, TRUE);

        co_await ClientReceive();
        co_await ProcessGameEvents();

        co_await Objects.Update(true);
    }

    BulletManager().Clear();
    ph_commander().clear();
    ph_commander_scripts().clear();

    space_restriction_manager().clear();
    psDeviceFlags.set(rsDisableObjectsAsCrows, b_stored);

    ai().script_engine().collect_all_garbage();

    Remove_all_statics();

    stalker_animation_data_storage().clear();

    VERIFY(Render);

    if (!g_prefetch)
        Render->models_Clear(TRUE);
    else
        Render->models_Clear(FALSE);

    Render->clear_static_wallmarks();

#ifdef DEBUG
    if (!client_spawn_manager().registry().empty())
        client_spawn_manager().dump();
#endif // DEBUG

    VERIFY(client_spawn_manager().registry().empty());
    client_spawn_manager().clear();

    for (int i = 0; i < 6; i++)
    {
        ++(Device.dwFrame);
        co_await Objects.Update(true);
    }

    g_pGamePersistent->destroy_particles(false);
    co_await ::Sound->stop_emitters();

    Device.m_pRender->ResourcesGetMemoryUsage(usage);

    XR_LOG_NOTICE("ObjectResources unload completed!");
    Msg("~ ObjectResources - base: {}, {} Kb", usage.c_base, usage.m_base / 1024);
    Msg("~ ObjectResources - lmap: {}, {} Kb", usage.c_lmaps, usage.m_lmaps / 1024);
    Msg("~ ObjectResources - Lua: {} Kb", usage.lua / 1024);

    ai().script_engine().collect_all_garbage();

    for (auto& i : m_debug_render_queue)
        xr_delete(i.second);

    m_debug_render_queue.clear();

    // clean up scheduler queues
    Engine.Sheduler.Destroy();

    m_is_removing_objects = false;
}

tmc::task<void> CLevel::net_Stop()
{
    XR_LOG_NOTICE("Disconnect");
    bReady = false;

    m_bGameConfigStarted = FALSE;
    game_configured = FALSE;

    co_await remove_objects();
    co_await IGame_Level::net_Stop();

    IPureClient::Disconnect();

    if (Server)
    {
        Server->Disconnect();
        xr_delete(Server);
    }

    ai().script_engine().collect_all_garbage();
    Remove_all_statics();

#ifdef DEBUG
    show_animation_stats();
#endif // DEBUG
}

void CLevel::ClientSend()
{
    for (u32 i = 0; i < Objects.o_count(); i++)
    {
        CObject* O = Objects.o_get_by_iterator(i);
        if (O->net_Relevant() && !O->getDestroy())
        {
            CSE_Abstract* E = Server->ID_to_entity(O->ID());
            if (E)
                O->net_Export(E);
        }
    }
}

void CLevel::ClientSave()
{
    for (u32 i = 0; i < Objects.o_count(); i++)
    {
        CObject* O = Objects.o_get_by_iterator(i);
        CGameObject* P = smart_cast<CGameObject*>(O);

        if (P && !P->getDestroy() && P->net_SaveRelevant())
        {
            NET_Packet Packet;

            P->net_Save(Packet); // вызов save(NET_Packet& output_packet)

            CSE_Abstract* E = Server->ID_to_entity(O->ID());

            if (E)
            {
                E->net_Ready = TRUE;
                E->load(Packet);
            }
        }
    }
}

extern float phTimefactor;

void CLevel::Send(NET_Packet& P, u32, u32)
{
    // optimize the case when server located in our memory
    ClientID _clid;
    _clid.set(1);
    std::ignore = Server->OnMessage(P, _clid);
}

void CLevel::net_Update()
{
    if (game_configured)
    {
        // If we have enought bandwidth - replicate client data on to server
        Device.Statistic->netClient2.Begin();
        ClientSend();
        Device.Statistic->netClient2.End();
    }
    // If server - perform server-update
    if (Server)
    {
        Device.Statistic->netServer.Begin();
        Server->Update();
        Device.Statistic->netServer.End();
    }
}

namespace
{
struct _NetworkProcessor final : public pureFrame
{
    RTTI_DECLARE_TYPEINFO(_NetworkProcessor, pureFrame);

public:
    tmc::task<void> OnFrame() override
    {
        if (g_pGameLevel && !Device.Paused())
            g_pGameLevel->net_Update();

        co_return;
    }
} NET_processor;

constexpr int ConnectionTimeOut{60000}; // 1 min
} // namespace

pureFrame* g_pNetProcessor = &NET_processor;

tmc::task<bool> CLevel::Connect2Server(gsl::czstring options)
{
    NET_Packet P;
    m_bConnectResultReceived = false;
    m_bConnectResult = true;

    if (!Connect(options))
        co_return false;

    //---------------------------------------------------------------------------
    m_bConnectResultReceived = true;
    u32 EndTime = GetTickCount() + ConnectionTimeOut;

    while (!m_bConnectResultReceived)
    {
        co_await ClientReceive();
        co_await tmc::spawn_clang(xr::sleep(std::chrono::milliseconds{5}), xr::tmc_cpu_st_executor());

        if (Server)
            Server->Update();
        //-----------------------------------------
        u32 CurTime = GetTickCount();
        if (CurTime > EndTime)
        {
            NET_Packet P;
            P.B.count = 0;
            P.r_pos = 0;

            P.w_u8(0);
            P.w_u8(0);
            P.w_stringZ("Data verification failed. Cheater? [1]");

            OnConnectResult(&P);
        }

        if (net_isFails_Connect())
        {
            OnConnectRejected();
            Disconnect();

            co_return false;
        }
        //-----------------------------------------
    }

    Msg("{} client : connection {} - <{}>", m_bConnectResult ? '*' : '!', m_bConnectResult ? "accepted" : "rejected", m_sConnectResult);

    if (!m_bConnectResult)
    {
        OnConnectRejected();
        Disconnect();

        co_return false;
    }

    net_Syncronised = TRUE;

    P.w_begin(gsl::narrow<u16>(xr::msg::M_CLIENT_REQUEST_CONNECTION_DATA));
    Send(P, net_flags(TRUE, TRUE, TRUE, TRUE));

    co_return true;
}

void CLevel::OnBuildVersionChallenge()
{
    NET_Packet P;
    P.w_begin(gsl::narrow<u16>(xr::msg::M_CL_AUTH));
    P.w_u64(0);

    Send(P, net_flags(TRUE, TRUE, TRUE, TRUE));
}

void CLevel::OnConnectResult(NET_Packet* P)
{
    // multiple results can be sent during connection they should be "AND-ed"
    m_bConnectResultReceived = true;

    u8 result = P->r_u8();
    std::ignore = P->r_u8(); // Оставлено для совместимости сейвов

    string128 ResultStr;
    P->r_stringZ(ResultStr);
    if (!result)
        m_bConnectResult = false;

    m_sConnectResult = ResultStr;
}

void CLevel::OnConnectRejected() { IPureClient::OnConnectRejected(); }

void CLevel::net_OnChangeSelfName(NET_Packet* P)
{
    if (!P)
        return;

    string64 NewName;
    P->r_stringZ(NewName);

    const auto opts = std::string_view{m_caClientOptions};

    if (const auto pos = opts.find("/name="); pos != std::string_view::npos)
    {
        const auto pref = opts.substr(0, pos + 6);
        std::string_view pfx;

        if (const auto ppos = opts.find('/', pos + 6); ppos != std::string_view::npos)
            pfx = opts.substr(ppos);

        m_caClientOptions._set(xr::format("{}{}{}", pref, NewName, pfx));
    }
    else
    {
        m_caClientOptions._set(xr::format("{}/name={}", opts, NewName));
    }
}
