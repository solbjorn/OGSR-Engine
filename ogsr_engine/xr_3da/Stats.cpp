#include "stdafx.h"

#include "Stats.h"

#include "GameFont.h"
#include "../xrCDB/ISpatial.h"
#include "IGame_Persistent.h"
#include "Render.h"
#include "xr_object.h"

#include "../Include/xrRender/DrawUtils.h"

#include "cpu.h"

#include <mmsystem.h>
#include <psapi.h>

enum DebugTextColor : DWORD
{
    DTC_RED = 0xFFF0672B,
    DTC_YELLOW = 0xFFF6D434,
    DTC_GREEN = 0xFF67F92E,
};

int g_ErrorLineCount = 15;
Flags32 g_stats_flags = {0};

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

#ifdef DEBUG
namespace
{
BOOL g_bDisableRedText = FALSE;
}
#endif

CStats::CStats()
{
#ifdef DEBUG
    Device.seqRender.Add(this, REG_PRIORITY_LOW - 1000);
#endif
}

CStats::~CStats()
{
#ifdef DEBUG
    Device.seqRender.Remove(this);
#endif

    xr_delete(pFont);
}

namespace
{
void _draw_cam_pos(CGameFont* pFont)
{
    float sz = pFont->GetHeight();
    pFont->SetHeightI(0.02f);
    pFont->SetColor(0xffffffff);
    pFont->Out(10.0f, 600.0f, "CAMERA POSITION:  {::3.2f}", Device.vCameraPosition);
    pFont->SetHeight(sz);
    pFont->OnRender();
}
} // namespace

