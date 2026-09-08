////////////////////////////////////////////////////////////////////////////
//	Module 		: ai_trader.cpp
//	Created 	: 13.05.2002
//  Modified 	: 13.05.2002
//	Author		: Jim
//	Description : AI Behaviour for monster "Trader"
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "ai_trader.h"

#include "../../trade.h"
#include "../../script_entity_action.h"
#include "../../script_game_object.h"
#include "../../Inventory.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "../../Artifact.h"
#include "../../xrServer.h"
#include "../../relation_registry.h"
#include "object_broker.h"
#include "../../sound_player.h"
#include "../../Level.h"
#include "../../game_object_space.h"
#include "clsid_game.h"
#include "trader_animation.h"

CAI_Trader::CAI_Trader() { AnimMan = xr_new<CTraderAnimation>(this); }

CAI_Trader::~CAI_Trader()
{
    xr_delete(m_sound_player);
    xr_delete(AnimMan);
}

void CAI_Trader::Load(LPCSTR section)
{
    inherited::Load(section);

    SetfHealth(pSettings->r_float(section, "Health"));

    float max_weight = pSettings->r_float(section, "max_item_mass");
    inventory().SetMaxWeight(max_weight * 1000);
    inventory().CalcTotalWeight();
}

void CAI_Trader::reinit()
{
    CScriptEntity::reinit();
    CEntityAlive::reinit();
    CInventoryOwner::reinit();
    sound().reinit();
    animation().reinit();

    m_busy_now = false;
}

void CAI_Trader::reload(LPCSTR section)
{
    CEntityAlive::reload(section);
    CInventoryOwner::reload(section);
    sound().reload(section);
}

bool CAI_Trader::bfAssignSound(CScriptEntityAction* tpEntityAction)
{
    if (!CScriptEntity::bfAssignSound(tpEntityAction))
        return false;

    return true;
}

//////////////////////////////////////////////////////////////////////////
// Look At Actor
//////////////////////////////////////////////////////////////////////////
void CAI_Trader::BoneCallback(CBoneInstance* B)
{
    CAI_Trader* this_class = static_cast<CAI_Trader*>(B->callback_param());

    this_class->LookAtActor(B);
}

void CAI_Trader::LookAtActor(CBoneInstance* B)
{
    Fvector dir;
    dir.sub(Level().CurrentEntity()->Position(), Position());

    float yaw, pitch;
    dir.getHP(yaw, pitch);

    float h, p, b;
    XFORM().getHPB(h, p, b);
    float cur_yaw = h;
    float dy = _abs(angle_normalize_signed(yaw - cur_yaw));

    if (angle_normalize_signed(yaw - cur_yaw) > 0)
        dy *= -1.f;

    Fmatrix M;
    M.setHPB(0.f, -dy, 0.f);
    B->mTransform.mulB_43(M);
}

//////////////////////////////////////////////////////////////////////////

tmc::task<bool> CAI_Trader::net_Spawn(CSE_Abstract* DC)
{
    CSE_Abstract* e = (CSE_Abstract*)(DC);
    CSE_ALifeTrader* l_tpTrader = smart_cast<CSE_ALifeTrader*>(e);
    R_ASSERT(l_tpTrader);

    // проспавнить PDA у InventoryOwner
    if (!co_await CInventoryOwner::net_Spawn(DC))
        co_return false;

    if (!co_await inherited::net_Spawn(DC) || !co_await CScriptEntity::net_Spawn(DC))
        co_return false;

    setVisible(TRUE);
    setEnabled(TRUE);

    set_money(l_tpTrader->m_dwMoney, false);

    shedule.t_min = 100;
    shedule.t_max = 2500; // This equaltiy is broken by Dima :-( // 30 * NET_Latency / 4;

    co_return true;
}

void CAI_Trader::net_Export(CSE_Abstract*) { R_ASSERT(Local()); }

tmc::task<void> CAI_Trader::OnEvent(NET_Packet& P, u16 type)
{
    co_await inherited::OnEvent(P, type);
    co_await CInventoryOwner::OnEvent(P, type);

    u16 id;
    CObject* Obj;

    switch (type)
    {
    case GE_TRADE_BUY:
    case GE_OWNERSHIP_TAKE:
    case GE_TRANSFER_TAKE:
        P.r_u16(id);
        Obj = Level().Objects.net_Find(id);
        if (inventory().CanTakeItem(smart_cast<CInventoryItem*>(Obj)))
        {
            Obj->H_SetParent(this);
            inventory().Take(smart_cast<CGameObject*>(Obj), false, false);
        }
        else
        {
            NET_Packet _P;
            u_EventGen(_P, GE_OWNERSHIP_REJECT, ID());
            _P.w_u16(u16(Obj->ID()));
            u_EventSend(_P);
        }
        break;
    case GE_TRADE_SELL:
    case GE_OWNERSHIP_REJECT:
    case GE_TRANSFER_REJECT: {
        P.r_u16(id);
        Obj = Level().Objects.net_Find(id);

        if (!Obj)
        {
            Msg("! [{}] Error: No object to reject/sell [{}]", std::source_location::current().function_name(), id);
            break;
        }

        bool just_before_destroy = !P.r_eof() && P.r_u8();
        bool dont_create_shell = (type == GE_TRADE_SELL) || (type == GE_TRANSFER_REJECT) || just_before_destroy;
        Obj->SetTmpPreDestroy(just_before_destroy);
        if (inventory().DropItem(smart_cast<CGameObject*>(Obj)))
            Obj->H_SetParent(nullptr, dont_create_shell);
    }
    break;
    case GE_TRANSFER_AMMO: break;
    }
}

