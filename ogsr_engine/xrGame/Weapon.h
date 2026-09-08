// Weapon.h: interface for the CWeapon class.
#pragma once

#include "WeaponAmmo.h"
#include "PhysicsShell.h"
#include "PHShellCreator.h"
#include "ShootingObject.h"
#include "hud_item_object.h"
#include "Actor_Flags.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "game_cl_single.h"
#include "xrServer_Objects_ALife.h"
#include "xrServer_Objects_ALife_Items.h"
#include "Actor.h"
#include "firedeps.h"

// refs
class CEntity;
class CMotionDef;
class CSE_ALifeItemWeapon;
class CSE_ALifeItemWeaponAmmo;
class CWeaponMagazined;
class CParticlesObject;
class CUIStaticItem;

constexpr inline float def_min_zoom_k{0.3f};
constexpr inline float def_zoom_step_count{4.0f};

class CWeapon : public CHudItemObject, public CShootingObject
{
    RTTI_DECLARE_TYPEINFO(CWeapon, CHudItemObject, CShootingObject);

public:
    friend class CWeaponScript;

private:
    typedef CHudItemObject inherited;

public:
    CWeapon();
    ~CWeapon() override;

    // Generic
    void Load(gsl::czstring section) override;

    tmc::task<bool> net_Spawn(CSE_Abstract* DC) override;
    tmc::task<void> net_Destroy() override;
    void net_Export(CSE_Abstract* E) override;

    [[nodiscard]] CWeapon* cast_weapon() override { return this; }
    [[nodiscard]] virtual CWeaponMagazined* cast_weapon_magazined() { return nullptr; }

    // serialization
    void save(NET_Packet& output_packet) override;
    void load(IReader& input_packet) override;
    [[nodiscard]] BOOL net_SaveRelevant() override { return inherited::net_SaveRelevant(); }

    tmc::task<void> UpdateCL() override;
    tmc::task<void> shedule_Update(u32 dt) override;

    void renderable_Render(u32 context_id, IRenderable* root) override;
    void render_hud_mode() override;
    void OnDrawUI() override;
    [[nodiscard]] bool need_renderable() override;

    void OnH_B_Chield() override;
    void OnH_A_Chield() override;
    void OnH_B_Independent(bool just_before_destroy) override;
    void OnH_A_Independent() override;
    tmc::task<void> OnEvent(NET_Packet& P, u16 type) override;
    void OnBeforeDrop() override;

    void Hit(SHit* pHDS) override;

    void reinit() override;
    void reload(gsl::czstring section) override;
    void create_physic_shell() override;
    void activate_physic_shell() override;
    void setup_physic_shell() override;

    void SwitchState(u32 S) override;
    [[nodiscard]] bool Activate(bool = false) override;

    void Hide(bool = false) override;
    void Show(bool = false) override;

    // инициализация если вещь в активном слоте или спрятана на OnH_B_Chield
    void OnActiveItem() override;
    void OnHiddenItem() override;

    [[nodiscard]] bool NeedBlendAnm() override;

    //////////////////////////////////////////////////////////////////////////
    //  Network
    //////////////////////////////////////////////////////////////////////////

public:
    [[nodiscard]] bool can_kill() const override;
    [[nodiscard]] CInventoryItem* can_kill(CInventory* inventory) const override;
    [[nodiscard]] const CInventoryItem* can_kill(const xr_vector<const CGameObject*>& items) const override;
    [[nodiscard]] bool ready_to_kill() const override;

    //////////////////////////////////////////////////////////////////////////
    //  Animation
    //////////////////////////////////////////////////////////////////////////
public:
    void signal_HideComplete();

    //////////////////////////////////////////////////////////////////////////
    //  InventoryItem methods
    //////////////////////////////////////////////////////////////////////////
public:
    [[nodiscard]] bool Action(EGameActions cmd, u32 flags) override;

    //////////////////////////////////////////////////////////////////////////
    //  Weapon state
    //////////////////////////////////////////////////////////////////////////
public:
    enum EWeaponSubStates
    {
        eSubstateReloadBegin = 0,
        eSubstateReloadInProcess,
        eSubstateReloadEnd,
        eSubstateIdleMoving,
        eSubstateIdleSprint,
    };

    [[nodiscard]] bool IsHidden() const override { return GetState() == eHidden; } // Does weapon is in hidden state
    [[nodiscard]] bool IsHiding() const override { return GetState() == eHiding; }
    [[nodiscard]] bool IsShowing() const override { return GetState() == eShowing; }

