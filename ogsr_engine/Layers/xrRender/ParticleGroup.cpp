#include "stdafx.h"

#include "ParticleGroup.h"

#include "PSLibrary.h"
#include "ParticleEffect.h"

#include "../../xrParticles/psystem.h"

using namespace PS;

//------------------------------------------------------------------------------

CPGDef::CPGDef()
{
    m_Flags.zero();
    m_fTimeLimit = 0.f;
}

CPGDef::~CPGDef()
{
    for (auto& e : m_Effects)
        xr_delete(e);

    m_Effects.clear();
}

//------------------------------------------------------------------------------
// I/O part
//------------------------------------------------------------------------------

BOOL CPGDef::Load(IReader& F)
{
    XR_ASSERT(F.find_chunk(PGD_CHUNK_VERSION) > 0);
    if (F.r_u16() != PGD_VERSION)
    {
        XR_LOG_ERROR("Unsupported PG version. Load failed");
        return FALSE;
    }

    XR_ASSERT(F.find_chunk(PGD_CHUNK_NAME) > 0);
    F.r_stringZ(m_Name);

    std::ignore = F.r_chunk(PGD_CHUNK_FLAGS, &m_Flags);

    if (F.find_chunk(PGD_CHUNK_TIME_LIMIT))
        m_fTimeLimit = F.r_float();
    else
        m_fTimeLimit = 0.0f;

    bool dont_calc_timelimit = m_fTimeLimit > 0.0f;

    if (F.find_chunk(PGD_CHUNK_EFFECTS))
    {
        m_Effects.resize(F.r_u32());

        for (auto& e : m_Effects)
        {
            e = xr_new<SEffect>();
            F.r_stringZ(e->m_EffectName);
            F.r_stringZ(e->m_OnPlayChildName);
            F.r_stringZ(e->m_OnBirthChildName);
            F.r_stringZ(e->m_OnDeadChildName);
            e->m_Time0 = F.r_float();
            e->m_Time1 = F.r_float();
            e->m_Flags.assign(F.r_u32());

            if (!dont_calc_timelimit)
                m_fTimeLimit = _max(m_fTimeLimit, e->m_Time1);
        }
    }

    return TRUE;
}

BOOL CPGDef::Load2(CInifile& ini)
{
    m_Flags.assign(ini.r_u32("_group", "flags"));
    m_Effects.resize(ini.r_u32("_group", "effects_count"));
    m_fTimeLimit = ini.r_float("_group", "timelimit");

    for (auto [counter, eff] : std::views::enumerate(m_Effects))
    {
        const auto sect = xr::format("effect_{:04}", counter);
        eff = xr_new<SEffect>();

        eff->m_EffectName._set(ini.r_string(sect.c_str(), "effect_name"));
        eff->m_OnPlayChildName._set(ini.r_string(sect.c_str(), "on_play_child"));
        eff->m_OnBirthChildName._set(ini.r_string(sect.c_str(), "on_birth_child"));
        eff->m_OnDeadChildName._set(ini.r_string(sect.c_str(), "on_death_child"));

        eff->m_Time0 = ini.r_float(sect.c_str(), "time0");
        eff->m_Time1 = ini.r_float(sect.c_str(), "time1");
        eff->m_Flags.assign(ini.r_u32(sect.c_str(), "flags"));
    }

    return TRUE;
}

void CPGDef::Save(IWriter& F)
{
    F.open_chunk(PGD_CHUNK_VERSION);
    F.w_u16(PGD_VERSION);
    F.close_chunk();

    F.open_chunk(PGD_CHUNK_NAME);
    F.w_stringZ(m_Name);
    F.close_chunk();

    F.w_chunk(PGD_CHUNK_FLAGS, &m_Flags, sizeof(m_Flags));

    F.open_chunk(PGD_CHUNK_EFFECTS);
    F.w_u32(m_Effects.size());

    for (const auto e : m_Effects)
    {
        F.w_stringZ(e->m_EffectName);
        F.w_stringZ(e->m_OnPlayChildName);
        F.w_stringZ(e->m_OnBirthChildName);
        F.w_stringZ(e->m_OnDeadChildName);
        F.w_float(e->m_Time0);
        F.w_float(e->m_Time1);
        F.w_u32(e->m_Flags.get());
    }

    F.close_chunk();

    F.open_chunk(PGD_CHUNK_TIME_LIMIT);
    F.w_float(m_fTimeLimit);
    F.close_chunk();
}

