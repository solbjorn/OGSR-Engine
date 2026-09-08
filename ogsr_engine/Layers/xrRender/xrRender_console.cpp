#include "stdafx.h"

#include "xrRender_console.h"

#include "../../xr_3da/XR_IOConsole.h"
#include "../../xr_3da/xr_ioc_cmd.h"

#include "../xrRenderDX10/3DFluid/dx103DFluidManager.h"
#include "../xrRenderDX10/StateManager/dx10SamplerStateCache.h"

u32 r2_SmapSize = 2048;
u32 ps_r_sunshafts_mode = SS_COMBINED_MANOWAR;

namespace
{
constexpr xr_token SmapSizeToken[]{{"1536x1536", 1536},
                                   {"2048x2048", 2048},
                                   {"2560x2560", 2560},
                                   {"3072x3072", 3072},
                                   {"4096x4096", 4096},
                                   {"6144x6144", 6144},
                                   {"8192x8192", 8192},
                                   //{ "16384x16384", 16384 },
                                   {nullptr, 0}};

constexpr xr_token sunshafts_mode_token[]{{"st_opt_off", SS_OFF},
                                          {"volumetric", SS_VOLUMETRIC},
                                          {"ss_ogse", SS_SS_OGSE},
                                          {"ss_manowar", SS_SS_MANOWAR},
                                          {"combined_ogse", SS_COMBINED_OGSE},
                                          {"combined_manowar", SS_COMBINED_MANOWAR},
                                          {nullptr, 0}};
} // namespace

// Sunshafts
u32 ps_r_sun_shafts = 3;
float ps_r_ss_sunshafts_length = 0.9f; // 1.0f;
float ps_r_ss_sunshafts_radius = 2.0f; // 1.0f;
float ps_r_prop_ss_radius = 1.56f;
float ps_r_prop_ss_blend = 0.25f; // 0.066f;
float ps_r_prop_ss_sample_step_phase0 = 0.09f;
float ps_r_prop_ss_sample_step_phase1 = 0.07f;

u32 ps_r_ssao = 3;
u32 ps_smaa_quality = 3;
u32 ps_r_ao_mode = AO_MODE_SSDO;
u32 ps_r_sun_quality = 1; //	=	0;
u32 ps_r3_msaa = 0; //	=	0;

namespace
{
u32 ps_Preset = 2;

constexpr xr_token qpreset_token[]{{"Minimum", 0}, {"Low", 1}, {"Default", 2}, {"High", 3}, {"Extreme", 4}, {nullptr, 0}};
constexpr xr_token qssao_token[]{{"st_opt_off", 0}, {"st_opt_low", 1}, {"st_opt_medium", 2}, {"st_opt_high", 3}, {nullptr, 0}};
constexpr xr_token smaa_quality_token[]{{"st_opt_off", 0}, {"st_opt_low", 1}, {"st_opt_medium", 2}, {"st_opt_high", 3}, {"st_opt_ultra", 4}, {nullptr, 0}};
constexpr xr_token ao_mode_token[]{{"st_ssdo", AO_MODE_SSDO}, {"st_gtao", AO_MODE_GTAO}, {nullptr, 0}};
constexpr xr_token qsun_quality_token[]{{"st_opt_low", 0}, {"st_opt_medium", 1}, {"st_opt_high", 2}, {"st_opt_ultra", 3}, {"st_opt_extreme", 4}, {nullptr, 0}};
constexpr xr_token qmsaa_token[]{{"st_opt_off", 0}, {"2x", 1}, {"4x", 2}, {"8x", 3}, {nullptr, 0}};
} // namespace

// Common
int ps_r__LightSleepFrames = 10;

float ps_r__Detail_l_ambient = 0.9f;
float ps_r__Detail_l_aniso = 0.25f;

float ps_r__Tree_w_rot = 10.0f;
float ps_r__Tree_w_speed = 1.00f;
float ps_r__Tree_w_amp = 0.005f;
Fvector ps_r__Tree_Wave{0.1f, 0.01f, 0.11f};
float ps_r__Tree_SBC = 1.5f; // scale bias correct

float ps_r__WallmarkTTL = 50.0f;
float ps_r__WallmarkSHIFT = 0.0001f;
float ps_r__WallmarkSHIFT_V = 0.0001f;

float ps_r__GLOD_ssa_start = 256.0f;
float ps_r__GLOD_ssa_end = 64.0f;
float ps_r__LOD = 1.2f;

int ps_r__tf_Anisotropic = 16;
float ps_r__tf_Mipbias = -0.5f;

// R1
float ps_r1_ssaLOD_A = 64.0f;
float ps_r1_ssaLOD_B = 48.0f;
float ps_r1_lmodel_lerp = 0.1f;
float ps_r1_dlights_clip = 40.0f;
float ps_r1_pps_u = 0.0f;
float ps_r1_pps_v = 0.0f;

// R1-specific
int ps_r1_GlowsPerFrame = 16; // r1-only
float ps_r1_fog_luminance = 1.1f; // r1-only
int ps_r1_SoftwareSkinning = 0; // r1-only

// R2
float ps_r2_ssaLOD_A = 64.0f;
float ps_r2_ssaLOD_B = 48.0f;

// R2-specific
Flags32 ps_r2_ls_flags{R2FLAG_SUN | R2FLAG_MBLUR | R3FLAG_DYN_WET_SURF | R3FLAG_VOLUMETRIC_SMOKE | R2FLAG_DOF | R2FLAG_STEEP_PARALLAX | R2FLAG_SUN_FOCUS |
                       R2FLAG_SUN_TSM | R2FLAG_TONEMAP | R2FLAG_VOLUMETRIC_LIGHTS};

Flags32 ps_r2_ls_flags_ext{R2FLAGEXT_ENABLE_TESSELLATION | R2FLAGEXT_FONT_SHADOWS};

bool ps_no_scale_on_fade{false}; // Alundaio
float ps_r2_df_parallax_h = 0.02f;
float ps_r2_df_parallax_range = 75.0f;
float ps_r2_tonemap_middlegray = 1.0f; // r2-only
float ps_r2_tonemap_adaptation = 1.0f; // r2-only
float ps_r2_tonemap_low_lum = 0.4f; // r2-only
float ps_r2_tonemap_amount = 0.7f; // r2-only
float ps_r2_ls_bloom_kernel_g = 3.0f; // r2-only
float ps_r2_ls_bloom_kernel_b = 0.7f; // r2-only
float ps_r2_ls_bloom_speed = 100.0f; // r2-only
float ps_r2_ls_bloom_kernel_scale = 0.7f; // r2-only	// gauss
float ps_r2_ls_dsm_kernel = 0.7f; // r2-only
float ps_r2_ls_psm_kernel = 0.7f; // r2-only
float ps_r2_ls_ssm_kernel = 0.7f; // r2-only
float ps_r2_ls_bloom_threshold = 1.0f; // r2-only
float ps_r2_mblur = 0.0f; // 0.5f
float ps_r2_ls_depth_scale = 1.00001f; // 1.00001f

