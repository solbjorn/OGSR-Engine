#include "stdafx.h"

#include "WeaponRPG7.h"

#include "xrServer_Objects_ALife_Items.h"
#include "ExplosiveRocket.h"
#include "Entity.h"
#include "Level.h"
#include "player_hud.h"
#include "../Include/xrRender/Kinematics.h"
#include "game_object_space.h"

namespace
{
constexpr const char* grenade_def_bone_cop = "grenade";
}

CWeaponRPG7::CWeaponRPG7() : CWeaponCustomPistol{} {}
CWeaponRPG7::~CWeaponRPG7() = default;

void CWeaponRPG7::Load(LPCSTR section)
{
    inherited::Load(section);
    CRocketLauncher::Load(section);

    m_fScopeZoomFactor = pSettings->r_float(section, "max_zoom_factor");

    m_sGrenadeBoneName._set(READ_IF_EXISTS(pSettings, r_string, section, "grenade_bone", grenade_def_bone_cop));
    m_sHudGrenadeBoneName._set(READ_IF_EXISTS(pSettings, r_string, hud_sect, "grenade_bone", grenade_def_bone_cop));

    m_sRocketSection._set(pSettings->r_string(section, "rocket_class"));

    // РПГ никогда клинить не должен
    misfireProbability = 0.0f;
    misfireConditionK = 0.0f;
}

void CWeaponRPG7::UpdateMissileVisibility()
{
    bool vis_hud, vis_weap;
    vis_hud = (!!iAmmoElapsed || GetState() == eReload);
    vis_weap = !!iAmmoElapsed;

    if (GetHUDmode())
    {
        HudItemData()->set_bone_visible(m_sHudGrenadeBoneName, vis_hud, TRUE);
    }

    IKinematics* pWeaponVisual = smart_cast<IKinematics*>(Visual());
    VERIFY(pWeaponVisual);
    pWeaponVisual->LL_SetBoneVisible(pWeaponVisual->LL_BoneID(m_sGrenadeBoneName), vis_weap, TRUE);
}

tmc::task<bool> CWeaponRPG7::net_Spawn(CSE_Abstract* DC)
{
    const bool l_res = co_await inherited::net_Spawn(DC);

    UpdateMissileVisibility();

    if (iAmmoElapsed && !getCurrentRocket())
        CRocketLauncher::SpawnRocket(m_sRocketSection.c_str(), this);

    co_return l_res;
}

void CWeaponRPG7::OnStateSwitch(u32 S, u32 oldState)
{
    inherited::OnStateSwitch(S, oldState);
    UpdateMissileVisibility();
}

void CWeaponRPG7::UnloadMagazine(bool spawn_ammo)
{
    inherited::UnloadMagazine(spawn_ammo);
    UpdateMissileVisibility();
}

void CWeaponRPG7::ReloadMagazine()
{
    inherited::ReloadMagazine();

    if (iAmmoElapsed && !getRocketCount())
        CRocketLauncher::SpawnRocket(m_sRocketSection.c_str(), this);
}

void CWeaponRPG7::SwitchState(u32 S) { inherited::SwitchState(S); }
void CWeaponRPG7::FireStart() { inherited::FireStart(); }

void CWeaponRPG7::on_a_hud_attach()
{
    inherited::on_a_hud_attach();
    UpdateMissileVisibility();
}

#include "Inventory.h"
#include "InventoryOwner.h"

void CWeaponRPG7::switch2_Fire()
{
    m_iShotNum = 0;
    m_bFireSingleShot = true;
    bWorking = false;

    if (GetState() == eFire && getRocketCount())
    {
        Fvector p1, d;
        p1.set(get_LastFP());
        d.set(get_LastFD());

        CEntity* E = smart_cast<CEntity*>(H_Parent());
        if (E)
        {
#ifdef DEBUG
            if (const auto io = smart_cast<CInventoryOwner*>(H_Parent()); io != nullptr && io->inventory().ActiveItem() == nullptr)
            {
                XR_LOG_TRACE_L1("current_state: {}", GetState());
                XR_LOG_TRACE_L1("next_state: {}", GetNextState());
                XR_LOG_TRACE_L1("state_time: {}", m_dwStateTime);
                XR_LOG_TRACE_L1("item_sect: {}", cNameSect());
                XR_LOG_TRACE_L1("H_Parent: {}", H_Parent()->cNameSect());
            }
#endif

            E->g_fireParams(this, p1, d);
        }

        Fmatrix launch_matrix;
        launch_matrix.identity();
        launch_matrix.k.set(d);
        Fvector::generate_orthonormal_basis(launch_matrix.k, launch_matrix.j, launch_matrix.i);
        launch_matrix.c.set(p1);

        d.normalize();
        d.mul(m_fLaunchSpeed);

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
    }
}

tmc::task<void> CWeaponRPG7::OnEvent(NET_Packet& P, u16 type)
{
    co_await inherited::OnEvent(P, type);

    u16 id;
    switch (type)
    {
    case GE_OWNERSHIP_TAKE: {
        P.r_u16(id);
        CRocketLauncher::AttachRocket(id, this);
    }
    break;
    case GE_OWNERSHIP_REJECT:
    case GE_LAUNCH_ROCKET: {
        bool bLaunch = (type == GE_LAUNCH_ROCKET);
        P.r_u16(id);
        CRocketLauncher::DetachRocket(id, bLaunch);
    }
    break;
    }
}

void CWeaponRPG7::PlayAnimReload()
{
    VERIFY(GetState() == eReload);
    // play anim with MixIn=FALSE to avoid issue with blinking rocket during reload
    PlayHUDMotion({"anim_reload", "anm_reload"}, false, GetState());
}
