//////////////////////////////////////////////////////////////////////
// CustomRocket.cpp:	ракета, которой стреляет RocketLauncher
//						(умеет лететь, светиться и отыгрывать партиклы)
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "CustomRocket.h"

#include "ParticlesObject.h"
#include "PhysicsShell.h"
#include "ExtendedGeom.h"
#include "Level.h"
#include "xrMessages.h"
#include "../xr_3da/GameMtlLib.h"
#include "tri-colliderknoopc/dTriList.h"
#include "../Include/xrRender/Kinematics.h"
#include "CalculateTriangle.h"
#include "Actor.h"

#ifdef DEBUG
#include "PHDebug.h"
#include "game_base_space.h"
#endif

#define CHOOSE_MAX(x, inst_x, y, inst_y, z, inst_z) \
    if (x > y) \
        if (x > z) \
        { \
            inst_x; \
        } \
        else \
        { \
            inst_z; \
        } \
    else if (y > z) \
    { \
        inst_y; \
    } \
    else \
    { \
        inst_z; \
    }

CCustomRocket::CCustomRocket() { m_LaunchXForm.identity(); }
CCustomRocket::~CCustomRocket() { m_pTrailLight.destroy(); }

void CCustomRocket::reinit()
{
    inherited::reinit();

    m_pTrailLight.destroy();
    m_pTrailLight._set(::Render->light_create());
    m_pTrailLight->set_shadow(true);
    m_pTrailLight->set_moveable(true);

    m_pEngineParticles = nullptr;
    m_pFlyParticles = nullptr;
    m_pOwner = nullptr;

    m_vPrevVel.set(0, 0, 0);
}

tmc::task<bool> CCustomRocket::net_Spawn(CSE_Abstract* DC)
{
    m_eState = eInactive;

    const bool result = co_await inherited::net_Spawn(DC);
    m_LaunchXForm.set(XFORM());

    co_return result;
}

tmc::task<void> CCustomRocket::net_Destroy()
{
    co_await inherited::net_Destroy();
    CPHUpdateObject::Deactivate();

    StopEngine();
    co_await StopFlying();
}

void CCustomRocket::SetLaunchParams(const Fmatrix& xform, const Fvector& vel, const Fvector& angular_vel)
{
    VERIFY2(_valid(xform), "SetLaunchParams. Invalid xform argument!");
    m_LaunchXForm = xform;
    m_vLaunchVelocity = vel;
    m_vLaunchAngularVelocity = angular_vel;
    m_time_to_explode = Device.fTimeGlobal + pSettings->r_float(cNameSect(), "force_explode_time") / 1000.0f;

#ifdef DEBUG
    gbg_rocket_speed1 = 0;
    gbg_rocket_speed2 = 0;
#endif
}

void CCustomRocket::activate_physic_shell()
{
    VERIFY(H_Parent());
    VERIFY(!m_pPhysicsShell);
    create_physic_shell();
    if (m_pPhysicsShell->isActive())
        return;

    VERIFY2(_valid(m_LaunchXForm), "CCustomRocket::activate_physic_shell. Invalid m_LaunchXForm!");
    m_pPhysicsShell->Activate(m_LaunchXForm, m_vLaunchVelocity, m_vLaunchAngularVelocity);
    m_pPhysicsShell->Update();

    XFORM().set(m_pPhysicsShell->mXFORM);
    Position().set(m_pPhysicsShell->mXFORM.c);
    m_pPhysicsShell->set_PhysicsRefObject(this);
    m_pPhysicsShell->set_ObjectContactCallback(ObjectContactCallback);
    m_pPhysicsShell->set_ContactCallback(nullptr);
    m_pPhysicsShell->SetAirResistance(0.f, 0.f);
    m_pPhysicsShell->set_DynamicScales(1.f, 1.f);
    m_pPhysicsShell->SetAllGeomTraced();
}

