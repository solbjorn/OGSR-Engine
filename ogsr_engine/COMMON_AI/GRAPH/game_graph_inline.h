////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_inline.h
//	Created 	: 18.02.2003
//  Modified 	: 13.11.2003
//	Author		: Dmitriy Iassenev
//	Description : Game graph inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC CGameGraph::CGameGraph(IReader* stream, bool separatedGraphs)
{
    m_reader = XR_ASSERT_VAL(stream != nullptr);
    m_separated_graphs = separatedGraphs;

    m_header.load(m_reader);
    XR_ASSERT(header().version() == XRAI_CURRENT_VERSION, "graph version mismatch");

    m_nodes = (const CVertex*)m_reader->pointer();
    m_current_level_some_vertex_id = _GRAPH_ID(-1);
    m_enabled.assign(header().vertex_count(), true);

    if (m_separated_graphs)
    {
        m_cross_tables = nullptr;
        m_current_level_cross_table = nullptr;
    }
    else
    {
        const u8* temp = (const u8*)(m_nodes + header().vertex_count());
        temp += header().edge_count() * sizeof(CGameGraph::CEdge);
        m_cross_tables = (const u32*)(((const CLevelPoint*)temp) + header().death_point_count());
        m_current_level_cross_table = nullptr;
    }
}

IC CGameGraph::~CGameGraph()
{
    xr_delete(m_current_level_cross_table);
    if (m_separated_graphs)
        FS.r_close(m_reader);
}

IC const CGameGraph::CHeader& CGameGraph::header() const { return (m_header); }

IC bool CGameGraph::mask(const std::inplace_vector<_LOCATION_ID, GameGraph::LOCATION_TYPE_COUNT>& M, const _LOCATION_ID E[GameGraph::LOCATION_TYPE_COUNT]) const
{
    for (int i = 0; i < GameGraph::LOCATION_TYPE_COUNT; ++i)
        if ((M[i] != E[i]) && (255 != M[i]))
            return (false);
    return (true);
}

IC float CGameGraph::distance(const _GRAPH_ID tGraphID0, const _GRAPH_ID tGraphID1) const
{
    const_iterator i, e;
    begin(tGraphID0, i, e);
    for (; i != e; ++i)
        if (value(tGraphID0, i) == tGraphID1)
            return (edge_weight(i));

    // TODO: KD: не всегда построенный путь для монстра валидный. Пока вывожу в лог и сбрасываю путь в вызвавшей функции.
    XR_LOG_ERROR("There is no way to get distance from vertex [{}] to vertex [{}]!!!", tGraphID0, tGraphID1);

    return (_GRAPH_ID(-1));
}

IC bool CGameGraph::accessible(const u32& vertex_id) const
{
#ifdef CRASH_ON_INVALID_VERTEX_ID
    return XR_ASSERT_VAL(valid_vertex_id(vertex_id)) && m_enabled[vertex_id];
#else
    return XR_DEBUG_ASSERT_VAL(valid_vertex_id(vertex_id)) && m_enabled[vertex_id];
#endif
}

IC void CGameGraph::accessible(const u32& vertex_id, bool value) const
{
#ifdef CRASH_ON_INVALID_VERTEX_ID
    XR_ASSERT(valid_vertex_id(vertex_id), "invalid vertex ID", vertex_id, value);
#else
    XR_DEBUG_ASSERT(valid_vertex_id(vertex_id), "invalid vertex ID", vertex_id, value);
#endif

    m_enabled[vertex_id] = value;
}

IC bool CGameGraph::valid_vertex_id(const u32& vertex_id) const { return (vertex_id < header().vertex_count()); }

IC void CGameGraph::begin(const u32& vertex_id, const_iterator& start, const_iterator& end) const
{
    end = (start = (const CEdge*)((const BYTE*)m_nodes + vertex(_GRAPH_ID(vertex_id))->edge_offset())) + vertex(_GRAPH_ID(vertex_id))->edge_count();
}

inline const CGameGraph::_GRAPH_ID& CGameGraph::value(const u32&, const_iterator& i) const { return i->vertex_id(); }
inline const float& CGameGraph::edge_weight(const_iterator i) const { return i->distance(); }

