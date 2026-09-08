#include "stdafx.h"

#include "../../xr_3da/Render.h"

#include "../xrRender/ResourceManager.h"
#include "../xrRender/tss.h"
#include "../xrRender/blenders/Blender.h"
#include "../xrRender/blenders/Blender_Recorder.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "../xrRenderDX10/dx10ConstantBuffer.h"

#include "../xrRender/ShaderResourceTraits.h"

#include <Utilities/FlexibleVertexFormat.h>

SHS* CResourceManager::_CreateHS(LPCSTR Name) { return CreateShader<SHS>(Name); }
void CResourceManager::_DeleteHS(const SHS* HS) { DestroyShader(HS); }

SDS* CResourceManager::_CreateDS(LPCSTR Name) { return CreateShader<SDS>(Name); }
void CResourceManager::_DeleteDS(const SDS* DS) { DestroyShader(DS); }

SCS* CResourceManager::_CreateCS(LPCSTR Name) { return CreateShader<SCS>(Name); }
void CResourceManager::_DeleteCS(const SCS* CS) { DestroyShader(CS); }

//--------------------------------------------------------------------------------------------------------------

SState* CResourceManager::_CreateState(SimulatorStates& state_code)
{
    // Search equal state-code
    for (SState* C : v_states)
    {
        if (C->state_code.equal(state_code))
            return C;
    }

    // Create New
    SState* S = v_states.emplace_back(xr_new<SState>());
    S->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    S->state = ID3DState::Create(state_code);
    S->state_code = state_code;

    return S;
}