    [[nodiscard]] BOOL IsValid() const { return iAmmoElapsed; }
    // Does weapon need's update?
    [[nodiscard]] BOOL IsUpdating();

    [[nodiscard]] BOOL CheckForMisfire();

    [[nodiscard]] bool IsTriStateReload() const { return m_bTriStateReload; }
    [[nodiscard]] EWeaponSubStates GetReloadState() const { return (EWeaponSubStates)m_sub_state; }
    [[nodiscard]] u8 idle_state();

protected:
    bool m_bTriStateReload;
    u8 m_sub_state;
    u8 m_idle_state;
    // Weapon fires now
    bool bWorking2;

    //////////////////////////////////////////////////////////////////////////
    //  Weapon Addons
    //////////////////////////////////////////////////////////////////////////
public:
    ///////////////////////////////////////////
    // работа с аддонами к оружию
    //////////////////////////////////////////

    [[nodiscard]] bool IsGrenadeLauncherAttached() const;
    [[nodiscard]] bool IsScopeAttached() const;
    [[nodiscard]] bool IsSilencerAttached() const;

    [[nodiscard]] virtual bool IsGrenadeMode() const { return false; }

    [[nodiscard]] virtual bool GrenadeLauncherAttachable() const;
    [[nodiscard]] virtual bool ScopeAttachable() const;
    [[nodiscard]] virtual bool SilencerAttachable() const;
    [[nodiscard]] virtual bool UseScopeTexture();

    // обновление видимости для косточек аддонов
    void UpdateAddonsVisibility();
    void UpdateHUDAddonsVisibility();
    // инициализация свойств присоединенных аддонов
    virtual void InitAddons();

    // для отоброажения иконок апгрейдов в интерфейсе
    [[nodiscard]] int GetScopeX() { return m_iScopeX; }
    [[nodiscard]] int GetScopeY() { return m_iScopeY; }
    [[nodiscard]] int GetSilencerX() { return m_iSilencerX; }
    [[nodiscard]] int GetSilencerY() { return m_iSilencerY; }
    [[nodiscard]] int GetGrenadeLauncherX() { return m_iGrenadeLauncherX; }
    [[nodiscard]] int GetGrenadeLauncherY() { return m_iGrenadeLauncherY; }

    [[nodiscard]] const shared_str& GetGrenadeLauncherName() const { return m_sGrenadeLauncherName; }
    [[nodiscard]] const shared_str& GetScopeName() const { return m_sScopeName; }
    [[nodiscard]] const shared_str& GetSilencerName() const { return m_sSilencerName; }

    [[nodiscard]] u8 GetAddonsState() const { return m_flagsAddOnState; }
    void SetAddonsState(u8 st) { m_flagsAddOnState = st; }

    // названия секций подключаемых аддонов
    shared_str m_sScopeName;
    xr_vector<shared_str> m_allScopeNames;
    shared_str m_sSilencerName;
    shared_str m_sGrenadeLauncherName;

    xr_vector<shared_str> m_sWpn_scope_bones;
    shared_str m_sWpn_silencer_bone;
    shared_str m_sWpn_launcher_bone;
    shared_str m_sWpn_laser_bone;
    shared_str m_sWpn_flashlight_bone;
    xr_vector<shared_str> m_sHud_wpn_scope_bones;
    shared_str m_sHud_wpn_silencer_bone;
    shared_str m_sHud_wpn_launcher_bone;
    shared_str m_sHud_wpn_laser_bone;
    shared_str m_sHud_wpn_flashlight_bone;

private:
    xr_vector<shared_str> hidden_bones;
    xr_vector<shared_str> hud_hidden_bones;

protected:
    // состояние подключенных аддонов
    u8 m_flagsAddOnState;

    // возможность подключения различных аддонов
    ALife::EWeaponAddonStatus m_eScopeStatus;
    ALife::EWeaponAddonStatus m_eSilencerStatus;
    ALife::EWeaponAddonStatus m_eGrenadeLauncherStatus;

    // смещение иконов апгрейдов в инвентаре
    int m_iScopeX, m_iScopeY;
    int m_iSilencerX, m_iSilencerY;
    int m_iGrenadeLauncherX, m_iGrenadeLauncherY;

