//---------------------------------------------------------------------------
#include "stdafx.h"

#include "SkeletonAnimated.h"

#include "AnimationKeyCalculate.h"
#include "SkeletonX.h"
#include "../../xr_3da/fmesh.h"

#ifdef DEBUG
#include "../../xrcore/dump_string.h"
#endif

using namespace animation;

//////////////////////////////////////////////////////////////////////////
// BoneInstance methods

void CBlendInstance::construct() { Blend.clear(); }

void CBlendInstance::blend_add(CBlend* H)
{
    if (Blend.size() == MAX_BLENDED)
    {
        if (H->fall_at_end)
            return;

        auto _d = Blend.begin();
        for (auto it = Blend.begin() + 1; it != Blend.end(); it++)
        {
            if ((*it)->blendAmount < (*_d)->blendAmount)
                _d = it;
        }

        Blend.erase(_d);
    }

    XR_ASSERT(Blend.size() < MAX_BLENDED);
    Blend.emplace_back(H);
}

void CBlendInstance::blend_remove(CBlend* H)
{
    if (const auto I = std::ranges::find(Blend, H); I != Blend.end())
        Blend.erase(I);
}

// Motion control
void CKinematicsAnimated::Bone_Motion_Start(CBoneData* bd, CBlend* handle)
{
    LL_GetBlendInstance(bd->GetSelfID()).blend_add(handle);

    for (auto& it : bd->children)
        Bone_Motion_Start(it, handle);
}

void CKinematicsAnimated::Bone_Motion_Stop(CBoneData* bd, CBlend* handle)
{
    LL_GetBlendInstance(bd->GetSelfID()).blend_remove(handle);

    for (auto& it : bd->children)
        Bone_Motion_Stop(it, handle);
}

void CKinematicsAnimated::Bone_Motion_Start_IM(CBoneData* bd, CBlend* handle) { LL_GetBlendInstance(bd->GetSelfID()).blend_add(handle); }
void CKinematicsAnimated::Bone_Motion_Stop_IM(CBoneData* bd, CBlend* handle) { LL_GetBlendInstance(bd->GetSelfID()).blend_remove(handle); }

std::pair<LPCSTR, LPCSTR> CKinematicsAnimated::LL_MotionDefName_dbg(MotionID ID)
{
    shared_motions& s_mots = m_Motions[ID.slot].motions;
    for (auto& it : *s_mots.motion_map())
    {
        if (it.second == ID.idx)
            return std::make_pair(it.first.c_str(), s_mots.id().c_str());
    }

    return std::make_pair((LPCSTR) nullptr, (LPCSTR) nullptr);
}

static LPCSTR name_bool(BOOL v)
{
    static constexpr xr_token token_bool[] = {{"false", 0}, {"true", 1}};
    return get_token_name(token_bool, v);
}

static LPCSTR name_blend_type(CBlend::ECurvature blend)
{
    static constexpr xr_token token_blend[] = {{"eFREE_SLOT", CBlend::eFREE_SLOT}, {"eAccrue", CBlend::eAccrue}, {"eFalloff", CBlend::eFalloff}};
    return get_token_name(token_blend, blend);
}

static void dump_blend(CKinematicsAnimated* K, CBlend& B, u32 index)
{
    XR_ASSERT(K != nullptr);

    XR_LOG_TRACE_L1("----------------------------------------------------------");

    Msg("blend index: {} ", index);
    Msg("time total: {}, speed: {} , power: {} ", B.timeTotal, B.speed, B.blendPower);
    Msg("ammount: {}, time current: {}, frame {} ", B.blendAmount, B.timeCurrent, B.dwFrame);
    Msg("accrue: {}, fallof: {} ", B.blendAccrue, B.blendFalloff);

    Msg("bonepart: {}, channel: {}, stop_at_end: {}, fall_at_end: {} ", B.bone_or_part, B.channel, name_bool(B.stop_at_end), name_bool(B.fall_at_end));
    Msg("state: {}, playing: {}, stop_at_end_callback: {} ", name_blend_type(B.blend_state()), name_bool(B.playing), name_bool(B.stop_at_end_callback));

    if (B.blend_state() != CBlend::eFREE_SLOT)
        Msg("motion : name {}, set: {} ", K->LL_MotionDefName_dbg(B.motionID).first, K->LL_MotionDefName_dbg(B.motionID).second);

    XR_LOG_TRACE_L1("----------------------------------------------------------");
}

