#include "stdafx.h"

#include "r4.h"

#include "../xrRender/ResourceManager.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xr_3da/fmesh.h"
#include "../../xr_3da/xrLevel.h"
#include "../../xr_3da/x_ray.h"
#include "../../xr_3da/IGame_Persistent.h"
#include "../../xrCore/stream_reader.h"

#include "../xrRender/dxRenderDeviceRender.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "../xrRenderDX10/3DFluid/dx103DFluidVolume.h"

#include "../xrRender/FHierrarhyVisual.h"

#include <Utilities/FlexibleVertexFormat.h>

tmc::task<void> CRender::level_Load(IReader* fs)
{
    XR_ASSERT(g_pGameLevel != nullptr && !b_loaded);

    xr::render_memory_usage usage;
    ResourcesGetMemoryUsage(usage);

    XR_LOG_NOTICE("LevelResources load...");
    Msg("~ LevelResources - base: {}, {} Kb", usage.c_base, usage.m_base / 1024);
    Msg("~ LevelResources - lmap: {}, {} Kb", usage.c_lmaps, usage.m_lmaps / 1024);
    Msg("~ LevelResources - Lua: {} Kb", usage.lua / 1024);

    // Begin
    pApp->LoadBegin();
    Resources->DeferredLoad(TRUE);

    IReader* chunk;

    // Shaders
    co_await g_pGamePersistent->LoadTitle("st_loading_shaders");

    {
        chunk = XR_ASSERT_VAL(fs->open_chunk(fsL_SHADERS) != nullptr, "invalid level files", fsL_SHADERS);
        u32 count = chunk->r_u32();
        Shaders.reserve(count);

        for (u32 i = 0; i < count; i++) // skip first shader as "reserved" one
        {
            string512 n_sh, n_tlist;
            LPCSTR n = LPCSTR(chunk->pointer());
            chunk->skip_stringZ();

            if (n[0] == '\0')
            {
                Shaders.emplace_back();
                continue;
            }

            xr_strcpy(n_sh, n);
            LPSTR delim = strchr(n_sh, '/');
            *delim = '\0';
            xr_strcpy(n_tlist, delim + 1);

            // Pass only the first texture if it is a list
            delim = std::strchr(n_tlist, ',');
            if (delim != nullptr)
                *delim = '\0';

            xr::override_shaders(n_tlist, n_sh, sizeof(n_sh));

            if (delim != nullptr)
                *delim = ',';

            Shaders.emplace_back(Resources->Create(n_sh, n_tlist));
        }

        XR_ASSERT(Shaders.size() == count);
    }

    // Components
    Wallmarks = xr_new<CWallmarksEngine>();
    Details = xr_new<CDetailManager>();

    // VB,IB,SWI
    co_await g_pGamePersistent->LoadTitle("st_loading_geometry");

    {
        const auto geom = XR_ASSERT_VAL(absl::WrapUnique(FS.rs_open("$level$", "level.geom")));
        LoadBuffers(geom.get(), false);
        LoadSWIs(geom.get());
    }

    //...and alternate/fast geometry
    LoadBuffers(XR_ASSERT_VAL(absl::WrapUnique(FS.rs_open("$level$", "level.geomx"))).get(), true);

    // Visuals
    co_await g_pGamePersistent->LoadTitle("st_loading_spatial_db");
    chunk = fs->open_chunk(fsL_VISUALS);
    LoadVisuals(chunk);
    chunk->close();

    // Details
    co_await g_pGamePersistent->LoadTitle("st_loading_details");
    Details->Load();

    // Sectors
    co_await g_pGamePersistent->LoadTitle("st_loading_sectors_portals");
    LoadSectors(fs);

    // 3D Fluid
    Load3DFluid();

    // HOM
    co_await HOM.Load();

    // Lights
    co_await g_pGamePersistent->LoadTitle("st_loading_lights");
    LoadLights(fs);

    // End
    pApp->LoadEnd();

    ResourcesGetMemoryUsage(usage);

    XR_LOG_NOTICE("LevelResources load completed!");
    Msg("~ LevelResources - base: {}, {} Kb", usage.c_base, usage.m_base / 1024);
    Msg("~ LevelResources - lmap: {}, {} Kb", usage.c_lmaps, usage.m_lmaps / 1024);
    Msg("~ LevelResources - Lua: {} Kb", usage.lua / 1024);

    // signal loaded
    b_loaded = TRUE;
}

