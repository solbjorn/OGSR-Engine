#include "stdafx.h"

#include "ElevatorState.h"

#include "ClimableObject.h"
#include "Level.h"
#include "PHCharacter.h"
#include "MathUtils.h"
#include "PHWorld.h"

#ifdef DEBUG
#include "PHDebug.h"
#include "../xr_3da/Statgraph.h"
#endif

namespace
{
constexpr float getting_on_dist{0.3f};
constexpr float stop_climbing_dist{0.1f};
constexpr float out_dist{1.5f};

constexpr float look_angle_cosine{0.9238795f}; // 22.5
constexpr float lookup_angle_sine{0.34202014f}; // 20
} // namespace

CElevatorState::CElevatorState() = default;

float CElevatorState::ClimbDirection()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector d;
    m_ladder->DToPlain(m_character, d);
    float dir = m_character->ControlAccel().dotproduct(d);
    if (dir > EPS_L)
        dir *= (m_character->CamDir().y + lookup_angle_sine);
    return dir;
}

void CElevatorState::PhTune(float)
{
    XR_ASSERT(m_character != nullptr && m_character->b_exist && m_character->is_active());

    if (!m_ladder)
        return;

    if ((!g_actor_allow_ladder && m_character->RestrictionType() == CPHCharacter::rtActor) || m_character->RestrictionType() == CPHCharacter::rtMonsterMedium)
    {
        if (m_state != clbNoLadder)
            UpdateDepart();
        else
        {
            m_state = clbNoLadder;
            m_ladder = nullptr;
        }

        return;
    }

    switch (m_state)
    {
    case clbNone: UpdateStNone(); break;
    case clbNearUp: UpdateStNearUp(); break;
    case clbNearDown: UpdateStNearDown(); break;
    case clbClimbingUp: UpdateStClimbingUp(); break;
    case clbClimbingDown: UpdateStClimbingDown(); break;
    case clbDepart: UpdateDepart(); break;
    case clbNoLadder: m_ladder = nullptr; break;
    default: xr::unreachable();
    }
}

void CElevatorState::PhDataUpdate(float) {}
void CElevatorState::InitContact(dContact*, bool&, u16, u16) {}

void CElevatorState::SetElevator(CClimableObject* climable)
{
    Fvector d;
    float dist = climable->DDToAxis(m_character, d);
    if (m_ladder == climable || dist > out_dist)
        return;
    if (m_ladder && m_ladder->DDToAxis(m_character, d) < dist)
        return;
    SwitchState(clbNone);
    m_ladder = climable;
}

void CElevatorState::SetCharacter(CPHCharacter* character)
{
    m_character = character;
    SwitchState(clbNoLadder);
}

void CElevatorState::EvaluateState() { XR_ASSERT(m_ladder != nullptr && m_character != nullptr); }

#ifdef DEBUG
const char* dbg_state[] = {"clbNone", "clbNearUp", "clbNearDown", "clbClimbingUp", "clbClimbingDown", "clbDepart", "clbNoLadder"};
#endif

void CElevatorState::SwitchState(Estate new_state)
{
    if (!StateSwitchInertion(new_state))
        return;

#ifdef DEBUG
    if (ph_dbg_draw_mask.test(phDbgLadder))
        Msg("{}", dbg_state[new_state]);
#endif

    XR_ASSERT(m_character != nullptr);

    if ((m_state != clbClimbingUp && m_state != clbClimbingDown) && (new_state == clbClimbingUp || new_state == clbClimbingDown))
        dBodySetGravityMode(m_character->get_body(), 0);

    if ((new_state != clbClimbingUp && new_state != clbClimbingDown) && (m_state == clbClimbingUp || m_state == clbClimbingDown))
        dBodySetGravityMode(m_character->get_body(), 1);

    NewState();
    m_state = new_state;
}

void CElevatorState::UpdateStNone()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector d;
    m_ladder->DToPlain(m_character, d);
    if (m_ladder->BeforeLadder(m_character) && m_ladder->InTouch(m_character) && dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine)
    {
        if (ClimbDirection() > 0.f)
        {
            SwitchState(clbClimbingUp);
        }
        else
        {
            SwitchState(clbClimbingDown);
        }
    }
    else
    {
        Fvector temp;
        float d_to_lower = m_ladder->DDLowerP(m_character, temp), d_to_upper = m_ladder->DDUpperP(m_character, temp);
        if (d_to_lower < d_to_upper)
        {
            if (getting_on_dist + m_character->FootRadius() > d_to_lower)
                SwitchState(clbNearDown);
        }
        else
        {
            if (getting_on_dist + m_character->FootRadius() > d_to_upper)
                SwitchState(clbNearUp);
        }
    }
}