void CPGDef::Save2(CInifile& ini)
{
    ini.w_u16("_group", "version", PGD_VERSION);
    ini.w_u32("_group", "flags", m_Flags.get());
    ini.w_u32("_group", "effects_count", m_Effects.size());
    ini.w_float("_group", "timelimit", m_fTimeLimit);

    for (auto [counter, eff] : std::views::enumerate(m_Effects))
    {
        const auto sect = xr::format("effect_{:04}", counter);

        ini.w_string(sect.c_str(), "effect_name", eff->m_EffectName.c_str());
        ini.w_string(sect.c_str(), "on_play_child", eff->m_Flags.test(SEffect::flOnPlayChild) ? eff->m_OnPlayChildName.c_str() : "");
        ini.w_string(sect.c_str(), "on_birth_child", eff->m_Flags.test(SEffect::flOnBirthChild) ? eff->m_OnBirthChildName.c_str() : "");
        ini.w_string(sect.c_str(), "on_death_child", eff->m_Flags.test(SEffect::flOnDeadChild) ? eff->m_OnDeadChildName.c_str() : "");
        ini.w_float(sect.c_str(), "time0", eff->m_Time0);
        ini.w_float(sect.c_str(), "time1", eff->m_Time1);
        ini.w_u32(sect.c_str(), "flags", eff->m_Flags.get());
    }
}

//------------------------------------------------------------------------------
// Particle Group item
//------------------------------------------------------------------------------

void CParticleGroup::SItem::Set(dxRender_Visual* e) { _effect = e; }

void CParticleGroup::SItem::Clear()
{
    VisualVec visuals;
    GetVisuals(visuals);

    for (auto& visual : visuals)
    {
        RImplementation.model_Delete((IRenderVisual*&)visual, false);
        visual = nullptr;
    }

    //	Igor: zero all pointers! Previous code didn't zero _source_ pointers,
    //	just temporary ones.
    _effect = nullptr;
    _children_related.clear();
    _children_free.clear();
}

void CParticleGroup::SItem::StartRelatedChild(CParticleEffect* emitter, LPCSTR eff_name, PAPI::Particle& m)
{
    auto C = smart_cast<CParticleEffect*>(RImplementation.model_CreatePE(eff_name));
    C->SetHudMode(emitter->GetHudMode());

    Fmatrix M;
    M.identity();
    Fvector vel;
    vel.sub(m.pos, m.posB);
    vel.div(C->m_Def->GetFStep());

    if (emitter->m_RT_Flags.is(CParticleEffect::flRT_XFORM))
    {
        M.set(emitter->m_XFORM);
        M.transform_dir(vel);
    }

    Fvector p;
    M.transform_tiny(p, m.pos);
    M.c.set(p);

    C->Play();
    C->UpdateParent(M, vel, FALSE);

    _children_related.push_back(C);
}

void CParticleGroup::SItem::StopRelatedChild(u32 idx)
{
    dxRender_Visual*& V = _children_related[idx];
    smart_cast<CParticleEffect*>(V)->Stop(true);

    _children_free.push_back(V);
    _children_related[idx] = _children_related.back();
    _children_related.pop_back();
}

