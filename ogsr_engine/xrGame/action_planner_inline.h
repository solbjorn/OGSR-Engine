////////////////////////////////////////////////////////////////////////////
//	Module 		: action_planner_inline.h
//	Created 	: 28.01.2004
//  Modified 	: 10.03.2004
//	Author		: Dmitriy Iassenev
//	Description : Action planner inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

#define TEMPLATE_SPECIALIZATION \
    template <typename _object_type, bool _reverse_search, typename _world_operator, typename _condition_evaluator, typename _world_operator_ptr, \
              typename _condition_evaluator_ptr>

#define CPlanner CActionPlanner<_object_type, _reverse_search, _world_operator, _condition_evaluator, _world_operator_ptr, _condition_evaluator_ptr>

TEMPLATE_SPECIALIZATION
IC CPlanner::~CActionPlanner() { m_object = nullptr; }

TEMPLATE_SPECIALIZATION
void CPlanner::setup(_object_type* object)
{
    inherited::setup();
    m_object = object;
    m_current_action_id = _action_id_type(-1);
    m_storage.clear();
    m_initialized = false;
    m_loaded = false;
}

TEMPLATE_SPECIALIZATION
IC _object_type& CPlanner::object() const
{
    VERIFY(m_object);
    return (*m_object);
}

TEMPLATE_SPECIALIZATION
void CPlanner::update()
{
    this->solve();

#ifdef LOG_ACTION
    // printing solution
    if (m_use_log)
    {
        if (this->m_solution_changed)
        {
            show_current_world_state();
            show_target_world_state();

            Msg("{:6} : Solution for object {} [{} vertices searched]", Device.dwTimeGlobal, object_name(),
                ai().graph_engine().solver_algorithm().data_storage().get_visited_node_count());

            for (auto sol : this->solution())
                Msg("{}", action2string(sol));
        }
    }

    if (this->m_failed)
    {
        // printing current world state
        show();

        XR_LOG_ERROR("There is no action sequence, which can transfer current world state to the target one");
        Msg("Time : {:6}", Device.dwTimeGlobal);
        Msg("Object : {}", object_name());

        show_current_world_state();
        show_target_world_state();
    }
#endif

    XR_ASSERT(!this->solution().empty());

    if (initialized())
    {
        if (current_action_id() != this->solution().front())
        {
            current_action().finalize();
            m_current_action_id = this->solution().front();
            current_action().initialize();
        }
    }
    else
    {
        m_initialized = true;
        m_current_action_id = this->solution().front();
        current_action().initialize();
    }

    current_action().execute();
}

TEMPLATE_SPECIALIZATION
IC typename CPlanner::COperator& CPlanner::action(const _action_id_type& action_id) { return (*(this->get_operator(action_id))); }

TEMPLATE_SPECIALIZATION
IC typename CPlanner::CConditionEvaluator& CPlanner::evaluator(const _condition_type& evaluator_id) { return (*inherited::evaluator(evaluator_id)); }

TEMPLATE_SPECIALIZATION
IC typename CPlanner::_action_id_type CPlanner::current_action_id() const
{
    VERIFY(initialized());
    return (m_current_action_id);
}

TEMPLATE_SPECIALIZATION
IC typename CPlanner::COperator& CPlanner::current_action() { return (action(current_action_id())); }

TEMPLATE_SPECIALIZATION
IC bool CPlanner::initialized() const { return (m_initialized); }

TEMPLATE_SPECIALIZATION
IC void CPlanner::add_condition(_world_operator* action, _condition_type condition_id, _value_type condition_value)
{
    action->add_condition(CWorldProperty(condition_id, condition_value));
}

TEMPLATE_SPECIALIZATION
IC void CPlanner::add_effect(_world_operator* action, _condition_type condition_id, _value_type condition_value)
{
    action->add_effect(CWorldProperty(condition_id, condition_value));
}

#ifdef LOG_ACTION
TEMPLATE_SPECIALIZATION
LPCSTR CPlanner::action2string(const _action_id_type& action_id) { return (action(action_id).m_action_name); }

TEMPLATE_SPECIALIZATION
LPCSTR CPlanner::property2string(const _condition_type& property_id) { return evaluator(property_id).m_evaluator_name; }

TEMPLATE_SPECIALIZATION
LPCSTR CPlanner::object_name() const { return (*m_object->cName()); }
#endif

TEMPLATE_SPECIALIZATION
IC void CPlanner::add_operator(const _edge_type& operator_id, _operator_ptr _operator)
{
    inherited::add_operator(operator_id, _operator);
    _operator->setup(m_object, &m_storage);
#ifdef LOG_ACTION
    _operator->set_use_log(m_use_log);
#endif
}

