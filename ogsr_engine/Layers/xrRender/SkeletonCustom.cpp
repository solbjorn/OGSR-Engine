#include "stdafx.h"

#include "SkeletonCustom.h"

#include "SkeletonX.h"

#include "../../xr_3da/fmesh.h"
#include "../../xr_3da/Render.h"

int psSkeletonUpdate = 32;

u16 CKinematics::LL_BoneID(const char* B) const
{
    if (const auto I = bone_map_N.find(B); I != bone_map_N.end())
        return I->second;

    return BI_NONE;
}

u16 CKinematics::LL_BoneID(const shared_str& B) const
{
    if (const auto I = bone_map_N.find(B); I != bone_map_N.end())
        return I->second;

    return BI_NONE;
}

const char* CKinematics::LL_BoneName_dbg(const u16 ID) const
{
    for (const auto& bone : bone_map_N)
    {
        if (bone.second == ID)
            return bone.first.c_str();
    }

    return nullptr;
}

#ifdef DEBUG
void CKinematics::DebugRender(Fmatrix& XFORM)
{
    auto& cmd_list = RImplementation.get_imm_context().cmd_list;

    cmd_list.set_Shader(RImplementation.m_WireShader);

    CalculateBones();

    CBoneData::BoneDebug dbgLines;
    (*bones)[iRoot]->DebugQuery(dbgLines);

    constexpr Fvector Z{0, 0, 0};
    constexpr Fvector H1{0.01f, 0.01f, 0.01f};
    Fvector H2;
    H2.mul(H1, 2);
    for (u32 i = 0; i < dbgLines.size(); i += 2)
    {
        Fmatrix& M1 = bone_instances[dbgLines[i]].mTransform;
        Fmatrix& M2 = bone_instances[dbgLines[i + 1]].mTransform;

        Fvector P1, P2;
        M1.transform_tiny(P1, Z);
        M2.transform_tiny(P2, Z);
        cmd_list.dbg_DrawLINE(XFORM, P1, P2, D3DCOLOR_XRGB(0, 255, 0));

        Fmatrix M;
        M.mul_43(XFORM, M2);
        cmd_list.dbg_DrawOBB(M, H1, D3DCOLOR_XRGB(255, 255, 255));
        cmd_list.dbg_DrawOBB(M, H2, D3DCOLOR_XRGB(255, 255, 255));
    }

    for (u32 b = 0; b < bones->size(); b++)
    {
        Fobb& obb = (*bones)[b]->obb;
        Fmatrix& Mbone = bone_instances[b].mTransform;
        Fmatrix Mbox;
        obb.xform_get(Mbox);
        Fmatrix X;
        X.mul(Mbone, Mbox);
        Fmatrix W;
        W.mul(XFORM, X);
        cmd_list.dbg_DrawOBB(W, obb.m_halfsize, D3DCOLOR_XRGB(0, 0, 255));
    }
}
#endif

CKinematics::CKinematics()
{
#ifdef DEBUG
    dbg_single_use_marker = FALSE;
#endif

    m_is_original_lod = false;
}

CKinematics::~CKinematics()
{
    IBoneInstances_Destroy();
    // wallmarks
    ClearWallmarks();

    if (m_lod == nullptr)
        return;

    if (auto lod_kinematics = smart_cast<CKinematics*>(m_lod); lod_kinematics != nullptr && lod_kinematics->m_is_original_lod)
        lod_kinematics->Release();

    xr_delete(m_lod);
}

void CKinematics::IBoneInstances_Create()
{
    u32 size = bones->size();
    bone_instances = xr_alloc<CBoneInstance>(size);
    for (u32 i = 0; i < size; i++)
        bone_instances[i].construct();
}

void CKinematics::IBoneInstances_Destroy()
{
    if (bone_instances)
    {
        xr_free(bone_instances);
        bone_instances = nullptr;
    }
}

CSkeletonX* CKinematics::LL_GetChild(u32 idx) { return smart_cast<CSkeletonX*>(children[idx]); }