    ///////////////////////////////////////////////////
    //	для режима приближения и снайперского прицела
    ///////////////////////////////////////////////////
protected:
    // разрешение регулирования приближения. Real Wolf.
    bool m_bScopeDynamicZoom;
    // run-time zoom factor
    float m_fRTZoomFactor;
    // разрешение режима приближения
    bool m_bZoomEnabled;
    // текущий фактор приближения
    float m_fZoomFactor;
    // текстура для снайперского прицела, в режиме приближения
    CUIStaticItem* m_UIScope;
    // коэффициент увеличения прицеливания
    float m_fIronSightZoomFactor;
    // коэффициент увеличения прицела
    float m_fScopeZoomFactor;
    // когда режим приближения включен
    bool m_bZoomMode;
    // коэффициент увеличения во втором вьюпорте при зуме
    float m_fSecondVPZoomFactor;
    // прятать перекрестие в режиме прицеливания
    bool m_bHideCrosshairInZoom;
    // разрешить инерцию оружия в режиме прицеливания
    bool m_bZoomInertionAllow;
    // или в режиме прицеливания через оптику
    bool m_bScopeZoomInertionAllow;
    // Целевой HUD FOV при зуме
    float m_fZoomHudFov;
    // Целевой HUD FOV для линзы
    float m_fSecondVPHudFov;

    bool m_bUseScopeZoom = false;
    bool m_bUseScopeGrenadeZoom = false;
    bool m_bZoomShowIndicators = true;
    bool m_bScopeShowIndicators = true;
    bool m_bIgnoreScopeTexture = false;

    float m_fMinZoomK = def_min_zoom_k;
    float m_fZoomStepCount = def_zoom_step_count;

    float m_fScopeInertionFactor;

public:
    [[nodiscard]] bool IsZoomEnabled() const { return m_bZoomEnabled; }
    void GetZoomData(float scope_factor, float& delta, float& min_zoom_factor);
    virtual void ZoomChange(bool inc);
    virtual void OnZoomIn();
    virtual void OnZoomOut();
    [[nodiscard]] bool IsZoomed() const override { return m_bZoomMode; }
    [[nodiscard]] CUIStaticItem* ZoomTexture();

    [[nodiscard]] bool ZoomHideCrosshair()
    {
        auto* pA = smart_cast<CActor*>(H_Parent());
        if (pA && pA->active_cam() == ACTOR_DEFS::eacLookAt)
            return false;

        return m_bHideCrosshairInZoom || ZoomTexture();
    }

    virtual void OnZoomChanged() {}

    [[nodiscard]] float GetZoomFactor() const { return m_fZoomFactor; }
    [[nodiscard]] virtual float CurrentZoomFactor();
    // показывает, что оружие находится в соостоянии поворота для приближенного прицеливания
    [[nodiscard]] bool IsRotatingToZoom() const { return (m_fZoomRotationFactor < 1.f); }
    [[nodiscard]] bool IsRotatingFromZoom() const { return m_fZoomRotationFactor > 0.f; }

    [[nodiscard]] f32 Weight() const override;
    [[nodiscard]] u32 Cost() const override;
    [[nodiscard]] f32 GetControlInertionFactor() const override;

public:
    [[nodiscard]] EHandDependence HandDependence() const override { return eHandDependence; }
    [[nodiscard]] bool IsSingleHanded() const override { return m_bIsSingleHanded; }

public:
    [[nodiscard]] gsl::czstring strap_bone0() const { return m_strap_bone0; }
    [[nodiscard]] gsl::czstring strap_bone1() const { return m_strap_bone1; }
    void strapped_mode(bool value) { m_strapped_mode = value; }
    [[nodiscard]] bool strapped_mode() const { return m_strapped_mode; }

protected:
    LPCSTR m_strap_bone0;
    LPCSTR m_strap_bone1;
    Fmatrix m_StrapOffset;

public:
    Fmatrix m_Offset;

protected:
    // 0-используется без участия рук, 1-одна рука, 2-две руки
    EHandDependence eHandDependence;
    bool m_bIsSingleHanded;

    bool m_strapped_mode;
    bool m_can_be_strapped;

public:
    // загружаемые параметры
    Fvector vLoadedFirePoint;
    Fvector vLoadedFirePoint2;

private:
    firedeps m_current_firedeps{};

protected:
    virtual void UpdateFireDependencies_internal();
    virtual void UpdatePosition(const Fmatrix& transform); //.
    void UpdateXForm() override;

