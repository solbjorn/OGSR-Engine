#include "stdafx.h"

#include "Level.h"

#include "Entity.h"
#include "xrServer_Objects.h"
#include "xrMessages.h"
#include "game_cl_base.h"
#include "NET_Queue.h"
#include "Physics.h"
#include "xrServer.h"
#include "Actor.h"
#include "ai_space.h"
#include "saved_game_wrapper.h"
#include "level_graph.h"
#include "clsid_game.h"

#include "HUDManager.h"
#include "UIGameSP.h"

tmc::task<void> CLevel::ClientReceive()
{
    m_dwRPC = 0;
    m_dwRPS = 0;

    for (NET_Packet* P = net_msg_Retreive(); P; P = net_msg_Retreive())
    {
        //-----------------------------------------------------
        m_dwRPC++;
        m_dwRPS += P->B.count;
        //-----------------------------------------------------

        u16 m_type;
        std::ignore = P->r_begin(m_type);

        switch (xr::msg{m_type})
        {
        case xr::msg::M_SPAWN:
            if (!m_bGameConfigStarted || !bReady)
            {
                Msg("Unconventional M_SPAWN received : cgf[{}] | bReady[{}]", (m_bGameConfigStarted) ? "true" : "false", (bReady) ? "true" : "false");
                break;
            }

            game_events->insert(*P);

            if (g_bDebugEvents)
                co_await ProcessGameEvents();

            break;
        case xr::msg::M_EVENT:
            game_events->insert(*P);

            if (g_bDebugEvents)
                co_await ProcessGameEvents();

            break;
        case xr::msg::M_EVENT_PACK: {
            NET_Packet tmpP;

            while (!P->r_eof())
            {
                tmpP.B.count = P->r_u8();
                P->r(&tmpP.B.data, tmpP.B.count);
                tmpP.timeReceive = P->timeReceive;

                game_events->insert(tmpP);

                if (g_bDebugEvents)
                    co_await ProcessGameEvents();
            }

            break;
        }
        case xr::msg::M_UPDATE: game->net_import_update(*P); break;
        case xr::msg::M_SV_CONFIG_NEW_CLIENT: co_await InitializeClientGame(*P); break;
        case xr::msg::M_SV_CONFIG_GAME: game->net_import_state(*P); break;
        case xr::msg::M_SV_CONFIG_FINISHED:
            XR_LOG_NOTICE("Game configuring : Finished");

            game_configured = TRUE;
            break;
        case xr::msg::M_RELOAD_GAME:
        case xr::msg::M_LOAD_GAME:
        case xr::msg::M_CHANGE_LEVEL:
            // костыль для автозакрытия любого диалога в случае смены уровня
            if (HUD().GetUI()->MainInputReceiver())
                HUD().GetUI()->StartStopMenu(HUD().GetUI()->MainInputReceiver(), true);

            if (xr::msg{m_type} == xr::msg::M_LOAD_GAME)
            {
                string256 saved_name;
                P->r_stringZ(saved_name);
                if (xr_strlen(saved_name) && ai().get_alife())
                {
                    CSavedGameWrapper wrapper(saved_name);
                    if (wrapper.level_id() == ai().level_graph().level_id())
                    {
                        co_await Engine.Event.Defer("Game:QuickLoad", reinterpret_cast<uintptr_t>(xr_strdup(saved_name)));
                        break;
                    }
                }
            }

            co_await Engine.Event.Defer("KERNEL:disconnect");
            co_await Engine.Event.Defer("KERNEL:start", reinterpret_cast<uintptr_t>(xr_strdup(m_caServerOptions.c_str())),
                                        reinterpret_cast<uintptr_t>(xr_strdup(m_caClientOptions.c_str())));

            break;
        case xr::msg::M_AUTH_CHALLENGE: OnBuildVersionChallenge(); break;
        case xr::msg::M_CLIENT_CONNECT_RESULT: OnConnectResult(P); break;
        case xr::msg::M_CHANGE_LEVEL_GAME:
            XR_LOG_NOTICE("M_CHANGE_LEVEL_GAME Received");

            {
                const char* m_SO = m_caServerOptions.c_str();
                //					const char* m_CO = m_caClientOptions.c_str();

                m_SO = strchr(m_SO, '/');
                if (m_SO)
                    m_SO++;
                m_SO = strchr(m_SO, '/');

                string128 LevelName = "";
                string128 GameType = "";

                P->r_stringZ(LevelName);
                P->r_stringZ(GameType);

                string4096 NewServerOptions;
                sprintf_s(NewServerOptions, "%s/%s", LevelName, GameType);
                if (m_SO)
                    strcat_s(NewServerOptions, m_SO);

                m_caServerOptions._set(NewServerOptions);

                co_await Engine.Event.Defer("KERNEL:disconnect");
                co_await Engine.Event.Defer("KERNEL:start", reinterpret_cast<uintptr_t>(xr_strdup(m_caServerOptions.c_str())),
                                            reinterpret_cast<uintptr_t>(xr_strdup(m_caClientOptions.c_str())));
            }

            break;
        case xr::msg::M_CHANGE_SELF_NAME: net_OnChangeSelfName(P); break;
        default: break;
        }

        net_msg_Release();
    }
}

void CLevel::OnMessage(void* data, u32 size) { IPureClient::OnMessage(data, size); }
