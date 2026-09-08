#include "stdafx.h"

#include "Actor.h"

#include "Inventory.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "xrServer.h"
#include "CameraFirstEye.h"

#include "ActorEffector.h"

#include "PHWorld.h"
#include "Level.h"
#include "alife_registry_wrappers.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "client_spawn_manager.h"
#include "CharacterPhysicsSupport.h"
#include "Grenade.h"
#include "WeaponMagazined.h"
#include "CustomOutfit.h"

#include "actor_anim_defs.h"

#include "map_manager.h"
#include "HUDManager.h"
#include "ui/UIArtefactPanel.h"
#include "GametaskManager.h"
#include "holder_custom.h"
#include "actor_memory.h"
#include "actor_statistic_mgr.h"
#include "alife_simulator_header.h"
#include "ActorCondition.h"
#include "player_hud.h"
#include "UIGameSP.h"
#include "ui/UIPdaWnd.h"
#include "ui/UIEncyclopediaWnd.h"
#include "ui/UIDiaryWnd.h"

#ifdef DEBUG
#include "debug_renderer.h"
#endif

CActor* g_actor{};

CActor* Actor()
{
    // KRodin: Эта функция теперь вызывается из многих новых каллбеков,
    // и вполне может быть вызвана, когда актора ещё нет.
    // Т.ч. вылетать не будем в этом случае.
    // VERIFY		(g_actor);
    return g_actor;
}

tmc::task<bool> CActor::net_Spawn(CSE_Abstract* DC)
{
    m_holder_id = ALife::_OBJECT_ID(-1);
    m_feel_touch_characters = 0;
    m_snd_noise = 0.0f;
    m_sndShockEffector = nullptr;
    /*	m_followers			= NULL;*/

    if (m_pPhysicsShell)
    {
        m_pPhysicsShell->Deactivate();
        xr_delete(m_pPhysicsShell);
    }

    // force actor to be local on server client
    CSE_Abstract* e = (CSE_Abstract*)(DC);
    CSE_ALifeCreatureActor* E = smart_cast<CSE_ALifeCreatureActor*>(e);

    {
        E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE);
    }

    if (TRUE == E->s_flags.test(M_SPAWN_OBJECT_LOCAL) && TRUE == E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER))
        g_actor = this;

    VERIFY(m_pActorEffector == NULL);
    m_pActorEffector = xr_new<CActorCameraManager>();

    // motions
    m_bAnimTorsoPlayed = false;
    m_current_legs_blend = nullptr;
    m_current_jump_blend = nullptr;
    m_current_legs.invalidate();
    m_current_torso.invalidate();
    m_current_head.invalidate();
    //-------------------------------------
    // инициализация реестров, используемых актером
    encyclopedia_registry->registry().init(ID());
    game_news_registry->registry().init(ID());

    {
        auto& news = game_news_registry->registry().objects();
        if (news.size() > NewsToShow())
        {
            size_t s = news.size();
            news.erase(news.begin(), news.begin() + (news.size() - NewsToShow()));
            Msg("[{}]: purge {} news items, {} left", std::source_location::current().function_name(), s - news.size(), news.size());
        }
    }

    if (HUD().GetUI())
    {
        CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
        if (pGameSP)
            pGameSP->PdaMenu->UIEncyclopediaWnd->FillEncyclopedia();
        pGameSP->PdaMenu->UIDiaryWnd->FillNews();
    }

    if (!co_await CInventoryOwner::net_Spawn(DC))
        co_return false;

    if (!co_await inherited::net_Spawn(DC))
        co_return false;

    CSE_ALifeTraderAbstract* pTA = smart_cast<CSE_ALifeTraderAbstract*>(e);
    set_money(pTA->m_dwMoney, false);

    ROS()->force_mode(IRender_ObjectSpecific::TRACE_ALL);

    m_pPhysics_support->in_NetSpawn(e);
    character_physics_support()->movement()->ActivateBox(m_loaded_ph_box_id);
    if (E->m_holderID != u16(-1))
    {
        character_physics_support()->movement()->DestroyCharacter();
    }
    if (m_bOutBorder)
        character_physics_support()->movement()->setOutBorder();
    r_torso_tgt_roll = 0;

    r_model_yaw = E->o_torso.yaw;
    r_torso.yaw = E->o_torso.yaw;
    r_torso.pitch = E->o_torso.pitch;
    r_torso.roll = 0.0f; // E->o_Angle.z;

    unaffected_r_torso.yaw = r_torso.yaw;
    unaffected_r_torso.pitch = r_torso.pitch;
    unaffected_r_torso.roll = r_torso.roll;

    // KRodin: это, мне кажется, лишнее.
    // if( psActorFlags.test(AF_PSP) )
    //	cam_Set(eacLookAt);
    // else
    cam_Set(ACTOR_DEFS::eacFirstEye);

    cam_Active()->Set(-E->o_torso.yaw, E->o_torso.pitch, 0); // E->o_Angle.z);

    // *** movement state - respawn
    mstate_wishful = 0;
    if (m_loaded_ph_box_id == 1 || m_loaded_ph_box_id == 3)
        mstate_real = ACTOR_DEFS::mcCrouch;
    else if (m_loaded_ph_box_id == 2 || m_loaded_ph_box_id == 4)
        mstate_real = ACTOR_DEFS::mcCrouch | ACTOR_DEFS::mcAccel;
    else
        mstate_real = 0;
    mstate_old = mstate_real;
    m_bJumpKeyPressed = FALSE;

    NET_SavedAccel.set(0, 0, 0);

    setEnabled(E->s_flags.is(M_SPAWN_OBJECT_LOCAL));

    Engine.Sheduler.Register(this, TRUE);

    hit_slowmo = 0.f;

    OnChangeVisual();

    processing_activate();