void CKinematics::Load(const char* N, IReader* data, u32 dwFlags)
{
    inherited::Load(N, data, dwFlags);

    pUserData = nullptr;
    m_lod = nullptr;
    // loading lods

    IReader* LD = data->open_chunk(OGF_S_LODS);
    if (LD)
    {
        // From stream
        {
            string_path lod_name;
            LD->r_string(lod_name, sizeof(lod_name));

            m_lod = XR_ASSERT_VAL(smart_cast<dxRender_Visual*>(RImplementation.model_CreateChild(lod_name, nullptr)) != nullptr, "can't create LOD model", N);

            if (auto lod_kinematics = smart_cast<CKinematics*>(m_lod); lod_kinematics != nullptr)
                lod_kinematics->m_is_original_lod = true;
        }

        LD->close();
    }

    string_path ini_path;
    strcpy_s(ini_path, N);

    if (const auto ext = strext(ini_path); ext != nullptr)
        *ext = '\0';

    strcat_s(ini_path, ".ltx");

    // try to read custom user data for module from ltx file
    IReader* UD = FS.r_open("$game_meshes$", ini_path);
    pUserData = UD ? xr_new<CInifile>(UD, FS.get_path("$game_config$")->m_Path) : nullptr;
    if (UD)
        UD->close();

    // read user data from model if no custom found
    if (!pUserData)
    {
        // User data
        UD = data->open_chunk(OGF_S_USERDATA);
        pUserData = UD ? xr_new<CInifile>(UD, FS.get_path("$game_config$")->m_Path) : nullptr;
        if (UD)
            UD->close();
    }

    // Globals
    bones = xr_new<vecBones>();
    bone_instances = nullptr;

    // Load bones
    // TODO: container is created in stack!
    xr_vector<shared_str> L_parents;

    XR_ASSERT(data->find_chunk(OGF_S_BONE_NAMES) > 0, "", N);
    visimask.zero();

    u32 dwCount = data->r_u32();
    bone_map_N.reserve(dwCount);

    for (; dwCount; dwCount--)
    {
        string256 buf;

        // Bone
        u16 ID = u16(bones->size());
        data->r_stringZ(buf, sizeof(buf));
        _strlwr(buf);
        CBoneData* pBone = CreateBoneData(ID);
        pBone->name._set(buf);
        pBone->child_faces.resize(children.size());
        bones->push_back(pBone);

        bone_map_N.emplace(pBone->name, ID);

        // It's parent
        data->r_stringZ(buf, sizeof(buf));
        _strlwr(buf);
        L_parents.emplace_back(buf);

        data->r(&pBone->obb, sizeof(Fobb));
        visimask.set(ID, true);
    }

    // Attach bones to their parents
    iRoot = BI_NONE;

    for (auto [i, P, B] : std::views::zip(std::views::indices(std::ssize(*bones)), L_parents, *bones))
    {
        if (P.empty())
        {
            // no parent - this is root bone
            XR_ASSERT(iRoot == BI_NONE, "", N, i);
            iRoot = gsl::narrow_cast<u16>(i);

            B->SetParentID(BI_NONE);
            continue;
        }

        const u16 ID = XR_ASSERT_VAL(LL_BoneID(P) != BI_NONE, N, i, P);
        (*bones)[ID]->children.push_back(B);
        B->SetParentID(ID);
    }

    XR_ASSERT(iRoot != BI_NONE, "", N);

    // Free parents
    L_parents.clear();

    // IK data
    IReader* IKD = data->open_chunk(OGF_S_IKDATA);
    if (IKD)
    {
        bool fix_cop_joints = pUserData ? READ_IF_EXISTS(pUserData, r_bool, "compat", "fix_cop_joints", false) : false;
        for (u32 i = 0; i < bones->size(); i++)
        {
            CBoneData* B = (*bones)[i];
            u16 vers = (u16)IKD->r_u32();
            IKD->r_stringZ(B->game_mtl_name);
            IKD->r(&B->shape, sizeof(SBoneShape));
            B->IK_data.Import(*IKD, vers);
            Fvector vXYZ, vT;
            IKD->r_fvector3(vXYZ);
            IKD->r_fvector3(vT);
            B->bind_transform.setXYZi(vXYZ);
            B->bind_transform.translate_over(vT);
            B->mass = IKD->r_float();
            IKD->r_fvector3(B->center_of_mass);

            if (fix_cop_joints)
            {
                // https://bitbucket.org/stalker/xray_re-tools/commits/209b9014129ceeb7d92375a77f60835553266bf1
                for (auto& it : B->IK_data.limits)
                {
                    Fvector2 vec = it.limit;
                    float tmp = vec.x;
                    it.limit.x = -vec.y;
                    it.limit.y = -tmp;
                }
            }
        }
        // calculate model to bone converting matrix
        (*bones)[LL_GetBoneRoot()]->CalculateM2B(Fidentity);
        IKD->close();
    }

    // after load process
    {
        for (u16 child_idx = 0; child_idx < (u16)children.size(); child_idx++)
            LL_GetChild(child_idx)->AfterLoad(this, child_idx);
    }

    // unique bone faces
    {
        for (u32 bone_idx = 0; bone_idx < bones->size(); bone_idx++)
        {
            CBoneData* B = (*bones)[bone_idx];
            for (u32 child_idx = 0; child_idx < children.size(); child_idx++)
            {
                CBoneData::FacesVec faces = B->child_faces[child_idx];
                std::ranges::sort(faces);
                CBoneData::FacesVecIt new_end = std::unique(faces.begin(), faces.end());
                faces.erase(new_end, faces.end());
                B->child_faces[child_idx].clear();
                B->child_faces[child_idx] = faces;
            }
        }
    }

    // reset update_callback
    Update_Callback = nullptr;
    // reset update frame
    wm_frame = std::numeric_limits<u32>::max();

    LL_Validate();
}

