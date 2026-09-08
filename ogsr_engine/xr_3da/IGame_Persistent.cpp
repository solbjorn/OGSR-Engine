#include "stdafx.h"

#include "IGame_Persistent.h"

#include "Environment.h"
#include "x_ray.h"
#include "IGame_Level.h"
#include "XR_IOConsole.h"
#include "Render.h"
#include "PS_instance.h"
#include "std_classes.h"
#include "CustomHUD.h"
#include "perlin.h"

extern Fvector4 ps_ssfx_grass_interactive;

IGame_Persistent* g_pGamePersistent{};
bool g_prefetch{true};

namespace xr
{
namespace detail
{
std::unique_ptr<xr::ingame_editor> editor;
std::unique_ptr<xr::social_app> social;
} // namespace detail
} // namespace xr

bool IGame_Persistent::IsMainMenuActive() const { return g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive(); }
bool IGame_Persistent::MainMenuActiveOrLevelNotExist() const { return !g_pGameLevel || IsMainMenuActive(); }

IGame_Persistent::IGame_Persistent()
{
    Device.seqAppStart.Add(this);
    Device.seqAppEnd.Add(this);
    Device.seqFrame.Add(this, REG_PRIORITY_HIGH + 1);
    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);

    PerlinNoise1D = xr_new<CPerlinNoise1D>();
    PerlinNoise1D->SetOctaves(2);
    PerlinNoise1D->SetAmplitude(0.66666f);

    pEnvironment = xr_new<CEnvironment>();

    m_pGShaderConstants = ShadersExternalData(); //--#SM+#--
}

tmc::task<std::unique_ptr<IGame_Persistent>> IGame_Persistent::co_create()
{
    auto pers = absl::WrapUnique(smart_cast<IGame_Persistent*>(NEW_INSTANCE(CLSID_GAME_PERSISTANT)));

    co_await pers->co_init();
    co_return pers;
}

tmc::task<void> IGame_Persistent::co_init() { co_await Environment().OnDeviceCreate(); }

tmc::task<void> IGame_Persistent::co_destroy()
{
    auto pers = this;
    DEL_INSTANCE(pers);

    co_return;
}

IGame_Persistent::~IGame_Persistent()
{
    Device.seqFrame.Remove(this);
    Device.seqAppStart.Remove(this);
    Device.seqAppEnd.Remove(this);
    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
    xr_delete(pEnvironment);
}

tmc::task<void> IGame_Persistent::OnAppStart()
{
    Environment().load();
    co_return;
}

tmc::task<void> IGame_Persistent::OnAppEnd()
{
    Environment().unload();
    OnGameEnd();

    DEL_INSTANCE(g_hud);
    co_return;
}

void IGame_Persistent::PreStart(LPCSTR op)
{
    string256 prev_type;
    params new_game_params;
    strcpy_s(prev_type, m_game_params.m_game_type);
    new_game_params.parse_cmd_line(op);

    // change game type
    if (0 != xr_strcmp(prev_type, new_game_params.m_game_type))
    {
        OnGameEnd();
    }
}

tmc::task<void> IGame_Persistent::Start(gsl::czstring op)
{
    string256 prev_type;
    strcpy_s(prev_type, m_game_params.m_game_type);
    m_game_params.parse_cmd_line(op);

    // change game type
    if ((0 != xr_strcmp(prev_type, m_game_params.m_game_type)))
    {
        if (*m_game_params.m_game_type)
            co_await OnGameStart();
        if (g_hud)
            DEL_INSTANCE(g_hud);
    }
    else
    {
        UpdateGameType();
    }

    XR_ASSERT(ps_destroy.size() == 0, "", ps_destroy);
}

tmc::task<void> IGame_Persistent::Disconnect()
{
    // clear "need to play" particles
    destroy_particles(true);

    if (g_hud)
        g_hud->OnDisconnected();

    if (!g_prefetch) // очистка при выходе из игры в главное меню
        ObjectPool.clear();

    co_return;
}

tmc::task<void> IGame_Persistent::OnGameStart()
{
    co_await LoadTitle("st_prefetching_objects");

    if (!g_prefetch)
        co_return;

    // prefetch game objects & models
    float p_time = 1000.f * Device.GetTimerGlobal()->GetElapsed_sec();
    u32 mem_0 = Memory.mem_usage();

    XR_LOG_NOTICE("Loading objects...");
    ObjectPool.prefetch();

    XR_LOG_NOTICE("Loading models...");
    Render->models_Prefetch();

    p_time = 1000.f * Device.GetTimerGlobal()->GetElapsed_sec() - p_time;
    u32 p_mem = Memory.mem_usage() - mem_0;

    Msg("* [prefetch] time:    {} ms", iFloor(p_time));
    Msg("* [prefetch] memory:  {} Kb", p_mem / 1024);
}