IC const CGameGraph::CVertex* CGameGraph::vertex(const u32& vertex_id) const
{
#ifdef CRASH_ON_INVALID_VERTEX_ID
    return XR_ASSERT_VAL(valid_vertex_id(vertex_id), "invalid vertex ID", vertex_id) ? (m_nodes + vertex_id) : (m_nodes + header().vertex_count() - 1);
#else
    return XR_DEBUG_ASSERT_VAL(valid_vertex_id(vertex_id), "invalid vertex ID", vertex_id) ? (m_nodes + vertex_id) : (m_nodes + header().vertex_count() - 1);
#endif
}

IC const u8& CGameGraph::CHeader::version() const { return (m_version); }

IC GameGraph::_LEVEL_ID GameGraph::CHeader::level_count() const
{
    XR_ASSERT(m_levels.size() < (1uz << (8 * sizeof(GameGraph::_LEVEL_ID))));
    return (GameGraph::_LEVEL_ID)m_levels.size();
}

IC const GameGraph::_GRAPH_ID& GameGraph::CHeader::vertex_count() const { return (m_vertex_count); }

IC const u32& GameGraph::CHeader::edge_count() const { return (m_edge_count); }

IC const u32& GameGraph::CHeader::death_point_count() const { return (m_death_point_count); }

IC const GameGraph::LEVEL_MAP& GameGraph::CHeader::levels() const { return (m_levels); }

IC bool GameGraph::CHeader::level_exist(const _LEVEL_ID& id) const { return levels().find(id) != levels().end(); }

IC bool GameGraph::CHeader::level_exist(pcstr level_name) const
{
    for (const auto& levelPair : levels())
        if (xr_strcmp(levelPair.second.name(), level_name) == 0)
            return true;
    return false;
}

IC const GameGraph::SLevel& GameGraph::CHeader::level(const _LEVEL_ID& id) const
{
    return XR_ASSERT_VAL(levels().find(id) != levels().end(), "no such level in the game graph", id)->second;
}

IC const GameGraph::SLevel& GameGraph::CHeader::level(LPCSTR level_name) const
{
    for (const auto& lvl : levels())
    {
        if (std::is_eq(xr_strcmp(lvl.second.name(), level_name)))
            return lvl.second;
    }

#ifdef DEBUG
    XR_LOG_CRITICAL("No such level (\"{}\") in the game graph", level_name);
    return levels().begin()->second;
#else
    XR_PANIC("no such level in the game graph", level_name);
#endif
}

IC const GameGraph::SLevel* GameGraph::CHeader::level(LPCSTR level_name, bool) const
{
    for (const auto& lvl : levels())
    {
        if (std::is_eq(xr_strcmp(lvl.second.name(), level_name)))
            return &lvl.second;
    }

    return nullptr;
}

IC const xrGUID& CGameGraph::CHeader::guid() const { return (m_guid); }

IC const Fvector& GameGraph::CVertex::level_point() const { return (tLocalPoint); }
IC const Fvector& GameGraph::CVertex::game_point() const { return (tGlobalPoint); }
IC GameGraph::_LEVEL_ID GameGraph::CVertex::level_id() const { return (tLevelID); }

IC u32 GameGraph::CVertex::level_vertex_id() const { return (tNodeID); }
IC const u8* GameGraph::CVertex::vertex_type() const { return (tVertexTypes); }

IC const u8& GameGraph::CVertex::edge_count() const { return (tNeighbourCount); }
IC const u32& GameGraph::CVertex::edge_offset() const { return (dwEdgeOffset); }

IC const GameGraph::_GRAPH_ID& GameGraph::CEdge::vertex_id() const { return (m_vertex_id); }
IC const float& GameGraph::CEdge::distance() const { return (m_path_distance); }

IC void CGameGraph::set_invalid_vertex(_GRAPH_ID& vertex_id) const
{
    vertex_id = _GRAPH_ID(-1);
    XR_DEBUG_ASSERT(!valid_vertex_id(vertex_id));
}

IC GameGraph::_GRAPH_ID CGameGraph::vertex_id(const CGameGraph::CVertex* vertex) const
{
    const auto id = _GRAPH_ID(XR_ASSERT_VAL(vertex >= m_nodes) - m_nodes);

#ifdef CRASH_ON_INVALID_VERTEX_ID
    XR_ASSERT(valid_vertex_id(id));
#else
    XR_DEBUG_ASSERT(valid_vertex_id(id));
#endif

    return id;
}