    [[nodiscard]] u8 GetCurrentHudOffsetIdx() const override;
    [[nodiscard]] bool IsHudModeNow() override;

    void LoadFireParams(gsl::czstring section, gsl::czstring prefix) override;

public:
    [[nodiscard]] const Fvector& get_LastFP()
    {
        UpdateFireDependencies_internal();
        return m_current_firedeps.vLastFP;
    }

    [[nodiscard]] const Fvector& get_LastFP2()
    {
        UpdateFireDependencies_internal();
        return m_current_firedeps.vLastFP2;
    }

    [[nodiscard]] const Fvector& get_LastFD()
    {
        UpdateFireDependencies_internal();
        return m_current_firedeps.vLastFD;
    }

    [[nodiscard]] const Fvector& get_LastSP()
    {
        UpdateFireDependencies_internal();
        return m_current_firedeps.vLastSP;
    }

    [[nodiscard]] const Fvector& get_LastShootPoint()
    {
        UpdateFireDependencies_internal();
        return m_current_firedeps.vLastShootPoint;
    }

    [[nodiscard]] const Fvector& get_CurrentFirePoint() override { return get_LastFP(); }
    [[nodiscard]] virtual const Fvector& get_CurrentFirePoint2() { return get_LastFP2(); }

    [[nodiscard]] const Fmatrix& get_ParticlesXFORM() override
    {
        UpdateFireDependencies_internal();
        return m_current_firedeps.m_FireParticlesXForm;
    }

    void ForceUpdateFireParticles() override;

    //////////////////////////////////////////////////////////////////////////
    // Weapon fire
    //////////////////////////////////////////////////////////////////////////
protected:
    virtual void SetDefaults();

    // трассирование полета пули
    virtual void FireTrace(const Fvector& P, const Fvector& D);
    [[nodiscard]] virtual f32 GetWeaponDeterioration();

    void FireStart() override { CShootingObject::FireStart(); }
    void FireEnd() override;

    virtual void Fire2Start();
    virtual void Fire2End();
    virtual void Reload();
    void StopShooting();

    virtual void Misfire() {}
    virtual void DeviceSwitch();

    // обработка визуализации выстрела
    virtual void OnShot() {}
    virtual void AddShotEffector();
    virtual void RemoveShotEffector();
    virtual void ClearShotEffector();

public:
    // текущая дисперсия (в радианах) оружия с учетом используемого патрона
    [[nodiscard]] f32 GetFireDispersion(bool with_cartridge);
    [[nodiscard]] f32 GetFireDispersion(float cartridge_k);
    [[nodiscard]] virtual s32 ShotsFired() { return 0; }

    // параметы оружия в зависимоти от его состояния исправности
    [[nodiscard]] f32 GetConditionDispersionFactor() const;
    [[nodiscard]] f32 GetConditionMisfireProbability() const;
    [[nodiscard]] f32 GetConditionToShow() const override;

public:
    // отдача при стрельбе
    float camMaxAngle;
    float camRelaxSpeed;
    float camRelaxSpeed_AI;
    float camDispersion;
    float camDispersionInc;
    float camDispertionFrac;
    float camMaxAngleHorz;
    float camStepAngleHorz;

protected:
    // фактор увеличения дисперсии при максимальной изношености
    //(на сколько процентов увеличится дисперсия)
    float fireDispersionConditionFactor;
    // вероятность осечки при максимальной изношености
    float misfireProbability;
    float misfireConditionK;
    // увеличение изношености при выстреле
    float conditionDecreasePerShot;
    float conditionDecreasePerShotOnHit;
    float conditionDecreasePerShotSilencer;

    //  [8/2/2005]
    float m_fPDM_disp_base;
    float m_fPDM_disp_vel_factor;
    float m_fPDM_disp_accel_factor;
    float m_fPDM_disp_crouch;
    float m_fPDM_disp_crouch_no_acc;
    //  [8/2/2005]

protected:
    // для отдачи оружия
    Fvector m_vRecoilDeltaAngle;

    // для сталкеров, чтоб они знали эффективные границы использования
    // оружия
    float m_fMinRadius;
    float m_fMaxRadius;

    // Давать ли доиграть анимацию выстрела после выстрела (надо для анимаций с вылетающими гильзами)
    bool dont_interrupt_shot_anm{};
    // Является ли это оружие оружием из Gunslinger Mod
    bool is_gunslinger_weapon{};