TEMPLATE_SPECIALIZATION
IC void CPlanner::add_evaluator(const _condition_type& condition_id, _condition_evaluator_ptr evaluator)
{
    inherited::add_evaluator(condition_id, evaluator);
    evaluator->setup(m_object, &m_storage);
}

#ifdef LOG_ACTION
TEMPLATE_SPECIALIZATION
IC void CPlanner::set_use_log(bool value)
{
    m_use_log = value;
    for (auto& it : this->m_operators)
        it.get_operator()->set_use_log(m_use_log);
}

TEMPLATE_SPECIALIZATION
IC void CPlanner::show_current_world_state()
{
    XR_LOG_TRACE_L1("Current world state :");

    for (const auto& it : this->evaluators())
    {
        auto J = std::lower_bound(this->current_state().conditions().cbegin(), this->current_state().conditions().cend(), CWorldProperty(it.first, false));
        char temp = '?';
        if ((J != this->current_state().conditions().end()) && ((*J).condition() == it.first))
        {
            temp = (*J).value() ? '+' : '-';
            Msg("{:5} : [{}][{}]", temp, it.first, property2string(it.first));
        }
    }
}

TEMPLATE_SPECIALIZATION
IC void CPlanner::show_target_world_state()
{
    XR_LOG_TRACE_L1("Target world state :");

    for (const auto& it : this->evaluators())
    {
        auto J = std::lower_bound(this->target_state().conditions().cbegin(), this->target_state().conditions().cend(), CWorldProperty(it.first, false));
        char temp = '?';
        if ((J != this->target_state().conditions().end()) && ((*J).condition() == it.first))
        {
            temp = (*J).value() ? '+' : '-';
            Msg("{:5} : [{}][{}]", temp, it.first, property2string(it.first));
        }
    }
}

TEMPLATE_SPECIALIZATION
IC void CPlanner::show(LPCSTR offset)
{
    string256 temp;
    strconcat(sizeof(temp), temp, offset, "    ");
    Msg("\n{}EVALUATORS : {}\n", offset, this->evaluators().size());

    for (const auto& it : this->evaluators())
    {
        auto J = std::lower_bound(this->current_state().conditions().cbegin(), this->current_state().conditions().cend(), CWorldProperty(it.first, false));
        char current = '?';

        if ((J != this->current_state().conditions().end()) && ((*J).condition() == it.first))
            current = (*J).value() ? '+' : '-';

        Msg("{}evaluator   [{}][{}][{}]", offset, it.first, property2string(it.first), current);
    }

    Msg("\n{}OPERATORS : {}\n", offset, this->operators().size());

    for (const auto& it : this->operators())
    {
        Msg("{}operator    [{}][{}]", offset, it.m_operator_id, it.m_operator->m_action_name);

        for (const auto& it2 : it.m_operator->conditions().conditions())
            Msg("{}\tcondition [{}][{}] = {}", offset, it2.condition(), property2string(it2.condition()), it2.value() ? "TRUE" : "FALSE");
        for (const auto& it2 : it.m_operator->effects().conditions())
            Msg("{}\teffect    [{}][{}] = {}", offset, it2.condition(), property2string(it2.condition()), it2.value() ? "TRUE" : "FALSE");

        it.m_operator->show(temp);

        XR_LOG_TRACE_L1("");
    }
}
#endif

TEMPLATE_SPECIALIZATION
IC void CPlanner::save(NET_Packet& packet)
{
    for (auto& it : this->m_evaluators)
        it.second->save(packet);

    for (auto& it : this->m_operators)
        it.m_operator->save(packet);

    packet.w_u32(m_storage.m_storage.size());
    for (const auto& it : m_storage.m_storage)
    {
        packet.w(&it.m_condition, sizeof(it.m_condition));
        packet.w(&it.m_value, sizeof(it.m_value));
    }
}

TEMPLATE_SPECIALIZATION
IC void CPlanner::load(IReader& packet)
{
    for (auto& it : this->m_evaluators)
        it.second->load(packet);

    for (auto& it : this->m_operators)
        it.m_operator->load(packet);

    {
        u32 count = packet.r_u32();
        GraphEngineSpace::_solver_condition_type condition;
        GraphEngineSpace::_solver_value_type value;
        for (u32 i = 0; i < count; ++i)
        {
            packet.r(&condition, sizeof(condition));
            packet.r(&value, sizeof(value));
            m_storage.set_property(condition, value);
        }
    }
    m_loaded = true;
}

#undef TEMPLATE_SPECIALIZATION
#undef CPlanner