IC void iBuildGroups(CBoneData* B, U16Vec& tgt, u16 id, u16& last_id)
{
    if (B->IK_data.ik_flags.is(SJointIKData::flBreakable))
        id = ++last_id;
    tgt[B->GetSelfID()] = id;
    for (xr_vector<CBoneData*>::iterator bone_it = B->children.begin(); bone_it != B->children.end(); bone_it++)
        iBuildGroups(*bone_it, tgt, id, last_id);
}

void CKinematics::LL_Validate()
{
    // check breakable
    BOOL bCheckBreakable = FALSE;
    for (u16 k = 0; k < LL_BoneCount(); k++)
    {
        if (LL_GetData(k).IK_data.ik_flags.is(SJointIKData::flBreakable) && (LL_GetData(k).IK_data.type != jtNone))
        {
            bCheckBreakable = TRUE;
            break;
        }
    }

    if (bCheckBreakable)
    {
        BOOL bValidBreakable = TRUE;

        // TODO: container is created in stack!
        xr_vector<xr_vector<u16>> groups;
        std::ignore = LL_GetBoneGroups(groups);

        // TODO: container is created in stack!
        xr_vector<u16> b_parts(LL_BoneCount(), BI_NONE);
        CBoneData* root = &LL_GetData(LL_GetBoneRoot());
        u16 last_id = 0;
        iBuildGroups(root, b_parts, 0, last_id);

        for (u16 g = 0; g < (u16)groups.size(); ++g)
        {
            xr_vector<u16>& group = groups[g];
            u16 bp_id = b_parts[group[0]];
            for (u32 b = 1; b < groups[g].size(); b++)
                if (bp_id != b_parts[groups[g][b]])
                {
                    bValidBreakable = FALSE;
                    break;
                }
        }

        if (bValidBreakable == FALSE)
        {
            for (u16 k = 0; k < LL_BoneCount(); k++)
            {
                CBoneData& BD = LL_GetData(k);
                if (BD.IK_data.ik_flags.is(SJointIKData::flBreakable))
                    BD.IK_data.ik_flags.set(SJointIKData::flBreakable, FALSE);
            }

#ifdef DEBUG
            Msg("! ERROR: Invalid breakable object: '{}'", dbg_name);
#endif
        }
    }
}

