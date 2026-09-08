#include "stdafx.h"

#include "PSLibrary.h"

#include "ParticleEffect.h"
#include "ParticleGroup.h"

#define _game_data_ "$game_data$"

void CPSLibrary::OnCreate()
{
    PAPI::ParticleManager()->init();
    LoadAll();
}

void CPSLibrary::LoadAll()
{
    if (!Load2()) // load ltx pg and pe
    {
        string_path fn;
        if (FS.exist(fn, _game_data_, "particles_cop.xr"))
        {
            Msg("Load [{}]", fn);
            Load(fn);
        }
        else if (FS.exist(fn, _game_data_, "particles.xr"))
        {
            Msg("Load [{}]", fn);
            Load(fn);
        }
    }

    for (auto& pair : m_PEDs)
        pair.second->CreateShader();
}

void CPSLibrary::ExportAllAsNew()
{
    string_path fn;
    std::ignore = FS.update_path(fn, _game_data_, "particles_cop.new_xr"); // dummy file name

    Msg("~ Exported all ltx shaders to [{}] in COP format. Rename file to use it.", fn);
    Save(fn);
}

void CPSLibrary::OnDestroy()
{
    for (auto& pair : m_PEDs)
        pair.second->DestroyShader();

    m_PEDs.clear();
    m_PGDs.clear();
}

PS::CPEDef* CPSLibrary::FindPED(const char* Name)
{
    // auto it = Name ? m_PEDs.find(Name) : m_PEDs.end(); //Сомневаюсь что сюда может попасть nullptr
    auto it = m_PEDs.find(Name);
    return it == m_PEDs.end() ? nullptr : it->second.get();
}

PS::CPGDef* CPSLibrary::FindPGD(const char* Name)
{
    // auto it = Name ? m_PGDs.find(Name) : m_PGDs.end(); //Сомневаюсь что сюда может попасть nullptr
    auto it = m_PGDs.find(Name);
    return it == m_PGDs.end() ? nullptr : it->second.get();
}

bool CPSLibrary::Load(const char* nm)
{
    IReader* F = FS.r_open(nm);
    if (F->length() == 0)
        return true;

    XR_ASSERT(F->find_chunk(PS_CHUNK_VERSION) > 0, "", nm);
    if (F->r_u16() != PS_VERSION)
        return false;

    bool bRes = true;
    size_t loaded_count{};
    const bool copFileFormat = strstr(nm, "_cop");

    if (copFileFormat)
        Msg("cop format used for file [{}]", nm);

    // second generation
    IReader* OBJ;
    OBJ = F->open_chunk(PS_CHUNK_SECONDGEN);
    if (OBJ)
    {
        IReader* O = OBJ->open_chunk(0);
        for (int count = 1; O; count++)
        {
            auto def = std::make_unique<PS::CPEDef>();
            if (def->Load(*O))
            {
                def->m_copFormat = copFileFormat;

                m_PEDs[def->m_Name] = std::move(def);
                loaded_count++;
            }
            else
            {
                bRes = false;
            }
            O->close();
            if (!bRes)
                break;
            O = OBJ->open_chunk(count);
        }
        OBJ->close();
    }
    // second generation
    OBJ = F->open_chunk(PS_CHUNK_THIRDGEN);
    if (OBJ)
    {
        IReader* O = OBJ->open_chunk(0);
        for (int count = 1; O; count++)
        {
            auto def = std::make_unique<PS::CPGDef>();
            if (def->Load(*O))
            {
                m_PGDs[def->m_Name] = std::move(def);
                loaded_count++;
            }
            else
            {
                bRes = false;
            }
            O->close();
            if (!bRes)
                break;
            O = OBJ->open_chunk(count);
        }
        OBJ->close();
    }

    // final
    FS.r_close(F);

    Msg("Loaded xr_particle files: [{}]", loaded_count);

    return bRes;
}

bool CPSLibrary::Save(const char* nm)
{
    CMemoryWriter F;

    F.open_chunk(PS_CHUNK_VERSION);
    F.w_u16(PS_VERSION);
    F.close_chunk();

    F.open_chunk(PS_CHUNK_SECONDGEN);
    u32 chunk_id = 0;
    for (auto it = m_PEDs.begin(); it != m_PEDs.end(); ++it, ++chunk_id)
    {
        F.open_chunk(chunk_id);
        it->second->Save(F);
        F.close_chunk();
    }
    F.close_chunk();

    F.open_chunk(PS_CHUNK_THIRDGEN);
    chunk_id = 0;
    for (auto g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it, ++chunk_id)
    {
        F.open_chunk(chunk_id);
        g_it->second->Save(F);
        F.close_chunk();
    }
    F.close_chunk();

    return F.save_to(nm);
}

void CPSLibrary::Reload()
{
    OnDestroy();
    OnCreate();

    XR_LOG_NOTICE("PS Library was successfully reloaded");
}

bool CPSLibrary::Load2()
{
    if (!FS.path_exist("$game_particles$"))
        return false;

    XR_LOG_NOTICE("Start load particle files...");

    FS_FileSet files;
    string_path _path;

    std::ignore = FS.update_path(_path, "$game_particles$", "");
    std::ignore = FS.file_list(files, _path, FS_ListFiles, "*.pe,*.pg");

    string_path p_path, p_name, p_ext;
    bool something_loaded{};

    for (const auto& f : files)
    {
        _splitpath(f.name.c_str(), nullptr, p_path, p_name, p_ext);
        std::ignore = FS.update_path(_path, "$game_particles$", f.name.c_str());
        CInifile ini{_path, true, true, false};

        xr_sprintf(_path, sizeof(_path), "%s%s", p_path, p_name);
        something_loaded = true;

        if (std::is_eq(xr::strcasecmp(p_ext, ".pe")))
        {
            auto def = std::make_unique<PS::CPEDef>();
            def->m_copFormat = true; // always in cop mode
            def->m_Name._set(_path);
            if (def->Load2(ini))
                m_PEDs[def->m_Name] = std::move(def);
        }
        else if (std::is_eq(xr::strcasecmp(p_ext, ".pg")))
        {
            auto def = std::make_unique<PS::CPGDef>();
            def->m_Name._set(_path);
            if (def->Load2(ini))
                m_PGDs[def->m_Name] = std::move(def);
        }
        else
        {
            XR_PANIC("invalid particle file format", f.name);
        }
    }

    Msg("Loaded particle files: [{}]", files.size());

    return something_loaded;
}

bool CPSLibrary::Save2(bool override)
{
    if (!FS.path_exist("$game_particles$"))
        XR_LOG_ERROR("Path $game_particles$ is not configured! Cannot export particles to ltx");

    string_path fn;

    for (auto& [m_Name, pe] : m_PEDs)
    {
        std::ignore = FS.update_path(fn, "$game_particles$", m_Name.c_str());
        strcat(fn, ".pe");

        if (FS.exist(fn) && !override)
            continue;

        CInifile ini{fn, false, true, true};
        pe->Save2(ini);
    }

    for (auto& [m_Name, pg] : m_PGDs)
    {
        std::ignore = FS.update_path(fn, "$game_particles$", m_Name.c_str());
        strcat(fn, ".pg");

        if (FS.exist(fn) && !override)
            continue;

        CInifile ini{fn, false, true, true};
        pg->Save2(ini);
    }

    return true;
}