float ps_r2_ls_depth_bias = -0.001f;
float ps_r2_ls_squality = 1.0f; // Base shadow map quality. The higher the value the higher the base shadow resolution and resolution through distance.

float ps_r2_sun_tsm_projection = 0.3f; // 0.18f
float ps_r2_sun_tsm_bias = -0.01f; //
float ps_r2_sun_near = 20.0f; // 12.0f
float ps_r2_sun_near_border = 0.75f; // 1.0f
float ps_r2_sun_far = 100.0f;
float ps_r2_sun_depth_far_scale = 1.00000f; // 1.00001f
float ps_r2_sun_depth_far_bias = -0.00002f; // -0.0000f
float ps_r2_sun_depth_near_scale = 1.0000f; // 1.00001f
float ps_r2_sun_depth_near_bias = 0.00001f; // -0.00005f
float ps_r2_sun_lumscale = 1.0f; // 1.0f
float ps_r2_sun_lumscale_hemi = 1.0f; // 1.0f
float ps_r2_sun_lumscale_amb = 1.0f;

float ps_r2_dhemi_sky_scale = 0.08f; // 1.5f
float ps_r2_dhemi_light_scale = 0.2f;
float ps_r2_dhemi_light_flow = 0.1f;
int ps_r2_dhemi_count = 5; // 5
int ps_r2_wait_timeout = 500;

float ps_r2_lt_smooth = 1.0f; // 1.0f
float ps_r2_slight_fade = 2.0f; // 1.0f

///////lvutner
Fvector4 ps_r2_mask_control{0.0f, 0.0f, 0.0f, 0.0f}; // r2-only
Fvector ps_r2_drops_control{0.0f, 1.15f, 0.0f}; // r2-only

float ps_particle_update_coeff = 0.3f;

Fvector4 ps_r__color_gamma{0.0f, 0.0f, 0.0f, 1.0f};
Fvector4 ps_r__color_slope{0.0f, 0.0f, 0.0f, 1.0f};
Fvector4 ps_r__color_offset{0.0f, 0.0f, 0.0f, 0.0f};
Fvector4 ps_r__color_power{0.0f, 0.0f, 0.0f, 1.0f};
Fvector4 ps_r__color_saturation{0.0f, 0.0f, 0.0f, 1.0f};
Fvector4 ps_r__color_contrast{0.0f, 0.0f, 0.0f, 0.7f};
Fvector4 ps_r__color_red{1.0f, 0.0f, 0.0f, 0.0f};
Fvector4 ps_r__color_green{0.0f, 1.0f, 0.0f, 0.0f};
Fvector4 ps_r__color_blue{0.0f, 0.0f, 1.0f, 0.0f};

// Screen Space Shaders Stuff
bool ps_ssfx_pom_refine{false};
Fvector4 ps_ssfx_pom{16, 12, 0.035f, 0.4f}; // Samples , Range, Height, AO

bool ps_ssfx_terrain_grass_align{false}; // Grass align
float ps_ssfx_terrain_grass_slope = 1.0f; // Grass slope limit // Recommended 0.3f
Fvector4 ps_ssfx_terrain_pom{12, 20, 0.04f, 1.0f}; // Samples, Range, Height, Water Limit
bool ps_ssfx_terrain_pom_refine{false};

bool ps_ssfx_bloom_use_presets{false};
Fvector4 ps_ssfx_bloom_1{3.5f, 3.0f, 0.0f, 0.6f}; // Threshold, Exposure, -, Sky
Fvector4 ps_ssfx_bloom_2{3.0f, 1.5f, 1.5f, 1.0f}; // Blur Radius, Vibrance, Lens, Dirt

Fvector4 ps_ssfx_sss_quality{12.0f, 4.0f, 1.0f, 1.0f}; // Dir Samples, Omni Samples, Dir Enable, Omni Enable
Fvector4 ps_ssfx_sss{1.0f, 1.0f, 1.0f, 0.0f}; // Dir Len, Omni Len, Grass shadows, -

float ps_ssfx_hud_hemi = 0.15f; // HUD Hemi Offset

int ps_ssfx_il_quality = 32; // IL Samples
Fvector4 ps_ssfx_il{6.66f, 1.0f, 1.0f, 5.0f}; // Res, Int, Vibrance, Blur
Fvector4 ps_ssfx_il_setup1{150.0f, 1.0f, 0.5f, 0.0f}; // Distance, HUD, Flora, -

int ps_ssfx_ao_quality = 4; // AO Samples
Fvector4 ps_ssfx_ao{1.0f, 5.0f, 1.0f, 2.5f}; // Res, AO int, Blur, Radius
Fvector4 ps_ssfx_ao_setup1{150.0, 1.0, 1.0, 0.0}; // Distance, HUD, Flora, Max OCC

Fvector4 ps_ssfx_water{1.0f, 0.8f, 1.0f, 0.0f}; // Res, Blur, Blur Perlin, -
Fvector3 ps_ssfx_water_quality{1.0, 2.0, 0.0}; // SSR Quality, Parallax Quality, -
Fvector4 ps_ssfx_water_setup1{0.6f, 3.0f, 0.3f, 0.05f}; // Distortion, Turbidity, Softborder, Parallax Height
Fvector4 ps_ssfx_water_setup2{0.8f, 6.0f, 0.3f, 0.5f}; // Reflection, Specular, Caustics, Ripples

int ps_ssfx_ssr_quality = 0; // Quality
Fvector4 ps_ssfx_ssr{1.0f, 0.2f, 0.0f, 0.0f}; // Res, Blur, Temp, Noise
Fvector4 ps_ssfx_ssr_2{0.0f, 1.3f, 1.0f, 0.015f}; // Quality, Fade, Int, Wpn Int

Fvector4 ps_ssfx_terrain_quality{8, 0, 0, 0};
Fvector4 ps_ssfx_terrain_offset{0, 0, 0, 0};

Fvector3 ps_ssfx_shadows{256, 1536, 0.0f}; // LOD min res, LOD max res, -
Fvector4 ps_ssfx_volumetric{1.0f, 1.0f, 3.0f, 1.0f}; // Force Volumetric, Vol Intensity, Vol Quality, -

Fvector3 ps_ssfx_shadow_bias{0.4f, 0.03f, 0.0f};

Fvector4 ps_ssfx_lut{0.0f, 0.0f, 0.0f, 0.0f};

Fvector4 ps_ssfx_wind_grass{9.5f, 1.4f, 1.5f, 0.4f};
Fvector4 ps_ssfx_wind_trees{11.0f, 0.15f, 0.5f, 0.15f};

