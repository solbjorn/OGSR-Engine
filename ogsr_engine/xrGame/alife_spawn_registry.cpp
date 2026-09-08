////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_spawn_registry.cpp
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife spawn registry
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "alife_spawn_registry.h"

#include "object_broker.h"
#include "game_base.h"
#include "ai_space.h"
#include "game_graph.h"
#include "script_engine.h"

CALifeSpawnRegistry::CALifeSpawnRegistry() { m_spawn_name._set(""); }

CALifeSpawnRegistry::~CALifeSpawnRegistry()
{
    xr_delete(m_game_graph);
    if (m_spawn_chunk)
        m_spawn_chunk->close();
    FS.r_close(m_file);
}

void CALifeSpawnRegistry::save(IWriter& memory_stream)
{
    XR_LOG_NOTICE("Saving spawns...");
    memory_stream.open_chunk(SPAWN_CHUNK_DATA);

    memory_stream.open_chunk(0);
    memory_stream.w_stringZ(m_spawn_name);
    memory_stream.w(&header().guid(), sizeof(header().guid())); //-V568
    memory_stream.close_chunk();

    memory_stream.open_chunk(1);
    // save_updates				(memory_stream); ???
    memory_stream.close_chunk();

    memory_stream.close_chunk();
}

void CALifeSpawnRegistry::load(IReader& file_stream, LPCSTR game_name)
{
    XR_LOG_NOTICE("Loading spawn registry...");
    XR_ASSERT(FS.exist(game_name) != nullptr, "", game_name);

    IReader *chunk, *chunk0;
    R_ASSERT2(file_stream.find_chunk(SPAWN_CHUNK_DATA), "Cannot find chunk SPAWN_CHUNK_DATA!");
    chunk0 = file_stream.open_chunk(SPAWN_CHUNK_DATA);

    xrGUID guid;
    chunk = chunk0->open_chunk(0);
    VERIFY(chunk);
    chunk->r_stringZ(m_spawn_name);
    chunk->r(&guid, sizeof(guid));
    chunk->close();

    string_path file_name;
    bool file_exists = !!FS.exist(file_name, "$game_spawn$", m_spawn_name.c_str(), ".spawn");
    R_ASSERT3(file_exists, "Can't find spawn file:", m_spawn_name.c_str());

    VERIFY(!m_file);
    m_file = FS.r_open(file_name);
    load(*m_file, &guid);

    chunk0->close();
}

void CALifeSpawnRegistry::load(LPCSTR spawn_name)
{
    XR_LOG_NOTICE("Loading spawn registry...");

    m_spawn_name._set(spawn_name);
    string_path file_name;
    R_ASSERT3(FS.exist(file_name, "$game_spawn$", m_spawn_name.c_str(), ".spawn"), "Can't find spawn file:", m_spawn_name.c_str());

    VERIFY(!m_file);
    m_file = FS.r_open(file_name);
    load(*m_file);
}

struct dummy final
{
    int count;
    lua_State* state;
    int ref;
};

void CALifeSpawnRegistry::load(IReader& file_stream, xrGUID* save_guid)
{
    IReader* chunk;
    chunk = file_stream.open_chunk(0);
    m_header.load(*chunk);
    chunk->close();
    R_ASSERT2(!save_guid || (*save_guid == header().guid()), "Saved game doesn't correspond to the spawn : DELETE SAVED GAME!");

    bool separated_graphs = false;
    VERIFY(!m_spawn_chunk);

    IReader* stream = file_stream.open_chunk(4);
    if (!stream)
    {
        string_path file_name;
        std::ignore = FS.update_path(file_name, _game_data_, GRAPH_NAME);
        stream = FS.r_open(file_name);
        separated_graphs = true;
    }
    else
    {
        m_spawn_chunk = stream;
    }

    R_ASSERT(stream, "Spawn version mismatch - REBUILD SPAWN!");

    VERIFY(!m_game_graph);
    m_game_graph = xr_new<CGameGraph>(stream, separated_graphs);
    ai().set_game_graph(m_game_graph);

    if (pSettings->line_exist("engine_callbacks", "after_graph_load"))
    {
        const char* callback = pSettings->r_string("engine_callbacks", "after_graph_load");
        if (sol::function lua_function; ai().script_engine().function(callback, lua_function))
            lua_function(m_game_graph);
    }

    chunk = file_stream.open_chunk(1);
    m_spawns.load(*chunk);
    chunk->close();

    chunk = file_stream.open_chunk(3);
    R_ASSERT2(chunk, "Spawn version mismatch - REBUILD SPAWN!");
    ai().patrol_path_storage(*chunk);
    chunk->close();

    if (pSettings->line_exist("engine_custom_spawn", "waypoints_file"))
    {
        string_path fname;
        std::ignore = FS.update_path(fname, "$game_config$", pSettings->r_string("engine_custom_spawn", "waypoints_file"));

        if (FS.exist(fname))
        {
            XR_LOG_NOTICE("Start load of custom waypoints...");

            CInifile way_inifile = CInifile(fname);
            ai().patrol_path_storage_ini(way_inifile);

            XR_LOG_NOTICE("End load of custom waypoints...");
        }
    }

    XR_LOG_NOTICE("build_story_spawns start...");
    XR_ASSERT(header().graph_guid() == ai().game_graph().header().guid(), "spawn doesn't correspond to the graph");

    build_story_spawns();
    build_root_spawns();

    Msg("* {} spawn points are successfully loaded", m_spawns.vertex_count());
}