IC const GameGraph::_GRAPH_ID& CGameGraph::current_level_vertex() const
{
#ifdef CRASH_ON_INVALID_VERTEX_ID
    XR_ASSERT(valid_vertex_id(m_current_level_some_vertex_id));
#else
    XR_DEBUG_ASSERT(valid_vertex_id(m_current_level_some_vertex_id));
#endif

    return m_current_level_some_vertex_id;
}

IC void GameGraph::SLevel::load(IReader* reader)
{
    reader->r_stringZ(m_name);
    reader->r_fvector3(m_offset);
    reader->r(&m_id, sizeof(m_id));
    reader->r_stringZ(m_section);
    reader->r(&m_guid, sizeof(m_guid));
}

IC void GameGraph::SLevel::save(IWriter* writer)
{
    writer->w_stringZ(m_name);
    writer->w_fvector3(m_offset);
    writer->w(&m_id, sizeof(m_id));
    writer->w_stringZ(m_section);
    writer->w(&m_guid, sizeof(m_guid));
}

IC void GameGraph::CHeader::load(IReader* reader)
{
    reader->r(&m_version, sizeof(m_version));
    reader->r(&m_vertex_count, sizeof(m_vertex_count));
    reader->r(&m_edge_count, sizeof(m_edge_count));
    reader->r(&m_death_point_count, sizeof(m_death_point_count));
    reader->r(&m_guid, sizeof(m_guid));

    u32 level_count = reader->r_u8();
    m_levels.clear();

    for (u32 i = 0; i < level_count; ++i)
    {
        SLevel l_tLevel;
        l_tLevel.load(reader);
        m_levels.emplace(l_tLevel.id(), std::move(l_tLevel));
    }
}

IC void GameGraph::CHeader::save(IWriter* writer)
{
    writer->w(&m_version, sizeof(m_version));
    writer->w(&m_vertex_count, sizeof(m_vertex_count));
    writer->w(&m_edge_count, sizeof(m_edge_count));
    writer->w(&m_death_point_count, sizeof(m_death_point_count));
    writer->w(&m_guid, sizeof(m_guid));
    writer->w_u8(XR_ASSERT_VAL(m_levels.size() < (1uz << (8 * sizeof(u8)))));

    for (auto& level : m_levels)
        level.second.save(writer);
}

inline CGameLevelCrossTable* CGameGraph::find_cross_table_for_level(u32 level_id) const
{
    if (m_cross_tables == nullptr)
        return nullptr;

    const u32* current_cross_table = m_cross_tables;

    for (const auto& levelPair : header().levels())
    {
        if (level_id != levelPair.first)
        {
            current_cross_table = (const u32*)((const u8*)current_cross_table + *current_cross_table);
            continue;
        }

        return xr_new<CGameLevelCrossTable>(current_cross_table + 1, *current_cross_table);
    }

    return nullptr;
}

inline void CGameGraph::set_current_level(u32 level_id)
{
    xr_delete(m_current_level_cross_table);

    if (m_cross_tables != nullptr)
    {
        // build in COP mode
        m_current_level_cross_table = find_cross_table_for_level(level_id);
    }
    else
    {
        string_path fName;
        std::ignore = FS.update_path(fName, "$level$", CROSS_TABLE_NAME);
        m_current_level_cross_table = xr_new<CGameLevelCrossTable>(fName);
    }

    XR_ASSERT(m_current_level_cross_table != nullptr);
    m_current_level_some_vertex_id = _GRAPH_ID(-1);

    for (_GRAPH_ID i = 0, n = header().vertex_count(); i < n; ++i)
    {
        if (level_id != vertex(i)->level_id())
            continue;

        m_current_level_some_vertex_id = i;
        break;
    }

#ifdef CRASH_ON_INVALID_VERTEX_ID
    XR_ASSERT(valid_vertex_id(m_current_level_some_vertex_id));
#else
    XR_DEBUG_ASSERT(valid_vertex_id(m_current_level_some_vertex_id));
#endif
}

IC const CGameLevelCrossTable& CGameGraph::cross_table() const { return *XR_ASSERT_VAL(m_current_level_cross_table != nullptr); }