void CKinematics::Copy(dxRender_Visual* P)
{
    inherited::Copy(P);

    auto pFrom = XR_ASSERT_VAL(smart_cast<CKinematics*>(P) != nullptr);
    pUserData = pFrom->pUserData;
    bones = pFrom->bones;
    iRoot = pFrom->iRoot;
    bone_map_N = pFrom->bone_map_N;
    visimask = pFrom->visimask;

    IBoneInstances_Create();

    for (u32 i = 0; i < children.size(); i++)
        LL_GetChild(i)->SetParent(this);

    CalculateBones_Invalidate();

    m_lod = pFrom->m_lod != nullptr ? smart_cast<dxRender_Visual*>(RImplementation.model_Duplicate(pFrom->m_lod)) : nullptr;
}

void CKinematics::CalculateBones_Invalidate()
{
    UCalc_Time = 0x0;
    UCalc_Visibox = psSkeletonUpdate;
}

void CKinematics::Spawn()
{
    inherited::Spawn();

    // bones
    for (u32 i = 0; i < bones->size(); i++)
        bone_instances[i].construct();

    Update_Callback = nullptr;
    CalculateBones_Invalidate();

    // wallmarks
    ClearWallmarks();
    Visibility_Invalidate();
    LL_SetBoneRoot(0);
}

void CKinematics::Depart()
{
    inherited::Depart();
    // wallmarks
    ClearWallmarks();

    // unmask all bones
    visimask.zero();
    if (bones)
    {
        for (u32 b = 0; b < bones->size(); b++)
            visimask.set(b, true);
    }

    // visibility
    children.insert(children.end(), children_invisible.begin(), children_invisible.end());
    children_invisible.clear();
}

void CKinematics::Release()
{
    // xr_free bones
    for (u32 i = 0; i < bones->size(); i++)
    {
        CBoneData*& B = (*bones)[i];
        xr_delete(B);
    }

    // destroy shared data
    xr_delete(pUserData);
    xr_delete(bones);
    bone_map_N.clear();

    inherited::Release();
}

void CKinematics::LL_SetBoneVisible(u16 bone_id, BOOL val, BOOL bRecursive)
{
    visimask.set(XR_ASSERT_VAL(bone_id < LL_BoneCount(), "invalid bone ID", dbg_name), !!val);

    if (!visimask.is(bone_id))
        bone_instances[bone_id].mTransform.scale(0.f, 0.f, 0.f);
    else
        CalculateBones_Invalidate();

    bone_instances[bone_id].mRenderTransform.mul_43(bone_instances[bone_id].mTransform, (*bones)[bone_id]->m2b_transform);

    if (bRecursive)
    {
        for (auto child : (*bones)[bone_id]->children)
            LL_SetBoneVisible(child->GetSelfID(), val, bRecursive);
    }

    Visibility_Invalidate();
}

void CKinematics::LL_SetBonesVisible(VisMask mask)
{
    visimask.zero();
    for (u32 b = 0; b < bones->size(); b++)
    {
        if (mask.is(b))
        {
            visimask.set(b, true);
        }
        else
        {
            Fmatrix& A = bone_instances[b].mTransform;
            Fmatrix& B = bone_instances[b].mRenderTransform;
            A.scale(0.f, 0.f, 0.f);
            B.mul_43(A, (*bones)[b]->m2b_transform);
        }
    }
    CalculateBones_Invalidate();
    Visibility_Invalidate();
}

void CKinematics::Visibility_Update()
{
    XR_TRACY_ZONE_SCOPED();

    Update_Visibility = false;

    // check visible
    for (std::size_t i{0}; i < children.size();)
    {
        auto& child = children[i];

        if (XR_ASSERT_VAL(smart_cast<CSkeletonX*>(child) != nullptr)->has_visible_bones())
        {
            ++i;
            continue;
        }

        // move into invisible list
        children_invisible.emplace_back(child);
        std::swap(child, children.back());
        children.pop_back();

        Update_Visibility = true;
    }

    // check invisible
    for (std::size_t i{0}; i < children_invisible.size();)
    {
        auto& child = children_invisible[i];

        if (!XR_ASSERT_VAL(smart_cast<CSkeletonX*>(child) != nullptr)->has_visible_bones())
        {
            ++i;
            continue;
        }

        // move into visible list
        children.emplace_back(child);
        std::swap(child, children_invisible.back());
        children_invisible.pop_back();

        Update_Visibility = true;
    }
}

