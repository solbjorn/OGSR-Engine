#include "stdafx.h"

#include "Level.h"

#include "Level_Bullet_Manager.h"
#include "xrServer.h"
#include "game_cl_base.h"
#include "xrMessages.h"
#include "../xr_3da/x_ray.h"
#include "../xr_3da/device.h"
#include "../xr_3da/IGame_Persistent.h"
#include "../xr_3da/XR_IOConsole.h"
#include "MainMenu.h"

#include "HUDManager.h"

BOOL CLevel::net_Start(LPCSTR op_server, LPCSTR op_client)
{
    net_start_result_total = TRUE;

    pApp->LoadBegin();

    // make Client Name if options doesn't have it
    LPCSTR NameStart = strstr(op_client, "/name=");
    if (!NameStart)
    {
        string512 tmp;
        strcpy_s(tmp, op_client);
        strcat_s(tmp, "/name=");
        strcat_s(tmp, xr_strlen(Core.UserName) ? Core.UserName : Core.CompName);

        m_caClientOptions._set(tmp);
    }
    else
    {
        if (const auto res = scn::scan<std::string_view>(std::string_view{NameStart + xr_strlen("/name=")}, "{:[^/]}"); !res || res->value().empty())
        {
            string1024 tmpstr;
            strcpy_s(tmpstr, op_client);
            *(strstr(tmpstr, "name=") + 5) = 0;
            strcat_s(tmpstr, xr_strlen(Core.UserName) ? Core.UserName : Core.CompName);

            const char* ptmp = strchr(strstr(op_client, "name="), '/');
            if (ptmp)
                strcat_s(tmpstr, ptmp);

            m_caClientOptions._set(tmpstr);
        }
        else
        {
            m_caClientOptions._set(op_client);
        }
    }

    m_caServerOptions._set(op_server);

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------------
    g_loading_events.emplace_back(CallMe::fromMethod<&CLevel::net_start1>(this));
    g_loading_events.emplace_back(CallMe::fromMethod<&CLevel::net_start2>(this));
    g_loading_events.emplace_back(CallMe::fromMethod<&CLevel::net_start3>(this));
    g_loading_events.emplace_back(CallMe::fromMethod<&CLevel::net_start4>(this));
    g_loading_events.emplace_back(CallMe::fromMethod<&CLevel::net_start5>(this));
    g_loading_events.emplace_back(CallMe::fromMethod<&CLevel::net_start6>(this));

    return net_start_result_total;
}

tmc::task<bool> CLevel::net_start1()
{
    // Start client and server if need it
    if (!m_caServerOptions.empty())
    {
        co_await g_pGamePersistent->LoadTitle("st_server_starting");

        // Connect
        Server = xr_new<xrServer>();

        if (std::is_neq(xr_strcmp(g_pGamePersistent->m_game_params.m_alife, "alife")))
        {
            std::string_view l_name{m_caServerOptions};

            if (const auto pos = l_name.find('/'); pos != std::string_view::npos)
                l_name = l_name.substr(0, pos);

            m_name._set(l_name);

            int id = pApp->Level_ID(m_name.c_str());
            if (id < 0)
            {
                pApp->LoadEnd();

                Msg("Can't find level: [{}]", l_name);
                net_start_result_total = FALSE;

                co_return true;
            }
        }
    }

    co_return true;
}

tmc::task<bool> CLevel::net_start2()
{
    if (net_start_result_total && !m_caServerOptions.empty())
    {
        if (m_connect_server_err = co_await Server->Connect(m_caServerOptions); m_connect_server_err != xrServer::ErrNoError)
        {
            XR_LOG_ERROR("Failed to start server");

            net_start_result_total = false;
            co_return true;
        }

        Server->SLS_Default();
        m_name = Server->level_name(m_caServerOptions);
    }

    co_return true;
}

tmc::task<bool> CLevel::net_start3()
{
    if (!net_start_result_total)
        co_return true;

    co_return true;
}

tmc::task<bool> CLevel::net_start4()
{
    if (!net_start_result_total)
        co_return true;

    g_loading_events.pop_front();

    g_loading_events.emplace_front(CallMe::fromMethod<&CLevel::net_start_client6>(this));
    g_loading_events.emplace_front(CallMe::fromMethod<&CLevel::net_start_client5>(this));
    g_loading_events.emplace_front(CallMe::fromMethod<&CLevel::net_start_client4>(this));
    g_loading_events.emplace_front(CallMe::fromMethod<&CLevel::net_start_client3>(this));
    g_loading_events.emplace_front(CallMe::fromMethod<&CLevel::net_start_client2>(this));
    g_loading_events.emplace_front(CallMe::fromMethod<&CLevel::net_start_client1>(this));

    co_return false;
}

tmc::task<bool> CLevel::net_start5()
{
    if (net_start_result_total)
    {
        NET_Packet NP;
        NP.w_begin(gsl::narrow<u16>(xr::msg::M_CLIENTREADY));
        Send(NP, net_flags(TRUE, TRUE));
    }

    co_return true;
}

namespace
{
struct LevelLoadFinalizer final
{
    tmc::task<bool> net_start_finalizer() { co_return true; }
};

LevelLoadFinalizer LF;
} // namespace

tmc::task<bool> CLevel::net_start6()
{
    g_loading_events.pop_front();
    g_loading_events.emplace_front(CallMe::fromMethod<&LevelLoadFinalizer::net_start_finalizer>(&LF));

    // init bullet manager
    BulletManager().Clear();
    BulletManager().Load();

    pApp->LoadEnd();

    if (!net_start_result_total)
        co_return false;

    const std::string_view params{Core.Params};

    if (const auto pos = params.rfind("-$"); pos != std::string_view::npos)
    {
        if (pos + 2 < params.size())
        {
            if (const auto res = scn::scan<std::string_view, std::string_view>(params.substr(pos + 2), "{} {}"); res)
            {
                const auto [cmd, param] = res->values();
                Console->Execute(xr::format("{} {}", cmd, param).c_str());
            }
            else
            {
                Msg("! Invalid -$ parameter arguments: {}", res.error().msg());
            }
        }
        else
        {
            Msg("! The -$ parameter requires 2 arguments");
        }
    }

    if (g_hud != nullptr)
        HUD().GetUI()->OnConnected();

    co_return false;
}

tmc::task<void> CLevel::InitializeClientGame(NET_Packet& P)
{
    string256 game_type_name;
    P.r_stringZ(game_type_name);

    if (game && std::is_eq(xr_strcmp(game_type_name, game->type_name())))
        co_return;

    xr_delete(game);
    XR_LOG_NOTICE("Game configuring : Started");

    CLASS_ID clsid = game_GameState::getCLASS_ID(false);
    game = smart_cast<game_cl_GameState*>(NEW_INSTANCE(clsid));
    game->set_type_name(game_type_name);
    game->Init();
    m_bGameConfigStarted = TRUE;

    XR_ASSERT(co_await Load_GameSpecific_After());
}