void CCustomRocket::create_physic_shell()
{
    VERIFY(!m_pPhysicsShell);
    Fobb obb;
    Visual()->getVisData().box.get_CD(obb.m_translate, obb.m_halfsize);
    obb.m_rotate.identity();

    // Physics (Elements)
    CPhysicsElement* E = P_create_Element();
    R_ASSERT(E);

    Fvector ax;
    float radius;
    CHOOSE_MAX(obb.m_halfsize.x, ax.set(obb.m_rotate.i); ax.mul(obb.m_halfsize.x); radius = _min(obb.m_halfsize.y, obb.m_halfsize.z); obb.m_halfsize.y /= 2.f;
               obb.m_halfsize.z /= 2.f, obb.m_halfsize.y, ax.set(obb.m_rotate.j); ax.mul(obb.m_halfsize.y); radius = _min(obb.m_halfsize.x, obb.m_halfsize.z);
               obb.m_halfsize.x /= 2.f; obb.m_halfsize.z /= 2.f, obb.m_halfsize.z, ax.set(obb.m_rotate.k); ax.mul(obb.m_halfsize.z);
               radius = _min(obb.m_halfsize.y, obb.m_halfsize.x); obb.m_halfsize.y /= 2.f; obb.m_halfsize.x /= 2.f)
    // radius*=1.4142f;
    Fsphere sphere1, sphere2;
    sphere1.P.add(obb.m_translate, ax);
    sphere1.R = radius * 1.4142f;

    sphere2.P.sub(obb.m_translate, ax);
    sphere2.R = radius / 2.f;

    E->add_Box(obb);
    E->add_Sphere(sphere1);
    E->add_Sphere(sphere2);

    // Physics (Shell)
    m_pPhysicsShell = P_create_Shell();
    R_ASSERT(m_pPhysicsShell);
    m_pPhysicsShell->add_Element(E);
    m_pPhysicsShell->setMass(7.f);
    m_pPhysicsShell->SetAirResistance();
}

//////////////////////////////////////////////////////////////////////////
// Rocket specific functions
//////////////////////////////////////////////////////////////////////////

void CCustomRocket::ObjectContactCallback(bool& do_colide, bool, dContact& c, SGameMtl* material_1, SGameMtl* material_2)
{
    do_colide = false;

    dxGeomUserData* l_pUD1 = retrieveGeomUserData(c.geom.g1);
    dxGeomUserData* l_pUD2 = retrieveGeomUserData(c.geom.g2);

    const SGameMtl* material;
    CCustomRocket* l_this = l_pUD1 ? smart_cast<CCustomRocket*>(l_pUD1->ph_ref_object) : nullptr;
    Fvector vUp;
    if (!l_this)
    {
        l_this = l_pUD2 ? smart_cast<CCustomRocket*>(l_pUD2->ph_ref_object) : nullptr;
        vUp.invert(*(Fvector*)&c.geom.normal);

        // if(dGeomGetClass(c.geom.g1)==dTriListClass)
        //	material=GMLib.GetMaterialByIdx((u16)c.surface.mode);
        // else
        //	material=GMLib.GetMaterialByIdx(l_pUD2->material);
        material = material_1;
    }
    else
    {
        vUp.set(*(Fvector*)&c.geom.normal);

        // if(dGeomGetClass(c.geom.g2)==dTriListClass)
        //	material=GMLib.GetMaterialByIdx((u16)c.surface.mode);
        // else
        //	material=GMLib.GetMaterialByIdx(l_pUD1->material);
        material = material_2;
    }
    VERIFY(material);
    if (material->Flags.is(SGameMtl::flPassable))
        return;

    if (!l_this || l_this->m_contact.contact)
        return;

    CGameObject* l_pOwner = l_pUD1 ? smart_cast<CGameObject*>(l_pUD1->ph_ref_object) : nullptr;
    if (!l_pOwner || l_pOwner == (CGameObject*)l_this)
        l_pOwner = l_pUD2 ? smart_cast<CGameObject*>(l_pUD2->ph_ref_object) : nullptr;

    if (!l_pOwner || l_pOwner != l_this->m_pOwner)
    {
        if (l_this->m_pOwner)
        {
            Fvector l_pos;
            l_pos.set(l_this->Position());
#ifdef DEBUG
            bool corrected_pos = false;
#endif
            if (!l_pUD1 || !l_pUD2)
            {
                dGeomID g{};
                dxGeomUserData*& l_pUD = l_pUD1 ? l_pUD1 : l_pUD2;
                if (l_pUD1)
                    g = c.geom.g1;
                else
                    g = c.geom.g2;

                if (l_pUD->pushing_neg)
                {
                    Fvector velocity;
                    l_this->PHGetLinearVell(velocity);
                    if (velocity.square_magnitude() > EPS)
                    { //. desync?
                        velocity.normalize();
                        Triangle neg_tri;
                        CalculateTriangle(l_pUD->neg_tri, g, neg_tri);
                        float cosinus = velocity.dotproduct(*((Fvector*)neg_tri.norm));
                        VERIFY(_valid(neg_tri.dist));
                        float dist = neg_tri.dist / cosinus;
                        velocity.mul(dist * 1.1f);
                        l_pos.sub(velocity);
#ifdef DEBUG
                        corrected_pos = true;
//.	DBG_OpenCashedDraw();
//.	const Fvector*	 V_array	= Level().ObjectSpace.GetStaticVerts();
//.	DBG_DrawTri(neg_tri.T, V_array, D3DCOLOR_XRGB(255,255,0));
//.	DBG_ClosedCashedDraw(50000);
#endif
                    }
                }
            }
#ifdef DEBUG
            if (ph_dbg_draw_mask.test(phDbgDrawExplosionPos))
                DBG_DrawPoint(l_pos, 0.05f, D3DCOLOR_XRGB(255, 255, (!corrected_pos) * 255));
#endif

            l_this->Contact(l_pos, vUp);
            l_this->m_pPhysicsShell->DisableCollision();
            l_this->m_pPhysicsShell->set_LinearVel(Fvector().set(0, 0, 0));
            l_this->m_pPhysicsShell->set_AngularVel(Fvector().set(0, 0, 0));
            l_this->m_pPhysicsShell->setForce(Fvector().set(0, 0, 0));
            l_this->m_pPhysicsShell->setTorque(Fvector().set(0, 0, 0));
            l_this->m_pPhysicsShell->set_ApplyByGravity(false);
            l_this->setEnabled(FALSE);
        }
    }
}