    //////////////////////////////////////////////////////////////////////////
    // партиклы
    //////////////////////////////////////////////////////////////////////////

protected:
    // для второго ствола
    void StartFlameParticles2();
    void StopFlameParticles2();
    void UpdateFlameParticles2();

protected:
    shared_str m_sFlameParticles2;
    // объект партиклов для стрельбы из 2-го ствола
    CParticlesObject* m_pFlameParticles2;

    //////////////////////////////////////////////////////////////////////////
    // Weapon and ammo
    //////////////////////////////////////////////////////////////////////////
protected:
    int GetAmmoCount_forType(shared_str const& ammo_type, u32 = 0) const;
    int GetAmmoCount(u8 ammo_type, u32 = 0) const;

public:
    [[nodiscard]] s32 GetAmmoElapsed() const { return /*int(m_magazine.size())*/ iAmmoElapsed; }
    [[nodiscard]] virtual s32 GetAmmoElapsed2() const { return 0; }
    [[nodiscard]] s32 GetAmmoMagSize() const { return iMagazineSize; }
    [[nodiscard]] s32 GetAmmoCurrent(bool use_item_to_spawn = false) const;
    void SetAmmoMagSize(s32 _size) { iMagazineSize = _size; }

    void SetAmmoElapsed(int ammo_count);

    virtual void OnMagazineEmpty();
    void SpawnAmmo(u32 boxCurr = std::numeric_limits<u32>::max(), LPCSTR ammoSect = nullptr, u32 ParentID = std::numeric_limits<u32>::max());

    //  [8/3/2005]
    [[nodiscard]] virtual f32 Get_PDM_Base() const { return m_fPDM_disp_base; }
    [[nodiscard]] virtual f32 Get_PDM_Vel_F() const { return m_fPDM_disp_vel_factor; }
    [[nodiscard]] virtual f32 Get_PDM_Accel_F() const { return m_fPDM_disp_accel_factor; }
    [[nodiscard]] virtual f32 Get_PDM_Crouch() const { return m_fPDM_disp_crouch; }
    [[nodiscard]] virtual f32 Get_PDM_Crouch_NA() const { return m_fPDM_disp_crouch_no_acc; }
    //  [8/3/2005]

protected:
    int iAmmoElapsed; // ammo in magazine, currently
    int iMagazineSize; // size (in bullets) of magazine

    // для подсчета в GetAmmoCurrent
    mutable int iAmmoCurrent;
    mutable u32 m_dwAmmoCurrentCalcFrame; // кадр на котором просчитали кол-во патронов

    [[nodiscard]] bool IsNecessaryItem(const shared_str& item_sect) override;

public:
    xr_vector<shared_str> m_ammoTypes;
    xr_vector<shared_str> m_highlightAddons;

    CWeaponAmmo* m_pAmmo;
    u32 m_ammoType;
    BOOL m_bHasTracers;
    u8 m_u8TracerColorID;
    u32 m_set_next_ammoType_on_reload;
    // Multitype ammo support
    xr_vector<CCartridge> m_magazine;
    CCartridge m_DefaultCartridge;
    float m_fCurrentCartirdgeDisp;

    [[nodiscard]] bool unlimited_ammo();
    [[nodiscard]] bool can_be_strapped() const { return m_can_be_strapped; }

    [[nodiscard]] gsl::czstring GetCurrentAmmo_ShortName();
    [[nodiscard]] f32 GetMagazineWeight(const decltype(m_magazine)& mag) const;

protected:
    u32 m_ef_main_weapon_type;
    u32 m_ef_weapon_type;

public:
    [[nodiscard]] u32 ef_main_weapon_type() const override;
    [[nodiscard]] u32 ef_weapon_type() const override;

protected:
    // This is because when scope is attached we can't ask scope for these params
    // therefore we should hold them by ourself :-((
    float m_addon_holder_range_modifier;
    float m_addon_holder_fov_modifier;

protected:
    [[nodiscard]] size_t GetWeaponTypeForCollision() const override { return CWeaponBase; }
    [[nodiscard]] Fvector GetPositionForCollision() override { return get_LastShootPoint(); }
    [[nodiscard]] Fvector GetDirectionForCollision() override { return get_LastFD(); }

public:
    void modify_holder_params(float& range, float& fov) const override;
    [[nodiscard]] virtual bool use_crosshair() const { return true; }
    [[nodiscard]] bool show_crosshair();
    [[nodiscard]] bool show_indicators();
    [[nodiscard]] bool ParentIsActor() const override;

private:
    float m_hit_probability[egdCount];

public:
    [[nodiscard]] const f32& hit_probability() const;
    void UpdateSecondVP();
    [[nodiscard]] f32 GetZRotatingFactor() const { return m_fZoomRotationFactor; } //--#SM+#--
    [[nodiscard]] f32 GetSecondVPFov() const; //--#SM+#--
    [[nodiscard]] bool SecondVPEnabled() const;
    [[nodiscard]] f32 GetHudFov() override;