void CElevatorState::UpdateStNearUp()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector d;
    if (m_ladder->InTouch(m_character) && m_character->CamDir().y < -M_PI / 20.f && m_ladder->DDToPlain(m_character, d) > m_character->FootRadius() / 3.f &&
        m_ladder->BeforeLadder(m_character, 0.1f))
        SwitchState(clbClimbingDown);

    float dist = m_ladder->DDUpperP(m_character, d);
    if (dist - m_character->FootRadius() > out_dist)
        SwitchState((clbNoLadder));
}

void CElevatorState::UpdateStNearDown()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector d;
    float dist = m_ladder->DDLowerP(m_character, d);
    if (m_ladder->InTouch(m_character) && dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine && d.dotproduct(m_character->ControlAccel()) > 0.f &&
        ClimbDirection() > 0.f && m_ladder->BeforeLadder(m_character))
        SwitchState(clbClimbingUp);
    if (dist - m_character->FootRadius() > out_dist)
        SwitchState((clbNoLadder));
}

void CElevatorState::UpdateStClimbingDown()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector d;
    if (ClimbDirection() > 0.f && m_ladder->BeforeLadder(m_character))
        SwitchState(clbClimbingUp);
    float to_ax = m_ladder->DDToAxis(m_character, d);
    Fvector ca;
    ca.set(m_character->ControlAccel());
    float control_a = to_mag_and_dir(ca);
    if (!fis_zero(to_ax) && !fis_zero(control_a) && abs(-ca.dotproduct(Fvector(m_ladder->Norm()).normalize())) < M_SQRT1_2)
        SwitchState(clbDepart);
    if (m_ladder->AxDistToLowerP(m_character) - m_character->FootRadius() < stop_climbing_dist)
        SwitchState(clbNearDown);
    UpdateClimbingCommon(d, to_ax, control_a);

    if (m_ladder->AxDistToUpperP(m_character) < -m_character->FootRadius())
        SwitchState(clbNoLadder);

    Fvector vel;
    m_character->GetVelocity(vel);
    if (vel.y > EPS_S)
    {
        m_character->ApplyForce(0.f, -m_character->Mass() * ph_world->Gravity(), 0.f);
    }
}

void CElevatorState::UpdateStClimbingUp()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector d;
    if (ClimbDirection() < 0.f && m_ladder->BeforeLadder(m_character))
        SwitchState(clbClimbingDown);
    float to_ax = m_ladder->DDToAxis(m_character, d);
    Fvector ca;
    ca.set(m_character->ControlAccel());
    float control_a = to_mag_and_dir(ca);
    if (!fis_zero(to_ax) && !fis_zero(control_a) && abs(-ca.dotproduct(Fvector(m_ladder->Norm()).normalize())) < M_SQRT1_2)
        SwitchState(clbDepart);
    if (m_ladder->AxDistToUpperP(m_character) + m_character->FootRadius() < stop_climbing_dist)
        SwitchState(clbNearUp);

    UpdateClimbingCommon(d, to_ax, control_a);
}

void CElevatorState::UpdateClimbingCommon(const Fvector& d_to_ax, float to_ax, float ca)
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    if (to_ax - m_character->FootRadius() > out_dist)
        SwitchState((clbNoLadder));

    if (fis_zero(ca) && d_to_ax.dotproduct(m_ladder->Norm()) < 0.f)
        m_character->ApplyForce(d_to_ax, m_character->Mass() * ph_world->Gravity()); //
}