void CRender::level_Unload()
{
    if (!g_pGameLevel)
        return;
    if (!b_loaded)
        return;

    xr::render_memory_usage usage;
    ResourcesGetMemoryUsage(usage);

    XR_LOG_NOTICE("LevelResources unload...");
    Msg("~ LevelResources - base: {}, {} Kb", usage.c_base, usage.m_base / 1024);
    Msg("~ LevelResources - lmap: {}, {} Kb", usage.c_lmaps, usage.m_lmaps / 1024);
    Msg("~ LevelResources - Lua: {} Kb", usage.lua / 1024);

    // HOM
    HOM.Unload();

    //*** Details
    Details->Unload();

    //*** Sectors
    // 1.
    xr_delete(rmPortals);
    last_sector_id = INVALID_SECTOR_ID;

    // 2.
    cleanup_contexts();

    //*** Lights
    Lights.Unload();

    //*** Visuals
    for (dxRender_Visual* visual : Visuals)
    {
        visual->Release();
        xr_delete(visual);
    }
    Visuals.clear();

    //*** SWI
    for (auto& swi : SWIs)
        xr_free(swi.sw);
    SWIs.clear();

    //*** VB/IB
    for (auto* buffer : nVB)
        _RELEASE(buffer);
    for (auto* buffer : xVB)
        _RELEASE(buffer);

    nVB.clear();
    xVB.clear();

    for (auto* buffer : nIB)
        _RELEASE(buffer);
    for (auto* buffer : xIB)
        _RELEASE(buffer);

    nIB.clear();
    xIB.clear();
    nDC.clear();
    xDC.clear();

    //*** Components
    xr_delete(Details);
    xr_delete(Wallmarks);

    //*** Shaders
    Shaders.clear();

    ResourcesGetMemoryUsage(usage);

    XR_LOG_NOTICE("LevelResources unload completed!");
    Msg("~ LevelResources - base: {}, {} Kb", usage.c_base, usage.m_base / 1024);
    Msg("~ LevelResources - lmap: {}, {} Kb", usage.c_lmaps, usage.m_lmaps / 1024);
    Msg("~ LevelResources - Lua: {} Kb", usage.lua / 1024);

    b_loaded = FALSE;
}