    virtual void OnBulletHit();
    [[nodiscard]] virtual bool IsPartlyReloading() const;

    void processing_deactivate() override
    {
        UpdateLaser();
        UpdateFlashlight();
        inherited::processing_deactivate();
    }

    void on_a_hud_attach() override;

    Fvector laserdot_attach_offset{}, laser_pos{};

protected:
    bool has_laser{};

public:
    shared_str laserdot_attach_bone;

private:
    Fvector laserdot_world_attach_offset{};
    ref_light laser_light_render;
    CLAItem* laser_lanim{};
    float laser_fBrightness{1.f};

    void UpdateLaser();

public:
    [[nodiscard]] bool SwitchLaser(bool on)
    {
        if (!has_laser)
            return false;

        if (on)
            m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonLaserOn;
        else
            m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonLaserOn;

        return true;
    }

    [[nodiscard]] bool IsLaserOn() const { return m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonLaserOn; }

    Fvector flashlight_attach_offset{}, flashlight_pos{};

protected:
    bool has_flashlight{};

public:
    Fvector flashlight_omni_attach_offset{};
    shared_str flashlight_attach_bone;

private:
    Fvector flashlight_world_attach_offset{}, flashlight_omni_world_attach_offset{};
    ref_light flashlight_render;
    ref_light flashlight_omni;
    ref_glow flashlight_glow;
    CLAItem* flashlight_lanim{};
    float flashlight_fBrightness{1.f};

    void UpdateFlashlight();

public:
    [[nodiscard]] bool SwitchFlashlight(bool on)
    {
        if (!has_flashlight)
            return false;

        if (on)
            m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonFlashlightOn;
        else
            m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponAddonFlashlightOn;

        return true;
    }

    [[nodiscard]] bool IsFlashlightOn() const { return m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonFlashlightOn; }

    void SwitchMisfire(bool on)
    {
        if (on)
            m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponMisfire;
        else
            m_flagsAddOnState &= ~CSE_ALifeItemWeapon::eWeaponMisfire;
    }

    [[nodiscard]] bool IsMisfire() const { return m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponMisfire; }

    [[nodiscard]] Fvector get_angle_offset() const override
    {
        Fvector v;
        m_strapped_mode ? m_StrapOffset.getHPB(v) : m_Offset.getHPB(v);
        return v;
    }

    [[nodiscard]] Fvector get_pos_offset() const override { return m_strapped_mode ? m_StrapOffset.c : m_Offset.c; }

    void set_angle_offset(Fvector val) override
    {
        Fvector c = get_pos_offset();
        Fmatrix& mat = m_strapped_mode ? m_StrapOffset : m_Offset;
        mat.setHPB(val);
        mat.c = c;
    }

    void rot(int axis, float val) override
    {
        Fvector v = get_angle_offset();
        v[axis] += val;
        set_angle_offset(v);
    }

    void mov(int axis, float val) override
    {
        Fvector c = get_pos_offset();
        c[axis] += val;
        if (m_strapped_mode)
            m_StrapOffset.c = c;
        else
            m_Offset.c = c;
    }

    void SaveAttachableParams() override;
    void ParseCurrentItem(CGameFont* F) override;

    // Up
    // Magazine system & etc
    xr_vector<shared_str> bullets_bones;
    int bullet_cnt{};
    int last_hide_bullet{};
    bool bHasBulletsToHide{};
    bool bullet_update = true;

    void UpdateVisualBullets();
    void HUD_VisualBulletUpdate();

private:
    xr_string current_bullet_texture;
    xr_vector<xr_string> bullet_textures_in_model;
    string_unordered_map<xr_string, xr_string> bullet_textures_for_ammos;

public:
    void update_visual_bullet_textures(const bool forced = false);
};
XR_SOL_BASE_CLASSES(CWeapon);
