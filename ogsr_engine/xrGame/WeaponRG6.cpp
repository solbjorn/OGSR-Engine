#include "stdafx.h"

#include "WeaponRG6.h"

#include "Entity.h"
#include "ExplosiveRocket.h"
#include "Level.h"
#include "clsid_game.h"

#include "MathUtils.h"

#include "Inventory.h"
#include "InventoryOwner.h"

#ifdef DEBUG
#include "PHDebug.h"
#endif

CWeaponRG6::~CWeaponRG6() = default;

tmc::task<bool> CWeaponRG6::net_Spawn(CSE_Abstract* DC)
{
    if (!co_await inheritedSG::net_Spawn(DC))
        co_return false;

    if (iAmmoElapsed && !getCurrentRocket())
    {
        gsl::czstring fake_grenade_name = pSettings->r_string(m_ammoTypes[0], "fake_grenade_name");
        if (fake_grenade_name != nullptr && fake_grenade_name[0] != '\0')
        {
            int k = iAmmoElapsed;
            while (k)
            {
                k--;
                inheritedRL::SpawnRocket(fake_grenade_name, this);
            }
        }
    }

    co_return true;
}

void CWeaponRG6::Load(LPCSTR section)
{
    inheritedRL::Load(section);
    inheritedSG::Load(section);
}

void CWeaponRG6::LaunchGrenade(const Fvector& p1, const Fvector& d1)
{
    if (getRocketCount())
    {
        Fvector p, d;
        p = p1;
        d = d1;

#ifdef DEBUG
        if (smart_cast<CEntity*>(H_Parent()) != nullptr)
        {
            if (const auto io = smart_cast<CInventoryOwner*>(H_Parent()); io != nullptr && io->inventory().ActiveItem() == nullptr)
            {
                XR_LOG_TRACE_L1("current_state: {}", GetState());
                XR_LOG_TRACE_L1("next_state: {}", GetNextState());
                XR_LOG_TRACE_L1("state_time: {}", m_dwStateTime);
                XR_LOG_TRACE_L1("item_sect: {}", cNameSect());
                XR_LOG_TRACE_L1("H_Parent: {}", H_Parent()->cNameSect());
            }
        }
#endif

        Fmatrix launch_matrix;
        launch_matrix.identity();
        launch_matrix.k.set(d);
        Fvector::generate_orthonormal_basis(launch_matrix.k, launch_matrix.j, launch_matrix.i);
        launch_matrix.c.set(p);

        if (IsZoomed() && H_Parent()->CLS_ID == CLSID_OBJECT_ACTOR)
        {
            H_Parent()->setEnabled(FALSE);
            setEnabled(FALSE);

            collide::rq_result RQ;
            BOOL HasPick = Level().ObjectSpace.RayPick(p, d, 300.0f, collide::rqtStatic, RQ, this);

            setEnabled(TRUE);
            H_Parent()->setEnabled(TRUE);

            if (HasPick)
            {
                //			collide::rq_result& RQ = HUD().GetCurrentRayQuery();
                Fvector Transference;
                // Transference.add(p1, Fvector().mul(d, RQ.range));
                Transference.mul(d, RQ.range);
                Fvector res[2];
#ifdef DEBUG
                DBG_OpenCashedDraw();
                DBG_DrawLine(p1, Fvector().add(p1, d), D3DCOLOR_XRGB(255, 0, 0));
#endif
                u8 canfire0 = TransferenceAndThrowVelToThrowDir(Transference, CRocketLauncher::m_fLaunchSpeed, EffectiveGravity(), res);
#ifdef DEBUG
                if (canfire0 > 0)
                    DBG_DrawLine(p1, Fvector().add(p1, res[0]), D3DCOLOR_XRGB(0, 255, 0));
                if (canfire0 > 1)
                    DBG_DrawLine(p1, Fvector().add(p1, res[1]), D3DCOLOR_XRGB(0, 0, 255));
                DBG_ClosedCashedDraw(30000);
#endif

                if (canfire0 != 0)
                    d = res[0];
            }
        }

        d.normalize();
        d.mul(m_fLaunchSpeed);
        VERIFY2(_valid(launch_matrix), "CWeaponRG6::LaunchGrenade. Invalid launch_matrix");
        CRocketLauncher::LaunchRocket(launch_matrix, d, zero_vel);

        CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());
        VERIFY(pGrenade);
        pGrenade->SetInitiator(H_Parent()->ID());
        pGrenade->SetRealGrenadeName(m_ammoTypes[m_ammoType]);

        {
            NET_Packet P;
            u_EventGen(P, GE_LAUNCH_ROCKET, ID());
            P.w_u16(u16(getCurrentRocket()->ID()));
            u_EventSend(P);
        }
        dropCurrentRocket();
    }
}

void CWeaponRG6::FireTrace(const Fvector& P, const Fvector& D)
{
    inheritedSG::FireTrace(P, D);
    if (!IsMisfire())
    {
        LaunchGrenade(P, D);
    }
}

u8 CWeaponRG6::AddCartridge(u8 cnt)
{
    u8 t = inheritedSG::AddCartridge(cnt);
    u8 k = cnt - t;
    gsl::czstring fake_grenade_name = pSettings->r_string(m_ammoTypes[m_ammoType], "fake_grenade_name");

    while (k)
    {
        --k;
        inheritedRL::SpawnRocket(fake_grenade_name, this);
    }

    return k;
}

tmc::task<void> CWeaponRG6::OnEvent(NET_Packet& P, u16 type)
{
    co_await inheritedSG::OnEvent(P, type);

    u16 id;
    switch (type)
    {
    case GE_OWNERSHIP_TAKE: {
        P.r_u16(id);
        inheritedRL::AttachRocket(id, this);
    }
    break;
    case GE_OWNERSHIP_REJECT:
    case GE_LAUNCH_ROCKET: {
        bool bLaunch = (type == GE_LAUNCH_ROCKET);
        P.r_u16(id);
        inheritedRL::DetachRocket(id, bLaunch);
    }
    break;
    }
}
