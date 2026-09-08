#include "stdafx.h"

#include "ParticlesObject.h"
#include "../xr_3da/GameMtlLib.h"
#include "Level.h"
#include "GamePersistent.h"
#include "ExtendedGeom.h"
#include "PhysicsGamePars.h"
#include "PhysicsCommon.h"
#include "PHSoundPlayer.h"
#include "PhysicsShellHolder.h"
#include "PHCommander.h"
#include "MathUtils.h"
#include "PHReqComparer.h"
#include "PHWorld.h"

namespace
{
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
constexpr float PARTICLE_EFFECT_DIST{70.f};
constexpr float SOUND_EFFECT_DIST{70.f};
//////////////////////////////////////////////////////////////////////////////////
constexpr float SQUARE_PARTICLE_EFFECT_DIST{PARTICLE_EFFECT_DIST * PARTICLE_EFFECT_DIST};
constexpr float SQUARE_SOUND_EFFECT_DIST{SOUND_EFFECT_DIST * SOUND_EFFECT_DIST};

constexpr float minimal_plane_distance_between_liquid_particles{0.2f};
} // namespace

class CPHParticlesPlayCall final : public CPHAction, public CPHReqComparerV
{
    RTTI_DECLARE_TYPEINFO(CPHParticlesPlayCall, CPHAction, CPHReqComparerV);

public:
    LPCSTR ps_name;
    dContactGeom c;

    u32 remove_time;

    bool b_called{};
    int psLifeTime{};

    const CPhysicsShellHolder* m_object;

    explicit CPHParticlesPlayCall(const dContactGeom& contact, bool invert_n, LPCSTR psn, const CPhysicsShellHolder* object = nullptr)
    {
        ps_name = psn;
        c = contact;
        if (invert_n)
        {
            c.normal[0] = -c.normal[0];
            c.normal[1] = -c.normal[1];
            c.normal[2] = -c.normal[2];
        }
        m_object = object;

        constexpr u32 time_to_call_remove = 3000;
        remove_time = Device.dwTimeGlobal + time_to_call_remove;
    }

    ~CPHParticlesPlayCall() override = default;

    void run() override
    {
        if (b_called)
            return;

        b_called = true;

        CParticlesObject* ps = CParticlesObject::Create(ps_name, TRUE);
        psLifeTime = ps->LifeTime();

        Fmatrix pos;
        pos.k.set(*((Fvector*)c.normal));
        Fvector::generate_orthonormal_basis(pos.k, pos.j, pos.i);
        pos.c.set(*((Fvector*)c.pos));

        ps->UpdateParent(pos, {});
        GamePersistent().ps_needtoplay.push_back(ps);

        if (m_object && psLifeTime > 0)
            remove_time = Device.dwTimeGlobal + iFloor(psLifeTime / 2.f);
    }

    [[nodiscard]] bool obsolete() const noexcept override { return Device.dwTimeGlobal > remove_time; }

    [[nodiscard]] const CPhysicsShellHolder* object() const noexcept { return m_object; }
    [[nodiscard]] const Fvector& position() const noexcept { return cast_fv(c.pos); }

    [[nodiscard]] bool compare(const CPHReqComparerV* v) const override { return v->compare(this); }
};

class CPHParticlesCondition final : public CPHCondition, public CPHReqComparerV
{
    RTTI_DECLARE_TYPEINFO(CPHParticlesCondition, CPHCondition, CPHReqComparerV);

public:
    ~CPHParticlesCondition() override = default;

private:
    [[nodiscard]] bool compare(const CPHReqComparerV* v) const noexcept override { return v->compare(this); }
    [[nodiscard]] bool is_true() noexcept override { return true; }
    [[nodiscard]] bool obsolete() const noexcept override { return false; }
};

class CPHFindParticlesComparer final : public CPHReqComparerV
{
    RTTI_DECLARE_TYPEINFO(CPHFindParticlesComparer, CPHReqComparerV);

public:
    const CPhysicsShellHolder* m_object;
    Fvector m_position;

    explicit CPHFindParticlesComparer(const Fvector& position, const CPhysicsShellHolder* object = nullptr) : m_object{object}, m_position{position} {}
    ~CPHFindParticlesComparer() override = default;

private:
    [[nodiscard]] bool compare(const CPHReqComparerV* v) const { return v->compare(this); }
    [[nodiscard]] bool compare(const CPHParticlesCondition*) const override { return true; }

    [[nodiscard]] bool compare(const CPHParticlesPlayCall* v) const override
    {
        VERIFY(v);

        if (m_object)
            return m_object == v->object();

        Fvector disp = Fvector().sub(m_position, v->position());
        return disp.x * disp.x + disp.z * disp.z < (minimal_plane_distance_between_liquid_particles * minimal_plane_distance_between_liquid_particles);
    }
};