void CALifeSpawnRegistry::build_root_spawns()
{
    m_temp0.clear();
    m_temp1.clear();

    {
        SPAWN_GRAPH::const_vertex_iterator I = m_spawns.vertices().begin();
        SPAWN_GRAPH::const_vertex_iterator E = m_spawns.vertices().end();
        for (; I != E; ++I)
            m_temp0.push_back((*I).second->vertex_id());
    }

    {
        SPAWN_GRAPH::const_vertex_iterator I = m_spawns.vertices().begin();
        SPAWN_GRAPH::const_vertex_iterator E = m_spawns.vertices().end();
        for (; I != E; ++I)
        {
            SPAWN_GRAPH::const_iterator i = (*I).second->edges().begin();
            SPAWN_GRAPH::const_iterator e = (*I).second->edges().end();
            for (; i != e; ++i)
                m_temp1.push_back((*i).vertex_id());
        }
    }

    process_spawns(m_temp0);
    process_spawns(m_temp1);

    m_spawn_roots.resize(m_temp0.size() + m_temp1.size());
    xr_vector<ALife::_SPAWN_ID>::iterator I = std::set_difference(m_temp0.begin(), m_temp0.end(), m_temp1.begin(), m_temp1.end(), m_spawn_roots.begin());

    m_spawn_roots.erase(I, m_spawn_roots.end());
}

void CALifeSpawnRegistry::build_story_spawns()
{
    for (const auto& [id, obj] : m_spawns.vertices())
    {
        const auto* object = smart_cast<const CSE_ALifeObject*>(&obj->data()->object());

        m_spawn_ids_by_name.emplace(object->name_replace(), id);

        if (object->m_spawn_story_id != INVALID_SPAWN_STORY_ID)
        {
#ifdef USE_STORY_ID_AS_SPAWN_ID
            // Особо умные могут назначить одинаковые спавн айди куче разных объектов.
            XR_ASSERT(m_spawn_story_ids.find(object->m_spawn_story_id) == m_spawn_story_ids.end(), "duplicate spawn story ID", object->name_replace(), id,
                      object->m_spawn_story_id);
#endif

            m_spawn_story_ids.emplace(object->m_spawn_story_id, id);
        }
#ifdef USE_STORY_ID_AS_SPAWN_ID
        else if (object->m_story_id != INVALID_STORY_ID)
        {
            // Особо умные могут назначить одинаковые спавн/стори айди куче разных объектов либо разные одному и тому же.
            XR_ASSERT(m_spawn_story_ids.find(object->m_story_id) == m_spawn_story_ids.end(), "duplicate story ID", object->name_replace(), id,
                      object->m_story_id);
            m_spawn_story_ids.emplace(object->m_story_id, id);
        }
#endif
    }
}

ALife::_SPAWN_ID CALifeSpawnRegistry::spawn_id(const ALife::_SPAWN_STORY_ID& spawn_story_id) const
{
    auto it = m_spawn_story_ids.find(spawn_story_id);
    if (it != m_spawn_story_ids.end())
        return it->second;

    // KRodin: В оригинале при ненахождении стори айди, возвращался рандомный мусор, на мой взгляд лучше вернуть -1 и в лог написать, потому что там стоял
    // VERIFY. Хотя я не уверен что надо писать...
    Msg("!![{}] Spawn story id [{}] cannot be found!", std::source_location::current().function_name(), spawn_story_id);

    return ALife::_SPAWN_ID(-1);
}

ALife::_SPAWN_ID CALifeSpawnRegistry::spawn_id(const char* obj_name) const
{
    auto it = m_spawn_ids_by_name.find(obj_name);
    if (it != m_spawn_ids_by_name.end())
        return it->second;

    // KRodin: а вот тут надо в лог писать или нет? Хз, надеюсь т.к. метод новый, код с его использованием будут писать более адекватно и поэтому вывод в лог не
    // нужен.
    return ALife::_SPAWN_ID(-1);
}