#ifdef DEBUG
    LastPosS.clear();
    LastPosH.clear();
    LastPosL.clear();
#endif
    //*

    //	if (OnServer())// && E->s_flags.is(M_SPAWN_OBJECT_LOCAL))
    /*
        if (OnClient())
        {
            if (!pStatGraph)
            {
                static g_Y = 0;
                pStatGraph = xr_new<CStatGraph>();
                pStatGraph->SetRect(0, g_Y, Device.dwWidth, 100, 0xff000000, 0xff000000);
                g_Y += 110;
                if (g_Y > 700) g_Y = 100;
                pStatGraph->SetGrid(0, 0.0f, 10, 1.0f, 0xff808080, 0xffffffff);
                pStatGraph->SetMinMax(0, 10, 300);
                pStatGraph->SetStyle(CStatGraph::stBar);
                pStatGraph->AppendSubGraph(CStatGraph::stCurve);
                pStatGraph->AppendSubGraph(CStatGraph::stCurve);
            }
        }
    */
    SetDefaultVisualOutfit(cNameVisual());

    smart_cast<IKinematics*>(Visual())->CalculateBones();

    //--------------------------------------------------------------
    inventory().SetPrevActiveSlot(NO_ACTIVE_SLOT);

    //-------------------------------------
    if (!g_Alive())
    {
        mstate_wishful &= ~ACTOR_DEFS::mcAnyMove;
        mstate_real &= ~ACTOR_DEFS::mcAnyMove;

        IKinematicsAnimated* K = smart_cast<IKinematicsAnimated*>(Visual());
        std::ignore = K->PlayCycle(shared_str{"death_init"});

        // остановить звук тяжелого дыхания
        co_await m_HeavyBreathSnd.stop();
    }

    m_holder_id = E->m_holderID;
    if (E->m_holderID != ALife::_OBJECT_ID(-1))
        Level().client_spawn_manager().add(E->m_holderID, ID(), CallMe::fromMethod<&CActor::on_requested_spawn>(this));

    // F
    //-------------------------------------------------------------
    m_iLastHitterID = u16(-1);
    m_iLastHittingWeaponID = u16(-1);
    m_s16LastHittedElement = -1;
    m_bWasHitted = false;

    Level().MapManager().AddMapLocation(shared_str{"actor_location"}, ID());
    Level().MapManager().AddMapLocation(shared_str{"actor_location_p"}, ID());

    m_game_task_manager = xr_new<CGameTaskManager>();
    GameTaskManager().initialize(ID());

    m_statistic_manager = xr_new<CActorStatisticMgr>();

    spatial.type |= STYPE_REACTTOSOUND;
    psHUD_Flags.set(HUD_WEAPON_RT, TRUE);

    g_player_hud->load_default();

    co_return true;
}