namespace
{
class CPHWallMarksCall final : public CPHAction
{
    RTTI_DECLARE_TYPEINFO(CPHWallMarksCall, CPHAction);

private:
    wm_shader pWallmarkShader;
    const CDB::TRI& T;
    Fvector pos;

public:
    explicit CPHWallMarksCall(const Fvector& p, const CDB::TRI& Tri, const wm_shader& s) : pWallmarkShader{s}, T{Tri}, pos{p} {}

    ~CPHWallMarksCall() override = default;

    void run() override
    {
        // добавить отметку на материале
        ::Render->add_StaticWallmark(pWallmarkShader, pos, 0.09f, T, Level().ObjectSpace.GetStaticVerts());
    }

    [[nodiscard]] bool obsolete() const override { return false; }
};

CPHSoundPlayer* object_snd_player(dxGeomUserData* data) noexcept { return data->ph_ref_object ? data->ph_ref_object->ph_sound_player() : nullptr; }

template <class Pars>
void TContactShotMark(const CDB::TRI* T, dContactGeom* c)
{
    dBodyID b = dGeomGetBody(c->g1);
    dxGeomUserData* data;
    bool b_invert_normal = false;
    if (!b)
    {
        b = dGeomGetBody(c->g2);
        data = dGeomGetUserData(c->g2);
        b_invert_normal = true;
    }
    else
    {
        data = dGeomGetUserData(c->g1);
    }
    if (!b)
        return;
    dVector3 vel;
    dMass m;
    dBodyGetMass(b, &m);
    dBodyGetPointVel(b, c->pos[0], c->pos[1], c->pos[2], vel);
    dReal vel_cret = dFabs(dDOT(vel, c->normal)) * _sqrt(m.mass);
    Fvector to_camera;
    to_camera.sub(cast_fv(c->pos), Device.vCameraPosition);
    float square_cam_dist = to_camera.square_magnitude();
    if (data)
    {
        SGameMtlPair* mtl_pair = GMLib.GetMaterialPair(T->material, data->material);
        if (mtl_pair)
        {
            if (vel_cret > Pars::vel_cret_wallmark && !mtl_pair->CollideMarks->empty())
            {
                wm_shader WallmarkShader = mtl_pair->CollideMarks->GenerateWallmark();
                std::ignore = Level().ph_commander().add_call(xr_new<CPHOnesCondition>(), xr_new<CPHWallMarksCall>(*((Fvector*)c->pos), *T, WallmarkShader));
            }
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (square_cam_dist < SQUARE_SOUND_EFFECT_DIST && !mtl_pair->CollideSounds.empty())
            {
                SGameMtl* static_mtl = GMLib.GetMaterialByIdx(T->material);
                VERIFY(static_mtl);
                if (static_mtl->Flags.test(SGameMtl::flPassable))
                {
                    if (auto sp = object_snd_player(data); sp)
                        sp->Play(mtl_pair, (Fvector*)c->pos, true, nullptr);
                }
                else
                {
                    float volume = collide_volume_min;

                    if (vel_cret > Pars::vel_cret_sound)
                        volume += vel_cret * (collide_volume_max - collide_volume_min) / (_sqrt(mass_limit) * default_l_limit - Pars::vel_cret_sound);

                    if (auto sp = object_snd_player(data); sp)
                        sp->PlayNext(mtl_pair, (Fvector*)c->pos, true, &volume);
                    else
                        GET_RANDOM(mtl_pair->CollideSounds).play_no_feedback(nullptr, 0, 0, ((Fvector*)c->pos), &volume);
                }
            }
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (square_cam_dist < SQUARE_PARTICLE_EFFECT_DIST)
            {
                if (vel_cret > Pars::vel_cret_particles && !mtl_pair->CollideParticles.empty())
                {
                    const auto ps_name = mtl_pair->CollideParticles[::Random.randI(0, mtl_pair->CollideParticles.size())].c_str();

                    // отыграть партиклы столкновения материалов
                    CPHFindParticlesComparer find(cast_fv(c->pos), data->ph_ref_object);
                    if (!Level().ph_commander().has_call(&find, &find))
                    {
                        XR_LOG_DEBUG("Adding collide particle for obj id={}", data->ph_ref_object->ID());
                        std::ignore = Level().ph_commander().add_call(xr_new<CPHParticlesCondition>(),
                                                                      xr_new<CPHParticlesPlayCall>(*c, b_invert_normal, ps_name, data->ph_ref_object));
                    }
                    else
                    {
                        XR_LOG_DEBUG("Skip collide particle...");
                    }
                }
            }
        }
    }
}
} // namespace

ContactCallbackFun* ContactShotMark = &TContactShotMark<EffectPars>;
ContactCallbackFun* CharacterContactShotMark = &TContactShotMark<CharacterEffectPars>;
