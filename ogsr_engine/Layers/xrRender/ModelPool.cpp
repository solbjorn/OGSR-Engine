#include "stdafx.h"

#include "ModelPool.h"

#include "../../xr_3da/IGame_Persistent.h"
#include "../../xr_3da/fmesh.h"
#include "FHierrarhyVisual.h"
#include "SkeletonAnimated.h"
#include "FVisual.h"
#include "FProgressive.h"
#include "FSkinned.h"
#include "FLOD.h"
#include "FTreeVisual.h"
#include "ParticleGroup.h"
#include "ParticleEffect.h"

#include "xr_ini.h"

dxRender_Visual* CModelPool::Instance_Create(u32 type)
{
    dxRender_Visual* V;

    // Check types
    switch (type)
    {
    case MT_NORMAL: // our base visual
        V = xr_new<Fvisual>();
        break;
    case MT_HIERRARHY: V = xr_new<FHierrarhyVisual>(); break;
    case MT_PROGRESSIVE: // dynamic-resolution visual
        V = xr_new<FProgressive>();
        break;
    case MT_SKELETON_ANIM: V = xr_new<CKinematicsAnimated>(); break;
    case MT_SKELETON_RIGID: V = xr_new<CKinematics>(); break;
    case MT_SKELETON_GEOMDEF_PM: V = xr_new<CSkeletonX_PM>(); break;
    case MT_SKELETON_GEOMDEF_ST: V = xr_new<CSkeletonX_ST>(); break;
    case MT_PARTICLE_EFFECT: V = xr_new<PS::CParticleEffect>(); break;
    case MT_PARTICLE_GROUP: V = xr_new<PS::CParticleGroup>(); break;
    case MT_LOD: V = xr_new<FLOD>(); break;
    case MT_TREE_ST: V = xr_new<FTreeVisual_ST>(); break;
    case MT_TREE_PM: V = xr_new<FTreeVisual_PM>(); break;
    default: XR_PANIC("unknown visual type", type);
    }

    V->Type = type;

    return V;
}

dxRender_Visual* CModelPool::Instance_Duplicate(dxRender_Visual* V)
{
    dxRender_Visual* N = Instance_Create(XR_ASSERT_VAL(V != nullptr)->Type);
    N->Copy(V);
    N->Spawn();

    // inc ref counter
    if (const auto it = std::ranges::find(Models, V, &ModelDef::model); it != Models.end())
        ++it->refs;

    return N;
}

dxRender_Visual* CModelPool::Instance_Load(const char* N, BOOL allow_register)
{
    dxRender_Visual* V;
    string_path fn;
    string_path name;

    // Add default ext if no ext at all
    if (!strext(N))
        strconcat(sizeof(name), name, N, ".ogf");
    else
        xr_strcpy(name, sizeof(name), N);

    // Load data from MESHES or LEVEL
    if (FS.exist(N) == nullptr)
        XR_ASSERT(FS.exist(fn, "$level$", name) != nullptr || FS.exist(fn, "$game_meshes$", name) != nullptr, "can't find model file", name);
    else
        xr_strcpy(fn, N);

    // Actual loading
#ifdef DEBUG
    if (bLogging)
        Msg("- Uncached model loading: {}", fn);
#endif // DEBUG

    IReader* data = FS.r_open(fn);
    ogf_header H;
    std::ignore = data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
    V = Instance_Create(H.type);
    V->Load(N, data, 0);
    FS.r_close(data);
    g_pGamePersistent->RegisterModel(V);

    // Registration
    if (allow_register)
        Instance_Register(N, V);

    return V;
}

dxRender_Visual* CModelPool::Instance_Load(LPCSTR name, IReader* data, BOOL allow_register)
{
    dxRender_Visual* V;

    ogf_header H;
    std::ignore = data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
    V = Instance_Create(H.type);
    V->Load(name, data, 0);

    // Registration
    if (allow_register)
        Instance_Register(name, V);

    return V;
}

void CModelPool::Instance_Register(LPCSTR N, dxRender_Visual* V) { Models.emplace_back(N, V); }

void CModelPool::Destroy()
{
    // Pool
    Pool.clear();

    // Registry
    while (!Registry.empty())
    {
        auto it = Registry.begin();
        dxRender_Visual* V = (dxRender_Visual*)it->first;

#ifdef DEBUG
        Msg("ModelPool: Destroy object: '{}'", V->dbg_name);
#endif

        DeleteInternal(V, TRUE);
    }

    // Base/Reference
    for (auto& md : Models)
    {
        md.model->Release();
        xr_delete(md.model);
    }

    Models.clear();

    // cleanup motions container
    g_pMotionsContainer->clean(false);

    if (vis_prefetch_ini)
        std::ignore = vis_prefetch_ini->save_as();

    m_prefetched.clear();
}

