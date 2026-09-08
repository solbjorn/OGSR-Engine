////////////////////////////////////////////////////////////////////////////
//	Module 		: ai_space.h
//	Created 	: 12.11.2003
//  Modified 	: 12.11.2003
//	Author		: Dmitriy Iassenev
//	Description : AI space class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "ai_space.h"

#include "game_graph.h"
#include "game_level_cross_table.h"
#include "level_graph.h"
#include "graph_engine.h"
#include "ef_storage.h"
#include "cover_manager.h"
#include "cover_point.h"
#include "script_engine.h"
#include "patrol_path_storage.h"
#include "alife_simulator.h"

CAI_Space* g_ai_space{};

CAI_Space::CAI_Space() = default;

void CAI_Space::init()
{
    VERIFY(!m_ef_storage);
    m_ef_storage = xr_new<CEF_Storage>();

    VERIFY(!m_graph_engine);
    m_graph_engine = xr_new<CGraphEngine>(1024u); // game_graph().header().vertex_count() ???

    VERIFY(!m_cover_manager);
    m_cover_manager = xr_new<CCoverManager>();

    VERIFY(!m_patrol_path_storage);
    m_patrol_path_storage = xr_new<CPatrolPathStorage>();

    VERIFY(!m_script_engine);
    m_script_engine = xr_new<CScriptEngine>();
    script_engine().init();
}

CAI_Space::~CAI_Space()
{
    xr_delete(m_graph_engine);
    xr_delete(m_level_graph);

    script_engine().unload();

    xr_delete(m_patrol_path_storage);

    xr_delete(m_patrol_path_storage);
    xr_delete(m_ef_storage);
    xr_delete(m_graph_engine);

    VERIFY(!m_game_graph);

    xr_delete(m_script_engine);
    xr_delete(m_cover_manager);
}

void CAI_Space::set_game_graph(CGameGraph* graph)
{
    if (graph)
    {
        VERIFY(!m_game_graph);
    }
    else
    {
        VERIFY(m_game_graph);
    }

    m_game_graph = graph;
    xr_delete(m_graph_engine);

    if (m_game_graph)
        m_graph_engine = xr_new<CGraphEngine>(game_graph().header().vertex_count());
}

const CGameLevelCrossTable& CAI_Space::cross_table() const { return (game_graph().cross_table()); }
const CGameLevelCrossTable* CAI_Space::get_cross_table() const { return (&game_graph().cross_table()); }

void CAI_Space::load(LPCSTR level_name)
{
    VERIFY(m_game_graph);

    unload(true); // перезагрузка на одной и тоже локации !!!

#ifdef DEBUG
    Memory.mem_compact();
    const auto mem_usage = Memory.mem_usage();

    CTimer timer;
    timer.Start();
#endif

    const CGameGraph::SLevel& current_level = game_graph().header().level(level_name);

    string_path file_name;
    std::ignore = FS.update_path(file_name, "$level$", LEVEL_GRAPH_NAME.data());
    m_level_graph = xr_new<CLevelGraph>(file_name);

    game_graph().set_current_level(current_level.id());
    auto& crossHeader = cross_table().header();
    auto& levelHeader = level_graph().header();
    auto& gameHeader = game_graph().header();
    R_ASSERT(crossHeader.level_guid() == levelHeader.guid(), "cross_table doesn't correspond to the AI-map");
    R_ASSERT(crossHeader.game_guid() == gameHeader.guid(), "graph doesn't correspond to the cross table");

    u32 vertexCount = _max(static_cast<u32>(gameHeader.vertex_count()), levelHeader.vertex_count());
    m_graph_engine = xr_new<CGraphEngine>(vertexCount);
    R_ASSERT(current_level.guid() == levelHeader.guid(), "graph doesn't correspond to the AI-map");

#ifdef DEBUG
    if (!xr_strcmp(current_level.name(), level_name))
        validate(current_level.id());
#endif

    level_graph().level_id(current_level.id());

    m_cover_manager->compute_static_cover();

#ifdef DEBUG
    Msg("* Loading ai space is successfully completed ({:.4}s, {} Kb)", timer.GetElapsed_sec(), (Memory.mem_usage() - mem_usage) / 1024);
#endif
}

void CAI_Space::unload(bool reload)
{
    xr_delete(m_graph_engine);
    xr_delete(m_level_graph);

    if (!reload)
    {
        script_engine().unload();
        script_engine().init();

        xr_delete(m_patrol_path_storage);
        m_cover_manager->clear();
    }
}

#ifdef DEBUG
void CAI_Space::validate(const u32 level_id) const
{
    VERIFY(level_graph().header().vertex_count() == cross_table().header().level_vertex_count());

    for (auto i : std::views::indices(game_graph().header().vertex_count()))
    {
        if ((level_id == game_graph().vertex(i)->level_id()) &&
            (!level_graph().valid_vertex_id(game_graph().vertex(i)->level_vertex_id()) ||
             (cross_table().vertex(game_graph().vertex(i)->level_vertex_id()).game_vertex_id() != i) ||
             !level_graph().inside(game_graph().vertex(i)->level_vertex_id(), game_graph().vertex(i)->level_point())))
            XR_PANIC("Graph doesn't correspond to the cross table");
    }

    for (u32 i = 0, n = game_graph().header().vertex_count(); i < n; ++i)
    {
        if (level_id != game_graph().vertex(i)->level_id())
            continue;

        CGameGraph::const_spawn_iterator I, E;
        game_graph().begin_spawn(i, I, E);

        for (; I != E; ++I)
            VERIFY(cross_table().vertex((*I).level_vertex_id()).game_vertex_id() == i);
    }
}
#endif

void CAI_Space::patrol_path_storage(IReader& stream)
{
    xr_delete(m_patrol_path_storage);

    m_patrol_path_storage = xr_new<CPatrolPathStorage>();
    m_patrol_path_storage->load(stream);
}

void CAI_Space::patrol_path_storage_ini(CInifile& way_inifile) { m_patrol_path_storage->append_from_ini(way_inifile); }

void CAI_Space::set_alife(CALifeSimulator* alife_simulator)
{
    VERIFY((!m_alife_simulator && alife_simulator) || (m_alife_simulator && !alife_simulator));
    m_alife_simulator = alife_simulator;

    VERIFY(!alife_simulator || !m_game_graph);
    if (alife_simulator)
        return;

    set_game_graph(nullptr);
}