void CStats::Show()
{
    // Stop timers
    {
        EngineTOTAL.FrameEnd();
        Sheduler.FrameEnd();
        UpdateClient.FrameEnd();
        Physics.FrameEnd();
        ph_collision.FrameEnd();
        ph_core.FrameEnd();
        Animation.FrameEnd();
        AI_Think.FrameEnd();
        AI_Range.FrameEnd();
        AI_Path.FrameEnd();
        AI_Node.FrameEnd();
        AI_Vis.FrameEnd();
        AI_Vis_Query.FrameEnd();
        AI_Vis_RayTests.FrameEnd();

        RenderCALC.FrameEnd();
        RenderCALC_HOM.FrameEnd();
        RenderDUMP.FrameEnd();
        RenderDUMP_RT.FrameEnd();
        RenderDUMP_SKIN.FrameEnd();
        RenderDUMP_Wait_W.FrameEnd();
        RenderDUMP_Wait.FrameEnd();
        RenderDUMP_Wait_P.FrameEnd();
        RenderDUMP_HUD.FrameEnd();
        RenderDUMP_Glows.FrameEnd();
        RenderDUMP_Lights.FrameEnd();
        RenderDUMP_WM.FrameEnd();
        RenderDUMP_DT_VIS.FrameEnd();
        RenderDUMP_DT_Render.FrameEnd();
        RenderDUMP_DT_Cache.FrameEnd();
        RenderDUMP_Pcalc.FrameEnd();
        RenderDUMP_Scalc.FrameEnd();
        RenderDUMP_Srender.FrameEnd();

        SoundUpdate.FrameEnd();
        SoundRender.FrameEnd();

        Input.FrameEnd();
        clRAY.FrameEnd();
        clBOX.FrameEnd();
        clFRUSTUM.FrameEnd();

        netClient1.FrameEnd();
        netClient2.FrameEnd();
        netServer.FrameEnd();

        netClientCompressor.FrameEnd();
        netServerCompressor.FrameEnd();

        TEST0.FrameEnd();
        TEST1.FrameEnd();
        TEST2.FrameEnd();
        TEST3.FrameEnd();

        g_SpatialSpace->stat_insert.FrameEnd();
        g_SpatialSpace->stat_remove.FrameEnd();
        g_SpatialSpacePhysic->stat_insert.FrameEnd();
        g_SpatialSpacePhysic->stat_remove.FrameEnd();
    }

    CGameFont& F = *pFont;
    float f_base_size = 0.01f;
    F.SetHeightI(f_base_size);

    // Show them
    if (psDeviceFlags.test(rsStatistic))
    {
        static float r_ps = 0;
        static float b_ps = 0;
        r_ps = .99f * r_ps + .01f * (clRAY.count / clRAY.result);
        b_ps = .99f * b_ps + .01f * (clBOX.count / clBOX.result);

        CSound_stats snd_stat;
        ::Sound->statistic(&snd_stat, nullptr);
        F.SetColor(0xFFFFFFFF);

        F.OutSet(0, 0);
        F.OutNext("FPS/RFPS:    {:3.1f}/{:3.1f}", fFPS, fRFPS);
        F.OutNext("TPS:         {:2.2f} M", fTPS);
        m_pRender->OutData1(F);

#ifdef DEBUG
        F.OutSkip();
        F.OutNext("mapped:      {}", g_file_mapped_memory);
        F.OutSkip();
        m_pRender->OutData2(F);
#endif

        m_pRender->OutData3(F);
        F.OutSkip();

#define PPP(a) (100.f * float(a) / float(EngineTOTAL.result))
        F.OutNext("*** ENGINE:  {:2.2f}ms", EngineTOTAL.result);
        F.OutNext("uClients:    {:2.2f}ms, {:2.1f}%, crow({})/active({})/total({})", UpdateClient.result, PPP(UpdateClient.result), UpdateClient_crows,
                  UpdateClient_active, UpdateClient_total);
        F.OutNext("uSheduler:   {:2.2f}ms, {:2.1f}%", Sheduler.result, PPP(Sheduler.result));
        F.OutNext("uSheduler_L: {:2.2f}ms", fShedulerLoad);
        F.OutNext("uParticles:  Qstart[{}] Qactive[{}] Qdestroy[{}]", Particles_starting, Particles_active, Particles_destroy);
        F.OutNext("spInsert:    o[{:.2f}ms, {:2.1f}%], p[{:.2f}ms, {:2.1f}%]", g_SpatialSpace->stat_insert.result, PPP(g_SpatialSpace->stat_insert.result),
                  g_SpatialSpacePhysic->stat_insert.result, PPP(g_SpatialSpacePhysic->stat_insert.result));
        F.OutNext("spRemove:    o[{:.2f}ms, {:2.1f}%], p[{:.2f}ms, {:2.1f}%]", g_SpatialSpace->stat_remove.result, PPP(g_SpatialSpace->stat_remove.result),
                  g_SpatialSpacePhysic->stat_remove.result, PPP(g_SpatialSpacePhysic->stat_remove.result));
        F.OutNext("Physics:     {:2.2f}ms, {:2.1f}%", Physics.result, PPP(Physics.result));
        F.OutNext("  collider:  {:2.2f}ms", ph_collision.result);
        F.OutNext("  solver:    {:2.2f}ms, {}", ph_core.result, ph_core.count);
        F.OutNext("aiThink:     {:2.2f}ms, {}", AI_Think.result, AI_Think.count);
        F.OutNext("  aiRange:   {:2.2f}ms, {}", AI_Range.result, AI_Range.count);
        F.OutNext("  aiPath:    {:2.2f}ms, {}", AI_Path.result, AI_Path.count);
        F.OutNext("  aiNode:    {:2.2f}ms, {}", AI_Node.result, AI_Node.count);
        F.OutNext("aiVision:    {:2.2f}ms, {}", AI_Vis.result, AI_Vis.count);
        F.OutNext("  Query:     {:2.2f}ms", AI_Vis_Query.result);
        F.OutNext("  RayCast:   {:2.2f}ms", AI_Vis_RayTests.result);
        F.OutSkip();

#undef PPP
#define PPP(a) (100.f * float(a) / float(RenderTOTAL.result))
        F.OutNext("*** RENDER:  {:2.2f}ms", RenderTOTAL.result);
        F.OutNext("R_CALC:      {:2.2f}ms, {:2.1f}%", RenderCALC.result, PPP(RenderCALC.result));
        F.OutNext("  HOM:       {:2.2f}ms, {}", RenderCALC_HOM.result, RenderCALC_HOM.count);
        F.OutNext("  Skeletons: {:2.2f}ms, {}", Animation.result, Animation.count);
        F.OutNext("R_DUMP:      {:2.2f}ms, {:2.1f}%", RenderDUMP.result, PPP(RenderDUMP.result));
        F.OutNext("  Wait-W:    {:2.2f}ms", RenderDUMP_Wait_W.result);
        F.OutNext("  Wait-L:    {:2.2f}ms", RenderDUMP_Wait.result);
        F.OutNext("  Wait-P:    {:2.2f}ms", RenderDUMP_Wait_P.result);
        F.OutNext("  Skinning:  {:2.2f}ms", RenderDUMP_SKIN.result);
        F.OutNext("  DT_Vis/Cnt:{:2.2f}ms/{}", RenderDUMP_DT_VIS.result, RenderDUMP_DT_Count);
        F.OutNext("  DT_Render: {:2.2f}ms", RenderDUMP_DT_Render.result);
        F.OutNext("  DT_Cache:  {:2.2f}ms", RenderDUMP_DT_Cache.result);
        F.OutNext("  Wallmarks: {:2.2f}ms, {}/{} - {}", RenderDUMP_WM.result, RenderDUMP_WMS_Count, RenderDUMP_WMD_Count, RenderDUMP_WMT_Count);
        F.OutNext("  Glows:     {:2.2f}ms", RenderDUMP_Glows.result);
        F.OutNext("  Lights:    {:2.2f}ms, {}", RenderDUMP_Lights.result, RenderDUMP_Lights.count);
        F.OutNext("  RT:        {:2.2f}ms, {}", RenderDUMP_RT.result, RenderDUMP_RT.count);
        F.OutNext("  HUD:       {:2.2f}ms", RenderDUMP_HUD.result);
        F.OutNext("  P_calc:    {:2.2f}ms", RenderDUMP_Pcalc.result);
        F.OutNext("  S_calc:    {:2.2f}ms", RenderDUMP_Scalc.result);
        F.OutNext("  S_render:  {:2.2f}ms, {}", RenderDUMP_Srender.result, RenderDUMP_Srender.count);
        F.OutSkip();
        F.OutNext("*** SOUND:   {:2.2f}ms", SoundUpdate.result);
        F.OutNext("  RENDER:    {:2.2f}ms", SoundRender.result);
        F.OutNext("  TGT/SIM/E: {}/{}/{}", snd_stat._rendered, snd_stat._simulated, snd_stat._events);
        F.OutSkip();
        F.OutNext("Input:       {:2.2f}ms", Input.result);
        F.OutNext("clRAY:       {:2.2f}ms, {}, {:2.0f}K", clRAY.result, clRAY.count, r_ps);
        F.OutNext("clBOX:       {:2.2f}ms, {}, {:2.0f}K", clBOX.result, clBOX.count, b_ps);
        F.OutNext("clFRUSTUM:   {:2.2f}ms, {}", clFRUSTUM.result, clFRUSTUM.count);
        F.OutSkip();
        F.OutNext("netClientRecv:   {:2.2f}ms, {}", netClient1.result, netClient1.count);
        F.OutNext("netClientSend:   {:2.2f}ms, {}", netClient2.result, netClient2.count);
        F.OutNext("netServer:   {:2.2f}ms, {}", netServer.result, netServer.count);
        F.OutNext("netClientCompressor:   {:2.2f}ms", netClientCompressor.result);
        F.OutNext("netServerCompressor:   {:2.2f}ms", netServerCompressor.result);

        F.OutSkip();

        F.OutSkip();
        F.OutNext("TEST 0:      {:2.2f}ms, {}", TEST0.result, TEST0.count);
        F.OutNext("TEST 1:      {:2.2f}ms, {}", TEST1.result, TEST1.count);
        F.OutNext("TEST 2:      {:2.2f}ms, {}", TEST2.result, TEST2.count);
        F.OutNext("TEST 3:      {:2.2f}ms, {}", TEST3.result, TEST3.count);

#ifdef DEBUG_MEMORY_MANAGER
        F.OutSkip();
        F.OutNext("str: cmp[{:3}], dock[{:3}], qpc[{:3}]", Memory.stat_strcmp, Memory.stat_strdock, CPU::qpc_counter);
        Memory.stat_strcmp = 0;
        Memory.stat_strdock = 0;
        CPU::qpc_counter = 0;
#else // DEBUG_MEMORY_MANAGER
        F.OutSkip();
        F.OutNext("qpc[{:3}]", CPU::qpc_counter);
        CPU::qpc_counter = 0;
#endif // DEBUG_MEMORY_MANAGER

        F.OutSkip();
        m_pRender->OutData4(F);

        //////////////////////////////////////////////////////////////////////////
        // Renderer specific
        F.SetHeightI(f_base_size);
        F.OutSet(200, 0);
        Render->Statistics(&F);

        //////////////////////////////////////////////////////////////////////////
        // Game specific
        F.SetHeightI(f_base_size);
        F.OutSet(400, 0);
        g_pGamePersistent->Statistics(&F);

        //////////////////////////////////////////////////////////////////////////
        // process PURE STATS
        F.SetHeightI(f_base_size);
        pFont->OnRender();
    }

    if (/*psDeviceFlags.test(rsStatistic) ||*/ psDeviceFlags.test(rsCameraPos))
    {
        _draw_cam_pos(pFont);
        pFont->OnRender();
    }

#ifdef DEBUG
    //////////////////////////////////////////////////////////////////////////
    // PERF ALERT
    if (!g_bDisableRedText)
    {
        CGameFont& F = *((CGameFont*)pFont);
        F.SetColor(color_rgba(255, 16, 16, 255));
        F.OutSet(300, 300);
        F.SetHeightI(f_base_size * 2);

        if (fFPS < 30)
            F.OutNext("FPS       < 30:   {:3.1f}", fFPS);

        m_pRender->GuardVerts(F);

        if (psDeviceFlags.test(rsStatistic))
        {
            m_pRender->GuardDrawCalls(F);

            if (RenderDUMP_DT_Count > 1000)
                F.OutNext("DT_count  > 1000: {}", RenderDUMP_DT_Count);

            F.OutSkip();

            if (Sheduler.result > 3.f)
                F.OutNext("Update     > 3ms:\t{:3.1f}", Sheduler.result);
            if (UpdateClient.result > 3.f)
                F.OutNext("UpdateCL   > 3ms: {:3.1f}", UpdateClient.result);
            if (Physics.result > 5.f)
                F.OutNext("Physics    > 5ms: {:3.1f}", Physics.result);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Show errors
    if (!g_bDisableRedText && errors.size())
    {
        CGameFont& F = *((CGameFont*)pFont);
        F.SetColor(color_rgba(255, 16, 16, 191));
        F.OutSet(200, 0);
        F.SetHeightI(f_base_size);

        for (u32 it = (u32)_max(int(0), (int)errors.size() - g_ErrorLineCount); it < errors.size(); it++)
            F.OutNext("{}", errors[it]);

        F.OnRender();
    }
#endif

    {
        EngineTOTAL.FrameStart();
        Sheduler.FrameStart();
        UpdateClient.FrameStart();
        Physics.FrameStart();
        ph_collision.FrameStart();
        ph_core.FrameStart();
        Animation.FrameStart();
        AI_Think.FrameStart();
        AI_Range.FrameStart();
        AI_Path.FrameStart();
        AI_Node.FrameStart();
        AI_Vis.FrameStart();
        AI_Vis_Query.FrameStart();
        AI_Vis_RayTests.FrameStart();

        RenderCALC.FrameStart();
        RenderCALC_HOM.FrameStart();
        RenderDUMP.FrameStart();
        RenderDUMP_RT.FrameStart();
        RenderDUMP_SKIN.FrameStart();
        RenderDUMP_Wait_W.FrameStart();
        RenderDUMP_Wait.FrameStart();
        RenderDUMP_Wait_P.FrameStart();
        RenderDUMP_HUD.FrameStart();
        RenderDUMP_Glows.FrameStart();
        RenderDUMP_Lights.FrameStart();
        RenderDUMP_WM.FrameStart();
        RenderDUMP_DT_VIS.FrameStart();
        RenderDUMP_DT_Render.FrameStart();
        RenderDUMP_DT_Cache.FrameStart();
        RenderDUMP_Pcalc.FrameStart();
        RenderDUMP_Scalc.FrameStart();
        RenderDUMP_Srender.FrameStart();

        SoundUpdate.FrameStart();
        SoundRender.FrameStart();

        Input.FrameStart();
        clRAY.FrameStart();
        clBOX.FrameStart();
        clFRUSTUM.FrameStart();

        netClient1.FrameStart();
        netClient2.FrameStart();
        netServer.FrameStart();
        netClientCompressor.FrameStart();
        netServerCompressor.FrameStart();

        TEST0.FrameStart();
        TEST1.FrameStart();
        TEST2.FrameStart();
        TEST3.FrameStart();

        g_SpatialSpace->stat_insert.FrameStart();
        g_SpatialSpace->stat_remove.FrameStart();

        g_SpatialSpacePhysic->stat_insert.FrameStart();
        g_SpatialSpacePhysic->stat_remove.FrameStart();
    }

    dwSND_Played = dwSND_Allocated = 0;
    Particles_starting = Particles_active = Particles_destroy = 0;
}

void CStats::Show_HW_Stats()
{
    static DWORD dwLastFrameTime = 0;
    DWORD dwCurrentTime = timeGetTime();
    if (dwCurrentTime - dwLastFrameTime > 500) // Апдейт раз в полсекунды
    {
        dwLastFrameTime = dwCurrentTime;

        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);

        AvailableMem = (float)mem.ullAvailPhys; // how much phys mem available
        AvailableMem /= (1024 * 1024);

        if (mem.ullTotalPageFile - mem.ullTotalPhys > 0.f)
        {
            AvailablePageFileMem = (float)mem.ullAvailPageFile - (float)mem.ullAvailPhys; // how much pagefile mem available
            AvailablePageFileMem /= (1024 * 1024);
        }

        PhysMemoryUsedPercent = (float)mem.dwMemoryLoad;

        // Getting info by request
        PROCESS_MEMORY_COUNTERS pc;
        std::memset(&pc, 0, sizeof(PROCESS_MEMORY_COUNTERS));
        pc.cb = sizeof(pc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pc, sizeof(pc)))
        {
            PagefileUsage = (float)pc.PagefileUsage;
            PagefileUsage /= (1024 * 1024);

            WorkingSetSize = (float)pc.WorkingSetSize;
            WorkingSetSize /= (1024 * 1024);
        }

        // Counting CPU load
        std::ignore = CPU::ID.getCPULoad(cpuLoad);
        CPU::ID.MTCPULoad();
    }

    pFontHW->SetHeightI(0.018f);

    if (AvailableMem < 512 || AvailablePageFileMem < 1596)
        pFontHW->SetColor(DebugTextColor::DTC_RED);
    else if (AvailableMem < 768 || AvailablePageFileMem < 2048)
        pFontHW->SetColor(DebugTextColor::DTC_YELLOW);
    else
        pFontHW->SetColor(DebugTextColor::DTC_GREEN);

    float y = 10.f;

    // Draw all your stuff
    pFontHW->Out(10, y += 15, "PHYS MEM AVAILABLE: {:0.0f}MB", AvailableMem); // Physical memory available
    pFontHW->Out(10, y += 15, "PAGE MEM AVAILABLE: {:0.0f}MB", AvailablePageFileMem); // Pagefile memory available

    pFontHW->Out(10, y += 15, "Engine Working Set: {:0.0f}MB", WorkingSetSize); // Physical memory used by app
    pFontHW->Out(10, y += 15, "Engine Commit Charge: {:0.0f}MB", PagefileUsage); // Physical memory used by app

    if (PhysMemoryUsedPercent > 80.0f)
        pFontHW->SetColor(DebugTextColor::DTC_RED);
    else if (PhysMemoryUsedPercent > 60.0f)
        pFontHW->SetColor(DebugTextColor::DTC_YELLOW);
    else
        pFontHW->SetColor(DebugTextColor::DTC_GREEN);

    pFontHW->Out(10, y += 15, "MEM USED: {:0.0f}%", PhysMemoryUsedPercent); // Total Phys. memory load (%)

    if (cpuLoad > 80.0)
        pFontHW->SetColor(DebugTextColor::DTC_RED);
    else if (cpuLoad > 60.0)
        pFontHW->SetColor(DebugTextColor::DTC_YELLOW);
    else
        pFontHW->SetColor(DebugTextColor::DTC_GREEN);

    pFontHW->Out(10, y += 15, "CPU LOAD: {:0.0f}%", cpuLoad); // CPU load

    // get MT Load
    float dwScale = 100;
    for (size_t i = 0; i < CPU::ID.m_dwNumberOfProcessors; i++)
    {
        pFontHW->Out(10, dwScale, "CPU{}: {:0.0f}%", i, CPU::ID.fUsage[i]);
        dwScale += 15;
    }

    pFontHW->OnRender();
}