tmc::task<void> CActor::net_Destroy()
{
    co_await inherited::net_Destroy();

    if (m_holder_id != ALife::_OBJECT_ID(-1))
        Level().client_spawn_manager().remove(m_holder_id, ID());

    delete_data(m_game_task_manager);
    delete_data(m_statistic_manager);

    Level().MapManager().RemoveMapLocationByObjectID(ID());

    // TODO: Dima to MadMax : do not comment inventory owner net_Destroy!!!
    co_await CInventoryOwner::net_Destroy();

    cam_UnsetLadder();
    character_physics_support()->movement()->DestroyCharacter();
    if (m_pPhysicsShell)
    {
        m_pPhysicsShell->Deactivate();
        xr_delete(m_pPhysicsShell);
    }
    m_pPhysics_support->in_NetDestroy();

    xr_delete(m_sndShockEffector);
    xr_delete(pStatGraph);
    xr_delete(m_pActorEffector);
    pCamBobbing = nullptr;

#ifdef DEBUG
    LastPosS.clear();
    LastPosH.clear();
    LastPosL.clear();
#endif

    processing_deactivate();
    m_holder = nullptr;
    m_holderID = u16(-1);

    SetDefaultVisualOutfit({});

    if (g_actor == this)
        g_actor = nullptr;

    Engine.Sheduler.Unregister(this);
}

void CActor::net_Relcase(CObject* O)
{
    VERIFY(O);

    CGameObject* GO = smart_cast<CGameObject*>(O);
    if (GO && m_pObjectWeLookingAt == GO)
        m_pObjectWeLookingAt = nullptr;

    CHolderCustom* HC = smart_cast<CHolderCustom*>(GO);
    if (HC && HC == m_pVehicleWeLookingAt)
        m_pVehicleWeLookingAt = nullptr;

    if (HC && HC == m_holder)
    {
        m_holder->detach_Actor();
        m_holder = nullptr;
    }

    inherited::net_Relcase(O);

    memory().remove_links(O);
    m_pPhysics_support->in_NetRelcase(O);
    conditions().net_Relcase(O);
}

BOOL CActor::net_Relevant() // relevant for export to server
{
    return getSVU() | getLocal();
}

void CActor::SetCallbacks()
{
    IKinematics* V = smart_cast<IKinematics*>(Visual());
    VERIFY(V);
    u16 spine0_bone = V->LL_BoneID("bip01_spine");
    u16 spine1_bone = V->LL_BoneID("bip01_spine1");
    u16 shoulder_bone = V->LL_BoneID("bip01_spine2");
    u16 head_bone = V->LL_BoneID("bip01_head");
    V->LL_GetBoneInstance(u16(spine0_bone)).set_callback(bctCustom, Spin0Callback, this);
    V->LL_GetBoneInstance(u16(spine1_bone)).set_callback(bctCustom, Spin1Callback, this);
    V->LL_GetBoneInstance(u16(shoulder_bone)).set_callback(bctCustom, ShoulderCallback, this);
    V->LL_GetBoneInstance(u16(head_bone)).set_callback(bctCustom, HeadCallback, this);
}

void CActor::ResetCallbacks()
{
    IKinematics* V = smart_cast<IKinematics*>(Visual());
    VERIFY(V);
    u16 spine0_bone = V->LL_BoneID("bip01_spine");
    u16 spine1_bone = V->LL_BoneID("bip01_spine1");
    u16 shoulder_bone = V->LL_BoneID("bip01_spine2");
    u16 head_bone = V->LL_BoneID("bip01_head");
    V->LL_GetBoneInstance(u16(spine0_bone)).reset_callback();
    V->LL_GetBoneInstance(u16(spine1_bone)).reset_callback();
    V->LL_GetBoneInstance(u16(shoulder_bone)).reset_callback();
    V->LL_GetBoneInstance(u16(head_bone)).reset_callback();
}