void IGame_Persistent::OnGameEnd()
{
    ObjectPool.clear();
    Render->models_Clear(TRUE);
}

tmc::task<void> IGame_Persistent::OnFrame()
{
    if (!Device.Paused() || Device.dwPrecacheFrame)
        co_await Environment().OnFrame();

    Device.Statistic->Particles_starting = ps_needtoplay.size();
    Device.Statistic->Particles_active = ps_active.size();
    Device.Statistic->Particles_destroy = ps_destroy.size();

    // Play req particle systems
    while (!ps_needtoplay.empty())
    {
        auto psi = ps_needtoplay.back();
        ps_needtoplay.pop_back();
        psi->Play();
    }

    // Destroy inactive particle systems
    while (!ps_destroy.empty())
    {
        auto psi = *ps_destroy.begin();
        XR_ASSERT(psi != nullptr && !psi->Locked());
        psi->PSI_internal_delete();
    }
}

void IGame_Persistent::destroy_particles(const bool& all_particles)
{
    ps_needtoplay.clear();

    while (!ps_destroy.empty())
    {
        auto psi = *ps_destroy.begin();
        XR_ASSERT(psi != nullptr && !psi->Locked());
        psi->PSI_internal_delete();
    }

    // delete active particles
    if (all_particles)
    {
        while (!ps_active.empty())
            (*ps_active.begin())->PSI_internal_delete();

        return;
    }

    size_t processed = 0;
    auto iter = ps_active.rbegin();
    while (iter != ps_active.rend())
    {
        const auto size = ps_active.size();
        auto& object = *(iter++);

        if (object->destroy_on_game_load())
            object->PSI_internal_delete();

        if (size != ps_active.size())
        {
            iter = ps_active.rbegin();
            std::advance(iter, processed);
        }
        else
        {
            processed++;
        }
    }
}

void IGame_Persistent::models_savePrefetch() { Render->models_savePrefetch(); }

void IGame_Persistent::GrassBendersUpdate(const u16 id, size_t& data_idx, u32& data_frame, const Fvector& position, const float init_radius,
                                          const float init_str, bool CheckDistance)
{
    // Interactive grass disabled
    if (ps_ssfx_grass_interactive.y < 1.f)
        return;

    // Just update position if not NULL
    if (data_idx)
    {
        // Explosions can take the mem spot, unassign and try to get a spot later.
        if (grass_shader_data.id[data_idx] != id)
        {
            data_idx = 0;
            data_frame = Device.dwFrame + Random.randI(10, 35);
        }
        else
        {
            const float saved_radius = grass_shader_data.pos[data_idx].w;
            grass_shader_data.pos[data_idx].set(position.x, position.y, position.z, saved_radius);
        }
    }

    if (Device.dwFrame < data_frame)
        return;

    // Wait some random frames to split the checks
    data_frame = Device.dwFrame + Random.randI(10, 35);

    // Check Distance
    if (CheckDistance)
    {
        if (position.distance_to_xz_sqr(Device.vCameraPosition) > ps_ssfx_grass_interactive.z)
        {
            GrassBendersRemoveByIndex(data_idx);
            return;
        }
    }

    const CFrustum& view_frust = ::Render->ViewBase;
    u32 mask = 0xff;
    float rad = data_idx == 0 ? 1.f : std::max(1.f, grass_shader_data.pos[data_idx].w + 0.5f);

    // In view frustum?
    if (!view_frust.testSphere(position, rad, mask))
    {
        GrassBendersRemoveByIndex(data_idx);
        return;
    }

    // Empty slot, let's use this
    if (data_idx == 0)
    {
        const size_t idx = grass_shader_data.index + 1;
        // Add to grass blenders array
        if (grass_shader_data.id[idx] == 0)
        {
            data_idx = idx;
            GrassBendersSet(idx, id, position, Fvector{0.0f, -99.0f, 0.0f}, 0.0f, 0.0f, 0.0f, init_radius, BENDER_ANIM_DEFAULT, true);

            grass_shader_data.str_target[idx] = init_str;
            grass_shader_data.pos[idx].w = init_radius;
        }

        // Back to 0 when the array limit is reached
        grass_shader_data.index = idx < (std::min(static_cast<size_t>(ps_ssfx_grass_interactive.y), std::size(grass_shader_data.id) - 1)) ? idx : 0;
    }
    else
    {
        // Already inview, let's add more time to re-check
        data_frame += 60;
        const float saved_radius = grass_shader_data.pos[data_idx].w;
        grass_shader_data.pos[data_idx].set(position.x, position.y, position.z, saved_radius);
    }
}