Fvector4 ps_ssfx_florafixes_1{0.3f, 0.21f, 0.3f, 0.21f}; // Flora fixes 1
Fvector4 ps_ssfx_florafixes_2{2.0f, 1.0f, 0.0f, 0.0f}; // Flora fixes 2

Fvector4 ps_ssfx_wetsurfaces_1{1.0f, 1.0f, 1.0f, 1.0f}; // Wet surfaces 1
Fvector4 ps_ssfx_wetsurfaces_2{1.0f, 1.0f, 1.0f, 1.0f}; // Wet surfaces 2

bool ps_ssfx_is_underground{false};
int ps_ssfx_gloss_method = 0;
float ps_ssfx_gloss_factor = 0.5f;
Fvector3 ps_ssfx_gloss_minmax{0.0f, 0.92f, 0.0f}; // Gloss

Fvector4 ps_ssfx_lightsetup_1{0.35f, 0.5f, 0.0f, 0.0f}; // Spec intensity

Fvector4 ps_ssfx_hud_drops_1{1.0f, 1.0f, 1.0f, 1.0f}; // Anim Speed, Int, Reflection, Refraction
Fvector4 ps_ssfx_hud_drops_2{1.5f, 0.85f, 0.0f, 2.0f}; // Density, Size, Extra Gloss, Gloss

Fvector4 ps_ssfx_blood_decals{0.6f, 0.6f, 0.0f, 0.0f};
Fvector4 ps_ssfx_rain_1{2.0f, 0.1f, 0.6f, 2.0f}; // Len, Width, Speed, Quality
Fvector4 ps_ssfx_rain_2{0.5f, 0.1f, 1.0f, 0.5f}; // Alpha, Brigthness, Refraction, Reflection
Fvector4 ps_ssfx_rain_3{0.5f, 1.0f, 0.0f, 0.0f}; // Alpha, Refraction ( Splashes )
Fvector4 ps_ssfx_rain_drops_setup{2500, 15, 0.0f, 0.0f};

Fvector3 ps_ssfx_shadow_cascades{20, 40, 160};
Fvector4 ps_ssfx_grass_shadows{0.0f, 0.35f, 30.0f, 0.0f};

Fvector4 ps_ssfx_grass_interactive{0.0f, 0.0f, 2000.0f, 1.0f};
Fvector4 ps_ssfx_int_grass_params_1{1.0f, 1.0f, 1.0f, 25.0f};
Fvector4 ps_ssfx_int_grass_params_2{1.0f, 5.0f, 1.0f, 1.0f};

Fvector4 ps_ssfx_wpn_dof_1{0.0f, 0.0f, 0.0f, 0.0f};
float ps_ssfx_wpn_dof_2 = 1.0f;

float ps_r3_dyn_wet_surf_near = 10.0f; // 10.0f
float ps_r3_dyn_wet_surf_far = 30.0f; // 30.0f
int ps_r3_dyn_wet_surf_sm_res = 256; // 256
Flags32 psDeviceFlags2;

namespace
{
int ps_r__detail_radius = 49;
}

u32 dm_size = 24;
u32 dm_cache1_line = 12; // dm_size*2/dm_cache1_count
u32 dm_cache_line = 49; // dm_size+1+dm_size
u32 dm_cache_size = 2401; // dm_cache_line*dm_cache_line
float dm_fade = 47.5; // float(2*dm_size)-.5f;
u32 dm_current_size = 24;
u32 dm_current_cache1_line = 12; // dm_current_size*2/dm_cache1_count
u32 dm_current_cache_line = 49; // dm_current_size+1+dm_current_size
u32 dm_current_cache_size = 2401; // dm_current_cache_line*dm_current_cache_line
float dm_current_fade = 47.5; // float(2*dm_current_size)-.5f;
float ps_current_detail_density = 0.6f;
float ps_current_detail_scale = 1.0f;

//- Mad Max
float ps_r2_gloss_factor = 4.0f;
float ps_r2_gloss_min = 0.0f;
//- Mad Max

// textures
#ifdef DEBUG
u32 psCurrentBPP = 32;
#endif

int opt_static = 2;
int opt_dynamic = 2;

namespace
{
class CCC_r__color final : public CCC_Vector4
{
    RTTI_DECLARE_TYPEINFO(CCC_r__color, CCC_Vector4);

public:
    explicit CCC_r__color(gsl::czstring N, Fvector4& V, Fvector4 _min, Fvector4 _max) : CCC_Vector4{N, V, _min, _max} {}
    ~CCC_r__color() override = default;

    void Execute(std::string_view args) override
    {
        const auto res = scn::scan_value<Fvector4>(args);
        if (!res)
        {
            InvalidSyntax(res.error().msg(), args);
            return;
        }

        const auto& v = res->value();
        if (v.w < min.w || v.x + v.w < min.x || v.y + v.w < min.y || v.z + v.w < min.z || v.w > max.w || v.x + v.w > max.x || v.y + v.w > max.y ||
            v.z + v.w > max.z)
        {
            InvalidSyntax("value(s) out of bounds", args);
            return;
        }

        value.set(v);
    }
};

//-----------------------------------------------------------------------

class CCC_detail_radius final : public CCC_Integer
{
    RTTI_DECLARE_TYPEINFO(CCC_detail_radius, CCC_Integer);

public:
    void apply()
    {
        dm_current_size = iFloor((float)ps_r__detail_radius / 4) * 2;
        dm_current_cache1_line = dm_current_size * 2 / 4; // assuming cache1_count = 4
        dm_current_cache_line = dm_current_size + 1 + dm_current_size;
        dm_current_cache_size = dm_current_cache_line * dm_current_cache_line;
        dm_current_fade = float(2 * dm_current_size) - 0.5f;
    }

    explicit CCC_detail_radius(gsl::czstring N, s32& V, s32 _min = 0, s32 _max = 999) : CCC_Integer{N, V, _min, _max} {}
    ~CCC_detail_radius() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Integer::Execute(args);
        apply();
    }
};

class CCC_detail_reset final : public CCC_Float
{
    RTTI_DECLARE_TYPEINFO(CCC_detail_reset, CCC_Float);

public:
    void apply()
    {
        if (RImplementation.Details)
            RImplementation.Details->need_init = true;
    }

    explicit CCC_detail_reset(gsl::czstring N, f32& V, f32 _min = 0.0f, f32 _max = 1.0f) : CCC_Float{N, V, _min, _max} {}
    ~CCC_detail_reset() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Float::Execute(args);
        apply();
    }
};

class CCC_tf_Aniso final : public CCC_Integer
{
    RTTI_DECLARE_TYPEINFO(CCC_tf_Aniso, CCC_Integer);

public:
    void apply()
    {
        if (!HW.pDevice)
            return;

        SSManager.SetMaxAnisotropy(std::clamp(value, 1, 16));
    }