void CStats::OnDeviceCreate()
{
#ifdef DEBUG
    g_bDisableRedText = strstr(Core.Params, "-xclsx") ? TRUE : FALSE;
#endif

    pFont = xr_new<CGameFont>("stat_font", CGameFont::fsDeviceIndependent);
    pFontHW = xr_new<CGameFont>("stat_font", CGameFont::fsDeviceIndependent);
}

void CStats::OnDeviceDestroy()
{
    xr_delete(pFont);
    xr_delete(pFontHW);
}

#ifdef DEBUG
tmc::task<void> CStats::OnRender()
{
    if (!g_stats_flags.is(st_sound))
        co_return;

    CSound_stats_ext snd_stat_ext;
    ::Sound->statistic(0, &snd_stat_ext);
    CSound_stats_ext::item_vec_it _I = snd_stat_ext.items.begin();
    CSound_stats_ext::item_vec_it _E = snd_stat_ext.items.end();
    for (; _I != _E; _I++)
    {
        const CSound_stats_ext::SItem& item = *_I;
        if (item._3D)
        {
            m_pRender->SetDrawParams(&*Device.m_pRender);
            DU->DrawCross(item.params.position, 0.5f, 0xFF0000FF, true);
            if (g_stats_flags.is(st_sound_min_dist))
                DU->DrawSphere(Fidentity, item.params.position, item.params.min_distance, 0x400000FF, 0xFF0000FF, true, true);
            if (g_stats_flags.is(st_sound_max_dist))
                DU->DrawSphere(Fidentity, item.params.position, item.params.max_distance, 0x4000FF00, 0xFF008000, true, true);

            xr_string out_txt = (out_txt.size() && g_stats_flags.is(st_sound_info_name)) ? item.name.c_str() : "";

            if (item.game_object)
            {
                if (g_stats_flags.is(st_sound_ai_dist))
                    DU->DrawSphere(Fidentity, item.params.position, item.params.max_ai_distance, 0x80FF0000, 0xFF800000, true, true);
                if (g_stats_flags.is(st_sound_info_object))
                {
                    out_txt += "  (";
                    out_txt += item.game_object->cNameSect().c_str();
                    out_txt += ")";
                }
            }
            if (g_stats_flags.is_any(st_sound_info_name | st_sound_info_object) && item.name.size())
                DU->OutText(item.params.position, out_txt.c_str(), 0xFFFFFFFF, 0xFF000000);
        }
    }
}
#endif