CModelPool::CModelPool()
{
    bLogging = TRUE;
    bForceDiscard = FALSE;
    bAllowChildrenDuplicate = TRUE;
    g_pMotionsContainer = xr_new<motions_container>();

    string_path fname;
    std::ignore = FS.update_path(fname, "$app_data_root$", "vis_prefetch.ltx");

    if (IReader* F = FS.r_open(fname))
    {
        // Костыль от ситуации когда в редких случаях почему-то у игроков бьётся vis_prefetch.ltx - оказывается набит нулями, в результате чего игра не
        // запускается. Не понятно почему так происходит, поэтому сделал тут обработку такой ситуации.

        if (F->elapsed() >= gsl::index{sizeof(u8)} && F->r_u8() == 0)
        {
            Msg("!![{}] file [{}] broken!", std::source_location::current().function_name(), fname);

            FS.r_close(F);
            FS.file_delete(fname);

            F = nullptr;
        }

        if (F)
            FS.r_close(F);
    }

    vis_prefetch_ini = xr_new<CInifile>(fname, false);
    process_vis_prefetch();
}

CModelPool::~CModelPool()
{
    Destroy();

    xr_delete(g_pMotionsContainer);
    xr_delete(vis_prefetch_ini);
}

dxRender_Visual* CModelPool::Instance_Find(LPCSTR N)
{
    for (const auto& model : Models)
    {
        if (std::is_eq(xr_strcmp(model.name, N)))
            return model.model;
    }

    return nullptr;
}

dxRender_Visual* CModelPool::Create(const char* name, IReader* data)
{
    string_path low_name;
    XR_ASSERT(name != nullptr && xr_strlen(name) < gsl::index{sizeof(low_name)});

    xr_strcpy(low_name, name);
    _strlwr(low_name);

    if (const auto ext = strext(low_name); ext != nullptr)
        *ext = '\0';

    // 0. Search POOL
    auto it = Pool.find(low_name);
    if (it != Pool.end())
    {
        // 1. Instance found
        dxRender_Visual* Model = it->second;
        Model->Spawn();
        Pool.erase(it);
        refresh_prefetch(low_name, Model->IsHudVisual);
        return Model;
    }
    else
    {
        // 1. Search for already loaded model (reference, base model)
        dxRender_Visual* Base = Instance_Find(low_name);
        if (!Base)
        {
            // 2. If not found
            bAllowChildrenDuplicate = FALSE;

            if (data)
                Base = Instance_Load(low_name, data, TRUE);
            else
                Base = Instance_Load(low_name, TRUE);

            bAllowChildrenDuplicate = TRUE;
        }

        // 3. If found - return (cloned) reference
        dxRender_Visual* Model = Instance_Duplicate(Base);
        Registry.emplace(Model, low_name);

        refresh_prefetch(low_name, Model->IsHudVisual);

        return Model;
    }
}

void CModelPool::refresh_prefetch(const char* low_name, const bool is_hud_visual)
{
    if (now_prefetch2)
        return;

    const std::string_view s{low_name};
    if (m_prefetched.find(s) != m_prefetched.end())
        return;

    if (now_prefetch1)
    {
        m_prefetched.emplace(s, true);
    }
    else if (vis_prefetch_ini)
    {
        bool is_global = !!FS.exist("$game_meshes$", shared_str{xr::format("{}.ogf", s)}.c_str());
        if (is_global)
            vis_prefetch_ini->w_fvector2("prefetch", low_name, Fvector2{2.f, is_hud_visual ? 2.f : 1.f});
    }
}

dxRender_Visual* CModelPool::CreateChild(LPCSTR name, IReader* data)
{
    string256 low_name;
    XR_ASSERT(name != nullptr && xr_strlen(name) < 256);

    xr_strcpy(low_name, name);
    _strlwr(low_name);

    if (const auto ext = strext(low_name); ext != nullptr)
        *ext = '\0';

    // 1. Search for already loaded model
    dxRender_Visual* Base = Instance_Find(low_name);
    if (!Base)
    {
        if (data)
            Base = Instance_Load(low_name, data, FALSE);
        else
            Base = Instance_Load(low_name, FALSE);
    }

    dxRender_Visual* Model = bAllowChildrenDuplicate ? Instance_Duplicate(Base) : Base;
    return Model;
}

void CModelPool::DeleteInternal(dxRender_Visual*& V, BOOL bDiscard)
{
    XR_ASSERT(!g_bRendering);

    if (!V)
        return;

    V->Depart();

    if (bDiscard || bForceDiscard)
    {
        Discard(V, TRUE);
    }
    else
    {
        //
        auto it = Registry.find(V);
        if (it != Registry.end())
        {
            // Registry entry found - move it to pool
            Pool.emplace(it->second, V);
        }
        else
        {
            // Registry entry not-found - just special type of visual / particles / etc.
            xr_delete(V);
        }
    }

    V = nullptr;
}

