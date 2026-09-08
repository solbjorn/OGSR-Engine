#include "stdafx.h"

#include "SkeletonCustom.h"

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s);
#endif

static std::recursive_mutex UCalc_Mutex;

void CKinematics::CalculateBones(BOOL bForceExact)
{
    // early out.
    // check if the info is still relevant
    // skip all the computations - assume nothing changes in a small period of time :)
    if (Device.dwTimeGlobal == UCalc_Time)
        return; // early out for "fast" update

    XR_TRACY_ZONE_SCOPED();

    std::scoped_lock UCalc_Lock(UCalc_Mutex);
    OnCalculateBones();

    if (!bForceExact && (Device.dwTimeGlobal < (UCalc_Time + UCalc_Interval)))
        return; // early out for "slow" update

    if (Update_Visibility)
        Visibility_Update();

    _DBG_SINGLE_USE_MARKER;
    // here we have either:
    //	1:	timeout elapsed
    //	2:	exact computation required
    UCalc_Time = Device.dwTimeGlobal;

    // exact computation
    // Calculate bones
#ifdef DEBUG
    Device.Statistic->Animation.Begin();
#endif

    Bone_Calculate((*bones)[iRoot], &Fidentity);

#ifdef DEBUG
    check_kinematics(this, dbg_name.c_str());
    Device.Statistic->Animation.End();
#endif

    XR_ASSERT(LL_GetBonesVisible() != VisMask{});

    // Calculate BOXes/Spheres if needed
    UCalc_Visibox++;
    if (UCalc_Visibox >= psSkeletonUpdate)
    {
        XR_TRACY_ZONE_SCOPED();

        // mark
        UCalc_Visibox = -(::Random.randI(psSkeletonUpdate - 1));

        // the update itself
        Fbox Box;
        Box.invalidate();
        for (u32 b = 0; b < bones->size(); b++)
        {
            if (!LL_GetBoneVisible(u16(b)))
                continue;
            Fobb& obb = (*bones)[b]->obb;
            Fmatrix& Mbone = bone_instances[b].mTransform;
            Fmatrix Mbox;
            obb.xform_get(Mbox);
            Fmatrix X;
            X.mul_43(Mbone, Mbox);
            Fvector& S = obb.m_halfsize;

            Fvector P, A;
            A.set(-S.x, -S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(-S.x, -S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, -S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, -S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(-S.x, S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(-S.x, S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
        }

        if (!bones->empty())
        {
            // previous frame we have updated box - update sphere
            vis.box.min = (Box.min);
            vis.box.max = (Box.max);
            vis.box.getsphere(vis.sphere.P, vis.sphere.R);
        }

#ifdef DEBUG
        // Validate
        XR_ASSERT(_valid(vis.box.min) && _valid(vis.box.max), "invalid bones-xform in model", dbg_name, vis.box.min, vis.box.max);

        if (vis.sphere.R > 1000.f)
        {
            for (u16 ii = 0; ii < LL_BoneCount(); ++ii)
            {
                Msg("bone: {}", LL_BoneName_dbg(ii));
                Msg("bone_matrix: {}", LL_GetTransform(ii));
            }

            XR_LOG_ERROR("end-------");
        }
#endif

        XR_ASSERT(vis.sphere.R < 1000.0f, "invalid bones-xform in model", dbg_name);
    }

    if (Update_Callback)
        Update_Callback(this);
}

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s)
{
    CKinematics* K = _k;
    Fmatrix& MrootBone = K->LL_GetBoneInstance(K->LL_GetBoneRoot()).mTransform;
    if (MrootBone.c.y > 10000)
    {
        Msg("all bones transform:--------[{}]", s);

        for (u16 ii = 0; ii < K->LL_BoneCount(); ++ii)
        {
            Msg("bone: {}", K->LL_BoneName_dbg(ii));
            Msg("bone_matrix: {}", K->LL_GetTransform(ii));
        }

        XR_LOG_ERROR("end-------");
        XR_PANIC("check_kinematics failed for ", s);
    }
}
#endif

// Добавить скриптовое смещение для кости --#SM+#--
void CKinematics::LL_AddTransformToBone(KinematicsABT::additional_bone_transform& offset) { m_bones_offsets.push_back(offset); }

// Обнулить скриптовое смещение для конкретной кости или всех сразу (bone_id = BI_NONE) --#SM+#--
void CKinematics::LL_ClearAdditionalTransform(u16 bone_id)
{
    if (bone_id == BI_NONE)
    {
        m_bones_offsets.clear();
        return;
    }

    BONE_TRANSFORM_VECTOR_IT it = m_bones_offsets.begin();
    while (it != m_bones_offsets.end())
    {
        if (it->m_bone_id == bone_id)
            it = m_bones_offsets.erase(it);
        else
            ++it;
    }
}

void CKinematics::BuildBoneMatrix(const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8)
{
    bi.mTransform.mul_43(*parent, bd->bind_transform);
    CalculateBonesAdditionalTransforms(bd, bi); //--#SM+#--
}

// Добавляем константные смещения к нужным костям --#SM+#--
void CKinematics::CalculateBonesAdditionalTransforms(const CBoneData* bd, CBoneInstance& bi)
{
    // bi.mTransform.c - содержит смещение относительно первой кости модели\центра сцены (0, 0, 0)
    for (auto& it : m_bones_offsets)
    {
        if (it.m_bone_id != bd->GetSelfID())
            continue;

        const Fvector vOldPos = bi.mTransform.c;
        bi.mTransform.mulB_43(it.m_transform); // Rotation
        bi.mTransform.c.add(vOldPos, it.m_transform.c); // Translation
    }
}

void CKinematics::CLBone(const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8 channel_mask)
{
    XR_TRACY_ZONE_SCOPED();

    if (!LL_GetBoneVisible(bd->GetSelfID()))
        return;

    if (bi.callback_overwrite())
    {
        if (bi.callback())
            bi.callback()(&bi);
    }
    else
    {
        BuildBoneMatrix(bd, bi, parent, channel_mask);
        XR_DEBUG_ASSERT(_valid(bi.mTransform), "", bd->name);

        if (bi.callback())
        {
            bi.callback()(&bi);
            XR_DEBUG_ASSERT(_valid(bi.mTransform), "", bd->name);
        }
    }

    bi.mRenderTransform.mul_43(bi.mTransform, bd->m2b_transform);
}

void CKinematics::Bone_GetAnimPos(Fmatrix& pos, u16 id, u8 mask_channel, bool ignore_callbacks)
{
    CBoneInstance& bi = LL_GetBoneInstance(XR_ASSERT_VAL(id < LL_BoneCount(), "invalid bone ID", dbg_name));
    BoneChain_Calculate(&LL_GetData(id), bi, mask_channel, ignore_callbacks);

    XR_DEBUG_ASSERT(_valid(bi.mTransform));
    pos.set(bi.mTransform);
}

void CKinematics::Bone_Calculate(CBoneData* bd, const Fmatrix* parent)
{
    XR_TRACY_ZONE_SCOPED();

    CBoneInstance& BONE_INST = LL_GetBoneInstance(bd->GetSelfID());
    CLBone(bd, BONE_INST, parent, std::numeric_limits<u8>::max());

    // Calculate children
    for (auto C : bd->children)
        Bone_Calculate(C, &BONE_INST.mTransform);
}

void CKinematics::BoneChain_Calculate(const CBoneData* bd, CBoneInstance& bi, u8 mask_channel, bool ignore_callbacks)
{
    XR_TRACY_ZONE_SCOPED();

    const u16 SelfID = bd->GetSelfID();

    // ignore callbacks
    BoneCallback bc = bi.callback();
    BOOL ow = bi.callback_overwrite();

    if (ignore_callbacks)
        bi.set_callback(bi.callback_type(), nullptr, bi.callback_param(), 0);

    if (LL_GetBoneRoot() == SelfID)
    {
        CLBone(bd, bi, &Fidentity, mask_channel);

        // restore callback
        bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
        return;
    }

    const u16 ParentID = XR_ASSERT_VAL(bd->GetParentID() != BI_NONE, "", bd->name);
    CBoneData* ParrentDT = &LL_GetData(ParentID);
    CBoneInstance& parrent_bi = LL_GetBoneInstance(ParentID);

    BoneChain_Calculate(ParrentDT, parrent_bi, mask_channel, ignore_callbacks);
    CLBone(bd, bi, &parrent_bi.mTransform, mask_channel);

    // restore callback
    bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
}
