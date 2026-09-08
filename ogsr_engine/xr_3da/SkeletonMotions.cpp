//---------------------------------------------------------------------------
#include "stdafx.h"

#include "SkeletonMotions.h"

#include "fmesh.h"
#include "motion.h"
#include "../Include/xrRender/Kinematics.h"

motions_container* g_pMotionsContainer{};

u16 CPartition::part_id(const shared_str& name) const
{
    for (u16 i = 0; i < MAX_PARTS; ++i)
    {
        const CPartDef& pd = part(i);
        if (pd.Name == name)
            return i;
    }

    Msg("!there is no part named [{}]", name);

    return u16(-1);
}

void CPartition::load(IKinematics* V)
{
    CInifile* ini = V->LL_UserData();
    if (!ini)
        return;

    if (ini->sections().empty() || !ini->section_exist("part_0"))
        return;

    static constexpr std::string_view part_name{"partition_name"};

    for (u32 i = 0; i < MAX_PARTS; ++i)
    {
        string64 buff;
        xr_sprintf(buff, sizeof(buff), "part_%d", i);

        const auto& S = ini->r_section(buff);
        if (!S.Data.empty())
            P[i].bones.clear();

        for (const auto& [k, v] : S.Data)
        {
            if (std::is_eq(xr_strcmp(k, part_name)))
                P[i].Name = v;
            else
                P[i].bones.push_back(V->LL_BoneID(k));
        }
    }
}

namespace
{
u16 find_bone_id(vecBones* bones, shared_str nm)
{
    for (u16 i = 0; i < (u16)bones->size(); i++)
        if (bones->at(i)->name == nm)
            return i;
    return BI_NONE;
}
} // namespace