    explicit CCC_tf_Aniso(gsl::czstring N, s32& v) : CCC_Integer{N, v, 1, 16} {}
    ~CCC_tf_Aniso() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Integer::Execute(args);
        apply();
    }
};

class CCC_tf_MipBias final : public CCC_Float
{
    RTTI_DECLARE_TYPEINFO(CCC_tf_MipBias, CCC_Float);

public:
    void apply()
    {
        if (!HW.pDevice)
            return;

        SSManager.SetMipLODBias(value);
    }

    explicit CCC_tf_MipBias(gsl::czstring N, f32& v) : CCC_Float{N, v, -3.0f, 3.0f} {}
    ~CCC_tf_MipBias() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Float::Execute(args);
        apply();
    }
};

class CCC_Screenshot final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Screenshot, IConsole_Command);

public:
    explicit CCC_Screenshot(LPCSTR N) : IConsole_Command{N} {}
    ~CCC_Screenshot() override = default;

    void Execute(std::string_view args) override
    {
        gsl::czstring image{};

        const auto res = scn::scan_value<xr_string>(args);
        if (res && !res->value().empty())
            image = res->value().c_str();

        RImplementation.Screenshot(IRender_interface::SM_NORMAL, image);
    }
};

#ifdef DEBUG
class CCC_ModelPoolStat final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_ModelPoolStat, IConsole_Command);

public:
    explicit CCC_ModelPoolStat(LPCSTR N) : IConsole_Command{N, true} {}
    ~CCC_ModelPoolStat() override = default;

    void Execute(std::string_view) override { RImplementation.Models->dump(); }
};
#endif

//-----------------------------------------------------------------------

class CCC_Preset final : public CCC_Token
{
    RTTI_DECLARE_TYPEINFO(CCC_Preset, CCC_Token);

public:
    explicit CCC_Preset(gsl::czstring N, u32& V, const xr_token* T) : CCC_Token{N, V, T} {}
    ~CCC_Preset() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Token::Execute(args);
        string_path _cfg;

        switch (value)
        {
        case 0: xr_strcpy(_cfg, "rspec_minimum.ltx"); break;
        case 1: xr_strcpy(_cfg, "rspec_low.ltx"); break;
        case 2: xr_strcpy(_cfg, "rspec_default.ltx"); break;
        case 3: xr_strcpy(_cfg, "rspec_high.ltx"); break;
        case 4: xr_strcpy(_cfg, "rspec_extreme.ltx"); break;
        }

        std::ignore = FS.update_path(_cfg, "$game_config$", _cfg);
        Console->Execute(xr::format("cfg_load {}", _cfg).c_str());
    }
};

class CCC_VideoMemoryStats final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_VideoMemoryStats, IConsole_Command);

public:
    explicit CCC_VideoMemoryStats(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_VideoMemoryStats() override = default;

    void Execute(std::string_view) override
    {
        XR_LOG_INFO("memory usage  mb \t \t video    \t managed      \t system");

        float vb_video = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_DEFAULT] / 1024 / 1024;
        float vb_managed = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_MANAGED] / 1024 / 1024;
        float vb_system = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex][D3DPOOL_SYSTEMMEM] / 1024 / 1024;
        Msg("vertex buffer\t \t {} \t {} \t {} ", vb_video, vb_managed, vb_system);

        float ib_video = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_DEFAULT] / 1024 / 1024;
        float ib_managed = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_MANAGED] / 1024 / 1024;
        float ib_system = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_index][D3DPOOL_SYSTEMMEM] / 1024 / 1024;
        Msg("index buffer\t \t {} \t {} \t {} ", ib_video, ib_managed, ib_system);

        float rt_video = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_DEFAULT] / 1024 / 1024;
        float rt_managed = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_MANAGED] / 1024 / 1024;
        float rt_system = (float)HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget][D3DPOOL_SYSTEMMEM] / 1024 / 1024;
        Msg("rtarget\t \t {} \t {} \t {} ", rt_video, rt_managed, rt_system);

        Msg("total\t \t {} \t {} \t {} \n", vb_video + ib_video + rt_video, vb_managed + ib_managed + rt_managed, vb_system + ib_system + rt_system);

        xr::render_memory_usage usage;
        RImplementation.ResourcesGetMemoryUsage(usage);

        Msg("textures loaded size: {} Mb ({} bytes)", gsl::narrow_cast<f32>(usage.m_base + usage.m_lmaps) / 1024.0f / 1024.0f, usage.m_base + usage.m_lmaps);
        Msg("Lua memory usage: {} Mb ({} bytes)", gsl::narrow_cast<f32>(usage.lua) / 1024.0f / 1024.0f, usage.lua);

        HW.DumpVideoMemoryUsage();
    }
};

#ifdef DEBUG
class CCC_DumpResources final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_DumpResources, IConsole_Command);

public:
    explicit CCC_DumpResources(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_DumpResources() override = default;

    void Execute(std::string_view) override
    {
        RImplementation.Models->dump();
        RImplementation.Resources->Dump(false);
    }
};

//	Allow real-time fog config reload
class CCC_Fog_Reload final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Fog_Reload, IConsole_Command);

public:
    explicit CCC_Fog_Reload(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_Fog_Reload() override = default;

    void Execute(std::string_view) override { FluidManager.UpdateProfiles(); }
};
#endif //	DEBUG

class CCC_PART_Export final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_PART_Export, IConsole_Command);

public:
    explicit CCC_PART_Export(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_PART_Export() override = default;

    void Execute(std::string_view args) override
    {
        if (g_pGameLevel != nullptr)
        {
            InvalidSyntax("unload the level to execute", args);
            return;
        }

        if (args != "0" && args != "1")
        {
            InvalidSyntax("not a boolean", args);
            return;
        }

        XR_LOG_NOTICE("Exporting particles...");

        RImplementation.PSLibrary.Reload();
        RImplementation.PSLibrary.Save2(args == "1");

        XR_LOG_NOTICE("Exporting particles Done!");
    }
};

class CCC_PART_Import final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_PART_Import, IConsole_Command);

public:
    explicit CCC_PART_Import(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_PART_Import() override = default;

    void Execute(std::string_view) override
    {
        if (g_pGameLevel != nullptr)
        {
            InvalidSyntax("unload the level to execute", "(no arguments)");
            return;
        }

        XR_LOG_NOTICE("Importing particles...");

        RImplementation.PSLibrary.OnDestroy();
        RImplementation.PSLibrary.Load2();
        RImplementation.PSLibrary.ExportAllAsNew();
        RImplementation.PSLibrary.OnCreate();

        XR_LOG_NOTICE("Importing particles Done!");
    }
};
} // namespace