void CModelPool::Delete(dxRender_Visual*& V, BOOL bDiscard)
{
    if (!V)
        return;

    if (g_bRendering)
    {
        XR_ASSERT(!bDiscard);
        ModelsToDelete.push_back(V);
    }
    else
    {
        DeleteInternal(V, bDiscard);
    }

    V = nullptr;
}

void CModelPool::DeleteQueue()
{
    for (u32 it = 0; it < ModelsToDelete.size(); it++)
        DeleteInternal(ModelsToDelete[it]);

    ModelsToDelete.clear();
}

void CModelPool::Discard(dxRender_Visual*& V, BOOL b_complete)
{
    //
    auto it = Registry.find(V);
    if (it != Registry.end())
    {
        // Pool - OK

        // Base
        const shared_str& name = it->second;
        auto I = Models.begin();
        auto I_e = Models.end();

        for (; I != I_e; ++I)
        {
            if (I->name == name)
            {
                if (b_complete || std::string_view{name}.contains('#'))
                {
                    XR_ASSERT(I->refs > 0);

                    if (--I->refs == 0)
                    {
                        bForceDiscard = TRUE;
                        I->model->Release();
                        xr_delete(I->model);
                        Models.erase(I);
                        bForceDiscard = FALSE;
                    }

                    break;
                }
                else
                {
                    if (I->refs > 0)
                        I->refs--;

                    break;
                }
            }
        }

        // Registry
        xr_delete(V);
        Registry.erase(it);
    }
    else
    {
        // Registry entry not-found - just special type of visual / particles / etc.
        xr_delete(V);
    }

    V = nullptr;
}

void CModelPool::Prefetch()
{
    Logging(FALSE);
    begin_prefetch1(true);
    // prefetch visuals
    CTimer timer;
    timer.Start();
    u32 cnt = 0;

    string256 section;
    strconcat(sizeof(section), section, "prefetch_visuals_", g_pGamePersistent->m_game_params.m_game_type);
    if (pSettings->section_exist(section))
    {
        const auto& sect = pSettings->r_section(section);
        for (const auto& pair : sect.Data)
        {
            const auto& low_name = pair.first;
            if (!Instance_Find(low_name.c_str()))
            {
                if (const auto fname = shared_str{xr::format("{}.ogf", low_name)}; FS.exist("$game_meshes$", fname.c_str()))
                {
                    dxRender_Visual* V = Create(low_name.c_str());
                    Delete(V, FALSE);
                    cnt++;
                }
                else
                {
                    Msg("! [{}]: {} not found in $game_meshes$", std::source_location::current().function_name(), fname);
                }
            }
        }
    }
    begin_prefetch1(false);

    if (!vis_prefetch_ini || !vis_prefetch_ini->section_exist("prefetch"))
    {
        Msg("[{}] models prefetching time ({}): [{:.3} s.]", std::source_location::current().function_name(), cnt, timer.GetElapsed_sec());
        return;
    }

    now_prefetch2 = true;
    const auto& sect = vis_prefetch_ini->r_section("prefetch");

    for (const auto& [low_name, val0] : sect.Data)
    {
        const std::string_view val{val0};
        const auto res = scn::scan<f32, f32>(val, "{},{}");
        XR_ASSERT(res, res.error().msg(), low_name, val);

        const auto [val1, val2] = res->values();

        if (!Instance_Find(low_name.c_str()))
        {
            if (const auto fname = shared_str{xr::format("{}.ogf", low_name)}; FS.exist("$game_meshes$", fname.c_str()))
            {
                RImplementation.hud_loading = fsimilar(val2, 2.0f);
                dxRender_Visual* V = Create(low_name.c_str());
                RImplementation.hud_loading = false;
                Delete(V, FALSE);
                cnt++;
            }
            else
            {
                Msg("! [{}]: {} not found in $game_meshes$", std::source_location::current().function_name(), fname);
            }
        }
    }

    now_prefetch2 = false;
    Logging(TRUE);

    Msg("[{}] models prefetching time ({}): [{:.3} s.]", std::source_location::current().function_name(), cnt, timer.GetElapsed_sec());
}

void CModelPool::ClearPool(BOOL b_complete)
{
    for (auto& I : Pool)
    {
        if (!b_complete && vis_prefetch_ini)
        {
            std::string s(I.first.c_str());
            if (m_prefetched.find(s) == m_prefetched.end() && !vis_prefetch_ini->line_exist("prefetch", I.first.c_str()))
                b_complete = TRUE;
        }
        Discard(I.second, b_complete);
    }
    Pool.clear();
}