bool CElevatorState::GetControlDir(Fvector& dir)
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    bool ret = true;
    Fvector d;
    float dist;

    switch (m_state)
    {
    case clbDepart:
    case clbNoLadder:
    case clbNone: break;
    case clbNearUp:
        dist = m_ladder->DDUpperP(m_character, d);
        if (dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine && !fis_zero(dist, EPS_L) && m_character->ControlAccel().dotproduct(d) > 0.f)
            dir.set(d);
        break;
    case clbNearDown:
        dist = m_ladder->DDLowerP(m_character, d);
        if (dXZDotNormalized(d, m_character->CamDir()) > look_angle_cosine && !fis_zero(dist, EPS_L) && m_character->ControlAccel().dotproduct(d) > 0.f)
            dir.set(d);
        break;
    case clbClimbingUp:
        m_ladder->DDAxis(dir);
        m_ladder->DDToAxis(m_character, d);
        dir.add(d);
        dir.normalize();
        break;
    case clbClimbingDown:
        m_ladder->DDToAxis(m_character, d);
        if (m_ladder->BeforeLadder(m_character) || d.dotproduct(dir) > 0.f)
        {
            m_ladder->DDAxis(dir);
            dir.invert();
            dir.add(d);
            dir.normalize();
        }
        else
        {
#ifdef DEBUG
            if (ph_dbg_draw_mask.test(phDbgLadder))
                XR_LOG_ERROR("no c dir");
#endif

            ret = false;
        }
        break;
    default: xr::unreachable();
    }

    return ret;
}

namespace
{
constexpr f32 depart_dist{2.0f};
constexpr u32 depart_time{3000};
} // namespace

void CElevatorState::UpdateDepart()
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector temp;
    float d_to_lower = m_ladder->DDLowerP(m_character, temp), d_to_upper = m_ladder->DDUpperP(m_character, temp);
    if (d_to_lower < d_to_upper)
    {
        if (getting_on_dist + m_character->FootRadius() > d_to_lower)
            SwitchState(clbNearDown);
    }
    else
    {
        if (getting_on_dist + m_character->FootRadius() > d_to_upper)
            SwitchState(clbNearUp);
    }

    SwitchState(clbNoLadder);
}

void CElevatorState::NewState()
{
    m_start_time = Device.dwTimeGlobal;
    XR_ASSERT_VAL(m_character != nullptr)->GetFootCenter(m_start_position);
}

void CElevatorState::Depart()
{
    XR_ASSERT(m_character != nullptr);

    if (m_ladder && ClimbingState())
        SwitchState(clbDepart);
}

void CElevatorState::GetLeaderNormal(Fvector& dir)
{
    if (!m_ladder)
        return;

    XR_ASSERT(m_character != nullptr);
    m_ladder->DDNorm(dir);
}

void CElevatorState::GetJumpDir(const Fvector& accel, Fvector& dir)
{
    XR_ASSERT(m_ladder != nullptr && m_character != nullptr);

    Fvector norm, side;
    m_ladder->DDNorm(norm);
    m_ladder->DDSide(side);
    Fvector ac;
    ac.set(accel).normalize_safe();
    float side_component = ac.dotproduct(side);
    dir.set(norm);
    if (_abs(side_component) > M_SQRT1_2)
    {
        if (side_component < 0.f)
            side.invert();
        dir.add(side);
        dir.normalize_safe();
    }
}

void CElevatorState::Deactivate()
{
    SwitchState(clbNoLadder);
    m_state = clbNoLadder;
    m_ladder = nullptr;
    m_character = nullptr;
}

CElevatorState::SEnertionState CElevatorState::m_etable[CElevatorState::clbNoState][CElevatorState::clbNoState] = {
    //						clbNone			clbNearUp		clbNearDown		clbClimbingUp	clbClimbingDown	clbDepart	clbNoLadder
    /*clbNone			*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, // clbNone
    /*clbNearUp			*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, // clbNearUp
    /*clbNearDown		*/ {{0, 0}, {0.0f, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, // clbNearDown
    /*clbClimbingUp		*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, // clbClimbingUp
    /*clbClimbingDown	*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, // clbClimbingDown
    /*clbDepart			*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {depart_dist, depart_time}}, // clbDepart
    /*clbNoLadder		*/ {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}} // clbNoLadder
};

bool CElevatorState::StateSwitchInertion(Estate new_state)
{
    Fvector p;
    m_character->GetFootCenter(p);
    p.sub(m_start_position);
    if (m_etable[m_state][new_state].dist < p.magnitude() || m_etable[m_state][new_state].time < Device.dwTimeGlobal - m_start_time)
        return true;
    else
        return false;
}