//-----------------------------------------------------------------------
BOOL motions_value::load(LPCSTR N, IReader* data, vecBones* bones)
{
    m_id._set(N);

    bool bRes = true;
    // Load definitions
    U16Vec rm_bones(bones->size(), BI_NONE);

    auto MP = XR_ASSERT_VAL(data->open_chunk(OGF_S_SMPARAMS) != nullptr, "old skinned model version is not supported", m_id);
    u16 vers = XR_ASSERT_VAL(MP->r_u16() <= xrOGF_SMParamsVersion, "invalid OGF/OMF version", m_id);
    u16 part_bone_cnt = 0;
    string128 buf;

    // partitions
    u16 part_count;
    part_count = MP->r_u16();

    for (u16 part_i = 0; part_i < part_count; part_i++)
    {
        CPartDef& PART = m_partition[part_i];
        MP->r_stringZ(buf, sizeof(buf));
        PART.Name._set(_strlwr(buf));
        PART.bones.resize(MP->r_u16());

        for (xr_vector<u32>::iterator b_it = PART.bones.begin(); b_it < PART.bones.end(); b_it++)
        {
            MP->r_stringZ(buf, sizeof(buf));
            u16 m_idx = u16(MP->r_u32());

            *b_it = XR_ASSERT_VAL(find_bone_id(bones, shared_str{buf}) != BI_NONE, "can't find bone", m_id, buf);
            if (bRes)
                rm_bones[m_idx] = u16(*b_it);
        }

        part_bone_cnt = u16(part_bone_cnt + (u16)PART.bones.size());
    }

    XR_ASSERT(bones->size() == part_bone_cnt, "bone count mismatch", m_id);

    if (bRes)
    {
        // motion defs (cycle&fx)
        u16 mot_count = MP->r_u16();
        m_mdefs.resize(mot_count);

        m_motion_map.reserve(mot_count);
        m_cycle.reserve(mot_count);
        m_fx.reserve(mot_count);

        for (u16 mot_i = 0; mot_i < mot_count; ++mot_i)
        {
            MP->r_stringZ(buf, sizeof(buf));
            XR_ASSERT(MP->elapsed() >= sizeof(u32) + sizeof(u16) + sizeof(u16) + sizeof(f32) + sizeof(f32) + sizeof(f32) + sizeof(f32), "broken motions file",
                      m_id, mot_i, mot_count);

            _strlwr(buf);
            u32 dwFlags = MP->r_u32();
            CMotionDef& D = m_mdefs[mot_i];
            D.Load(MP, dwFlags, vers);

            if (dwFlags & esmFX)
                m_fx.emplace(buf, mot_i);
            else
                m_cycle.emplace(buf, mot_i);

            m_motion_map.emplace(buf, mot_i);
        }
    }

    MP->close();

    if (!bRes)
        return false;

    // Load animation
    IReader* MS = data->open_chunk(OGF_S_MOTIONS);
    if (!MS)
        return false;

    // MotionID 2 bit - slot, 14 bit - motion index
    u32 dwCNT{std::numeric_limits<u32>::max()};
    std::ignore = MS->r_chunk_safe(0, &dwCNT, sizeof(dwCNT));
    XR_ASSERT(dwCNT < 0x3fff, "", m_id);

    m_motions.reserve(bones->size());

    // set per bone motion size
    for (u32 i = 0; i < bones->size(); i++)
        m_motions[bones->at(i)->name].resize(dwCNT);

    // load motions
    for (u16 m_idx = 0; m_idx < (u16)dwCNT; m_idx++)
    {
        string128 mname;
        XR_ASSERT(MS->find_chunk(m_idx + 1) > 0);
        MS->r_stringZ(mname, sizeof(mname));

        if constexpr (false) // В ганслингере полно таких ошибок в анимациях, не вижу смысла спамить ими в лог
        {
            // sanity check
            xr_strlwr(mname);
            const auto I = XR_ASSERT_VAL(m_motion_map.find(mname) != m_motion_map.end(), "can't find motion", m_id, mname);
            XR_ASSERT(I->second == m_idx, "invalid motion index", m_id, mname);
        }

        u32 dwLen = MS->r_u32();
        for (u32 i = 0; i < bones->size(); i++)
        {
            const auto bone_id = XR_ASSERT_VAL(rm_bones[i] != BI_NONE, "invalid remap index", m_id, i);
            CMotion& M = m_motions[bones->at(bone_id)->name][m_idx];

            M.set_count(dwLen);
            M.set_flags(MS->r_u8());

            if (M.test_flag(flRKeyAbsent))
            {
                const CKeyQR* r = (const CKeyQR*)MS->pointer();
                // u32 crc_q = crc32(r, sizeof(CKeyQR));
                M._keysR.create(1, r);
                MS->advance(1 * sizeof(CKeyQR));
            }
            else
            {
                std::ignore = MS->r_u32();
                M._keysR.create(dwLen, (const CKeyQR*)MS->pointer());
                MS->advance(dwLen * sizeof(CKeyQR));
            }
            if (M.test_flag(flTKeyPresent))
            {
                std::ignore = MS->r_u32();
                if (M.test_flag(flTKey16IsBit))
                {
                    M._keysT16.create(dwLen, (const CKeyQT16*)MS->pointer());
                    MS->advance(dwLen * sizeof(CKeyQT16));
                }
                else
                {
                    M._keysT8.create(dwLen, (const CKeyQT8*)MS->pointer());
                    MS->advance(dwLen * sizeof(CKeyQT8));
                }

                MS->r_fvector3(M._sizeT);
                MS->r_fvector3(M._initT);
            }
            else
            {
                MS->r_fvector3(M._initT);
            }
        }
    }

    MS->close();

    return bRes;
}

MotionVec* motions_value::bone_motions(const shared_str& bone_name)
{
    auto I = m_motions.find(bone_name);
    if (I == m_motions.end())
        return nullptr;

    return &(*I).second;
}

//-----------------------------------

motions_container::motions_container() = default;
motions_container::~motions_container() { XR_ASSERT(container.size() == 0, "", container); }

bool motions_container::has(shared_str key) const { return container.contains(key); }

motions_value* motions_container::dock(shared_str key, IReader* data, vecBones* bones)
{
    if (const auto I = container.find(key); I != container.end())
        return I->second;

    // loading motions
    auto result = xr_new<motions_value>();
    result->m_dwReference = 0;

    if (result->load(key.c_str(), XR_ASSERT_VAL(data != nullptr), bones))
        container.emplace(key, result);
    else
        xr_delete(result);

    return result;
}

void motions_container::clean(bool force_destroy)
{
    auto it = container.begin();
    auto _E = container.end();
    if (force_destroy)
    {
        for (; it != _E; it++)
        {
            motions_value* sv = it->second;
            xr_delete(sv);
        }
        container.clear();
    }
    else
    {
        for (; it != _E;)
        {
            motions_value* sv = it->second;
            if (0 == sv->m_dwReference)
            {
                auto i_current = it;
                auto i_next = ++it;
                xr_delete(sv);
                container.erase(i_current);
                it = i_next;
            }
            else
            {
                it++;
            }
        }
    }
}