void CActor::OnChangeVisual()
{
    ///	inherited::OnChangeVisual();
    {
        CPhysicsShell* tmp_shell = PPhysicsShell();
        PPhysicsShell() = nullptr;
        inherited::OnChangeVisual();
        PPhysicsShell() = tmp_shell;
        tmp_shell = nullptr;
    }

    IKinematicsAnimated* V = smart_cast<IKinematicsAnimated*>(Visual());
    if (V)
    {
        SetCallbacks();
        m_anims->Create(V);
        m_vehicle_anims->Create(V);
        CDamageManager::reload(cNameSect().c_str(), "damage", pSettings);
        //-------------------------------------------------------------------------------
        m_head = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_head");
        m_r_hand = smart_cast<IKinematics*>(Visual())->LL_BoneID(pSettings->r_string(cNameSect(), "weapon_bone0"));
        m_l_finger1 = smart_cast<IKinematics*>(Visual())->LL_BoneID(pSettings->r_string(cNameSect(), "weapon_bone1"));
        m_r_finger2 = smart_cast<IKinematics*>(Visual())->LL_BoneID(pSettings->r_string(cNameSect(), "weapon_bone2"));
        //-------------------------------------------------------------------------------
        m_neck = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_neck");
        m_l_clavicle = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_l_clavicle");
        m_r_clavicle = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_r_clavicle");
        m_spine2 = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_spine2");
        m_spine1 = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_spine1");
        m_spine = smart_cast<IKinematics*>(Visual())->LL_BoneID("bip01_spine");
        //-------------------------------------------------------------------------------
        reattach_items();
        //-------------------------------------------------------------------------------
        m_pPhysics_support->in_ChangeVisual();
        //-------------------------------------------------------------------------------
        SetCallbacks();
        //-------------------------------------------------------------------------------
        m_current_head.invalidate();
        m_current_legs.invalidate();
        m_current_torso.invalidate();
        m_current_legs_blend = nullptr;
        m_current_torso_blend = nullptr;
        m_current_jump_blend = nullptr;

        CStepManager::reload(cNameSect().c_str());
    }
}

void CActor::ChangeVisual(shared_str NewVisual)
{
    if (!NewVisual.size())
        return;

    if (cNameVisual().size())
    {
        if (cNameVisual() == NewVisual)
            return;
    }

    cNameVisual_set(NewVisual);

    g_SetAnimation(mstate_real);
    Visual()->dcast_PKinematics()->CalculateBones_Invalidate();
    Visual()->dcast_PKinematics()->CalculateBones();
}

void CActor::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);
    CInventoryOwner::save(output_packet);
    output_packet.w_u8(u8(m_bOutBorder));
    output_packet.w_u8(u8(character_physics_support()->movement()->BoxID()));
}

void CActor::load(IReader& input_packet)
{
    inherited::load(input_packet);
    CInventoryOwner::load(input_packet);
    m_bOutBorder = !!(input_packet.r_u8());
    if (ai().get_alife()->header().version() > 5)
        m_loaded_ph_box_id = input_packet.r_u8();
}