void CCustomRocket::Load(LPCSTR section)
{
    inherited::Load(section);

    reload(section);
}

void CCustomRocket::reload(LPCSTR section)
{
    inherited::reload(section);
    m_eState = eInactive;

    m_bEnginePresent = !!pSettings->r_bool(section, "engine_present");
    if (m_bEnginePresent)
    {
        m_dwEngineWorkTime = pSettings->r_u32(section, "engine_work_time");
        m_fEngineImpulse = pSettings->r_float(section, "engine_impulse");
        m_fEngineImpulseUp = pSettings->r_float(section, "engine_impulse_up");
    }

    m_bLightsEnabled = !!pSettings->r_bool(section, "lights_enabled");
    if (m_bLightsEnabled)
    {
        const auto res = scn::scan<f32, f32, f32>(std::string_view{pSettings->r_string(section, "trail_light_color")}, "{},{},{}");
        R_ASSERT(res, res.error().msg());

        const auto [r, g, b] = res->values();
        m_TrailLightColor.r = r;
        m_TrailLightColor.g = g;
        m_TrailLightColor.b = b;

        m_fTrailLightRange = pSettings->r_float(section, "trail_light_range");
    }

    if (pSettings->line_exist(section, "engine_particles"))
        m_sEngineParticles._set(pSettings->r_string(section, "engine_particles"));
    if (pSettings->line_exist(section, "fly_particles"))
        m_sFlyParticles._set(pSettings->r_string(section, "fly_particles"));

    if (pSettings->line_exist(section, "snd_fly_sound"))
        m_flyingSound.create(pSettings->r_string(section, "snd_fly_sound"), st_Effect, sg_SourceType);
}

void CCustomRocket::Contact(const Fvector& pos, const Fvector& normal)
{
    m_contact.contact = true;
    m_contact.pos.set(pos);
    m_contact.up.set(normal);
}