void CAI_Trader::feel_touch_new(CObject* O)
{
    if (!g_Alive())
        return;

    if (Remote())
        return;

    // Now, test for game specific logical objects to minimize traffic
    CInventoryItem* I = smart_cast<CInventoryItem*>(O);

    if (I && I->useful_for_NPC())
    {
        Msg("Taking item {}!", I->object().cName());

        NET_Packet P;
        u_EventGen(P, GE_OWNERSHIP_TAKE, ID());
        P.w_u16(u16(I->object().ID()));
        u_EventSend(P);
    }
}

void CAI_Trader::DropItemSendMessage(CObject* O)
{
    if (!O || !O->H_Parent() || (this != O->H_Parent()))
        return;

    XR_LOG_TRACE_L1("Dropping item!");

    // We doesn't have similar weapon - pick up it
    NET_Packet P;
    u_EventGen(P, GE_OWNERSHIP_REJECT, ID());
    P.w_u16(u16(O->ID()));
    u_EventSend(P);
}

tmc::task<void> CAI_Trader::shedule_Update(u32 dt)
{
    co_await inherited::shedule_Update(dt);
    UpdateInventoryOwner();

    if (GetScriptControl())
        ProcessScripts();
    else
        Think();
}

void CAI_Trader::g_WeaponBones(int& L, int& R1, int& R2)
{
    IKinematics* V = smart_cast<IKinematics*>(Visual());
    R1 = V->LL_BoneID("bip01_r_hand");
    R2 = V->LL_BoneID("bip01_r_finger2");
    L = V->LL_BoneID("bip01_l_finger1");
}

void CAI_Trader::g_fireParams(CHudItem*, Fvector& P, Fvector& D, const bool)
{
    VERIFY(inventory().ActiveItem());
    if (g_Alive() && inventory().ActiveItem())
    {
        Center(P);
        D.setHP(0, 0);
        D.normalize_safe();
    }
}

void CAI_Trader::Think() {}

tmc::task<void> CAI_Trader::Die(CObject* who) { co_await inherited::Die(who); }

void CAI_Trader::Hit(SHit* pHDS)
{
    SHit HDS = *pHDS;
    callback(GameObject::entity_alive_before_hit)(&HDS);
    if (!HDS.ignore_flag)
        inherited::Hit(&HDS);
}

tmc::task<void> CAI_Trader::net_Destroy()
{
    co_await inherited::net_Destroy();
    co_await CScriptEntity::net_Destroy();
}

tmc::task<void> CAI_Trader::UpdateCL()
{
    co_await inherited::UpdateCL();

    sound().update();

    if (!GetScriptControl() && !bfScriptAnimation())
        animation().update_frame();
}

BOOL CAI_Trader::UsedAI_Locations() { return (TRUE); }

void CAI_Trader::OnStartTrade()
{
    m_busy_now = true;
    callback(GameObject::eTradeStart)();
}

void CAI_Trader::OnStopTrade()
{
    m_busy_now = false;
    callback(GameObject::eTradeStop)();
}

////////////////////////////////////////////////////////////////////////////////////////////

bool CAI_Trader::can_attach(const CInventoryItem*) const { return false; }
bool CAI_Trader::use_bolts() const { return false; }

void CAI_Trader::spawn_supplies()
{
    inherited::spawn_supplies();
    CInventoryOwner::spawn_supplies();
}

void CAI_Trader::save(NET_Packet& output_packet)
{
    inherited::save(output_packet);
    CInventoryOwner::save(output_packet);
}
void CAI_Trader::load(IReader& input_packet)
{
    inherited::load(input_packet);
    CInventoryOwner::load(input_packet);
}

ALife::ERelationType CAI_Trader::tfGetRelationType(const CEntityAlive* tpEntityAlive) const
{
    const CInventoryOwner* pOtherIO = smart_cast<const CInventoryOwner*>(tpEntityAlive);

    ALife::ERelationType relation = ALife::eRelationTypeDummy;

    if (pOtherIO && !(const_cast<CEntityAlive*>(tpEntityAlive)->cast_base_monster()))
        relation = RELATION_REGISTRY().GetRelationType(static_cast<const CInventoryOwner*>(this), pOtherIO);

    if (ALife::eRelationTypeDummy != relation)
        return relation;
    else
        return inherited::tfGetRelationType(tpEntityAlive);
}

DLL_Pure* CAI_Trader::_construct()
{
    m_sound_player = xr_new<CSoundPlayer>(this);

    std::ignore = CEntityAlive::_construct();
    std::ignore = CInventoryOwner::_construct();
    std::ignore = CScriptEntity::_construct();

    return this;
}

bool CAI_Trader::AllowItemToTrade(CInventoryItem const* item, EItemPlace place) const
{
    if (!g_Alive())
        return (true);

    if (item->object().CLS_ID == CLSID_DEVICE_PDA)
        return (false);

    return (CInventoryOwner::AllowItemToTrade(item, place));
}

void CAI_Trader::dialog_sound_start(LPCSTR phrase) { animation().external_sound_start(phrase); }
void CAI_Trader::dialog_sound_stop() { animation().external_sound_stop(); }
