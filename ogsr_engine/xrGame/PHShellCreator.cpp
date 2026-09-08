#include "stdafx.h"

#include "PHShellCreator.h"

#include "PhysicsShell.h"
#include "GameObject.h"
#include "PhysicsShellHolder.h"
#include "../Include/xrRender/Kinematics.h"

void CPHShellSimpleCreator::CreatePhysicsShell()
{
    CPhysicsShellHolder* owner = smart_cast<CPhysicsShellHolder*>(this);
    VERIFY(owner);
    if (!owner->Visual())
        return;

    IKinematics* pKinematics = smart_cast<IKinematics*>(owner->Visual());
    VERIFY(pKinematics);

    if (owner->PPhysicsShell())
        return;

    owner->PPhysicsShell() = P_create_Shell();
#ifdef DEBUG
    owner->PPhysicsShell()->dbg_obj = owner;
#endif
    owner->m_pPhysicsShell->build_FromKinematics(pKinematics, nullptr);

    if (owner->m_pPhysicsShell->get_ElementsNumber() == 0)
    {
#ifdef DEBUG
        Msg(" ! Error: world item visual [{}] has no elements!", pKinematics->getDebugName());
#else
        XR_LOG_ERROR("World item visual has no elements!");
#endif
    }
    else if (!owner->m_pPhysicsShell->get_ElementByStoreOrder(0)->has_geoms())
    {
#ifdef DEBUG
        Msg(" ! Error: world item visual [{}] has no shape!", pKinematics->getDebugName());
#else
        XR_LOG_ERROR("World item visual has no shape!");
#endif
    }

    owner->PPhysicsShell()->set_PhysicsRefObject(owner);
    owner->PPhysicsShell()->mXFORM.set(owner->XFORM());
    owner->PPhysicsShell()->SetAirResistance(0.001f, 0.02f);
}