void CRender::LoadBuffers(CStreamReader* base_fs, BOOL _alternative)
{
    XR_ASSERT(base_fs != nullptr, "geometry file not found");

    auto& _DC = _alternative ? xDC : nDC;
    auto& _VB = _alternative ? xVB : nVB;
    auto& _IB = _alternative ? xIB : nIB;
    xr_vector<std::byte> data;

    // Vertex buffers
    // Use DX9-style declarators
    auto fs = XR_ASSERT_VAL(absl::WrapUnique(base_fs->open_chunk(fsL_VB)), "invalid geometry file", fsL_VB);
    const auto count = fs->r_u32();

    _DC.resize(count);
    _VB.resize(count);

    // decl
    for (auto [dc, vb] : std::views::zip(_DC, _VB))
    {
        dc.resize(MAXD3DDECLLENGTH + 1);
        const auto buffer_size = xr::ssize_bytes(dc);
        fs->r(dc.data(), buffer_size);

        const auto vSize = FVF::ComputeVertexSize(dc.data(), 0);
        dc.resize(FVF::GetDeclLength(dc.data()) + 1);
        fs->advance(xr::ssize_bytes(dc) - buffer_size);

        // count * size
        const auto vCount = fs->r_u32();
        const auto len = vCount * vSize;

#ifndef MASTER_GOLD
        Msg("* [Loading VB] {} verts, {} Kb", vCount, len / 1024);
#endif

        // Create and fill
        //	TODO: DX10: Check fragmentation.
        //	Check if buffer is less then 2048 kb
        data.resize(len);
        fs->r(data.data(), len);
        XR_ASSERT(xr::hr(dx10BufferUtils::CreateVertexBuffer(&vb, data.data(), len)));
    }

    // Index buffers
    fs = XR_ASSERT_VAL(absl::WrapUnique(base_fs->open_chunk(fsL_IB)), "", fsL_IB);
    _IB.resize(fs->r_u32());

    for (auto& ib : _IB)
    {
        const auto iCount = fs->r_u32();
        const auto len = iCount * sizeof(u16);

#ifndef MASTER_GOLD
        Msg("* [Loading IB] {} indices, {} Kb", iCount, len / 1024);
#endif

        // Create and fill
        //	TODO: DX10: Check fragmentation.
        //	Check if buffer is less then 2048 kb
        data.resize(len);
        fs->r(data.data(), len);
        XR_ASSERT(xr::hr(dx10BufferUtils::CreateIndexBuffer(&ib, data.data(), len)));
    }
}

void CRender::LoadVisuals(IReader* fs)
{
    u32 index = 0;
    IReader* chunk = nullptr;

    while ((chunk = fs->open_chunk(index)) != nullptr)
    {
        ogf_header H;
        std::ignore = chunk->r_chunk_safe(OGF_HEADER, &H, sizeof(H));

        dxRender_Visual* V = Models->Instance_Create(H.type);
        V->Load(nullptr, chunk, 0);
        Visuals.push_back(V);

        chunk->close();
        index++;
    }
}

void CRender::LoadLights(IReader* fs)
{
    // lights
    Lights.Load(fs);
    Lights.LoadHemi();
}

void CRender::LoadSectors(IReader* fs)
{
    // allocate memory for portals
    const auto pt_size = fs->find_chunk(fsL_PORTALS);
    XR_ASSERT(xr::is_aligned(pt_size, gsl::index{sizeof(CPortal::level_portal_data_t)}), "", pt_size, sizeof(CPortal::level_portal_data_t));

    const auto portals_count = pt_size / sizeof(CPortal::level_portal_data_t);
    xr_vector<CPortal::level_portal_data_t> portals_data{portals_count};

    // load sectors
    xr_vector<CSector::level_sector_data_t> sectors_data;

    IReader* S = fs->open_chunk(fsL_SECTORS);
    f32 largest_sector_vol{0.0f};

    for (sector_id_t i{0};; ++i)
    {
        const auto P = absl::WrapUnique(S->open_chunk(i));
        if (!P)
            break;

        const auto size = P->find_chunk(fsP_Portals);
        XR_ASSERT(xr::is_aligned(size, gsl::index{sizeof(u16)}), "", i, size);

        auto portals_in_sector = size / sizeof(u16);
        auto& sector_data = sectors_data.emplace_back();
        sector_data.portals_id.reserve(portals_in_sector);

        while (portals_in_sector)
        {
            const u16 ID = P->r_u16();
            sector_data.portals_id.emplace_back(ID);
            --portals_in_sector;
        }

        XR_ASSERT(P->find_chunk(fsP_Root) == sizeof(u32), "", i);
        sector_data.root_id = P->r_u32();

        // Search for default sector - assume "default" or "outdoor" sector is the largest one
        // XXX: hack: need to know real outdoor sector
        if (const auto vol = smart_cast<dxRender_Visual*>(getVisual(sector_data.root_id))->vis.box.getvolume(); vol > largest_sector_vol)
        {
            largest_sector_vol = vol;
            largest_sector_id = i;
        }
    }

    S->close();

    // load portals
    if (portals_count)
    {
        CDB::Collector CL;
        std::ignore = fs->find_chunk(fsL_PORTALS);

        for (auto [i, P] : std::views::enumerate(portals_data))
        {
            fs->r(&P, sizeof(P));
            XR_ASSERT(P.vertices.size() <= P.vertices.capacity());

            for (gsl::index j{2}; j < std::ssize(P.vertices); ++j)
                CL.add_face_packed_D(P.vertices[0], P.vertices[j - 1], P.vertices[j], i);
        }

        if (CL.getTS() < 2)
        {
            constexpr Fvector v1{-20000.f, -20000.f, -20000.f};
            constexpr Fvector v2{-20001.f, -20001.f, -20001.f};
            constexpr Fvector v3{-20002.f, -20002.f, -20002.f};
            CL.add_face_packed_D(v1, v2, v3, 0);
        }

        // build portal model
        rmPortals = xr_new<CDB::MODEL>();
        rmPortals->build(CL.get_verts(), CL.get_faces());
    }
    else
    {
        rmPortals = nullptr;
    }

    for (auto& dsgraph : contexts_pool)
    {
        dsgraph.reset();
        dsgraph.load(sectors_data, portals_data);
    }

    contexts_used.reset();
}