namespace
{
void RecursiveBindTransform(CKinematics* K, std::span<Fmatrix> matrices, u16 bone_id, const Fmatrix& parent)
{
    CBoneData& BD = K->LL_GetData(bone_id);
    Fmatrix& BM = matrices[bone_id];

    // Build matrix
    BM.mul_43(parent, BD.bind_transform);

    for (auto child : BD.children)
        RecursiveBindTransform(K, matrices, child->GetSelfID(), BM);
}

void BuildMatrix(Fmatrix& mView, f32 invsz, Fvector norm, Fvector from)
{
    // build projection
    Fmatrix mScale;
    Fvector at, up, right, y;
    at.sub(from, norm);
    y.set(0, 1, 0);
    if (_abs(norm.y) > .99f)
        y.set(1, 0, 0);
    right.crossproduct(y, norm);
    up.crossproduct(norm, right);
    mView.build_camera(from, at, up);
    mScale.scale(invsz, invsz, invsz);
    mView.mulA_43(mScale);
}
} // namespace

void CKinematics::LL_GetBindTransform(xr_vector<Fmatrix>& matrices)
{
    matrices.resize(LL_BoneCount());
    RecursiveBindTransform(this, matrices, iRoot, Fidentity);
}

void CKinematics::EnumBoneVertices(SEnumVerticesCallback& C, u16 bone_id)
{
    for (u32 i = 0; i < children.size(); i++)
        LL_GetChild(i)->EnumBoneVertices(C, bone_id);
}

DEFINE_VECTOR(Fobb, OBBVec, OBBVecIt);

bool CKinematics::PickBone(const Fmatrix& parent_xform, IKinematics::pick_result& r, float dist, const Fvector& start, const Fvector& dir, u16 bone_id)
{
    Fvector S, D; // normal		= {0,0,0}
    // transform ray from world to model
    Fmatrix P;
    P.invert(parent_xform);
    P.transform_tiny(S, start);
    P.transform_dir(D, dir);
    for (u32 i = 0; i < children.size(); i++)
        if (LL_GetChild(i)->PickBone(r, dist, S, D, bone_id))
        {
            parent_xform.transform_dir(r.normal);
            parent_xform.transform_tiny(r.tri[0]);
            parent_xform.transform_tiny(r.tri[1]);
            parent_xform.transform_tiny(r.tri[2]);
            return true;
        }
    return false;
}

void CKinematics::AddWallmark(const Fmatrix* parent_xform, const Fvector3& start, const Fvector3& dir, ref_shader shader, float size)
{
    XR_TRACY_ZONE_SCOPED();

    Fvector S, D, normal{};
    // transform ray from world to model
    Fmatrix P;
    P.invert(*parent_xform);
    P.transform_tiny(S, start);
    P.transform_dir(D, dir);
    // find pick point
    float dist = flt_max;
    BOOL picked = FALSE;

    xr_vector<Fobb> cache_obb(LL_BoneCount());
    IKinematics::pick_result r;

    r.normal = normal;
    r.dist = dist;

    for (u16 k = 0; k < LL_BoneCount(); k++)
    {
        CBoneData& BD = LL_GetData(k);
        if (LL_GetBoneVisible(k) && !BD.shape.flags.is(SBoneShape::sfNoPickable))
        {
            Fobb& obb = cache_obb[k];
            obb.transform(BD.obb, LL_GetBoneInstance(k).mTransform);
            if (CDB::TestRayOBB(S, D, obb))
                for (u32 i = 0; i < children.size(); i++)
                {
                    if (LL_GetChild(i)->PickBone(r, dist, S, D, k))
                    {
                        picked = TRUE;
                        dist = r.dist;
                        normal = r.normal;
                        // dynamics set wallmarks bug fix
                    }
                }
        }
    }

    if (!picked)
        return;

    // calculate contact point
    Fvector cp;
    cp.mad(S, D, dist);

    // collect collide boxes
    Fsphere test_sphere;
    test_sphere.set(cp, size);
    U16Vec test_bones;
    test_bones.reserve(LL_BoneCount());
    for (u16 k = 0; k < LL_BoneCount(); k++)
    {
        CBoneData& BD = LL_GetData(k);
        if (LL_GetBoneVisible(k) && !BD.shape.flags.is(SBoneShape::sfNoPickable))
        {
            Fobb& obb = cache_obb[k];
            if (CDB::TestSphereOBB(test_sphere, obb))
                test_bones.push_back(k);
        }
    }

    // find similar wm
    for (u32 wm_idx = 0; wm_idx < wallmarks.size(); wm_idx++)
    {
        intrusive_ptr<CSkeletonWallmark>& wm = wallmarks[wm_idx];
        if (wm->Similar(shader, cp, 0.02f))
        {
            if (wm_idx < wallmarks.size() - 1)
                wm = wallmarks.back();
            wallmarks.pop_back();
            break;
        }
    }

    // ok. allocate wallmark
    intrusive_ptr<CSkeletonWallmark> wm = xr_new<CSkeletonWallmark>(this, parent_xform, shader, cp, Device.fTimeGlobal);
    wm->m_LocalBounds.set(cp, size * 2.f);
    wm->XFORM()->transform_tiny(wm->m_Bounds.P, cp);
    wm->m_Bounds.R = wm->m_LocalBounds.R;

    Fvector tmp;
    tmp.invert(D);
    normal.add(tmp).normalize();

    // build UV projection matrix
    Fmatrix mView, mRot;
    BuildMatrix(mView, 1 / (0.9f * size), normal, cp);
    mRot.rotateZ(::Random.randF(deg2rad(-20.f), deg2rad(20.f)));
    mView.mulA_43(mRot);

    // fill vertices
    for (u32 i = 0; i < children.size(); i++)
    {
        CSkeletonX* S = LL_GetChild(i);
        for (U16It b_it = test_bones.begin(); b_it != test_bones.end(); b_it++)
            S->FillVertices(mView, *wm, normal, size, *b_it);
    }

    wallmarks.push_back(wm);
}

