#include "stdafx.h"

#include "Car.h"

#include "alife_space.h"
#include "Hit.h"
#include "PHDestroyable.h"
#include "Actor.h"
#include "cameralook.h"
#include "CameraFirstEye.h"
#include "script_entity_action.h"
#include "xr_level_controller.h"
#include "../Include/xrRender/Kinematics.h"
#include "Level.h"
#include "CarWeapon.h"
#include "Torch.h"
#include "Inventory.h"

#ifdef DEBUG
#include "ode_include.h"
#include "../xr_3da/StatGraph.h"
#include "PHDebug.h"
#endif

void CCar::OnMouseMove(int dx, int dy)
{
    if (Remote())
        return;

    CCameraBase* C = active_camera;
    float scale = (C->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.f;
    if (dx)
    {
        float d = float(dx) * scale;
        C->Move((d < 0) ? EGameActions::kLEFT : EGameActions::kRIGHT, _abs(d));
    }
    if (dy)
    {
        float d = ((psMouseInvert.test(1)) ? -1 : 1) * float(dy) * scale * 3.f / 4.f;
        C->Move((d > 0) ? EGameActions::kUP : EGameActions::kDOWN, _abs(d));
    }
}

bool CCar::bfAssignMovement(CScriptEntityAction* tpEntityAction)
{
    if (tpEntityAction->m_tMovementAction.m_bCompleted)
        return (false);

    u32 l_tInput = tpEntityAction->m_tMovementAction.m_tInputKeys;

    vfProcessInputKey(EGameActions::kFWD, !!(l_tInput & CScriptMovementAction::eInputKeyForward));
    vfProcessInputKey(EGameActions::kBACK, !!(l_tInput & CScriptMovementAction::eInputKeyBack));
    vfProcessInputKey(EGameActions::kL_STRAFE, !!(l_tInput & CScriptMovementAction::eInputKeyLeft));
    vfProcessInputKey(EGameActions::kR_STRAFE, !!(l_tInput & CScriptMovementAction::eInputKeyRight));
    vfProcessInputKey(EGameActions::kACCEL, !!(l_tInput & CScriptMovementAction::eInputKeyShiftUp));
    vfProcessInputKey(EGameActions::kCROUCH, !!(l_tInput & CScriptMovementAction::eInputKeyShiftDown));
    vfProcessInputKey(EGameActions::kJUMP, !!(l_tInput & CScriptMovementAction::eInputKeyBreaks));

    if (!!(l_tInput & CScriptMovementAction::eInputKeyEngineOn))
        StartEngine();

    if (!!(l_tInput & CScriptMovementAction::eInputKeyEngineOff))
        StopEngine();

    return (true);
}

bool CCar::bfAssignObject(CScriptEntityAction* tpEntityAction)
{
    CScriptObjectAction& l_tObjectAction = tpEntityAction->m_tObjectAction;
    if (l_tObjectAction.m_bCompleted || !xr_strlen(l_tObjectAction.m_caBoneName))
    {
        l_tObjectAction.m_bCompleted = true;
        return false;
    }

    s16 l_sBoneID = smart_cast<IKinematics*>(Visual())->LL_BoneID(l_tObjectAction.m_caBoneName);
    if (is_Door(l_sBoneID))
    {
        switch (l_tObjectAction.m_tGoalType)
        {
        case MonsterSpace::eObjectActionActivate: {
            if (!DoorOpen(l_sBoneID))
            {
                l_tObjectAction.m_bCompleted = true;
                return false;
            }
            break;
        }
        case MonsterSpace::eObjectActionDeactivate: {
            if (!DoorClose(l_sBoneID))
            {
                l_tObjectAction.m_bCompleted = true;
                return false;
            }
            break;
        }
        case MonsterSpace::eObjectActionUse: {
            if (!DoorSwitch(l_sBoneID))
            {
                l_tObjectAction.m_bCompleted = true;
                return false;
            }
            break;
        }
        default: l_tObjectAction.m_bCompleted = true; return false;
        }
        return false;
    }

    SCarLight* light{};
    if (m_lights.findLight(l_sBoneID, light))
    {
        switch (l_tObjectAction.m_tGoalType)
        {
        case MonsterSpace::eObjectActionActivate: {
            light->TurnOn();
            l_tObjectAction.m_bCompleted = true;
            return false;
        }
        case MonsterSpace::eObjectActionDeactivate: {
            light->TurnOff();
            l_tObjectAction.m_bCompleted = true;
            return false;
        }
        case MonsterSpace::eObjectActionUse: {
            light->Switch();
            l_tObjectAction.m_bCompleted = true;
            return false;
        }
        default: l_tObjectAction.m_bCompleted = true; return false;
        }
    }

    return false;
}

void CCar::vfProcessInputKey(EGameActions iCommand, bool bPressed)
{
    if (bPressed)
        OnKeyboardPress(iCommand);
    else
        OnKeyboardRelease(iCommand);
}

void CCar::OnKeyboardPress(EGameActions cmd)
{
    if (Remote())
        return;

    switch (cmd)
    {
    case EGameActions::kCAM_1: OnCameraChange(ectFirst); break;
    case EGameActions::kCAM_2: OnCameraChange(ectChase); break;
    case EGameActions::kCAM_3: OnCameraChange(ectFree); break;
    case EGameActions::kACCEL: TransmissionUp(); break;
    case EGameActions::kCROUCH: TransmissionDown(); break;
    case EGameActions::kFWD: PressForward(); break;
    case EGameActions::kBACK: PressBack(); break;
    case EGameActions::kR_STRAFE:
        PressRight();

        if (OwnerActor())
            OwnerActor()->steer_Vehicle(1);

        break;
    case EGameActions::kL_STRAFE:
        PressLeft();

        if (OwnerActor())
            OwnerActor()->steer_Vehicle(-1);

        break;
    case EGameActions::kJUMP: PressBreaks(); break;
    case EGameActions::kENGINE: SwitchEngine(); break;
    case EGameActions::kTORCH: m_lights.SwitchHeadLights(); break;
    case EGameActions::kNIGHT_VISION: {
        auto* Act = OwnerActor();
        if (Act)
        {
            auto* pTorch = smart_cast<CTorch*>(Act->inventory().ItemFromSlot(TORCH_SLOT));
            if (pTorch)
            {
                pTorch->SwitchNightVision();
            }
        }

        break;
    }
    default: break;
    }
}

void CCar::OnKeyboardRelease(EGameActions cmd)
{
    if (Remote())
        return;

    switch (cmd)
    {
    case EGameActions::kACCEL: break;
    case EGameActions::kFWD: ReleaseForward(); break;
    case EGameActions::kBACK: ReleaseBack(); break;
    case EGameActions::kL_STRAFE:
        ReleaseLeft();

        if (OwnerActor())
            OwnerActor()->steer_Vehicle(0);

        break;
    case EGameActions::kR_STRAFE:
        ReleaseRight();

        if (OwnerActor())
            OwnerActor()->steer_Vehicle(0);

        break;
    case EGameActions::kJUMP: ReleaseBreaks(); break;
    default: break;
    }
}

void CCar::OnKeyboardHold(EGameActions cmd)
{
    if (Remote())
        return;

    switch (cmd)
    {
    case EGameActions::kSHOWHUD:
    case EGameActions::kHIDEHUD:
    case EGameActions::kUP:
    case EGameActions::kDOWN:
    case EGameActions::kLEFT:
    case EGameActions::kRIGHT: active_camera->Move(cmd); break;
    default: break;
    }
}

void CCar::Action(EGameActions id, u32 flags)
{
    if (m_car_weapon)
        m_car_weapon->Action(std::to_underlying(id), flags);
}

void CCar::SetParam(int id, Fvector2 val)
{
    if (m_car_weapon)
        m_car_weapon->SetParam(id, val);
}

void CCar::SetParam(int id, Fvector val)
{
    if (m_car_weapon)
        m_car_weapon->SetParam(id, val);
}

bool CCar::WpnCanHit()
{
    if (m_car_weapon)
        return m_car_weapon->AllowFire();
    return false;
}

float CCar::FireDirDiff()
{
    if (m_car_weapon)
        return m_car_weapon->FireDirDiff();
    return 0.0f;
}

#include "script_game_object.h"
#include "car_memory.h"
#include "visual_memory_manager.h"

bool CCar::isObjectVisible(CScriptGameObject* O_)
{
    if (O_ == nullptr)
    {
        XR_LOG_ERROR("No object passed");
        return false;
    }

    auto O = &O_->object();

    if (m_memory != nullptr)
        return m_memory->visual().visible_now(O);

    Fvector to_point;
    O->Center(to_point);

    Fvector from_point;
    Center(from_point);

    if (HasWeapon())
        from_point.y = XFORM().c.y + m_car_weapon->_height();

    Fvector dir_to_object;
    dir_to_object.sub(to_point, from_point).normalize_safe();

    return !Level().ObjectSpace.RayTest(from_point, dir_to_object, from_point.distance_to(to_point), collide::rqtStatic, nullptr, nullptr);
}

bool CCar::HasWeapon() { return !!m_car_weapon; }

Fvector CCar::CurrentVel()
{
    Fvector lin_vel;
    m_pPhysicsShell->get_LinearVel(lin_vel);

    return lin_vel;
}