dxRender_Visual* CModelPool::CreatePE(PS::CPEDef* source)
{
    PS::CParticleEffect* V = (PS::CParticleEffect*)Instance_Create(MT_PARTICLE_EFFECT);
    V->Compile(source);
    return V;
}

dxRender_Visual* CModelPool::CreatePG(PS::CPGDef* source)
{
    PS::CParticleGroup* V = (PS::CParticleGroup*)Instance_Create(MT_PARTICLE_GROUP);
    V->Compile(source);
    return V;
}

void CModelPool::dump()
{
    XR_LOG_TRACE_L1("--- model pool --- begin:");

    gsl::index sz{};
    u32 k = 0;
    for (xr_vector<ModelDef>::iterator I = Models.begin(); I != Models.end(); I++)
    {
        CKinematics* K = PCKinematics(I->model);
        if (K)
        {
            const auto cur = K->mem_usage(false);
            sz += cur;

            Msg("#{:3}: [{:3}/{:5} Kb] - {}", k++, I->refs, cur / 1024, I->name);
        }
    }

    Msg("--- models: {}, mem usage: {} Kb ", k, sz / 1024);

    sz = 0;
    k = 0;
    u32 free_cnt{};

    for (auto it = Registry.begin(); it != Registry.end(); it++)
    {
        auto K = XR_ASSERT_VAL(PCKinematics(smart_cast<dxRender_Visual*>(it->first)) != nullptr);
        const auto cur = K->mem_usage(true);
        sz += cur;

        const bool b_free = Pool.find(it->second) != Pool.end();
        if (b_free)
            ++free_cnt;

        Msg("#{:3}: [{}] [{:5} Kb] - {}", k++, b_free ? "free" : "used", cur / 1024, it->second);
    }

    Msg("--- instances: {}, free {}, mem usage: {} Kb ", k, free_cnt, sz / 1024);
    XR_LOG_TRACE_L1("--- model pool --- end");
}

void CModelPool::memory_stats(u32& vb_mem_video, u32& vb_mem_system, u32& ib_mem_video, u32& ib_mem_system)
{
    vb_mem_video = 0;
    vb_mem_system = 0;
    ib_mem_video = 0;
    ib_mem_system = 0;

    for (const auto& item : Models)
    {
        const auto vis = smart_cast<const Fvisual*>(item.model);
        if (vis == nullptr)
            continue;

        D3D_BUFFER_DESC IB_desc;
        vis->p_rm_Indices->GetDesc(&IB_desc);
        ib_mem_video += IB_desc.ByteWidth;
        ib_mem_system += IB_desc.ByteWidth;

        D3D_BUFFER_DESC VB_desc;
        vis->p_rm_Vertices->GetDesc(&VB_desc);
        vb_mem_video += IB_desc.ByteWidth;
        vb_mem_system += IB_desc.ByteWidth;

        if (vis->m_fast != nullptr)
        {
            vis->m_fast->p_rm_Indices->GetDesc(&IB_desc);
            ib_mem_video += IB_desc.ByteWidth;
            ib_mem_system += IB_desc.ByteWidth;

            vis->m_fast->p_rm_Vertices->GetDesc(&VB_desc);
            vb_mem_video += IB_desc.ByteWidth;
            vb_mem_system += IB_desc.ByteWidth;
        }
    }
}

void CModelPool::save_vis_prefetch()
{
    if (vis_prefetch_ini)
    {
        process_vis_prefetch();
        std::ignore = vis_prefetch_ini->save_as();
    }
}

void CModelPool::process_vis_prefetch()
{
    if (!vis_prefetch_ini->section_exist("prefetch"))
        return;

    xr_vector<const char*> expired;
    const auto& sect = vis_prefetch_ini->r_section("prefetch");

    for (const auto& [key, val0] : sect.Data)
    {
        const std::string_view val{val0};
        const auto res = scn::scan<f32, f32>(val, "{},{}");
        XR_ASSERT(res, res.error().msg(), key, val);

        auto [val1, val2] = res->values();
        // скорость уменьшения популярности визуала
        const float need = val1 * 0.8f;
        // -0.5..+0.5 - добавить случайность, чтобы не было общего выключения
        const float rnd = Random.randF() - 0.5f;

        val1 = need + rnd * 0.1f;
        if (val1 > 0.1f && val2 > 0.0f)
            vis_prefetch_ini->w_fvector2("prefetch", key.c_str(), Fvector2{val1, val2});
        else
            expired.emplace_back(key.c_str());
    }

    for (const char* s : expired)
        vis_prefetch_ini->remove_line("prefetch", s);
}

void CModelPool::begin_prefetch1(bool val) { now_prefetch1 = val; }