void CResourceManager::_DeleteState(const SState* state)
{
    if (!(state->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(v_states, state))
        return;

    XR_LOG_ERROR("Failed to find compiled stateblock");
}

//--------------------------------------------------------------------------------------------------------------

SPass* CResourceManager::_CreatePass(const SPass& proto)
{
    for (SPass* pass : v_passes)
    {
        if (pass->equal(proto))
            return pass;
    }

    SPass* P = v_passes.emplace_back(xr_new<SPass>());
    P->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    P->state = proto.state;
    P->ps = proto.ps;
    P->vs = proto.vs;
    P->gs = proto.gs;
    P->hs = proto.hs;
    P->ds = proto.ds;
    P->cs = proto.cs;
    P->constants = proto.constants;
    P->T = proto.T;

    return P;
}

void CResourceManager::_DeletePass(const SPass* P)
{
    if (!(P->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(v_passes, P))
        return;

    XR_LOG_ERROR("Failed to find compiled pass");
}

//--------------------------------------------------------------------------------------------------------------

SVS* CResourceManager::_CreateVS(LPCSTR _name)
{
    int skinning = -1;
    string_path name;
    xr_strcpy(name, _name);
    if (0 == RImplementation.m_skinning)
    {
        xr_strcat(name, "_0");
        skinning = 0;
    }
    if (1 == RImplementation.m_skinning)
    {
        xr_strcat(name, "_1");
        skinning = 1;
    }
    if (2 == RImplementation.m_skinning)
    {
        xr_strcat(name, "_2");
        skinning = 2;
    }
    if (3 == RImplementation.m_skinning)
    {
        xr_strcat(name, "_3");
        skinning = 3;
    }
    if (4 == RImplementation.m_skinning)
    {
        xr_strcat(name, "_4");
        skinning = 4;
    }

    if (const auto I = m_vs.find(name); I != m_vs.end())
        return I->second;

    SVS* _vs = xr_new<SVS>();
    _vs->skinning = skinning;
    _vs->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    m_vs.try_emplace(_vs->set_name(name), _vs);

    if (std::is_eq(xr::strcasecmp(_name, "null")))
        return _vs;

    string_path shName;
    {
        const char* pchr = strchr(_name, '(');
        ptrdiff_t size = pchr ? pchr - _name : xr_strlen(_name);
        strncpy_s(shName, _name, size);
        shName[size] = 0;
    }

    string_path cname;
    strconcat(sizeof(cname), cname, RImplementation.getShaderPath(), shName, ".vs");
    std::ignore = FS.update_path(cname, "$game_shaders$", cname);

    const auto file = XR_ASSERT_VAL(absl::WrapUnique(FS.r_open(cname)), "", cname);
    file->skip_bom(cname);
    const std::string_view strbuf{reinterpret_cast<const char*>(file->pointer()), static_cast<size_t>(file->elapsed())};

    // Select target
    LPCSTR c_target = "vs_5_0";
    LPCSTR c_entry = "main";

    DWORD Flags{D3DCOMPILE_FLAGS_DEFAULT};
    if (strstr(Core.Params, "-shadersdbg"))
        Flags |= D3DCOMPILE_FLAGS_DEBUG;

    XR_ASSERT(xr::hr(RImplementation.shader_compile(name, reinterpret_cast<DWORD const*>(strbuf.data()), static_cast<UINT>(strbuf.size()), c_entry, c_target,
                                                    Flags, (void*&)_vs)),
              "failed to compile shader", cname, c_target);

    return _vs;
}

void CResourceManager::_DeleteVS(const SVS* vs)
{
    if (!(vs->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    const auto I = m_vs.find(vs->cName);
    if (I == m_vs.end())
    {
        Msg("! ERROR: Failed to find compiled vertex-shader '{}'", vs->cName);
        return;
    }

    m_vs.erase(I);

    for (auto iDecl = v_declarations.begin(); iDecl != v_declarations.end(); ++iDecl)
    {
        xr_map<ID3DBlob*, ID3DInputLayout*>::iterator iLayout;
        iLayout = (*iDecl)->vs_to_layout.find(vs->signature->signature);
        if (iLayout != (*iDecl)->vs_to_layout.end())
        {
            //	Release vertex layout
            _RELEASE(iLayout->second);
            (*iDecl)->vs_to_layout.erase(iLayout);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------

SPS* CResourceManager::_CreatePS(LPCSTR _name)
{
    string_path name;
    xr_strcpy(name, _name);
    if (0 == RImplementation.m_MSAASample)
        xr_strcat(name, "_0");
    if (1 == RImplementation.m_MSAASample)
        xr_strcat(name, "_1");
    if (2 == RImplementation.m_MSAASample)
        xr_strcat(name, "_2");
    if (3 == RImplementation.m_MSAASample)
        xr_strcat(name, "_3");
    if (4 == RImplementation.m_MSAASample)
        xr_strcat(name, "_4");
    if (5 == RImplementation.m_MSAASample)
        xr_strcat(name, "_5");
    if (6 == RImplementation.m_MSAASample)
        xr_strcat(name, "_6");
    if (7 == RImplementation.m_MSAASample)
        xr_strcat(name, "_7");

    if (const auto I = m_ps.find(name); I != m_ps.end())
        return I->second;

    SPS* _ps = xr_new<SPS>();
    _ps->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    m_ps.try_emplace(_ps->set_name(name), _ps);

    if (std::is_eq(xr::strcasecmp(_name, "null")))
    {
        _ps->ps = nullptr;
        return _ps;
    }

    string_path shName;
    const char* pchr = strchr(_name, '(');
    ptrdiff_t strSize = pchr ? pchr - _name : xr_strlen(_name);
    strncpy_s(shName, _name, strSize);
    shName[strSize] = 0;

    // Open file
    string_path cname;
    strconcat(sizeof(cname), cname, RImplementation.getShaderPath(), shName, ".ps");
    std::ignore = FS.update_path(cname, "$game_shaders$", cname);

    const auto file = XR_ASSERT_VAL(absl::WrapUnique(FS.r_open(cname)), "", cname);
    file->skip_bom(cname);
    const std::string_view strbuf{reinterpret_cast<const char*>(file->pointer()), static_cast<size_t>(file->elapsed())};

    // Select target
    LPCSTR c_target = "ps_5_0";
    LPCSTR c_entry = "main";

    DWORD Flags{D3DCOMPILE_FLAGS_DEFAULT};
    if (strstr(Core.Params, "-shadersdbg"))
        Flags |= D3DCOMPILE_FLAGS_DEBUG;

    XR_ASSERT(xr::hr(RImplementation.shader_compile(name, reinterpret_cast<DWORD const*>(strbuf.data()), static_cast<UINT>(strbuf.size()), c_entry, c_target,
                                                    Flags, (void*&)_ps)),
              "failed to compile shader", cname, c_target);

    return _ps;
}

void CResourceManager::_DeletePS(const SPS* ps)
{
    if (!(ps->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (const auto I = m_ps.find(ps->cName); I == m_ps.end())
        Msg("! ERROR: Failed to find compiled pixel-shader '{}'", ps->cName);
    else
        m_ps.erase(I);
}

//--------------------------------------------------------------------------------------------------------------

SGS* CResourceManager::_CreateGS(LPCSTR name)
{
    if (const auto I = m_gs.find(name); I != m_gs.end())
        return I->second;

    SGS* _gs = xr_new<SGS>();
    _gs->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    m_gs.try_emplace(_gs->set_name(name), _gs);

    if (std::is_eq(xr::strcasecmp(name, "null")))
    {
        _gs->gs = nullptr;
        return _gs;
    }

    // Open file
    string_path cname;
    strconcat(sizeof(cname), cname, RImplementation.getShaderPath(), name, ".gs");
    std::ignore = FS.update_path(cname, "$game_shaders$", cname);

    const auto file = XR_ASSERT_VAL(absl::WrapUnique(FS.r_open(cname)), "", cname);
    file->skip_bom(cname);

    // Select target
    LPCSTR c_target = "gs_5_0";
    LPCSTR c_entry = "main";

    DWORD Flags{D3DCOMPILE_FLAGS_DEFAULT};
    if (strstr(Core.Params, "-shadersdbg"))
        Flags |= D3DCOMPILE_FLAGS_DEBUG;

    XR_ASSERT(xr::hr(RImplementation.shader_compile(name, (DWORD const*)file->pointer(), file->elapsed(), c_entry, c_target, Flags, (void*&)_gs)),
              "failed to compile shader", cname, c_target);

    return _gs;
}

void CResourceManager::_DeleteGS(const SGS* gs)
{
    if (!(gs->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (const auto I = m_gs.find(gs->cName); I == m_gs.end())
        Msg("! ERROR: Failed to find compiled geometry shader '{}'", gs->cName);
    else
        m_gs.erase(I);
}

//--------------------------------------------------------------------------------------------------------------

SDeclaration* CResourceManager::_CreateDecl(const D3DVERTEXELEMENT9* dcl)
{
    const auto dcls = std::span{dcl, FVF::GetDeclLength(dcl) + 1};

    // Search equal code
    if (const auto it = std::ranges::find_if(v_declarations,
                                             [dcls] [[nodiscard]] (const auto item) {
                                                 return std::ranges::equal(dcls, item->dcl_code, [] [[nodiscard]] (const auto& lhs, const auto& rhs) {
                                                     return lhs.Stream == rhs.Stream && lhs.Offset == rhs.Offset && lhs.Type == rhs.Type &&
                                                         lhs.Method == rhs.Method && lhs.Usage == rhs.Usage && lhs.UsageIndex == rhs.UsageIndex;
                                                 });
                                             });
        it != v_declarations.end())
        return *it;

    // Create _new
    SDeclaration* D = v_declarations.emplace_back(xr_new<SDeclaration>());
    D->dcl_code.assign_range(dcls);
    dx10BufferUtils::ConvertVertexDeclaration(D->dcl_code, D->dx10_dcl_code);
    D->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    return D;
}

void CResourceManager::_DeleteDecl(const SDeclaration* dcl)
{
    if (!(dcl->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(v_declarations, dcl))
        return;

    XR_LOG_ERROR("Failed to find compiled vertex-declarator");
}

//--------------------------------------------------------------------------------------------------------------

R_constant_table* CResourceManager::_CreateConstantTable(const R_constant_table& C)
{
    if (C.empty())
        return nullptr;

    if (const auto it = std::ranges::find_if(v_constant_tables, [&C] [[nodiscard]] (const auto item) { return item->equal(C); }); it != v_constant_tables.end())
        return *it;

    R_constant_table* table = v_constant_tables.emplace_back(xr_new<R_constant_table>());
    table->clone(C);
    table->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    return table;
}

void CResourceManager::_DeleteConstantTable(const R_constant_table* C)
{
    if (!(C->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(v_constant_tables, C))
        return;

    XR_LOG_ERROR("Failed to find compiled constant-table");
}

//--------------------------------------------------------------------------------------------------------------

CRT* CResourceManager::_CreateRT(LPCSTR Name, u32 w, u32 h, D3DFORMAT f, u32 sampleCount, u32 slices_num, Flags32 flags)
{
    XR_ASSERT(Name != nullptr && Name[0] != '\0');
    XR_ASSERT(w > 0 && h > 0, "", Name, w, h);

    // ***** first pass - search already created RT
    if (const auto I = m_rtargets.find(Name); I != m_rtargets.end())
        return I->second;

    CRT* RT = xr_new<CRT>();
    RT->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    m_rtargets.emplace(RT->set_name(Name), RT);

    if (Device.b_is_Ready)
        RT->create(Name, w, h, f, sampleCount, slices_num, flags);

    return RT;
}

void CResourceManager::_DeleteRT(const CRT* RT)
{
    if (!(RT->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (const auto I = m_rtargets.find(RT->cName); I == m_rtargets.end())
        Msg("! ERROR: Failed to find render-target '{}'", RT->cName);
    else
        m_rtargets.erase(I);
}

//--------------------------------------------------------------------------------------------------------------

SGeometry* CResourceManager::CreateGeom(const D3DVERTEXELEMENT9* decl, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
    XR_ASSERT(decl != nullptr && vb != nullptr);

    SDeclaration* dcl = _CreateDecl(decl);
    const auto vb_stride = FVF::ComputeVertexSize(decl, 0);

    if (const auto it = std::ranges::find_if(v_geoms,
                                             [dcl, vb, vb_stride, ib] [[nodiscard]] (const auto elem) {
                                                 return elem->dcl == dcl && elem->vb == vb && elem->vb_stride == vb_stride && elem->ib == ib;
                                             });
        it != v_geoms.end())
        return *it;

    SGeometry* Geom = v_geoms.emplace_back(xr_new<SGeometry>());
    Geom->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    Geom->dcl._set(dcl);
    Geom->vb = vb;
    Geom->vb_stride = vb_stride;
    Geom->ib = ib;

    return Geom;
}

SGeometry* CResourceManager::CreateGeom(u32 FVF, ID3DVertexBuffer* vb, ID3DIndexBuffer* ib)
{
    std::vector<D3DVERTEXELEMENT9> dcl;
    dcl.reserve(MAXD3DDECLLENGTH + 1);

    XR_ASSERT(FVF::CreateDeclFromFVF(FVF, dcl));

    return CreateGeom(dcl.data(), vb, ib);
}

void CResourceManager::DeleteGeom(const SGeometry* Geom)
{
    if (!(Geom->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(v_geoms, Geom))
        return;

    XR_LOG_ERROR("Failed to find compiled geometry-declaration");
}

CTexture* CResourceManager::_CreateTexture(LPCSTR _Name)
{
    XR_ASSERT(_Name != nullptr && _Name[0] != '\0');

    if (std::is_eq(xr_strcmp(_Name, "null")))
        return nullptr;

    string_path Name;
    xr_strcpy(Name, _Name);
    fix_texture_name(Name);

    // ***** first pass - search already loaded texture
    if (const auto I = m_textures.find(Name); I != m_textures.end())
        return I->second;

    CTexture* T = xr_new<CTexture>();
    T->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    m_textures.emplace(T->set_name(Name), T);

    T->Preload();
    if (Device.b_is_Ready && !bDeferredLoad)
        T->Load();

    return T;
}

void CResourceManager::_DeleteTexture(const CTexture* T)
{
    if (!(T->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (const auto I = m_textures.find(T->cName); I == m_textures.end())
        Msg("! ERROR: Failed to find texture surface '{}'", T->cName);
    else
        m_textures.erase(I);
}

#ifdef DEBUG
void CResourceManager::DBG_VerifyTextures()
{
    for (const auto pair : m_textures)
    {
        XR_ASSERT(pair.first != nullptr);
        XR_ASSERT(pair.second != nullptr && pair.second->cName, "", pair.first);
        XR_ASSERT(std::is_eq(xr_strcmp(pair.first, pair.second->cName)), "", pair.first, pair.second->cName);
    }
}
#endif

STextureList* CResourceManager::_CreateTextureList(STextureList& L)
{
    std::ranges::sort(L.list, {}, &std::pair<u32, ref_texture>::first);

    for (STextureList* base : lst_textures)
    {
        if (L.equal(*base))
            return base;
    }

    STextureList* lst = lst_textures.emplace_back(xr_new<STextureList>());
    lst->clone(L);
    lst->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    return lst;
}

void CResourceManager::_DeleteTextureList(const STextureList* L)
{
    if (!(L->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(lst_textures, L))
        return;

    XR_LOG_ERROR("Failed to find compiled list of textures");
}

dx10ConstantBuffer* CResourceManager::_CreateConstantBuffer(ctx_id_t context_id, ID3DShaderReflectionConstantBuffer* pTable)
{
    auto pTempBuffer = xr_new<dx10ConstantBuffer>(XR_ASSERT_VAL(pTable != nullptr));

    for (dx10ConstantBuffer* buf : v_constant_buffer[context_id])
    {
        if (pTempBuffer->Similar(*buf))
        {
            xr_delete(pTempBuffer);
            return buf;
        }
    }

    pTempBuffer->dwFlags |= xr_resource_flagged::RF_REGISTERED;
    v_constant_buffer[context_id].emplace_back(pTempBuffer);

    return pTempBuffer;
}

void CResourceManager::_DeleteConstantBuffer(const dx10ConstantBuffer* pBuffer)
{
    if (!(pBuffer->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    for (auto& buf : v_constant_buffer)
    {
        if (reclaim(buf, pBuffer))
            return;
    }

    XR_LOG_ERROR("Failed to find compiled constant buffer");
}

//--------------------------------------------------------------------------------------------------------------

SInputSignature* CResourceManager::_CreateInputSignature(ID3DBlob* pBlob)
{
    XR_ASSERT(pBlob != nullptr);

    for (SInputSignature* sign : v_input_signature)
    {
        if ((pBlob->GetBufferSize() == sign->signature->GetBufferSize()) &&
            (!(memcmp(pBlob->GetBufferPointer(), sign->signature->GetBufferPointer(), pBlob->GetBufferSize()))))
        {
            return sign;
        }
    }

    SInputSignature* pSign = v_input_signature.emplace_back(xr_new<SInputSignature>(pBlob));
    pSign->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    return pSign;
}

void CResourceManager::_DeleteInputSignature(const SInputSignature* pSignature)
{
    if (!(pSignature->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;

    if (reclaim(v_input_signature, pSignature))
        return;

    XR_LOG_ERROR("Failed to find input signature");
}