tmc::task<void> CCustomRocket::PlayContact()
{
    if (!m_contact.contact)
        co_return;
    if (m_eState == eCollide)
        co_return;

    StopEngine();
    co_await StopFlying();

    m_eState = eCollide;

    // дективировать физическую оболочку,чтоб ракета не летела дальше
    if (m_pPhysicsShell)
    {
        m_pPhysicsShell->set_LinearVel(zero_vel);
        m_pPhysicsShell->set_AngularVel(zero_vel);
        m_pPhysicsShell->set_ObjectContactCallback(nullptr);
        m_pPhysicsShell->Disable();
    }
    //	if (OnClient()) return;

    Position().set(m_contact.pos);
    m_contact.contact = false;
}

void CCustomRocket::OnH_B_Chield()
{
    VERIFY(m_eState == eInactive);
    inherited::OnH_B_Chield();
}

void CCustomRocket::OnH_A_Chield()
{
    VERIFY(m_eState == eInactive);
    inherited::OnH_A_Chield();
}

void CCustomRocket::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);
    //-------------------------------------------
    m_pOwner = H_Parent() ? smart_cast<CGameObject*>(H_Parent()->H_Root()) : nullptr;
    //-------------------------------------------
}

void CCustomRocket::OnH_A_Independent()
{
    inherited::OnH_A_Independent();

    if (!g_pGameLevel->bReady || !m_bLaunched)
        return;

    setVisible(true);
    StartFlying();
    StartEngine();
}

tmc::task<void> CCustomRocket::UpdateCL()
{
    co_await inherited::UpdateCL();
    co_await PlayContact();

    switch (m_eState)
    {
    // состояния eEngine и eFlying отличаются, тем
    // что вызывается UpdateEngine у eEngine, остальные
    // функции общие
    case eEngine: UpdateEngine(); [[fallthrough]];
    case eFlying:
        UpdateLights();
        UpdateParticles();
        break;
    default: break;
    }

    if (m_eState == eEngine || m_eState == eFlying)
    {
        if (m_time_to_explode < Device.fTimeGlobal)
            Contact(Position(), Direction());
    }
}

void CCustomRocket::StartEngine()
{
    VERIFY(NULL == H_Parent());

    if (!m_bEnginePresent)
    {
        m_eState = eFlying;
        return;
    }

    m_eState = eEngine;
    m_dwEngineTime = m_dwEngineWorkTime;

    StartEngineParticles();

    CPHUpdateObject::Activate();
}

void CCustomRocket::StopEngine()
{
    m_eState = eFlying;

    m_dwEngineTime = 0;

    if (m_bStopLightsWithEngine)
        StopLights();

    StopEngineParticles();

    CPHUpdateObject::Deactivate();
}

void CCustomRocket::UpdateEnginePh()
{
    float force = m_fEngineImpulse * fixed_step; // * Device.fTimeDelta;
    float k_back = 1.f;
    Fvector l_pos, l_dir;
    l_pos.set(0, 0, -2.f);
    l_dir.set(XFORM().k);

    l_dir.normalize();
    m_pPhysicsShell->applyImpulse(l_dir, (1.f + k_back) * force);
    m_pPhysicsShell->get_LinearVel(l_dir);
    l_dir.normalize_safe();
    l_dir.invert();
    m_pPhysicsShell->applyImpulseTrace(l_pos, l_dir, force);
    l_dir.set(0, 1.f, 0);
    force = m_fEngineImpulseUp * fixed_step; // * Device.fTimeDelta;
    m_pPhysicsShell->applyImpulse(l_dir, force);

    // m_pPhysicsShell->set_AngularVel()
}

void CCustomRocket::UpdateEngine()
{
    if (!m_pPhysicsShell)
        XR_LOG_ERROR("Update called without physic shell");

    if (!getVisible())
        Msg("! CCustomRocket::UpdateEngine called, but false==getVisible() id[{}] frame[{}]", ID(), Device.dwFrame);

    if (m_dwEngineTime <= 0)
    {
        StopEngine();
        return;
    }

    m_dwEngineTime -= Device.dwTimeDelta;
}

