////////////////////////////////////////////////////////////////////////////
//	Module 		: attachable_item.cpp
//	Created 	: 11.02.2004
//  Modified 	: 11.02.2004
//	Author		: Dmitriy Iassenev
//	Description : Attachable item
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "attachable_item.h"

#include "PhysicsShellHolder.h"
#include "InventoryOwner.h"
#include "Inventory.h"
#include "../xr_3da/xr_input.h"
#include "HUDManager.h"

CAttachableItem* CAttachableItem::m_dbgItem{};

IC CPhysicsShellHolder& CAttachableItem::object() const { return (item().object()); }

DLL_Pure* CAttachableItem::_construct()
{
    VERIFY(!m_item);
    m_item = smart_cast<CInventoryItem*>(this);
    VERIFY(m_item);
    return (&item().object());
}

CAttachableItem::~CAttachableItem()
{
    if (CAttachableItem::m_dbgItem == this)
        CAttachableItem::m_dbgItem = nullptr;
}

void CAttachableItem::reload(LPCSTR section)
{
    if (!pSettings->line_exist(section, "attach_angle_offset"))
        return;

    Fvector angle_offset = pSettings->r_fvector3(section, "attach_angle_offset");
    Fvector position_offset = pSettings->r_fvector3(section, "attach_position_offset");
    m_offset.setHPB(angle_offset);
    m_offset.c = position_offset;
    m_bone_name._set(pSettings->r_string(section, "attach_bone_name"));
    enable(false);

#ifdef DEBUG
    m_valid = true;
#endif
}

void CAttachableItem::OnH_A_Chield()
{
    const CInventoryOwner* inventory_owner = smart_cast<const CInventoryOwner*>(object().H_Parent());
    if (inventory_owner && inventory_owner->attached(&item()))
        object().setVisible(true);
}

void CAttachableItem::renderable_Render(u32 context_id, IRenderable* root) { ::Render->add_Visual(context_id, root, object().Visual(), object().XFORM()); }

void CAttachableItem::OnH_A_Independent() { enable(false /*m_auto_attach*/); }

void CAttachableItem::enable(bool value)
{
    if (!object().H_Parent())
    {
        m_enabled = value;
        return;
    }

    if (value && !enabled() && object().H_Parent())
    {
        CGameObject* game_object = smart_cast<CGameObject*>(object().H_Parent());
        CAttachmentOwner* owner = smart_cast<CAttachmentOwner*>(game_object);
        //		VERIFY				(owner);
        if (owner)
        {
            m_enabled = value;
            owner->attach(&item());
            object().setVisible(true);
        }
    }

    if (!value && enabled() && object().H_Parent())
    {
        CGameObject* game_object = smart_cast<CGameObject*>(object().H_Parent());
        CAttachmentOwner* owner = smart_cast<CAttachmentOwner*>(game_object);
        //		VERIFY				(owner);
        if (owner)
        {
            m_enabled = value;
            owner->detach(&item());
            object().setVisible(false);
        }
    }
}

bool CAttachableItem::can_be_attached() const
{
    if (!item().m_pCurrentInventory)
        return (false);

    if (!item().m_pCurrentInventory->IsBeltUseful())
        return (true);

    if (item().m_eItemPlace != eItemPlaceBelt)
        return (false);

    return (true);
}
void CAttachableItem::afterAttach()
{
#ifdef DEBUG
    VERIFY(m_valid);
#endif

    object().processing_activate();
}

void CAttachableItem::afterDetach()
{
#ifdef DEBUG
    VERIFY(m_valid);
#endif

    object().processing_deactivate();
}

void CAttachableItem::ParseCurrentItem(CGameFont*) {}

void CAttachableItem::SaveAttachableParams()
{
    gsl::czstring sect_name = object().cNameSect().c_str();
    string_path buff;

    std::ignore = FS.update_path(buff, "$logs$", xr::format("_world\\{}.ltx", sect_name).c_str());
    CInifile pCfg{buff, false, false, true};

    pCfg.w_fvector3(sect_name, "attach_position_offset", m_offset.c);

    Fvector ypr;
    m_offset.getHPB(ypr.x, ypr.y, ypr.z);

    sprintf_s(buff, "%f,%f,%f", ypr.y, ypr.x, ypr.z);
    pCfg.w_string(sect_name, "attach_angle_offset", buff);

    Msg("--[{}] data saved to [{}]", std::source_location::current().function_name(), pCfg.fname());
}

bool attach_adjust_mode_keyb(xr::key_id dik)
{
    if (!CAttachableItem::m_dbgItem)
        return false;

    if (pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::LShift}) && pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::Enter}))
    {
        CAttachableItem::m_dbgItem->SaveAttachableParams();
        return true;
    }

    const bool b_move = pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::LShift});
    const bool b_rot = pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::LAlt});

    if (!b_move && !b_rot)
        return false;

    const int axis = pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::Z}) ?
        0 :
        (pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::X}) ? 1 : (pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::C}) ? 2 : -1));

    if (axis == -1)
        return false;

    if (dik == xr::key_id{sf::Keyboard::Scancode::PageUp})
    {
        if (b_move)
            CAttachableItem::m_dbgItem->mov(axis, adj_delta_pos);
        else
            CAttachableItem::m_dbgItem->rot(axis, adj_delta_rot);

        return true;
    }
    else if (dik == xr::key_id{sf::Keyboard::Scancode::PageDown})
    {
        if (b_move)
            CAttachableItem::m_dbgItem->mov(axis, -adj_delta_pos);
        else
            CAttachableItem::m_dbgItem->rot(axis, -adj_delta_rot);

        return true;
    }

    return false;
}

void attach_draw_adjust_mode()
{
    if (!CAttachableItem::m_dbgItem)
        return;

    CGameFont* F = UI()->Font()->pFontDI;
    F->SetAligment(CGameFont::alCenter);
    F->OutSetI(0.f, -0.8f);
    F->SetColor(D3DCOLOR_XRGB(125, 0, 0));
    F->OutNext("Adjusting attachable item [{}]", CAttachableItem::m_dbgItem->object().cNameSect());

    CAttachableItem::m_dbgItem->ParseCurrentItem(F);

    F->OutNext("move step  [{:3.3f}] rotate step  [{:3.3f}]", adj_delta_pos, adj_delta_rot);

    F->OutNext("HOLD LShift to move. ALT to rotate");
    F->OutNext("HOLD [Z]-x axis [X]-y axis [C]-z axis");

    F->OutNext("PageUP/PageDown - move.");
    F->OutSkip();

    F->OutNext("Console commands: adjust_delta_pos, adjust_delta_rot");
    F->OutSkip();

    const Fvector _pos = CAttachableItem::m_dbgItem->get_pos_offset();
    F->OutNext("attach_position_offset IS [{:3.3f}][{:3.3f}][{:3.3f}]", _pos.x, _pos.y, _pos.z);

    const Fvector _ang = CAttachableItem::m_dbgItem->get_angle_offset();
    F->OutNext("attach_angle_offset IS [{:3.3f}][{:3.3f}][{:3.3f}]", _ang.y, _ang.x, _ang.z);
}