#ifdef DEBUG
void dbg_draw_piramid(Fvector pos, Fvector dir, float size, float xdir, u32 color)
{
    Fvector p0, p1, p2, p3, p4;
    p0.set(size, size, 0.0f);
    p1.set(-size, size, 0.0f);
    p2.set(-size, -size, 0.0f);
    p3.set(size, -size, 0.0f);
    p4.set(0, 0, size * 4);

    bool Double = false;
    Fmatrix t;
    t.identity();
    if (_valid(dir) && dir.square_magnitude() > 0.01f)
    {
        t.k.normalize(dir);
        Fvector::generate_orthonormal_basis(t.k, t.j, t.i);
    }
    else
    {
        t.rotateY(xdir);
        Double = true;
    }
    t.c.set(pos);

    if (!Double)
    {
        DRender->dbg_DrawTRI(t, p0, p1, p4, color);
        DRender->dbg_DrawTRI(t, p1, p2, p4, color);
        DRender->dbg_DrawTRI(t, p2, p3, p4, color);
        DRender->dbg_DrawTRI(t, p3, p0, p4, color);
    }
    else
    {
        Level().debug_renderer().draw_line(t, p0, p1, color);
        Level().debug_renderer().draw_line(t, p1, p2, color);
        Level().debug_renderer().draw_line(t, p2, p3, color);
        Level().debug_renderer().draw_line(t, p3, p0, color);

        Level().debug_renderer().draw_line(t, p0, p4, color);
        Level().debug_renderer().draw_line(t, p1, p4, color);
        Level().debug_renderer().draw_line(t, p2, p4, color);
        Level().debug_renderer().draw_line(t, p3, p4, color);
    };
};
#endif

void CActor::net_Save(NET_Packet& P)
{
#ifdef DEBUG
    XR_LOG_DEBUG("Actor net_Save");

    u32 pos = P.w_tell();
    inherited::net_Save(P);
    Msg("inherited::net_Save() : {}", P.w_tell() - pos);

    pos = P.w_tell();
    m_pPhysics_support->in_NetSave(P);
    P.w_u16(m_holderID);
    Msg("m_pPhysics_support->in_NetSave() : {}", P.w_tell() - pos);
#else
    inherited::net_Save(P);
    m_pPhysics_support->in_NetSave(P);
    P.w_u16(m_holderID);
#endif
}

BOOL CActor::net_SaveRelevant() { return TRUE; }

void CActor::SetHitInfo(CObject* who, CObject* weapon, s16 element, Fvector Pos, Fvector Dir)
{
    m_iLastHitterID = who ? who->ID() : std::numeric_limits<u16>::max();
    m_iLastHittingWeaponID = weapon ? weapon->ID() : std::numeric_limits<u16>::max();
    m_s16LastHittedElement = element;
    m_fLastHealth = GetfHealth();
    m_bWasHitted = true;
    m_vLastHitDir = Dir;
    m_vLastHitPos = Pos;
}

bool CActor::InventoryAllowSprint()
{
    PIItem pActiveItem = inventory().ActiveItem();
    if (pActiveItem && !pActiveItem->IsSprintAllowed())
    {
        return false;
    }

    if (auto wpn = smart_cast<const CWeapon*>(pActiveItem); wpn != nullptr)
    {
        if (psActorFlags.test(AF_LOCK_RELOAD) && wpn->GetState() == CWeapon::eReload)
            return false;
    }

    const PIItem pOutfitItem = inventory().ItemFromSlot(OUTFIT_SLOT);
    if (pOutfitItem && !pOutfitItem->IsSprintAllowed())
        return false;

    return true;
}

void CActor::On_B_NotCurrentEntity() { inventory().Items_SetCurrentEntityHud(); }

void CActor::net_Export(CSE_Abstract* E)
{
    CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(E);
    creature->fHealth = GetfHealth();
    creature->timestamp = Level().timeServer();
    creature->flags = 0;
    creature->o_Position = Position();
    creature->o_model = angle_normalize(r_model_yaw);
    creature->o_torso.yaw = angle_normalize(unaffected_r_torso.yaw);
    creature->o_torso.pitch = angle_normalize(unaffected_r_torso.pitch);
    creature->o_torso.roll = angle_normalize(unaffected_r_torso.roll);
    creature->s_team = u8(g_Team());
    creature->s_squad = u8(g_Squad());
    creature->s_group = u8(g_Group());

    CSE_ALifeCreatureActor* actor = smart_cast<CSE_ALifeCreatureActor*>(E);
    actor->mstate = (u16)(mstate_real & 0x0000ffff);
    actor->accel = NET_SavedAccel;
    actor->velocity = character_physics_support()->movement()->GetVelocity();
    actor->fRadiation = g_Radiation();
    actor->weapon = u8(inventory().GetActiveSlot());
    /////////////////////////////////////////////////
    actor->m_u16NumItems = 0;
}