void CRender::LoadSWIs(CStreamReader* base_fs)
{
    // allocate memory for portals
    if (base_fs->find_chunk(fsL_SWIS) == 0)
        return;

    const auto fs = absl::WrapUnique(base_fs->open_chunk(fsL_SWIS));

    for (auto& SWI : SWIs)
        xr_free(SWI.sw);

    SWIs.clear();
    const auto item_count = fs->r_u32();
    SWIs.resize(item_count);

    for (auto& swi : SWIs)
    {
        swi.reserved[0] = fs->r_u32();
        swi.reserved[1] = fs->r_u32();
        swi.reserved[2] = fs->r_u32();
        swi.reserved[3] = fs->r_u32();
        swi.count = fs->r_u32();

        XR_ASSERT(swi.sw == nullptr);
        swi.sw = xr_alloc<FSlideWindow>(swi.count);
        fs->r(swi.sw, sizeof(FSlideWindow) * swi.count);
    }
}

void CRender::Load3DFluid()
{
    if (!ps_r2_ls_flags.test(R3FLAG_VOLUMETRIC_SMOKE))
        return;

    string_path fn_game;
    if (FS.exist(fn_game, "$level$", "level.fog_vol") == nullptr)
        return;

    const auto F = absl::WrapUnique(FS.r_open(fn_game));
    // Version, must be 3
    if (F->r_u16() != 3)
        return;

    for (u32 i = 0, cnt = F->r_u32(); i < cnt; ++i)
    {
        dx103DFluidVolume* pVolume = xr_new<dx103DFluidVolume>();
        pVolume->Load("", F.get(), 0);

        const auto& v = pVolume->getVisData().sphere.P;

        Msg("~ Loading fog volume with profile [{}]. Position: {}", pVolume->getProfileName(), v);

        //	Attach to sector's static geometry
        for (auto& dsgraph : contexts_pool)
        {
            auto pSector = dsgraph.get_sector(dsgraph.detect_sector(pVolume->getVisData().sphere.P));
            //	3DFluid volume must be in render sector
            if (pSector == nullptr)
            {
                Msg("!!Cannot find sector for fog volume. Position: {}", v);

                xr_delete(pVolume);
                continue;
            }

            dxRender_Visual* pRoot = pSector->root();
            //	Sector must have root
            XR_ASSERT(pRoot != nullptr && pRoot->getType() == MT_HIERRARHY);

            smart_cast<FHierrarhyVisual*>(pRoot)->children.push_back(pVolume);
        }
    }
}