void CKinematics::CalculateWallmarks(bool hud)
{
    if (!wallmarks.empty() && (wm_frame != Device.dwFrame))
    {
        XR_TRACY_ZONE_SCOPED();

        wm_frame = Device.dwFrame;
        bool need_remove = false;
        for (SkeletonWMVecIt it = wallmarks.begin(); it != wallmarks.end(); it++)
        {
            intrusive_ptr<CSkeletonWallmark>& wm = *it;
            float w = (Device.fTimeGlobal - wm->TimeStart()) / ps_r__WallmarkTTL;
            if (w < 1.f)
            {
                // append wm to WallmarkEngine
                if (!hud && RImplementation.ViewBase.testSphere_dirty(wm->m_Bounds.P, wm->m_Bounds.R))
                    RImplementation.add_SkeletonWallmark(wm);
            }
            else
            {
                // remove wallmark
                need_remove = true;
            }
        }
        if (need_remove)
        {
            auto new_end = std::remove_if(wallmarks.begin(), wallmarks.end(), [](const auto& x) { return !x; });
            wallmarks.erase(new_end, wallmarks.end());
        }
    }
}

void CKinematics::RenderWallmark(CSkeletonWallmark& wm, std::span<FVF::LIT> verts) const
{
    XR_TRACY_ZONE_SCOPED();

    XR_ASSERT(bones != nullptr && bone_instances != nullptr);

    const auto color = color_rgba(128, 128, 128, std::clamp(iFloor(((Device.fTimeGlobal - wm.TimeStart()) / ps_r__WallmarkTTL) * 255.0f), 0, 255));

    // skin vertices
    for (auto [vc, face] : std::views::zip(verts | std::views::chunk(3), wm.m_Faces))
    {
        for (auto [V, uv, vert, weight, bone_id] : std::views::zip(vc, face.uv, face.vert, face.weight, face.bone_id))
        {
            Fvector P;

            if (bone_id[0] == bone_id[1])
            {
                // 1-link
                LL_GetBoneInstance(bone_id[0]).mRenderTransform.transform_tiny(P, vert);
            }
            else if (bone_id[1] == bone_id[2])
            {
                // 2-link
                Fvector P0, P1;
                LL_GetBoneInstance(bone_id[0]).mRenderTransform.transform_tiny(P0, vert);
                LL_GetBoneInstance(bone_id[1]).mRenderTransform.transform_tiny(P1, vert);

                P.lerp(P0, P1, weight[0]);
            }
            else if (bone_id[2] == bone_id[3])
            {
                // 3-link
                Fvector P0, P1, P2;
                LL_GetBoneInstance(bone_id[0]).mRenderTransform.transform_tiny(P0, vert);
                LL_GetBoneInstance(bone_id[1]).mRenderTransform.transform_tiny(P1, vert);
                LL_GetBoneInstance(bone_id[2]).mRenderTransform.transform_tiny(P2, vert);

                P0.mul(weight[0]);
                P1.mul(weight[1]);
                P2.mul(1.0f - weight[0] - weight[1]);

                P = P0;
                P.add(P1);
                P.add(P2);
            }
            else
            {
                // 4-link
                std::array<Fvector, 4> PB;

                for (auto [pb, bone] : std::views::zip(PB, bone_id))
                    LL_GetBoneInstance(bone).mRenderTransform.transform_tiny(pb, vert);

                f32 s{0.0f};

                for (auto [pb, wt] : std::views::zip(PB, weight))
                {
                    pb.mul(wt);
                    s += wt;
                }

                PB[3].mul(1.0f - s);
                P = PB[0];

                for (auto& pb : PB | std::views::drop(1))
                    P.add(pb);
            }

            wm.XFORM()->transform_tiny(V.p, P);
            V.color = color;
            V.t.set(uv);
        }
    }

    wm.XFORM()->transform_tiny(wm.m_Bounds.P, wm.m_LocalBounds.P);
}