void CParticleGroup::SItem::StartFreeChild(CParticleEffect* emitter, LPCSTR nm, PAPI::Particle& m)
{
    auto C = smart_cast<CParticleEffect*>(RImplementation.model_CreatePE(nm));

    C->SetHudMode(emitter->GetHudMode());
    XR_ASSERT(!C->IsLooped(), "can't use looped effect as 'On Birth' child for group", nm);

    Fmatrix M;
    M.identity();

    Fvector vel;
    vel.sub(m.pos, m.posB);
    vel.div(C->m_Def->GetFStep());

    if (emitter->m_RT_Flags.is(CParticleEffect::flRT_XFORM))
    {
        M.set(emitter->m_XFORM);
        M.transform_dir(vel);
    }

    Fvector p;
    M.transform_tiny(p, m.pos);
    M.c.set(p);

    C->Play();
    C->UpdateParent(M, vel, FALSE);

    _children_free.push_back(C);
}

void CParticleGroup::SItem::Play()
{
    if (const auto E = smart_cast<CParticleEffect*>(_effect); E != nullptr)
        E->Play();
}

void CParticleGroup::SItem::Stop(BOOL def_stop)
{
    // stop all effects
    if (const auto E = smart_cast<CParticleEffect*>(_effect); E != nullptr)
        E->Stop(def_stop);

    for (auto p : _children_related)
        smart_cast<CParticleEffect*>(p)->Stop(def_stop);

    for (auto p : _children_free)
        smart_cast<CParticleEffect*>(p)->Stop(def_stop);

    // and delete if !deffered
    if (def_stop)
        return;

    for (auto& child : _children_related)
    {
        RImplementation.model_Delete((IRenderVisual*&)child, false);
        child = nullptr;
    }

    for (auto& child : _children_free)
    {
        RImplementation.model_Delete((IRenderVisual*&)child, false);
        child = nullptr;
    }

    _children_related.clear();
    _children_free.clear();
}

BOOL CParticleGroup::SItem::IsPlaying() const
{
    if (const auto E = smart_cast<CParticleEffect*>(_effect); E != nullptr)
        return E->IsPlaying();

    return false;
}

void CParticleGroup::SItem::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)
{
    if (const auto E = smart_cast<CParticleEffect*>(_effect); E != nullptr)
        E->UpdateParent(m, velocity, bXFORM);
}

//------------------------------------------------------------------------------

namespace
{
void OnGroupParticleBirth(void* owner, u32 param, PAPI::Particle& m, u32 idx)
{
    auto PG = static_cast<CParticleGroup*>(XR_ASSERT_VAL(owner != nullptr));
    auto PE = smart_cast<CParticleEffect*>(PG->items[param]._effect);

    PS::OnEffectParticleBirth(PE, param, m, idx);

    // if have child
    const CPGDef::SEffect* eff = XR_ASSERT_VAL(PG->GetDefinition() != nullptr)->m_Effects[param];

    if (eff->m_Flags.is(CPGDef::SEffect::flOnBirthChild))
        PG->items[param].StartFreeChild(PE, eff->m_OnBirthChildName.c_str(), m);

    if (eff->m_Flags.is(CPGDef::SEffect::flOnPlayChild))
        PG->items[param].StartRelatedChild(PE, eff->m_OnPlayChildName.c_str(), m);
}

void OnGroupParticleDead(void* owner, u32 param, PAPI::Particle& m, u32 idx)
{
    auto PG = static_cast<CParticleGroup*>(XR_ASSERT_VAL(owner != nullptr));
    auto PE = smart_cast<CParticleEffect*>(PG->items[param]._effect);

    PS::OnEffectParticleDead(PE, param, m, idx);

    // if have child
    const CPGDef* PGD = XR_ASSERT_VAL(PG->GetDefinition() != nullptr);
    const CPGDef::SEffect* eff = PGD->m_Effects[param];

    if (eff->m_Flags.is(CPGDef::SEffect::flOnPlayChild))
        PG->items[param].StopRelatedChild(idx);

    if (eff->m_Flags.is(CPGDef::SEffect::flOnDeadChild))
        PG->items[param].StartFreeChild(PE, eff->m_OnDeadChildName.c_str(), m);
}
} // namespace

//------------------------------------------------------------------------------