//////////////////////////////////////////////////////////////////////////
//	Lights
//////////////////////////////////////////////////////////////////////////
void CCustomRocket::StartLights()
{
    if (!m_bLightsEnabled)
        return;

    // включить световую подсветку от двигателя
    m_pTrailLight->set_color(m_TrailLightColor.r, m_TrailLightColor.g, m_TrailLightColor.b);

    m_pTrailLight->set_range(m_fTrailLightRange);
    m_pTrailLight->set_position(Position());
    m_pTrailLight->set_active(true);
}

void CCustomRocket::StopLights()
{
    if (!m_bLightsEnabled)
        return;

    m_pTrailLight->set_active(false);
}

void CCustomRocket::UpdateLights()
{
    if (!m_bLightsEnabled || !m_pTrailLight->get_active())
        return;

    m_pTrailLight->set_position(Position());
}

void CCustomRocket::PhDataUpdate(float) {}
void CCustomRocket::PhTune(float) { UpdateEnginePh(); }

//////////////////////////////////////////////////////////////////////////
//	Particles
//////////////////////////////////////////////////////////////////////////

void CCustomRocket::UpdateParticles()
{
    if (m_flyingSound._handle() && m_flyingSound._feedback())
        m_flyingSound.set_position(XFORM().c);

    if (!m_pEngineParticles && !m_pFlyParticles)
        return;

    Fvector vel;
    PHGetLinearVell(vel);

    vel.add(m_vPrevVel, vel);
    vel.mul(0.5f);
    m_vPrevVel.set(vel);

    Fmatrix particles_xform;
    particles_xform.identity();
    particles_xform.k.set(XFORM().k);
    particles_xform.k.mul(-1.f);
    Fvector::generate_orthonormal_basis(particles_xform.k, particles_xform.j, particles_xform.i);
    particles_xform.c.set(XFORM().c);

    if (m_pEngineParticles)
        m_pEngineParticles->UpdateParent(particles_xform, vel);
    if (m_pFlyParticles)
        m_pFlyParticles->UpdateParent(particles_xform, vel);
}

void CCustomRocket::StartEngineParticles()
{
    VERIFY(!m_pEngineParticles);
    if (!m_sEngineParticles)
        return;

    m_pEngineParticles = CParticlesObject::Create(m_sEngineParticles.c_str(), FALSE);

    UpdateParticles();
    m_pEngineParticles->Play();

    VERIFY(m_pEngineParticles);
    VERIFY3(m_pEngineParticles->IsLooped(), "must be a looped particle system for rocket engine: %s", m_sEngineParticles.c_str());
}

void CCustomRocket::StopEngineParticles()
{
    if (!m_pEngineParticles)
        return;

    m_pEngineParticles->Stop();
    m_pEngineParticles->SetAutoRemove(true);
    m_pEngineParticles = nullptr;
}

void CCustomRocket::StartFlyParticles()
{
    if (m_flyingSound._handle())
        m_flyingSound.play_at_pos(nullptr, XFORM().c, sm_Looped);

    VERIFY(!m_pFlyParticles);

    if (!m_sFlyParticles)
        return;

    m_pFlyParticles = CParticlesObject::Create(m_sFlyParticles.c_str(), FALSE);

    UpdateParticles();
    m_pFlyParticles->Play();

    VERIFY(m_pFlyParticles);
    VERIFY3(m_pFlyParticles->IsLooped(), "must be a looped particle system for rocket fly: %s", m_sFlyParticles.c_str());
}

tmc::task<void> CCustomRocket::StopFlyParticles()
{
    if (m_flyingSound._handle() != nullptr)
        co_await m_flyingSound.stop();

    if (m_pFlyParticles == nullptr)
        co_return;

    m_pFlyParticles->Stop();
    m_pFlyParticles->SetAutoRemove(true);
    m_pFlyParticles = nullptr;
}

void CCustomRocket::StartFlying()
{
    StartFlyParticles();
    StartLights();
}

tmc::task<void> CCustomRocket::StopFlying()
{
    co_await StopFlyParticles();
    StopLights();
}

tmc::task<void> CCustomRocket::OnEvent(NET_Packet& P, u16 type) { co_await inherited::OnEvent(P, type); }