void CKinematics::ClearWallmarks() { wallmarks.clear(); }

int CKinematics::LL_GetBoneGroups(xr_vector<xr_vector<u16>>& groups)
{
    groups.resize(children.size());
    for (u16 bone_idx = 0; bone_idx < (u16)bones->size(); bone_idx++)
    {
        CBoneData* B = (*bones)[bone_idx];
        for (u32 child_idx = 0; child_idx < children.size(); child_idx++)
        {
            if (!B->child_faces[child_idx].empty())
            {
                groups[child_idx].push_back(bone_idx);
            }
        }
    }
    return groups.size();
}

#ifdef DEBUG
CSkeletonWallmark::~CSkeletonWallmark() { XR_ASSERT(used_in_render == std::numeric_limits<u32>::max()); }
#endif

/************************* Add by Zander *************************************************************/
/************************* Дебаговые функции для исследования моделей. *******************************/

static Fbox& get_mesh_RC_data(FHierrarhyVisual* pHV, const u32 id) { return pHV->children.at(id)->getVisData().box; }

Fvector3 CKinematics::RC_VisBox(const u32 id)
{
    const Fbox& t = get_mesh_RC_data(this, id);
    Fvector3 modelSize;
    t.getsize(modelSize);
    return modelSize;
}

Fvector3 CKinematics::RC_VisCenter(const u32 id)
{
    const Fbox& t = get_mesh_RC_data(this, id);
    Fvector3 modelSize;
    t.getcenter(modelSize);
    return modelSize;
}

Fvector3 CKinematics::RC_VisBorderMin(const u32 id)
{
    const Fbox& t = get_mesh_RC_data(this, id);
    Fvector3 borderModelMin;
    borderModelMin.set(t.min);
    return borderModelMin;
}

Fvector3 CKinematics::RC_VisBorderMax(const u32 id)
{
    const Fbox& t = get_mesh_RC_data(this, id);
    Fvector3 borderModelMax;
    borderModelMax.set(t.max);
    return borderModelMax;
}

