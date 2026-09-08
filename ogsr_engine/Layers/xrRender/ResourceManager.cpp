// TextureManager.cpp: implementation of the CResourceManager class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "ResourceManager.h"

#include "blenders/Blender.h"
#include "blenders/Blender_Recorder.h"
#include "tss.h"

IBlender* CResourceManager::_GetBlender(LPCSTR Name)
{
    XR_ASSERT(Name != nullptr && Name[0] != '\0');
    return XR_ASSERT_VAL(m_blenders.find(Name) != m_blenders.end(), "shader not found in library", Name)->second;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void CResourceManager::_ParseList(sh_list& dest, LPCSTR names)
{
    if (nullptr == names || 0 == names[0])
        names = "$null";

    dest.clear();
    const char* P = (const char*)names;
    std::inplace_vector<char, 128> N;

    while (*P)
    {
        if (*P == ',')
        {
            // flush
            N.push_back(0);
            _strlwr(N.data());

            fix_texture_name(N.data());
            dest.emplace_back(N.data());

            N.clear();
        }
        else
        {
            N.push_back(*P);
        }

        P++;
    }

    if (!N.empty())
    {
        // flush
        N.push_back(0);
        _strlwr(N.data());

        fix_texture_name(N.data());
        dest.emplace_back(N.data());
    }
}

ShaderElement* CResourceManager::_CreateElement(ShaderElement&& S)
{
    if (S.passes.empty())
        return nullptr;

    // Search equal in shaders array
    for (ShaderElement* elem : v_elements)
        if (S.equal(*elem))
            return elem;

    // Create _new_ entry
    ShaderElement* N = v_elements.emplace_back(xr_new<ShaderElement>());
    N->clone(std::move(S));
    N->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    return N;
}

void CResourceManager::_DeleteElement(const ShaderElement* S)
{
    if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;
    if (reclaim(v_elements, S))
        return;

    XR_LOG_ERROR("Failed to find compiled 'shader-element'");
}

Shader* CResourceManager::Create(IBlender* B, const char* s_shader, const char* s_textures) { return _cpp_Create(B, s_shader, s_textures); }

Shader* CResourceManager::Create(const char* s_shader, const char* s_textures)
{
    //	TODO: DX10: When all shaders are ready switch to common path
    if (_lua_HasShader(s_shader))
        return _lua_Create(s_shader, s_textures);

    return XR_ASSERT_VAL(_cpp_Create(s_shader, s_textures) != nullptr, "can't find shader", s_shader, s_textures);
}

Shader* CResourceManager::_cpp_Create(const char* s_shader, const char* s_textures)
{
    //	TODO: DX10: When all shaders are ready switch to common path
    IBlender* pBlender = _GetBlender(s_shader ? s_shader : "null");
    if (!pBlender)
        return nullptr;
    return _cpp_Create(pBlender, s_shader, s_textures);
}

Shader* CResourceManager::_cpp_Create(IBlender* B, const char* s_shader, const char* s_textures)
{
    CBlender_Compile C;
    Shader S;

    // Access to template
    C.BT = B;
    C.bDetail = FALSE;
    C.HudElement = false;

    // Parse names
    _ParseList(C.L_textures, s_textures);

    if (RImplementation.hud_loading && RImplementation.o.ssfx_hud_raindrops)
        C.HudElement = true;

    // Compile element	(LOD0 - HQ)
    {
        C.iElement = 0;
        C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[0]._set(_CreateElement(std::move(E)));
    }

    // Compile element	(LOD1)
    {
        C.iElement = 1;
        C.bDetail = m_textures_description.GetDetailTexture(C.L_textures[0], C.detail_texture, C.detail_scaler);
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[1]._set(_CreateElement(std::move(E)));
    }

    // Compile element
    {
        C.iElement = 2;
        C.bDetail = FALSE;
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[2]._set(_CreateElement(std::move(E)));
    }

    // Compile element
    {
        C.iElement = 3;
        C.bDetail = FALSE;
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[3]._set(_CreateElement(std::move(E)));
    }

    // Compile element
    {
        C.iElement = 4;
        C.bDetail = TRUE; //.$$$ HACK :)
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[4]._set(_CreateElement(std::move(E)));
    }

    // Compile element
    {
        C.iElement = 5;
        C.bDetail = FALSE;
        ShaderElement E;
        C._cpp_Compile(&E);
        S.E[5]._set(_CreateElement(std::move(E)));
    }

    // Hacky way to remove from the HUD mask transparent stuff. ( Let's try something better later... )
    if (RImplementation.hud_loading)
    {
        if (strstr(s_shader, "lens"))
            S.E[0]->passes[0]->ps->dwFlags |= xr_resource_flagged::RF_HUD_DISABLED;
    }

    // Search equal in shaders array
    for (auto sh : v_shaders)
    {
        if (S.equal(sh))
            return sh;
    }

    // Create _new_ entry
    Shader* N = v_shaders.emplace_back(xr_new<Shader>());
    N->clone(std::move(S));
    N->dwFlags |= xr_resource_flagged::RF_REGISTERED;

    return N;
}

void CResourceManager::Delete(const Shader* S)
{
    if (0 == (S->dwFlags & xr_resource_flagged::RF_REGISTERED))
        return;
    if (reclaim(v_shaders, S))
        return;

    XR_LOG_ERROR("Failed to find complete shader");
}

tmc::task<void> CResourceManager::DeferredUpload()
{
    if (!Device.b_is_Ready)
        co_return;

    Msg("CResourceManager::DeferredUpload [MT] -> START, size = [{}]", m_textures.size());
    XR_LOG_INFO("VRAM usage before:");

    xr::render_memory_usage usage;
    _GetMemoryUsage(usage);

    Msg("textures loaded size: {} Mb ({} bytes)", gsl::narrow_cast<f32>(usage.m_base + usage.m_lmaps) / 1024.0f / 1024.0f, usage.m_base + usage.m_lmaps);
    HW.DumpVideoMemoryUsage();

    // Теперь многопоточная загрузка текстур даёт очень существенный прирост скорости, проверено.
    co_await tmc::spawn_many(m_textures | std::views::transform([](const auto& m_tex) -> tmc::task<void> {
                                 m_tex.second->Load();
                                 co_return;
                             }))
        .with_priority(xr::tmc_priority_any);

    XR_LOG_INFO("VRAM usage after:");
    _GetMemoryUsage(usage);

    Msg("textures loaded size: {} Mb ({} bytes)", gsl::narrow_cast<f32>(usage.m_base + usage.m_lmaps) / 1024.0f / 1024.0f, usage.m_base + usage.m_lmaps);
    HW.DumpVideoMemoryUsage();

    XR_LOG_NOTICE("END");
}

void CResourceManager::_GetMemoryUsage(xr::render_memory_usage& usage) const
{
    std::memset(&usage, 0, sizeof(usage));

    for (auto [name, tex] : m_textures)
    {
        const auto m = gsl::narrow_cast<gsl::index>(tex->memUsage);

        if (std::strstr(name, "lmap") != nullptr)
        {
            usage.c_lmaps++;
            usage.m_lmaps += m;
        }
        else
        {
            usage.c_base++;
            usage.m_base += m;
        }
    }

    usage.lua = LS_mem();
}

void CResourceManager::_DumpMemoryUsage() const
{
    xr_multimap<gsl::index, std::pair<gsl::index, gsl::czstring>> mtex;

    // sort
    for (auto [_, tex] : m_textures)
        mtex.emplace(gsl::narrow_cast<gsl::index>(tex->memUsage), std::make_pair(tex->ref_count.load(), tex->cName.c_str()));

    // dump
    for (const auto [mem, entry] : mtex)
        Msg("* {:4.1f} : {:4} {}", gsl::narrow_cast<f32>(mem) / 1024.0f, entry.first, entry.second);

    Msg("* {:4.1f} : Lua", gsl::narrow_cast<f32>(LS_mem()) / 1024.0f);
}

xr_vector<ITexture*> CResourceManager::FindTexture(const char* Name) const
{
    XR_ASSERT(Name != nullptr && Name[0] != '\0');

    string_path filename;
    strcpy_s(filename, Name);
    fix_texture_name(filename);

    char* ch = strchr(filename, '*');

    xr_vector<ITexture*> res;

    if (!ch) // no wildcard?
    {
        auto I = m_textures.find(filename);
        if (I != m_textures.end())
            res.emplace_back(I->second);
    }
    else
    {
        // alpet: test for wildcard matching
        ch[0] = 0; // remove *

        for (const auto& [name, tex] : m_textures)
        {
            if (strstr(name, filename) != nullptr)
                res.emplace_back(tex);
        }
    }

    return res;
}