void CKinematicsAnimated::LL_DumpBlends_dbg()
{
    XR_LOG_TRACE_L1("==================dump blends=================================================");

    for (auto [idx, blend] : std::views::enumerate(blend_pool))
        dump_blend(this, blend, idx);
}

u32 CKinematicsAnimated::LL_PartBlendsCount(u32 bone_part_id) { return blend_cycle(bone_part_id).size(); }

CBlend* CKinematicsAnimated::LL_PartBlend(u32 bone_part_id, u32 n)
{
    if (LL_PartBlendsCount(bone_part_id) <= n)
        return nullptr;

    return blend_cycle(bone_part_id)[n];
}

void CKinematicsAnimated::LL_IterateBlends(IterateBlendsCallback& callback)
{
    for (auto& it : blend_pool)
    {
        if (it.blend_state() != CBlend::eFREE_SLOT)
            callback(it);
    }
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

MotionID CKinematicsAnimated::LL_MotionID(LPCSTR B)
{
    MotionID motion_ID;
    for (int k = int(m_Motions.size()) - 1; k >= 0; --k)
    {
        shared_motions* s_mots = &m_Motions[k].motions;
        auto I = s_mots->motion_map()->find(B);
        if (I != s_mots->motion_map()->end())
        {
            motion_ID.set(u16(k), I->second);
            break;
        }
    }
    return motion_ID;
}

u16 CKinematicsAnimated::LL_PartID(LPCSTR B)
{
    if (!m_Partition)
        return BI_NONE;

    for (u16 id = 0; id < MAX_PARTS; id++)
    {
        CPartDef& P{(*m_Partition)[id]};
        if (!P.Name)
            continue;

        if (std::is_eq(xr::strcasecmp(B, P.Name)))
            return id;
    }

    return BI_NONE;
}

// cycles
MotionID CKinematicsAnimated::ID_Cycle(const shared_str& N) { return XR_ASSERT_VAL(ID_Cycle_Safe(N), "can't find cycle for model", dbg_name, N); }

MotionID CKinematicsAnimated::ID_Cycle_Safe(const shared_str& N)
{
    MotionID motion_ID;
    for (int k = int(m_Motions.size()) - 1; k >= 0; --k)
    {
        shared_motions* s_mots = &m_Motions[k].motions;
        auto I = s_mots->cycle()->find(N);
        if (I != s_mots->cycle()->end())
        {
            motion_ID.set(u16(k), I->second);
            break;
        }
    }
    return motion_ID;
}

void CKinematicsAnimated::LL_FadeCycle(u16 part, float falloff, u8 mask_channel /*= (1<<0)*/)
{
    auto& Blend = blend_cycles[part];

    for (u32 I = 0; I < Blend.size(); I++)
    {
        CBlend& B = *Blend[I];
        if (!(mask_channel & (1 << B.channel)))
            continue;

        B.set_falloff_state();
        B.blendFalloff = falloff;
        if (B.stop_at_end)
            B.stop_at_end_callback = FALSE; // callback не должен приходить!
    }
}

void CKinematicsAnimated::LL_CloseCycle(u16 part, u8 mask_channel /*= (1<<0)*/)
{
    if (BI_NONE == part)
        return;
    if (part >= MAX_PARTS)
        return;

    // destroy cycle(s)
    auto& cycles = blend_cycles[part];
    auto I = cycles.begin();
    auto E = cycles.end();
    while (I != E)
    {
        CBlend& B = *(*I);

        if (!(mask_channel & (1 << B.channel)))
        {
            ++I;
            continue;
        }

        // B.blend = CBlend::eFREE_SLOT;
        B.set_free_state();

        CPartDef& P = (*m_Partition)[B.bone_or_part];
        for (u32 i = 0; i < P.bones.size(); i++)
            Bone_Motion_Stop_IM((*bones)[P.bones[i]], *I);

        I = cycles.erase(I); // ?
        E = cycles.end();
    }
}

float CKinematicsAnimated::get_animation_length(MotionID motion_ID)
{
    SMotionsSlot& slot = m_Motions[motion_ID.slot];
    const CMotionDef* m_def = slot.motions.motion_def(motion_ID.idx);

    return (*slot.bone_motions[LL_GetBoneRoot()])[motion_ID.idx].GetLength() / (m_def != nullptr ? m_def->Speed() : 1.0f);
}

void CKinematicsAnimated::IBlendSetup(CBlend& B, u16 part, u8 channel, MotionID motion_ID, BOOL bMixing, float blendAccrue, float, float Speed, BOOL noloop,
                                      PlayCallback Callback, LPVOID CallbackParam)
{
    B.set_accrue_state();

    if (bMixing)
        B.blendAmount = EPS_S;
    else
        B.blendAmount = 1.0f;

    B.blendAccrue = blendAccrue;
    B.blendFalloff = 0; // blendFalloff used for previous cycles
    B.blendPower = 1;
    B.speed = Speed;
    B.motionID = motion_ID;
    B.timeCurrent = 0;
    B.timeTotal = (*m_Motions[B.motionID.slot].bone_motions[LL_GetBoneRoot()])[motion_ID.idx].GetLength();
    B.bone_or_part = part;
    B.stop_at_end = noloop;
    B.playing = TRUE;
    B.stop_at_end_callback = TRUE;
    B.Callback = Callback;
    B.CallbackParam = CallbackParam;

    B.channel = XR_ASSERT_VAL(channel < MAX_CHANNELS);
    B.fall_at_end = B.stop_at_end && channel > 1;
}

void CKinematicsAnimated::IFXBlendSetup(CBlend& B, MotionID motion_ID, float blendAccrue, float blendFalloff, float Power, float Speed, u16 bone)
{
    B.set_accrue_state();

    B.blendAmount = EPS_S;
    B.blendAccrue = blendAccrue;
    B.blendFalloff = blendFalloff;
    B.blendPower = Power;
    B.speed = Speed;
    B.motionID = motion_ID;
    B.timeCurrent = 0;
    B.timeTotal = (*m_Motions[B.motionID.slot].bone_motions[bone])[motion_ID.idx].GetLength();
    B.bone_or_part = bone;

    B.playing = TRUE;
    B.stop_at_end_callback = TRUE;
    B.stop_at_end = FALSE;
    //
    B.Callback = nullptr;
    B.CallbackParam = nullptr;

    B.channel = 0;
    B.fall_at_end = FALSE;
}

CBlend* CKinematicsAnimated::LL_PlayCycle(u16 part, MotionID motion_ID, BOOL bMixing, float blendAccrue, float blendFalloff, float Speed, BOOL noloop,
                                          PlayCallback Callback, LPVOID CallbackParam, u8 channel /*=0*/)
{
    // validate and unroll
    if (!motion_ID.valid())
        return nullptr;

    if (BI_NONE == part)
    {
        for (u16 i = 0; i < MAX_PARTS; i++)
            std::ignore = LL_PlayCycle(i, motion_ID, bMixing, blendAccrue, blendFalloff, Speed, noloop, Callback, CallbackParam, channel);

        return nullptr;
    }

    if (part >= MAX_PARTS)
        return nullptr;

    if (!m_Partition->part(part).Name)
        return nullptr;

    // Process old cycles and create _new_
    if (channel == 0)
    {
        _DBG_SINGLE_USE_MARKER;
        if (bMixing)
            LL_FadeCycle(part, blendFalloff, 1 << channel);
        else
            LL_CloseCycle(part, 1 << channel);
    }

    CPartDef& P = (*m_Partition)[part];
    CBlend* B = IBlend_Create();

    _DBG_SINGLE_USE_MARKER;
    IBlendSetup(*B, part, channel, motion_ID, bMixing, blendAccrue, blendFalloff, Speed, noloop, Callback, CallbackParam);

    for (u32 i = 0; i < P.bones.size(); i++)
        Bone_Motion_Start_IM((*bones)[P.bones[i]], B);

    blend_cycles[part].push_back(B);
    return B;
}

CBlend* CKinematicsAnimated::LL_PlayCycle(u16 part, MotionID motion_ID, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    XR_ASSERT(motion_ID.valid());
    CMotionDef* m_def = XR_ASSERT_VAL(m_Motions[motion_ID.slot].motions.motion_def(motion_ID.idx) != nullptr);

    return LL_PlayCycle(part, motion_ID, bMixIn, m_def->Accrue(), m_def->Falloff(), m_def->Speed(), m_def->StopAtEnd(), Callback, CallbackParam, channel);
}

CBlend* CKinematicsAnimated::PlayCycle(const shared_str& N, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    MotionID motion_ID = ID_Cycle(N);
    return PlayCycle(motion_ID, bMixIn, Callback, CallbackParam, channel);
}

CBlend* CKinematicsAnimated::PlayCycle(MotionID motion_ID, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    XR_ASSERT(motion_ID.valid());
    CMotionDef* m_def = XR_ASSERT_VAL(m_Motions[motion_ID.slot].motions.motion_def(motion_ID.idx) != nullptr);

    return LL_PlayCycle(m_def->bone_or_part, motion_ID, bMixIn, m_def->Accrue(), m_def->Falloff(), m_def->Speed(), m_def->StopAtEnd(), Callback, CallbackParam,
                        channel);
}

CBlend* CKinematicsAnimated::PlayCycle(u16 partition, MotionID motion_ID, BOOL bMixIn, PlayCallback Callback, LPVOID CallbackParam, u8 channel)
{
    XR_ASSERT(motion_ID.valid());
    CMotionDef* m_def = XR_ASSERT_VAL(m_Motions[motion_ID.slot].motions.motion_def(motion_ID.idx) != nullptr);

    return LL_PlayCycle(partition, motion_ID, bMixIn, m_def->Accrue(), m_def->Falloff(), m_def->Speed(), m_def->StopAtEnd(), Callback, CallbackParam, channel);
}

// fx'es
MotionID CKinematicsAnimated::ID_FX_Safe(std::string_view N)
{
    MotionID motion_ID;
    for (int k = int(m_Motions.size()) - 1; k >= 0; --k)
    {
        shared_motions* s_mots = &m_Motions[k].motions;
        auto I = s_mots->fx()->find(N);
        if (I != s_mots->fx()->end())
        {
            motion_ID.set(u16(k), I->second);
            break;
        }
    }
    return motion_ID;
}

MotionID CKinematicsAnimated::ID_FX(std::string_view N) { return XR_ASSERT_VAL(ID_FX_Safe(N), "can't find FX for model", dbg_name, N); }

CBlend* CKinematicsAnimated::PlayFX(MotionID motion_ID, float power_scale)
{
    if (!motion_ID.valid())
        return nullptr;

    CMotionDef* m_def = XR_ASSERT_VAL(m_Motions[motion_ID.slot].motions.motion_def(motion_ID.idx) != nullptr);

    return LL_PlayFX(m_def->bone_or_part, motion_ID, m_def->Accrue(), m_def->Falloff(), m_def->Speed(), m_def->Power() * power_scale);
}

CBlend* CKinematicsAnimated::PlayFX(std::string_view N, f32 power_scale)
{
    MotionID motion_ID = ID_FX(N);
    return PlayFX(motion_ID, power_scale);
}

CBlend* CKinematicsAnimated::LL_PlayFX(u16 bone, MotionID motion_ID, float blendAccrue, float blendFalloff, float Speed, float Power)
{
    if (!motion_ID.valid())
        return nullptr;

    if (blend_fx.size() >= MAX_BLENDED)
        return nullptr;

    if (BI_NONE == bone)
        bone = iRoot;

    CBlend* B = IBlend_Create();
    _DBG_SINGLE_USE_MARKER;
    IFXBlendSetup(*B, motion_ID, blendAccrue, blendFalloff, Power, Speed, bone);
    Bone_Motion_Start((*bones)[bone], B);

    blend_fx.push_back(B);
    return B;
}

void CKinematicsAnimated::DestroyCycle(CBlend& B)
{
    if (auto cb = GetBlendDestroyCallback(); cb != nullptr)
        cb->BlendDestroy(B);

    B.set_free_state();

    for (auto idx : m_Partition->part(B.bone_or_part).bones)
        Bone_Motion_Stop_IM((*bones)[idx], &B);
}

// returns true if play time out

void CKinematicsAnimated::LL_UpdateTracks(float dt, bool b_force, bool leave_blends)
{
    // Cycles
    for (auto [part, cycles] : std::views::enumerate(blend_cycles))
    {
        if (!m_Partition->part(part).Name)
            continue;

        auto I = cycles.begin();
        auto E = cycles.end();
        while (I != E)
        {
            CBlend& B = *(*I);

            if (!b_force && B.dwFrame == Device.dwFrame)
                goto next;

            B.dwFrame = Device.dwFrame;

            if (B.update(dt, B.Callback) && !leave_blends)
            {
                DestroyCycle(B);

                I = cycles.erase(I);
                E = cycles.end();
            }
            else
            {
            next:
                ++I;
            }
        }
    }

    LL_UpdateFxTracks(dt);
}

void CKinematicsAnimated::LL_UpdateFxTracks(float dt)
{
    // FX
    auto I = blend_fx.begin();
    auto E = blend_fx.end();
    while (I != E)
    {
        CBlend& B = *(*I);

        if (!B.stop_at_end_callback)
        {
            B.playing = FALSE;
            goto next;
        }

        B.update_time(dt);

        switch (B.blend_state())
        {
        case CBlend::eAccrue:
            B.blendAmount += dt * B.blendAccrue * B.blendPower * B.speed;
            if (B.blendAmount >= B.blendPower)
            {
                // switch to fixed
                B.blendAmount = B.blendPower;
                B.set_falloff_state();
            }

            break;
        case CBlend::eFalloff:
            B.blendAmount -= dt * B.blendFalloff * B.blendPower * B.speed;
            if (B.blendAmount <= 0)
            {
                // destroy fx
                B.set_free_state();
                Bone_Motion_Stop((*bones)[B.bone_or_part], *I);

                I = blend_fx.erase(I);
                E = blend_fx.end();
                continue;
            }

            break;
        default: xr::unreachable();
        }

    next:
        ++I;
    }
}

void CKinematicsAnimated::UpdateTracks()
{
    _DBG_SINGLE_USE_MARKER;
    if (Update_LastTime == Device.dwTimeGlobal)
        return;

    XR_TRACY_ZONE_SCOPED();

    u32 DT = Device.dwTimeGlobal - Update_LastTime;
    if (DT > 66)
        DT = 66;
    float dt = float(DT) / 1000.f;

    if (GetUpdateTracksCalback())
    {
        if ((*GetUpdateTracksCalback())(float(Device.dwTimeGlobal - Update_LastTime) / 1000.f, *this))
            Update_LastTime = Device.dwTimeGlobal;
        return;
    }
    Update_LastTime = Device.dwTimeGlobal;
    LL_UpdateTracks(dt, false, false);
}

void CKinematicsAnimated::Release() { inherited::Release(); }

CKinematicsAnimated::CKinematicsAnimated() : CKinematics{}, IKinematicsAnimated{} {}
CKinematicsAnimated::~CKinematicsAnimated() { IBoneInstances_Destroy(); }

void CKinematicsAnimated::IBoneInstances_Create()
{
    inherited::IBoneInstances_Create();
    u32 size = bones->size();
    blend_instances = xr_alloc<CBlendInstance>(size);
    for (u32 i = 0; i < size; i++)
        blend_instances[i].construct();
}

void CKinematicsAnimated::IBoneInstances_Destroy()
{
    inherited::IBoneInstances_Destroy();

    if (blend_instances)
    {
        xr_free(blend_instances);
        blend_instances = nullptr;
    }
}

#define PCOPY(a) a = pFrom->a
void CKinematicsAnimated::Copy(dxRender_Visual* P)
{
    inherited::Copy(P);

    CKinematicsAnimated* pFrom = (CKinematicsAnimated*)P;
    PCOPY(m_Motions);
    PCOPY(m_Partition);

    IBlend_Startup();
}

void CKinematicsAnimated::Spawn()
{
    inherited::Spawn();
    IBlend_Startup();

    for (u32 i = 0; i < bones->size(); i++)
        blend_instances[i].construct();

    m_update_tracks_callback = nullptr;
    channels.init();
}

void CKinematicsAnimated::ChannelFactorsStartup() { channels.init(); }
void CKinematicsAnimated::LL_SetChannelFactor(u16 channel, float factor) { channels.set_factor(channel, factor); }

void CKinematicsAnimated::IBlend_Startup()
{
    _DBG_SINGLE_USE_MARKER;

    blend_pool.clear();
    blend_pool.resize(MAX_BLENDED_POOL);

    // cycles+fx clear
    for (u32 i = 0; i < MAX_PARTS; i++)
        blend_cycles[i].clear();
    blend_fx.clear();
    ChannelFactorsStartup();
}

CBlend* CKinematicsAnimated::IBlend_Create()
{
    UpdateTracks();
    _DBG_SINGLE_USE_MARKER;

    return std::to_address(XR_ASSERT_VAL(std::ranges::find(blend_pool, CBlend::eFREE_SLOT, &CBlend::blend_state) != blend_pool.end(),
                                         "too many blended motions requested", dbg_name, blend_pool.size()));
}

void CKinematicsAnimated::Load(const char* N, IReader* data, u32 dwFlags)
{
    inherited::Load(N, data, dwFlags);

    // Globals
    blend_instances = nullptr;
    m_Partition = nullptr;
    Update_LastTime = 0;

    // Load animation
    xr_vector<xr_string> omfs;
    if (pUserData && pUserData->section_exist("omf_override"))
    {
        LPCSTR nm, val;
        for (u32 i = 0; pUserData->r_line("omf_override", i, &nm, &val); ++i)
        {
            xr_string s = nm;
            s += ".omf";
            omfs.push_back(s);
        }
    }
    else if (data->find_chunk(OGF_S_MOTION_REFS))
    {
        string4096 items_nm;
        data->r_stringZ(items_nm, sizeof(items_nm));
        u32 set_cnt = _GetItemCount(items_nm);
        for (u32 k = 0; k < set_cnt; ++k)
        {
            string_path nm;
            std::ignore = _GetItem(items_nm, k, nm);
            xr_strcat(nm, ".omf");
            omfs.push_back(nm);
        }
    }
    else if (data->find_chunk(OGF_S_MOTION_REFS2))
    {
        u32 set_cnt = data->r_u32();
        for (u32 k = 0; k < set_cnt; ++k)
        {
            string_path nm;
            data->r_stringZ(nm, sizeof(nm));
            xr_strcat(nm, ".omf");
            omfs.push_back(nm);
        }
    }

    if (!omfs.empty())
    {
        m_Motions.reserve(omfs.size());

        for (const auto& s : omfs)
        {
            auto nm = s.c_str();
            string_path fn;
            XR_ASSERT(FS.exist(fn, "$level$", nm) != nullptr || FS.exist(fn, "$game_meshes$", nm) != nullptr, "can't find motion file", N, s);

            // Check compatibility
            m_Motions.emplace_back();
            bool create_res = true;
            shared_str key{s};

            if (!g_pMotionsContainer->has(key))
            {
                // optimize fs operations
                IReader* MS = FS.r_open(fn);
                create_res = m_Motions.back().motions.create(key, MS, bones);
                FS.r_close(MS);
            }

            if (create_res)
            {
                std::ignore = m_Motions.back().motions.create(key, nullptr, bones);
            }
            else
            {
                m_Motions.pop_back();
                Msg("! error in model [{}]. Unable to load motion file '{}'.", N, s);
            }
        }
    }
    else
    {
        string_path nm;
        strconcat(sizeof(nm), nm, N, ".ogf");
        std::ignore = m_Motions.emplace_back().motions.create(shared_str{nm}, data, bones);
    }

    XR_ASSERT(!m_Motions.empty(), "", N);

    m_Partition = m_Motions[0].motions.partition();
    m_Partition->load(this);

    // initialize motions
    for (auto& m_it : m_Motions)
    {
        SMotionsSlot& MS = m_it;
        MS.bone_motions.resize(bones->size());
        for (u32 i = 0; i < bones->size(); i++)
        {
            CBoneData* BD = (*bones)[i];
            MS.bone_motions[i] = MS.motions.bone_motions(BD->name);
        }
    }

    // Init blend pool
    IBlend_Startup();
}

void CKinematicsAnimated::LL_BuldBoneMatrixDequatize(const CBoneData* bd, u8 channel_mask, SKeyTable& keys)
{
    u16 SelfID = bd->GetSelfID();
    CBlendInstance& BLEND_INST = LL_GetBlendInstance(SelfID);
    CKey BK[MAX_CHANNELS][MAX_BLENDED]; // base keys

    for (auto& it : BLEND_INST.blend_vector())
    {
        CBlend* B = it;
        int& b_count = keys.chanel_blend_conts[B->channel];
        CKey* D = &keys.keys[B->channel][b_count];
        if (!(channel_mask & (1 << B->channel)))
            continue;
        u8 channel = B->channel;
        // keys.blend_factors[channel][b_count]	=  B->blendAmount;
        keys.blends[channel][b_count] = B;
        CMotion& M = *LL_GetMotion(B->motionID, SelfID);
        Dequantize(*D, *B, M);
        QR2Quat(M._keysR[0], BK[channel][b_count].Q);
        if (M.test_flag(flTKeyPresent))
        {
            if (M.test_flag(flTKey16IsBit))
                QT16_2T(M._keysT16[0], M, BK[channel][b_count].T);
            else
                QT8_2T(M._keysT8[0], M, BK[channel][b_count].T);
        }
        else
            BK[channel][b_count].T.set(M._initT);
        ++b_count;
    }
    for (u16 j = 0; MAX_CHANNELS > j; ++j)
        if (channels.rule(j).extern_ == animation::add)
            keys_substruct(keys.keys[j], BK[j], keys.chanel_blend_conts[j]);
}

// calculate single bone with key blending
void CKinematicsAnimated::LL_BoneMatrixBuild(CBoneInstance& bi, const Fmatrix* parent, const SKeyTable& keys)
{
    // Blend them together
    CKey channel_keys[MAX_CHANNELS];
    animation::channel_def BC[MAX_CHANNELS];
    u16 ch_count = 0;

    for (u16 j = 0; MAX_CHANNELS > j; ++j)
    {
        if (j != 0 && keys.chanel_blend_conts[j] == 0)
            continue;

        // data for channel mix cycle based on ch_count
        channels.get_def(j, BC[ch_count]);
        process_single_channel(channel_keys[ch_count], keys.keys[j], keys.blends[j], keys.chanel_blend_conts[j]);
        ++ch_count;
    }

    CKey Result;
    // Mix channels
    MixChannels(Result, channel_keys, BC, ch_count);

    Fmatrix RES;
    RES.mk_xform(Result.Q, Result.T);
    bi.mTransform.mul_43(*parent, RES);

#ifdef DEBUG
    if (!check_scale(RES))
        XR_ASSERT(check_scale(bi.mTransform));

    XR_ASSERT(_valid(bi.mTransform));

    Fbox dbg_box;
    constexpr auto box_size = 100000.0f;
    dbg_box.set(-box_size, -box_size, -box_size, box_size, box_size, box_size);

    XR_ASSERT(dbg_box.contains(bi.mTransform.c), "invalid bone position", getDebugName(), get_string(bi.mTransform));
#endif
}

// Добавить скриптовое смещение для кости --#SM+#--
void CKinematicsAnimated::LL_AddTransformToBone(KinematicsABT::additional_bone_transform& offset) { inherited::LL_AddTransformToBone(offset); }

// Обнулить скриптовое смещение для конкретной кости или всех сразу (bone_id = BI_NONE) --#SM+#--
void CKinematicsAnimated::LL_ClearAdditionalTransform(u16 bone_id) { inherited::LL_ClearAdditionalTransform(bone_id); }

void CKinematicsAnimated::BuildBoneMatrix(const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8 channel_mask)
{
    XR_TRACY_ZONE_SCOPED();

    SKeyTable keys;

    LL_BuldBoneMatrixDequatize(bd, channel_mask, keys);
    LL_BoneMatrixBuild(bi, parent, keys);

    CalculateBonesAdditionalTransforms(bd, bi); //--#SM+#--
}

void CKinematicsAnimated::OnCalculateBones() { UpdateTracks(); }

IBlendDestroyCallback* CKinematicsAnimated::GetBlendDestroyCallback() { return m_blend_destroy_callback; }
void CKinematicsAnimated::SetUpdateTracksCalback(IUpdateTracksCallback* callback) { m_update_tracks_callback = callback; }
void CKinematicsAnimated::SetBlendDestroyCallback(IBlendDestroyCallback* cb) { m_blend_destroy_callback = cb; }