void motions_container::dump() const
{
    XR_LOG_TRACE_L1("--- motion container --- begin:");

    gsl::index sz{sizeof(*this)};

    for (auto [k, kv] : std::views::enumerate(container))
    {
        sz += kv.second->mem_usage();
        Msg("#{:3}: [{:3}/{:5} Kb] - {}", k, kv.second->m_dwReference, kv.second->mem_usage() / 1024, kv.first);
    }

    Msg("--- items: {}, mem usage: {} Kb ", std::ssize(container), sz / 1024);

    XR_LOG_TRACE_L1("--- motion container --- end");
}

//////////////////////////////////////////////////////////////////////////
// High level control
void CMotionDef::Load(IReader* MP, u32 fl, u16 version)
{
    // params
    bone_or_part = MP->r_u16(); // bCycle?part_id:bone_id;
    motion = MP->r_u16(); // motion_id
    speed = MP->r_float();
    power = MP->r_float();
    accrue = MP->r_float();
    falloff = MP->r_float();
    flags = (u16)fl;
    constexpr float fQuantizerRangeExt = 1.5f; // Какое-то магическое число

    if (!(flags & esmFX) && (falloff >= accrue))
    {
        // KRodin: 0.003f наиболее приближённо к тому что было до этого. Разница в результате буквально в тысячных долях. При Quantize/Dequantize точность в
        // любом случае терялась, так что это не сильно важно.
        falloff = accrue /* - 0.003f*/;
        if (/*negative(falloff)*/ negative(accrue - 0.003f))
            // И вообще это были какие-то костыли от ПЫС. Если при вычитании falloff становился меньше нуля (при том что он был unsigned!!!), то после
            // Quantize/Dequantize всегда получалось 100.
            falloff = 100.f;
    }

    accrue *= fQuantizerRangeExt;
    falloff *= fQuantizerRangeExt;

    if (version >= 4)
    {
        u32 cnt = MP->r_u32();
        if (cnt > 0)
        {
            marks.resize(cnt);

            for (u32 i = 0; i < cnt; ++i)
                marks[i].Load(MP);
        }
    }
}

bool CMotionDef::StopAtEnd() const { return !!(flags & esmStopAtEnd); }

bool shared_motions::create(shared_str key, IReader* data, vecBones* bones)
{
    motions_value* v = g_pMotionsContainer->dock(key, data, bones);
    if (v)
        v->m_dwReference++;

    destroy();
    p_ = v;

    return !!v;
}

bool shared_motions::create(shared_motions const& rhs)
{
    motions_value* v = rhs.p_;
    if (v)
        v->m_dwReference++;

    destroy();
    p_ = v;

    return !!v;
}

const motion_marks::interval* motion_marks::pick_mark(const float& t) const
{
    C_ITERATOR it = intervals.begin();
    C_ITERATOR it_e = intervals.end();

    for (; it != it_e; ++it)
    {
        const interval& I = (*it);
        if (I.first <= t && I.second >= t)
            return &I;

        if (I.first > t)
            break;
    }

    return nullptr;
}

bool motion_marks::is_mark_between(float const& t0, float const& t1) const
{
    XR_ASSERT(t0 <= t1);

    C_ITERATOR i = intervals.begin();
    C_ITERATOR e = intervals.end();
    for (; i != e; ++i)
    {
        XR_ASSERT(i->first <= i->second);

        if (fsimilar((*i).first, t0))
            return true;

        if ((*i).first > t0)
        {
            if ((*i).second <= t1)
                return (true);

            if ((*i).first <= t1)
                return (true);

            return (false);
        }

        if ((*i).second < t0)
            continue;

        if (fsimilar((*i).second, t0))
            return true;

        return (true);
    }

    return (false);
}

float motion_marks::time_to_next_mark(float time) const
{
    C_ITERATOR i = intervals.begin();
    C_ITERATOR e = intervals.end();
    float result_dist = FLT_MAX;
    for (; i != e; ++i)
    {
        float dist = (*i).first - time;
        if (dist > 0.f && dist < result_dist)
            result_dist = dist;
    }
    return result_dist;
}

void motion_marks::Load(IReader* R)
{
    xr_string tmp;
    R->r_string(tmp);
    name._set(tmp.c_str());
    u32 cnt = R->r_u32();
    intervals.resize(cnt);
    for (u32 i = 0; i < cnt; ++i)
    {
        interval& item = intervals[i];
        item.first = R->r_float();
        item.second = R->r_float();
    }
}