void CParticleGroup::SItem::OnFrame(u32 u_dt, const CPGDef::SEffect& def, Fbox& box, bool& bPlaying)
{
    XR_TRACY_ZONE_SCOPED();

    if (const auto E = smart_cast<CParticleEffect*>(_effect); E != nullptr)
    {
        E->OnFrame(u_dt);

        if (E->IsPlaying())
        {
            bPlaying = true;

            if (E->vis.box.is_valid())
                box.merge(E->vis.box);

            if (def.m_Flags.is(CPGDef::SEffect::flOnPlayChild) && !def.m_OnPlayChildName.empty())
            {
                PAPI::Particle* particles;
                u32 p_cnt;
                PAPI::ParticleManager()->GetParticles(E->GetHandleEffect(), particles, p_cnt);
                XR_ASSERT(p_cnt == _children_related.size());

                for (auto [i, rel] : std::views::enumerate(_children_related))
                {
                    auto C = smart_cast<CParticleEffect*>(rel);
                    PAPI::Particle& m = particles[i];

                    Fmatrix M;
                    M.translate(m.pos);
                    Fvector vel;
                    vel.sub(m.pos, m.posB);
                    vel.div(C->m_Def->GetFStep());

                    C->UpdateParent(M, vel, FALSE);
                }
            }
        }
    }

    for (auto p : _children_related)
    {
        auto E = smart_cast<CParticleEffect*>(p);
        if (E == nullptr)
            continue;

        E->OnFrame(u_dt);

        if (E->IsPlaying())
        {
            bPlaying = true;

            if (E->vis.box.is_valid())
                box.merge(E->vis.box);
        }
        else
        {
            if (def.m_Flags.is(CPGDef::SEffect::flOnPlayChildRewind))
                E->Play();
        }
    }

    u32 rem_cnt = 0;

    for (auto& child : _children_free)
    {
        auto E = smart_cast<CParticleEffect*>(child);
        if (E == nullptr)
            continue;

        E->OnFrame(u_dt);

        if (E->IsPlaying())
        {
            bPlaying = true;

            if (E->vis.box.is_valid())
                box.merge(E->vis.box);
        }
        else
        {
            rem_cnt++;
            RImplementation.model_Delete((IRenderVisual*&)child, false);
            child = nullptr;
        }
    }

    // remove if stopped
    if (rem_cnt > 0)
    {
        const auto rng = std::ranges::remove_if(_children_free, [] [[nodiscard]] (const auto x) { return x == nullptr; });
        _children_free.erase(rng.begin(), rng.end());
    }
}

void CParticleGroup::SItem::OnDeviceCreate()
{
    VisualVec visuals;
    GetVisuals(visuals);

    for (auto visual : visuals)
        smart_cast<CParticleEffect*>(visual)->OnDeviceCreate();
}

void CParticleGroup::SItem::OnDeviceDestroy()
{
    VisualVec visuals;
    GetVisuals(visuals);

    for (auto visual : visuals)
        smart_cast<CParticleEffect*>(visual)->OnDeviceDestroy();
}

u32 CParticleGroup::SItem::ParticlesCount()
{
    VisualVec visuals;
    GetVisuals(visuals);

    u32 p_count{0};

    for (auto visual : visuals)
        p_count += smart_cast<CParticleEffect*>(visual)->ParticlesCount();

    return p_count;
}

//------------------------------------------------------------------------------
// Particle Group part
//------------------------------------------------------------------------------

CParticleGroup::CParticleGroup()
{
    m_RT_Flags.zero();
    m_InitialPosition.set(0, 0, 0);
}

CParticleGroup::~CParticleGroup()
{
    for (auto& pg : items)
        pg.Clear();

    items.clear();
}