void IGame_Persistent::GrassBendersAddExplosion(const u16 id, const Fvector& position, const Fvector3& dir, const float fade, const float speed,
                                                const float intensity, const float radius)
{
    if (ps_ssfx_grass_interactive.y < 1.f)
        return;

    for (int idx = 1; idx < ps_ssfx_grass_interactive.y + 1; idx++)
    {
        // Add explosion to any spot not already taken by an explosion.
        if (grass_shader_data.anim[idx] != BENDER_ANIM_EXPLOSION)
        {
            // Add 99 to the ID to avoid conflicts between explosions and basic benders happening at the same time with the same ID.
            GrassBendersSet(idx, id + 99, position, dir, fade, speed, intensity, radius, BENDER_ANIM_EXPLOSION, true);
            grass_shader_data.str_target[idx] = intensity;
            break;
        }
    }
}

void IGame_Persistent::GrassBendersAddShot(const u16 id, const Fvector& position, const Fvector3& dir, const float fade, const float speed,
                                           const float intensity, const float radius)
{
    // Is disabled?
    if (ps_ssfx_grass_interactive.y < 1.f || intensity <= 0.0f)
        return;

    // Check distance
    if (position.distance_to_xz_sqr(Device.vCameraPosition) > ps_ssfx_grass_interactive.z)
        return;

    int AddAt = -1;

    // Look for a spot
    for (int idx = 1; idx < ps_ssfx_grass_interactive.y + 1; idx++)
    {
        // Already exist, just update and increase intensity
        if (grass_shader_data.id[idx] == id)
        {
            const float currentSTR = grass_shader_data.dir[idx].w;
            GrassBendersSet(idx, id, position, dir, fade, speed, currentSTR, radius, BENDER_ANIM_EXPLOSION, false);
            grass_shader_data.str_target[idx] += intensity;
            AddAt = -1;
            break;
        }
        else
        {
            // Check all indexes and keep usable index to use later if needed...
            if (AddAt == -1 && grass_shader_data.radius[idx] == 0.f)
                AddAt = idx;
        }
    }

    // We got an available index... Add bender at AddAt
    if (AddAt != -1)
    {
        GrassBendersSet(AddAt, id, position, dir, fade, speed, 0.001f, radius, BENDER_ANIM_EXPLOSION, true);
        grass_shader_data.str_target[AddAt] = intensity;
    }
}

void IGame_Persistent::GrassBendersUpdateAnimations()
{
    for (int idx = 1; idx < ps_ssfx_grass_interactive.y + 1; idx++)
    {
        if (grass_shader_data.radius[idx] != 0.f && grass_shader_data.id[idx] != 0)
        {
            switch (grass_shader_data.anim[idx])
            {
            case BENDER_ANIM_EXPLOSION: // Internal Only ( You can use BENDER_ANIM_PULSE for anomalies )
            {
                // Radius
                grass_shader_data.time[idx] += Device.fTimeDelta * grass_shader_data.speed[idx];
                grass_shader_data.pos[idx].w = grass_shader_data.radius[idx] * std::min(1.0f, grass_shader_data.time[idx]);

                grass_shader_data.str_target[idx] = std::min(1.0f, grass_shader_data.str_target[idx]);

                // Easing
                const float diff = std::max(0.1f, abs(grass_shader_data.dir[idx].w - grass_shader_data.str_target[idx]));

                // Intensity
                if (grass_shader_data.str_target[idx] <= grass_shader_data.dir[idx].w)
                {
                    grass_shader_data.dir[idx].w -= Device.fTimeDelta * grass_shader_data.fade[idx] * diff;
                }
                else
                {
                    grass_shader_data.dir[idx].w += Device.fTimeDelta * grass_shader_data.speed[idx] * diff;

                    if (grass_shader_data.dir[idx].w >= grass_shader_data.str_target[idx])
                        grass_shader_data.str_target[idx] = 0;
                }

                // Remove Bender
                if (grass_shader_data.dir[idx].w < 0.0f)
                    GrassBendersReset(idx);

                break;
            }

            case BENDER_ANIM_WAVY: {
                // Anim Speed
                grass_shader_data.time[idx] += Device.fTimeDelta * 1.5f * grass_shader_data.speed[idx];

                // Curve
                float curve = sin(grass_shader_data.time[idx]);

                // Intensity using curve
                grass_shader_data.dir[idx].w = curve * cos(curve * 1.4f) * 1.8f * grass_shader_data.str_target[idx];

                break;
            }

            case BENDER_ANIM_SUCK: {
                // Anim Speed
                grass_shader_data.time[idx] += Device.fTimeDelta * grass_shader_data.speed[idx];

                // Perlin Noise
                float curve = std::clamp(PerlinNoise1D->GetContinious(grass_shader_data.time[idx]) + 0.5f, 0.f, 1.f) * -1.0f;

                // Intensity using Perlin
                grass_shader_data.dir[idx].w = curve * grass_shader_data.str_target[idx];

                break;
            }

            case BENDER_ANIM_BLOW: {
                // Anim Speed
                grass_shader_data.time[idx] += Device.fTimeDelta * 1.2f * grass_shader_data.speed[idx];

                // Perlin Noise
                float curve = std::clamp(PerlinNoise1D->GetContinious(grass_shader_data.time[idx]) + 1.0f, 0.f, 2.0f) * 0.25f;

                // Intensity using Perlin
                grass_shader_data.dir[idx].w = curve * grass_shader_data.str_target[idx];

                break;
            }

            case BENDER_ANIM_PULSE: {
                // Anim Speed
                grass_shader_data.time[idx] += Device.fTimeDelta * grass_shader_data.speed[idx];

                // Radius
                grass_shader_data.pos[idx].w = grass_shader_data.radius[idx] * std::min(1.0f, grass_shader_data.time[idx]);

                // Diminish intensity when radius target is reached
                if (grass_shader_data.pos[idx].w >= grass_shader_data.radius[idx])
                {
                    float tmp = grass_shader_data.dir[idx].w;
                    tmp += GrassBenderToValue(tmp, 0.0f, grass_shader_data.speed[idx] * 0.6f, true);
                    grass_shader_data.dir[idx].w = tmp;
                }

                // Loop when intensity is <= 0
                if (grass_shader_data.dir[idx].w <= 0.0f)
                {
                    grass_shader_data.dir[idx].w = grass_shader_data.str_target[idx];
                    grass_shader_data.pos[idx].w = 0.0f;
                    grass_shader_data.time[idx] = 0.0f;
                }

                break;
            }

            case BENDER_ANIM_DEFAULT: {
                // Just fade to target strength
                float tmp = grass_shader_data.dir[idx].w;
                tmp += GrassBenderToValue(tmp, grass_shader_data.str_target[idx], 2.0f, true);
                grass_shader_data.dir[idx].w = tmp;

                break;
            }
            }
        }
    }
}