void CKinematics::RC_Dump()
{
    XR_LOG_TRACE_L1("|********** Dump children meshes **************|");

    const Fbox& mBox = getVisData().box;
    Fvector3 temp;
    mBox.getsize(temp);
    Msg("[core] : box[{:3.3f},{:3.3f},{:3.3f}]", temp.x, temp.y, temp.z);
    mBox.getcenter(temp);
    Msg("[core] : center[{:3.3f},{:3.3f},{:3.3f}]", temp.x, temp.y, temp.z);
    temp.set(mBox.min);
    Msg("[core] : min[{:3.3f},{:3.3f},{:3.3f}]", temp.x, temp.y, temp.z);
    temp.set(mBox.max);
    Msg("[core] : max[{:3.3f},{:3.3f},{:3.3f}]", temp.x, temp.y, temp.z);

    for (u32 i = 0; i < children.size(); i++)
    {
        temp.set(RC_VisBox(i));
        Msg("[child {}] : box[{:3.3f},{:3.3f},{:3.3f}]", i, temp.x, temp.y, temp.z);
        temp.set(RC_VisCenter(i));
        Msg("[child {}] : center[{:3.3f},{:3.3f},{:3.3f}]", i, temp.x, temp.y, temp.z);
        temp.set(RC_VisBorderMin(i));
        Msg("[child {}] : min[{:3.3f},{:3.3f},{:3.3f}]", i, temp.x, temp.y, temp.z);
        temp.set(RC_VisBorderMax(i));
        Msg("[child {}] : max[{:3.3f},{:3.3f},{:3.3f}]", i, temp.x, temp.y, temp.z);

        FHierrarhyVisual* HV = smart_cast<FHierrarhyVisual*>(children.at(i));
        if (HV && HV->children.size())
        {
            for (u32 j = 0; j < HV->children.size(); j++)
            {
                const Fbox& FB = get_mesh_RC_data(HV, j);
                FB.getsize(temp);
                Msg("[child {}->{}] : box[{:3.3f},{:3.3f},{:3.3f}]", i, j, temp.x, temp.y, temp.z);
                FB.getcenter(temp);
                Msg("[child {}->{}] : center[{:3.3f},{:3.3f},{:3.3f}]", i, j, temp.x, temp.y, temp.z);
                temp.set(FB.min);
                Msg("[child {}->{}] : min[{:3.3f},{:3.3f},{:3.3f}]", i, j, temp.x, temp.y, temp.z);
                temp.set(FB.max);
                Msg("[child {}->{}] : max[{:3.3f},{:3.3f},{:3.3f}]", i, j, temp.x, temp.y, temp.z);

                FHierrarhyVisual* kHV = smart_cast<FHierrarhyVisual*>(HV->children.at(j));
                if (kHV && kHV->children.size())
                {
                    for (u32 k = 0; k < kHV->children.size(); k++)
                    {
                        const Fbox& kFB = get_mesh_RC_data(kHV, k);
                        kFB.getsize(temp);
                        Msg("[child {}->{}->{}] : box[{:3.3f},{:3.3f},{:3.3f}]", i, j, k, temp.x, temp.y, temp.z);
                        kFB.getcenter(temp);
                        Msg("[child {}->{}->{}] : center[{:3.3f},{:3.3f},{:3.3f}]", i, j, k, temp.x, temp.y, temp.z);
                        temp.set(kFB.min);
                        Msg("[child {}->{}->{}] : min[{:3.3f},{:3.3f},{:3.3f}]", i, j, k, temp.x, temp.y, temp.z);
                        temp.set(kFB.max);
                        Msg("[child {}->{}->{}] : max[{:3.3f},{:3.3f},{:3.3f}]", i, j, k, temp.x, temp.y, temp.z);
                    }
                }
            }
        }
    }

    XR_LOG_TRACE_L1("|********** End Dump children meshes **********|");
    XR_LOG_TRACE_L1("|********** Dump children bones ***************|");

    for (u32 i = 0; i < bones->size(); i++)
    {
        CBoneData* B = (*bones)[i];
        Msg("Bone [{}][{}]:", i, LL_BoneName_dbg(u16(i)));
        Msg("bind_transform[{:3.3f},{:3.3f},{:3.3f}] m2b_transform[{:3.3f},{:3.3f},{:3.3f}] center_of_mass[{:3.3f},{:3.3f},{:3.3f}]", B->bind_transform.c.x,
            B->bind_transform.c.y, B->bind_transform.c.z, B->m2b_transform.c.x, B->m2b_transform.c.y, B->m2b_transform.c.z, B->center_of_mass.x,
            B->center_of_mass.y, B->center_of_mass.z);
    }

    XR_LOG_TRACE_L1("|********** End Dump children bones ***********|");
}
/************************* End add *************************************/