void xrRender_initconsole()
{
    if (!FS.path_exist("$game_weathers$"))
        ps_r2_gloss_factor = 1.0f;

    XR_CMD(CCC_Preset, "_preset", ps_Preset, qpreset_token);

    XR_CMD(CCC_Integer, "rs_skeleton_update", psSkeletonUpdate, 2, 128);
#ifdef DEBUG
    XR_CMD(CCC_DumpResources, "dump_resources");
#endif //	 DEBUG

    XR_CMD(CCC_Float, "r__dtex_range", r__dtex_range, 5.0f, 175.0f);

    // Common
    XR_CMD(CCC_Screenshot, "screenshot");

#ifdef DEBUG
    XR_CMD(CCC_BuildSSA, "build_ssa");

    XR_CMD(CCC_Integer, "r__lsleep_frames", ps_r__LightSleepFrames, 4, 30);
    XR_CMD(CCC_Float, "r__ssa_glod_start", ps_r__GLOD_ssa_start, 128.0f, 512.0f);
    XR_CMD(CCC_Float, "r__ssa_glod_end", ps_r__GLOD_ssa_end, 16.0f, 96.0f);
    XR_CMD(CCC_Float, "r__wallmark_shift_pp", ps_r__WallmarkSHIFT, 0.0f, 1.0f);
    XR_CMD(CCC_Float, "r__wallmark_shift_v", ps_r__WallmarkSHIFT_V, 0.0f, 1.0f);
    XR_CMD(CCC_ModelPoolStat, "stat_models");
    XR_CMD(CCC_Token, "vid_bpp", psCurrentBPP, vid_bpp_token);
#endif // DEBUG
    XR_CMD(CCC_Float, "r__wallmark_ttl", ps_r__WallmarkTTL, 1.0f, 10.0f * 60.0f);

    XR_CMD(CCC_Float, "r__geometry_lod", ps_r__LOD, 1.0f, 3.0f); // AVO: extended from 1.2f to 3.0f
    //.	XR_CMD(CCC_Float,		"r__geometry_lod_pow",	&ps_r__LOD_Power,			0,		2		);

    XR_CMD(CCC_detail_reset, "r__detail_density", ps_current_detail_density, 0.04f, 1.0f);
    XR_CMD(CCC_detail_reset, "r__detail_scale", ps_current_detail_scale, 0.2f, 3.0f);

#ifdef DEBUG
    XR_CMD(CCC_Float, "r__detail_l_ambient", ps_r__Detail_l_ambient, 0.5f, 0.95f);
    XR_CMD(CCC_Float, "r__detail_l_aniso", ps_r__Detail_l_aniso, 0.1f, 0.5f);

    XR_CMD(CCC_Float, "r__d_tree_w_amp", ps_r__Tree_w_amp, 0.001f, 1.0f);
    XR_CMD(CCC_Float, "r__d_tree_w_rot", ps_r__Tree_w_rot, 0.01f, 100.0f);
    XR_CMD(CCC_Float, "r__d_tree_w_speed", ps_r__Tree_w_speed, 1.0f, 10.0f);

    tw_min.set(EPS, EPS, EPS);
    tw_max.set(2.0f, 2.0f, 2.0f);
    XR_CMD(CCC_Vector3, "r__d_tree_wave", ps_r__Tree_Wave, tw_min, tw_max);
#endif // DEBUG

    XR_CMD(CCC_tf_Aniso, "r__tf_aniso", ps_r__tf_Anisotropic); //	{1..16}
    XR_CMD(CCC_tf_MipBias, "r__tf_mipbias", ps_r__tf_Mipbias); // {-3 +3}

    XR_CMD(CCC_Mask, "r__actor_shadow", ps_r2_ls_flags_ext, R2FLAGEXT_ACTOR_SHADOW);

    XR_CMD(CCC_Float, "r1_pps_u", ps_r1_pps_u, -1.0f, 1.0f);
    XR_CMD(CCC_Float, "r1_pps_v", ps_r1_pps_v, -1.0f, 1.0f);

    // R2
    XR_CMD(CCC_Float, "r2_ssa_lod_a", ps_r2_ssaLOD_A, 16.0f, 96.0f);
    XR_CMD(CCC_Float, "r2_ssa_lod_b", ps_r2_ssaLOD_B, 32.0f, 64.0f);

    // R2-specific
    XR_CMD(CCC_Mask, "r2_tonemap", ps_r2_ls_flags, R2FLAG_TONEMAP);
    XR_CMD(CCC_Float, "r2_tonemap_middlegray", ps_r2_tonemap_middlegray, 0.0f, 2.0f);
    XR_CMD(CCC_Float, "r2_tonemap_adaptation", ps_r2_tonemap_adaptation, 0.01f, 10.0f);
    XR_CMD(CCC_Float, "r2_tonemap_lowlum", ps_r2_tonemap_low_lum, 0.0001f, 1.0f);
    XR_CMD(CCC_Float, "r2_tonemap_amount", ps_r2_tonemap_amount, 0.0000f, 1.0f);
    XR_CMD(CCC_Float, "r2_ls_bloom_kernel_scale", ps_r2_ls_bloom_kernel_scale, 0.05f, 2.0f);
    XR_CMD(CCC_Float, "r2_ls_bloom_kernel_g", ps_r2_ls_bloom_kernel_g, 1.0f, 7.0f);
    XR_CMD(CCC_Float, "r2_ls_bloom_kernel_b", ps_r2_ls_bloom_kernel_b, 0.01f, 1.0f);
    XR_CMD(CCC_Float, "r2_ls_bloom_threshold", ps_r2_ls_bloom_threshold, 0.0f, 1.0f);
    XR_CMD(CCC_Float, "r2_ls_bloom_speed", ps_r2_ls_bloom_speed, 0.0f, 100.0f);
    XR_CMD(CCC_Float, "r2_ls_dsm_kernel", ps_r2_ls_dsm_kernel, 0.1f, 3.0f);
    XR_CMD(CCC_Float, "r2_ls_psm_kernel", ps_r2_ls_psm_kernel, 0.1f, 3.0f);
    XR_CMD(CCC_Float, "r2_ls_ssm_kernel", ps_r2_ls_ssm_kernel, 0.1f, 3.0f);
    XR_CMD(CCC_Float, "r2_ls_squality", ps_r2_ls_squality, 0.5f, 3.0f);

    //- Mad Max
    XR_CMD(CCC_Float, "r2_gloss_factor", ps_r2_gloss_factor, 0.001f, 10.0f);
    XR_CMD(CCC_Float, "r2_gloss_min", ps_r2_gloss_min, 0.001f, 1.0f);
    //- Mad Max

    XR_CMD(CCC_Mask, "r2_disable_hom", ps_r2_ls_flags_ext, R2FLAGEXT_DISABLE_HOM);
    XR_CMD(CCC_Mask, "r_font_shadows", ps_r2_ls_flags_ext, R2FLAGEXT_FONT_SHADOWS);

    XR_CMD(CCC_Mask, "r2_sun", ps_r2_ls_flags, R2FLAG_SUN);
    XR_CMD(CCC_Mask, "r2_sun_details", ps_r2_ls_flags, R2FLAG_SUN_DETAILS);
    XR_CMD(CCC_Mask, "r2_sun_focus", ps_r2_ls_flags, R2FLAG_SUN_FOCUS);
    XR_CMD(CCC_Mask, "r2_exp_donttest_shad", ps_r2_ls_flags, R2FLAG_EXP_DONT_TEST_SHADOWED);

    XR_CMD(CCC_Mask, "r2_sun_tsm", ps_r2_ls_flags, R2FLAG_SUN_TSM);
    XR_CMD(CCC_Float, "r2_sun_tsm_proj", ps_r2_sun_tsm_projection, 0.001f, 0.8f);
    XR_CMD(CCC_Float, "r2_sun_tsm_bias", ps_r2_sun_tsm_bias, -0.5f, 0.5f);

    XR_CMD(CCC_Token, "r__smap_size", r2_SmapSize, SmapSizeToken);

    XR_CMD(CCC_Float, "r2_sun_near", ps_r2_sun_near, 1.0f, 150.0f);
    XR_CMD(CCC_Float, "r2_sun_far", ps_r2_sun_far, 51.0f, 180.0f);
    XR_CMD(CCC_Float, "r2_sun_near_border", ps_r2_sun_near_border, 0.5f, 3.0f);
    XR_CMD(CCC_Float, "r2_sun_depth_far_scale", ps_r2_sun_depth_far_scale, 0.5f, 1.5f);
    XR_CMD(CCC_Float, "r2_sun_depth_far_bias", ps_r2_sun_depth_far_bias, -0.5f, 0.5f);
    XR_CMD(CCC_Float, "r2_sun_depth_near_scale", ps_r2_sun_depth_near_scale, 0.5f, 1.5f);
    XR_CMD(CCC_Float, "r2_sun_depth_near_bias", ps_r2_sun_depth_near_bias, -0.5f, 0.5f);
    XR_CMD(CCC_Float, "r2_sun_lumscale", ps_r2_sun_lumscale, -1.0f, 3.0f);
    XR_CMD(CCC_Float, "r2_sun_lumscale_hemi", ps_r2_sun_lumscale_hemi, 0.0f, 3.0f);
    XR_CMD(CCC_Float, "r2_sun_lumscale_amb", ps_r2_sun_lumscale_amb, 0.0f, 3.0f);

    XR_CMD(CCC_Float, "r2_mblur", ps_r2_mblur, 0.0f, 1.0f);

    ///////lvutner
    XR_CMD(CCC_Vector4, "r2_mask_control", ps_r2_mask_control, Fvector4{}, Fvector4{10.0f, 3.0f, 1.0f, 1.0f});

    Fvector3 tw_min{};
    Fvector3 tw_max{1.0f, 2.0f, 1.0f};
    XR_CMD(CCC_Vector3, "r2_drops_control", ps_r2_drops_control, tw_min, tw_max);

    Fvector4 tw2_min{0.5f, 0.5f, 0.5f, -2.0f};
    Fvector4 tw2_max{2.0f, 2.0f, 2.0f, 2.0f};

    XR_CMD(CCC_r__color, "r__color_gamma", ps_r__color_gamma, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_slope", ps_r__color_slope, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_saturation", ps_r__color_saturation, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_contrast", ps_r__color_contrast, tw2_min, tw2_max);

    tw2_min.set(0.0f, 0.0f, 0.0f, -2.0f);

    XR_CMD(CCC_r__color, "r__color_offset", ps_r__color_offset, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_power", ps_r__color_power, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_red", ps_r__color_red, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_green", ps_r__color_green, tw2_min, tw2_max);
    XR_CMD(CCC_r__color, "r__color_blue", ps_r__color_blue, tw2_min, tw2_max);

    XR_CMD(CCC_Mask, "r2_terrain_z_prepass", ps_r2_ls_flags, R2FLAG_TERRAIN_PREPASS); // Terrain Z Prepass @Zagolski

    //////////other
    XR_CMD(CCC_Mask, "r2_mblur_enabled", ps_r2_ls_flags, R2FLAG_MBLUR);
    XR_CMD(CCC_Token, "r2_smaa", ps_smaa_quality, smaa_quality_token);

    // Shader param stuff
    tw2_max.set(100.0f, 100.0f, 100.0f, 100.0f);

    // Screen Space Shaders
    XR_CMD(CCC_Bool, "ssfx_pom_refine", ps_ssfx_pom_refine);
    XR_CMD(CCC_Vector4, "ssfx_pom", ps_ssfx_pom, Fvector4{}, Fvector4{36.0f, 60.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Bool, "ssfx_terrain_pom_refine", ps_ssfx_terrain_pom_refine);
    XR_CMD(CCC_Bool, "ssfx_terrain_grass_align", ps_ssfx_terrain_grass_align);
    XR_CMD(CCC_Float, "ssfx_terrain_grass_slope", ps_ssfx_terrain_grass_slope, 0.0f, 1.0f);
    XR_CMD(CCC_Vector4, "ssfx_terrain_pom", ps_ssfx_terrain_pom, Fvector4{}, Fvector4{36.0f, 60.0f, 1.0f, 2.0f});

    XR_CMD(CCC_Bool, "ssfx_bloom_use_presets", ps_ssfx_bloom_use_presets);
    XR_CMD(CCC_Vector4, "ssfx_bloom_1", ps_ssfx_bloom_1, Fvector4{1.0f, 1.0f, 0.0f, 0.0f}, Fvector4{10.0f, 100.0f, 100.0f, 10.0f});
    XR_CMD(CCC_Vector4, "ssfx_bloom_2", ps_ssfx_bloom_2, Fvector4{1.0f, 0.0f, 0.0f, 0.0f}, Fvector4{5.0f, 10.0f, 10.0f, 10.0f});
    XR_CMD(CCC_Vector4, "ssfx_sss_quality", ps_ssfx_sss_quality, Fvector4{1.0f, 1.0f, 0.0f, 0.0f}, Fvector4{24.0f, 12.0f, 1.0f, 1.0f});
    XR_CMD(CCC_Vector4, "ssfx_sss", ps_ssfx_sss, Fvector4{}, Fvector4{3.0f, 3.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Float, "ssfx_hud_hemi", ps_ssfx_hud_hemi, 0.0f, 1.0f);

    XR_CMD(CCC_Integer, "ssfx_il_quality", ps_ssfx_il_quality, 16, 64);
    XR_CMD(CCC_Vector4, "ssfx_il", ps_ssfx_il, Fvector4{}, Fvector4{8.0f, 10.0f, 3.0f, 6.0f});
    XR_CMD(CCC_Vector4, "ssfx_il_setup1", ps_ssfx_il_setup1, Fvector4{}, Fvector4{300.0f, 1.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Integer, "ssfx_ao_quality", ps_ssfx_ao_quality, 2, 8);
    XR_CMD(CCC_Vector4, "ssfx_ao", ps_ssfx_ao, Fvector4{}, Fvector4{8.0f, 10.0f, 1.0f, 10.0f});
    XR_CMD(CCC_Vector4, "ssfx_ao_setup1", ps_ssfx_ao_setup1, Fvector4{}, Fvector4{300.0f, 1.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Vector4, "ssfx_water", ps_ssfx_water, Fvector4{1.0f, 0.0f, 0.0f, 0.0f}, Fvector4{8.0f, 1.0f, 1.0f, 0.0f});
    XR_CMD(CCC_Vector3, "ssfx_water_quality", ps_ssfx_water_quality, Fvector3{}, Fvector3{4.0f, 3.0f, 0.0f});
    XR_CMD(CCC_Vector4, "ssfx_water_setup1", ps_ssfx_water_setup1, Fvector4{}, Fvector4{2.0f, 10.0f, 1.0f, 0.1f});
    XR_CMD(CCC_Vector4, "ssfx_water_setup2", ps_ssfx_water_setup2, Fvector4{}, Fvector4{1.0f, 10.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Integer, "ssfx_ssr_quality", ps_ssfx_ssr_quality, 0, 5);
    XR_CMD(CCC_Vector4, "ssfx_ssr", ps_ssfx_ssr, Fvector4{1.0f, 0.0f, 0.0f, 0.0f}, Fvector4{2.0f, 1.0f, 1.0f, 1.0f});
    XR_CMD(CCC_Vector4, "ssfx_ssr_2", ps_ssfx_ssr_2, Fvector4{}, Fvector4{2.0f, 2.0f, 2.0f, 2.0f});

    XR_CMD(CCC_Vector4, "ssfx_terrain_quality", ps_ssfx_terrain_quality, Fvector4{}, Fvector4{40.0f, 0.0f, 0.0f, 0.0f});
    XR_CMD(CCC_Vector4, "ssfx_terrain_offset", ps_ssfx_terrain_offset, Fvector4{-1.0f, -1.0f, -1.0f, -1.0f}, Fvector4{1.0f, 1.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Vector3, "ssfx_shadows", ps_ssfx_shadows, Fvector3{128.0f, 1536.0f, 0.0f}, Fvector3{1536.0f, 4096.0f, 0.0f});
    XR_CMD(CCC_Vector4, "ssfx_volumetric", ps_ssfx_volumetric, Fvector4{0.0f, 0.0f, 1.0f, 1.0f}, Fvector4{1.0f, 5.0f, 5.0f, 16.0f});

    XR_CMD(CCC_Vector3, "ssfx_shadow_bias", ps_ssfx_shadow_bias, Fvector3{}, Fvector3{1.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Vector4, "ssfx_lut", ps_ssfx_lut, Fvector4{}, tw2_max);

    XR_CMD(CCC_Vector4, "ssfx_wind_grass", ps_ssfx_wind_grass, Fvector4{}, Fvector4{20.0f, 5.0f, 5.0f, 5.0f});
    XR_CMD(CCC_Vector4, "ssfx_wind_trees", ps_ssfx_wind_trees, Fvector4{}, Fvector4{20.0f, 5.0f, 5.0f, 1.0f});

    XR_CMD(CCC_Vector4, "ssfx_florafixes_1", ps_ssfx_florafixes_1, Fvector4{}, Fvector4{1.0f, 1.0f, 1.0f, 1.0f});
    XR_CMD(CCC_Vector4, "ssfx_florafixes_2", ps_ssfx_florafixes_2, Fvector4{}, Fvector4{10.0f, 1.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Vector4, "ssfx_wetsurfaces_1", ps_ssfx_wetsurfaces_1, Fvector4{0.01f, 0.01f, 0.01f, 0.01f}, Fvector4{2.0f, 2.0f, 2.0f, 2.0f});
    XR_CMD(CCC_Vector4, "ssfx_wetsurfaces_2", ps_ssfx_wetsurfaces_2, Fvector4{0.01f, 0.01f, 0.01f, 0.01f}, Fvector4{2.0f, 2.0f, 2.0f, 2.0f});

    XR_CMD(CCC_Bool, "ssfx_is_underground", ps_ssfx_is_underground);
    XR_CMD(CCC_Integer, "ssfx_gloss_method", ps_ssfx_gloss_method, 0, 1);
    XR_CMD(CCC_Vector3, "ssfx_gloss_minmax", ps_ssfx_gloss_minmax, Fvector3{}, Fvector3{1.0f, 1.0f, 1.0f});
    XR_CMD(CCC_Float, "ssfx_gloss_factor", ps_ssfx_gloss_factor, 0.0f, 1.0f);

    XR_CMD(CCC_Vector4, "ssfx_lightsetup_1", ps_ssfx_lightsetup_1, Fvector4{}, Fvector4{1.0f, 1.0f, 1.0f, 1.0f});

    XR_CMD(CCC_Vector4, "ssfx_hud_drops_1", ps_ssfx_hud_drops_1, Fvector4{}, Fvector4{100000.0f, 100.0f, 100.0f, 100.0f});
    XR_CMD(CCC_Vector4, "ssfx_hud_drops_2", ps_ssfx_hud_drops_2, Fvector4{}, tw2_max);

    XR_CMD(CCC_Vector4, "ssfx_blood_decals", ps_ssfx_blood_decals, Fvector4{}, Fvector4{5.0f, 5.0f, 0.0f, 0.0f});

    XR_CMD(CCC_Vector4, "ssfx_rain_drops_setup", ps_ssfx_rain_drops_setup, Fvector4{1000.0f, 10.0f, 0.0f, 0.0f}, Fvector4{5000.0f, 30.0f, 0.0f, 0.0f});
    XR_CMD(CCC_Vector4, "ssfx_rain_1", ps_ssfx_rain_1, Fvector4{}, Fvector4{10.0f, 5.0f, 5.0f, 2.0f});
    XR_CMD(CCC_Vector4, "ssfx_rain_2", ps_ssfx_rain_2, Fvector4{}, Fvector4{1.0f, 10.0f, 10.0f, 10.0f});
    XR_CMD(CCC_Vector4, "ssfx_rain_3", ps_ssfx_rain_3, Fvector4{}, Fvector4{1.0f, 10.0f, 10.0f, 10.0f});

    XR_CMD(CCC_Vector4, "ssfx_grass_shadows", ps_ssfx_grass_shadows, Fvector4{}, Fvector4{3.0f, 1.0f, 100.0f, 100.0f});
    XR_CMD(CCC_Vector3, "ssfx_shadow_cascades", ps_ssfx_shadow_cascades, Fvector3{1.0f, 1.0f, 1.0f}, Fvector3{300.0f, 300.0f, 300.0f});

    XR_CMD(CCC_Vector4, "ssfx_grass_interactive", ps_ssfx_grass_interactive, Fvector4{}, Fvector4{1.0f, 15.0f, 5000.0f, 1.0f});
    XR_CMD(CCC_Vector4, "ssfx_int_grass_params_1", ps_ssfx_int_grass_params_1, Fvector4{}, Fvector4{5.0f, 5.0f, 5.0f, 60.0f});
    XR_CMD(CCC_Vector4, "ssfx_int_grass_params_2", ps_ssfx_int_grass_params_2, Fvector4{}, Fvector4{5.0f, 20.0f, 1.0f, 5.0f});

    XR_CMD(CCC_Vector4, "ssfx_wpn_dof_1", ps_ssfx_wpn_dof_1, tw2_min, tw2_max);
    XR_CMD(CCC_Float, "ssfx_wpn_dof_2", ps_ssfx_wpn_dof_2, 0.0f, 1.0f);

    // Geometry optimization
    XR_CMD(CCC_Integer, "r__optimize_static_geom", opt_static, 0, 4);
    XR_CMD(CCC_Integer, "r__optimize_dynamic_geom", opt_dynamic, 0, 4);
    psDeviceFlags2.set(rsOptShadowGeom, TRUE);
    XR_CMD(CCC_Mask, "r__optimize_shadow_geom", psDeviceFlags2, rsOptShadowGeom);

    XR_CMD(CCC_Integer, "r2_wait_timeout", ps_r2_wait_timeout, 100, 1000);

#ifndef MASTER_GOLD
    XR_CMD(CCC_Integer, "r2_dhemi_count", ps_r2_dhemi_count, 4, 25);
    XR_CMD(CCC_Float, "r2_dhemi_sky_scale", ps_r2_dhemi_sky_scale, 0.0f, 100.0f);
    XR_CMD(CCC_Float, "r2_dhemi_light_scale", ps_r2_dhemi_light_scale, 0.0f, 100.0f);
    XR_CMD(CCC_Float, "r2_dhemi_light_flow", ps_r2_dhemi_light_flow, 0.0f, 1.0f);
    XR_CMD(CCC_Float, "r2_dhemi_smooth", ps_r2_lt_smooth, 0.0f, 10.0f);
    XR_CMD(CCC_Mask, "rs_hom_depth_draw", ps_r2_ls_flags_ext, R_FLAGEXT_HOM_DEPTH_DRAW);
#endif // DEBUG

    XR_CMD(CCC_Mask, "r2_shadow_cascede_zcul", ps_r2_ls_flags_ext, R2FLAGEXT_SUN_ZCULLING);

    XR_CMD(CCC_Float, "r2_ls_depth_scale", ps_r2_ls_depth_scale, 0.5f, 1.5f);
    XR_CMD(CCC_Float, "r2_ls_depth_bias", ps_r2_ls_depth_bias, -0.5f, 0.5f);

    XR_CMD(CCC_Float, "r2_parallax_h", ps_r2_df_parallax_h, 0.0f, 0.5f);
    XR_CMD(CCC_Float, "r2_slight_fade", ps_r2_slight_fade, 0.2f, 2.0f);

    XR_CMD(CCC_Mask, "r2_dof_enable", ps_r2_ls_flags, R2FLAG_DOF);
    XR_CMD(CCC_Mask, "r2_volumetric_lights", ps_r2_ls_flags, R2FLAG_VOLUMETRIC_LIGHTS);

    // Sunshafts
    XR_CMD(CCC_Token, "r_sunshafts_mode", ps_r_sunshafts_mode, sunshafts_mode_token);
    XR_CMD(CCC_Float, "r_ss_sunshafts_length", ps_r_ss_sunshafts_length, 0.2f, 1.5f);
    XR_CMD(CCC_Float, "r_ss_sunshafts_radius", ps_r_ss_sunshafts_radius, 0.5f, 2.0f);

    XR_CMD(CCC_Float, "r_SunShafts_Radius", ps_r_prop_ss_radius, 0.5f, 2.0f);
    XR_CMD(CCC_Float, "r_SunShafts_Blend", ps_r_prop_ss_blend, 0.01f, 1.0f);

    XR_CMD(CCC_Token, "r_ao_mode", ps_r_ao_mode, ao_mode_token);
    XR_CMD(CCC_Token, "r2_ssao", ps_r_ssao, qssao_token);

    XR_CMD(CCC_Mask, "r4_enable_tessellation", ps_r2_ls_flags_ext, R2FLAGEXT_ENABLE_TESSELLATION); // Need restart
    XR_CMD(CCC_Mask, "r4_wireframe", ps_r2_ls_flags_ext, R2FLAGEXT_WIREFRAME); // Need restart
    XR_CMD(CCC_Mask, "r2_steep_parallax", ps_r2_ls_flags, R2FLAG_STEEP_PARALLAX);

    XR_CMD(CCC_Token, "r2_sun_quality", ps_r_sun_quality, qsun_quality_token);

    XR_CMD(CCC_Token, "r3_msaa", ps_r3_msaa, qmsaa_token);

    XR_CMD(CCC_detail_radius, "r__detail_radius", ps_r__detail_radius, 49, 300);
    XR_CMD(CCC_Bool, "r__no_scale_on_fade", ps_no_scale_on_fade); // Alundaio

    //	Allow real-time fog config reload
#ifdef DEBUG
    XR_CMD(CCC_Fog_Reload, "r3_fog_reload");
#endif //	DEBUG

    XR_CMD(CCC_Mask, "r3_dynamic_wet_surfaces", ps_r2_ls_flags, R3FLAG_DYN_WET_SURF);
    XR_CMD(CCC_Float, "r3_dynamic_wet_surfaces_near", ps_r3_dyn_wet_surf_near, 5.0f, 70.0f);
    XR_CMD(CCC_Float, "r3_dynamic_wet_surfaces_far", ps_r3_dyn_wet_surf_far, 30.0f, 300.0f);
    XR_CMD(CCC_Integer, "r3_dynamic_wet_surfaces_sm_res", ps_r3_dyn_wet_surf_sm_res, 64, 2048);

    XR_CMD(CCC_Mask, "r3_volumetric_smoke", ps_r2_ls_flags, R3FLAG_VOLUMETRIC_SMOKE);
    XR_CMD(CCC_VideoMemoryStats, "video_memory_stats");

    XR_CMD(CCC_PART_Export, "particles_export");
    XR_CMD(CCC_PART_Import, "particles_import");

    XR_CMD(CCC_Bool, "shaders_xr_export", bShadersXrExport);

    XR_CMD(CCC_Float, "particle_update_mod", ps_particle_update_coeff, 0.04f, 10.0f);
}