void IGame_Persistent::GrassBendersRemoveByIndex(size_t& idx)
{
    if (idx != 0)
    {
        GrassBendersReset(idx);
        idx = 0;
    }
}

void IGame_Persistent::GrassBendersRemoveById(const u16 id)
{
    // Search by Object ID ( Used when removing benders CPHMovementControl::DestroyCharacter() )
    for (int i = 1; i < ps_ssfx_grass_interactive.y + 1; i++)
        if (grass_shader_data.id[i] == id)
            GrassBendersReset(i);
}

void IGame_Persistent::GrassBendersReset(const size_t idx)
{
    // Reset Everything
    GrassBendersSet(idx, 0, {}, Fvector{0.0f, -99.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, BENDER_ANIM_DEFAULT, true);
    grass_shader_data.str_target[idx] = 0;
}

void IGame_Persistent::GrassBendersSet(const size_t idx, const u16 id, const Fvector& position, const Fvector3& dir, const float fade, const float speed,
                                       const float intensity, const float radius, const GrassBenders_Anim anim, const bool resetTime)
{
    // Set values
    const float saved_radius = grass_shader_data.pos[idx].w;
    grass_shader_data.pos[idx].set(position.x, position.y, position.z, saved_radius);
    grass_shader_data.anim[idx] = anim;
    grass_shader_data.id[idx] = id;
    grass_shader_data.radius[idx] = radius;
    grass_shader_data.fade[idx] = fade;
    grass_shader_data.speed[idx] = speed;
    grass_shader_data.dir[idx].set(dir.x, dir.y, dir.z, intensity);
    if (resetTime)
    {
        grass_shader_data.pos[idx].w = 0.01f;
        grass_shader_data.time[idx] = 0.f;
    }
}

float IGame_Persistent::GrassBenderToValue(float& current, const float go_to, const float intensity, const bool use_easing)
{
    float diff = abs(current - go_to);

    float r_value = Device.fTimeDelta * intensity * (use_easing ? std::min(0.5f, diff) : 1.0f);

    if (diff - r_value <= 0)
    {
        current = go_to;
        return 0;
    }

    return current < go_to ? r_value : -r_value;
}

bool IGame_Persistent::IsActorInHideout() const
{
    static bool actor_in_hideout = true;
    static u32 next_ray_pick_time = 0;

    if (Device.dwTimeGlobal >= next_ray_pick_time)
    {
        // Апдейт рейтрейса - 5 раз в секунду. Чаще апдейтить нет смысла.
        next_ray_pick_time = Device.dwTimeGlobal + 200;

        collide::rq_result RQ;
        actor_in_hideout =
            !!g_pGameLevel->ObjectSpace.RayPick(Device.vCameraPosition, Fvector{0.f, 1.f, 0.f}, 50.f, collide::rqtBoth, RQ, g_pGameLevel->CurrentViewEntity());
    }

    return actor_in_hideout;
}