void CParticleGroup::OnFrame(u32 u_dt)
{
    XR_TRACY_ZONE_SCOPED();

    std::scoped_lock slock(lock);

    if (m_Def && m_RT_Flags.is(flRT_Playing))
    {
        float ct = m_CurrentTime;
        float f_dt = float(u_dt) / 1000.f;

        for (auto [i, eff] : std::views::enumerate(m_Def->m_Effects))
        {
            if (eff->m_Flags.is(CPGDef::SEffect::flEnabled))
            {
                XR_ASSERT(items.size() == m_Def->m_Effects.size());
                SItem& I = items[i];

                if (I.IsPlaying())
                {
                    if (ct <= eff->m_Time1 && ct + f_dt >= eff->m_Time1)
                        I.Stop(eff->m_Flags.is(CPGDef::SEffect::flDefferedStop));
                }
                else
                {
                    if (!m_RT_Flags.is(flRT_DefferedStop) && ct <= eff->m_Time0 && ct + f_dt >= eff->m_Time0)
                        I.Play();
                }
            }
        }

        m_CurrentTime += f_dt;
        if (!m_RT_Flags.is(flRT_DefferedStop) && m_Def->m_fTimeLimit > 0.0f && m_CurrentTime > m_Def->m_fTimeLimit)
            Stop(true);

        bool bPlaying = false;
        Fbox box;
        box.invalidate();

        for (auto [i, item] : std::views::enumerate(items))
            item.OnFrame(u_dt, *m_Def->m_Effects[i], box, bPlaying);

        if (m_RT_Flags.is(flRT_DefferedStop) && !bPlaying)
            m_RT_Flags.set(flRT_Playing | flRT_DefferedStop, FALSE);

        if (box.is_valid())
        {
            vis.box.set(box);
            vis.box.getsphere(vis.sphere.P, vis.sphere.R);
        }
    }
    else
    {
        vis.box.set(m_InitialPosition, m_InitialPosition);
        vis.box.grow(EPS_L);
        vis.box.getsphere(vis.sphere.P, vis.sphere.R);
    }
}

void CParticleGroup::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)
{
    std::scoped_lock slock(lock);

    m_InitialPosition = m.c;

    for (auto& item : items)
        item.UpdateParent(m, velocity, bXFORM);
}

BOOL CParticleGroup::Compile(CPGDef* def)
{
    m_Def = def;

    // destroy existing
    for (auto& item : items)
        item.Clear();

    items.clear();

    // create new
    if (def == nullptr)
        return true;

    items.resize(def->m_Effects.size());

    for (auto [i, e] : std::views::enumerate(def->m_Effects))
    {
        auto eff = smart_cast<CParticleEffect*>(RImplementation.model_CreatePE(e->m_EffectName.c_str()));

        eff->SetBirthDeadCB(OnGroupParticleBirth, OnGroupParticleDead, this, i);
        items[i].Set(eff);
    }

    return TRUE;
}

void CParticleGroup::Play()
{
    m_CurrentTime = 0;

    m_RT_Flags.set(flRT_DefferedStop, FALSE);
    m_RT_Flags.set(flRT_Playing, TRUE);
}

void CParticleGroup::Stop(BOOL bDefferedStop)
{
    std::scoped_lock slock(lock);

    if (bDefferedStop)
        m_RT_Flags.set(flRT_DefferedStop, TRUE);
    else
        m_RT_Flags.set(flRT_Playing, FALSE);

    for (auto& item : items)
        item.Stop(bDefferedStop);
}

void CParticleGroup::OnDeviceCreate()
{
    for (auto& item : items)
        item.OnDeviceCreate();
}

void CParticleGroup::OnDeviceDestroy()
{
    for (auto& item : items)
        item.OnDeviceDestroy();
}

u32 CParticleGroup::ParticlesCount()
{
    std::scoped_lock slock(lock);

    u32 p_count{0};

    for (auto& item : items)
        p_count += item.ParticlesCount();

    return p_count;
}

void CParticleGroup::SetHudMode(BOOL b)
{
    std::scoped_lock slock(lock);

    for (auto& item : items)
        smart_cast<CParticleEffect*>(item._effect)->SetHudMode(b);
}

BOOL CParticleGroup::GetHudMode()
{
    std::scoped_lock slock(lock);

    if (!items.empty())
        return smart_cast<CParticleEffect*>(items.front()._effect)->GetHudMode();

    return false;
}
