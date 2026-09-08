#include "stdafx.h"

#include "xr_ioc_cmd.h"

#include "IGame_Level.h"
#include "x_ray.h"
#include "CameraManager.h"
#include "Environment.h"
#include "xr_input.h"
#include "CustomHUD.h"
#include "GameFont.h"
#include "../Include/xrRender/RenderDeviceRender.h"
#include "xr_object.h"
#include "SkeletonMotions.h"
#include "IGame_Persistent.h"
#include "LightAnimLibrary.h"
#include "xr_efflensflare.h"

xr_token* vid_quality_token = nullptr;

u32 g_screenmode = 1;

namespace
{
constexpr xr_token screen_mode_tokens[]{{"windowed", 0}, {"borderless", 1}, {"fullscreen", 2}, {nullptr, 0}};

#ifdef DEBUG
constexpr xr_token vid_bpp_token[]{{"16", 16}, {"32", 32}, {0, 0}};
#endif
} // namespace

void IConsole_Command::InvalidSyntax(std::string_view msg, std::string_view args) const
{
    Msg("~ Invalid syntax in call to '{}': {}: {}", cName, msg, args);
    Msg("~ Valid arguments: {}", Info());
}

void IConsole_Command::add_to_LRU(shared_str const& arg)
{
    if (arg.size() == 0 || bEmptyArgsHandled)
    {
        return;
    }

    bool dup = (std::find(m_LRU.begin(), m_LRU.end(), arg) != m_LRU.end());
    if (!dup)
    {
        m_LRU.push_back(arg);
        if (m_LRU.size() > LRU_MAX_COUNT)
        {
            m_LRU.erase(m_LRU.begin());
        }
    }
}

void IConsole_Command::add_LRU_to_tips(vecTips& tips)
{
    vecLRU::reverse_iterator it_rb = m_LRU.rbegin();
    vecLRU::reverse_iterator it_re = m_LRU.rend();
    for (; it_rb != it_re; ++it_rb)
    {
        tips.push_back((*it_rb));
    }
}

// =======================================================

namespace
{
class CCC_Quit final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Quit, IConsole_Command);

public:
    explicit CCC_Quit(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_Quit() override = default;

    void Execute(std::string_view) override { Device.add_frame_async(CallMe::fromMethod<&CCC_Quit::execute_async>(this)); }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>&)
    {
        co_await Console->Hide();

        co_await Engine.Event.Defer("KERNEL:disconnect");
        co_await Engine.Event.Defer("KERNEL:quit");
    }
};

#ifdef DEBUG_MEMORY_MANAGER
class CCC_MemStat final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_MemStat, IConsole_Command);

public:
    explicit CCC_MemStat(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_MemStat() override = default;

    void Execute(std::string_view args) override
    {
        xr_string fn;

        if (!args.empty())
            fn = xr::format("{}.dump", args);
        else
            fn.assign("x:\\$memory$.dump");

        Memory.mem_statistic(fn.c_str());
    }
};

class CCC_DbgMemCheck final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_DbgMemCheck, IConsole_Command);

public:
    explicit CCC_DbgMemCheck(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_DbgMemCheck() override = default;

    void Execute(std::string_view) override
    {
        if (Memory.debug_mode)
            Memory.dbg_check();
        else
            XR_LOG_WARNING("Run with -mem_debug options");
    }
};
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG
class CCC_DbgStrCheck final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_DbgStrCheck, IConsole_Command);

public:
    explicit CCC_DbgStrCheck(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_DbgStrCheck() override = default;

    void Execute(std::string_view) override { str_container::verify(); }
};

class CCC_DbgStrDump final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_DbgStrDump, IConsole_Command);

public:
    explicit CCC_DbgStrDump(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_DbgStrDump() override = default;

    void Execute(std::string_view) override { str_container::dump(); }
};
#endif // DEBUG

class CCC_DbgLALibDump final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_DbgLALibDump, IConsole_Command);

public:
    explicit CCC_DbgLALibDump(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_DbgLALibDump() override = default;

    void Execute(std::string_view) override { LALib.DbgDumpInfo(); }
};

//-----------------------------------------------------------------------
class CCC_MotionsStat final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_MotionsStat, IConsole_Command);

public:
    explicit CCC_MotionsStat(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_MotionsStat() override = default;

    void Execute(std::string_view) override { g_pMotionsContainer->dump(); }
};

#ifdef DEBUG
class CCC_TexturesStat final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_TexturesStat, IConsole_Command);

public:
    explicit CCC_TexturesStat(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_TexturesStat() override = default;

    void Execute(std::string_view) override { Device.DumpResourcesMemoryUsage(); }
};

//-----------------------------------------------------------------------

class CCC_E_Dump final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_E_Dump, IConsole_Command);

public:
    explicit CCC_E_Dump(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_E_Dump() override = default;

    void Execute(std::string_view) override { Engine.Event.Dump(); }
};

class CCC_E_Signal final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_E_Signal, IConsole_Command);

public:
    explicit CCC_E_Signal(gsl::czstring N) : IConsole_Command{N} {}
    ~CCC_E_Signal() override = default;

    void Execute(std::string_view args) override
    {
        const auto res = scn::scan<xr_string, xr_string>(args, "{:[^,]},{}");
        if (!res)
        {
            InvalidSyntax(res.error().msg(), args);
            return;
        }

        const auto& [event, param] = res.values();

        auto& arg = Device.add_frame_async(CallMe::fromMethod<&CCC_E_Signal::execute_async>(this));
        *reinterpret_cast<std::pair<gsl::zstring, gsl::zstring>*>(&arg) = std::make_pair(xr_strdup(event.c_str()), xr_strdup(param.c_str()));
    }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>& arg)
    {
        auto& args = *reinterpret_cast<std::pair<gsl::zstring, gsl::zstring>*>(&arg);
        co_await Engine.Event.Signal(args.first, reinterpret_cast<uintptr_t>(args.second));

        xr_free(args.first);
        xr_free(args.second);
    }
};
#endif // DEBUG

//-----------------------------------------------------------------------

class CCC_Help final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Help, IConsole_Command);

public:
    explicit CCC_Help(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_Help() override = default;

    void Execute(std::string_view) override
    {
        XR_LOG_INFO("--- Command listing: start ---");

        for (auto it = Console->Commands.begin(); it != Console->Commands.end(); it++)
        {
            IConsole_Command& C = *(it->second);
            Msg("{:20} ({:10}) --- {}", C.Name(), C.Status(), C.Info());
        }

        XR_LOG_INFO("Key: Ctrl + A         === Select all ");
        XR_LOG_INFO("Key: Ctrl + C         === Copy to clipboard ");
        XR_LOG_INFO("Key: Ctrl + V         === Paste from clipboard ");
        XR_LOG_INFO("Key: Ctrl + X         === Cut to clipboard ");
        XR_LOG_INFO("Key: Ctrl + Z         === Undo ");
        XR_LOG_INFO("Key: Ctrl + Insert    === Copy to clipboard ");
        XR_LOG_INFO("Key: Shift + Insert   === Paste from clipboard ");
        XR_LOG_INFO("Key: Shift + Delete   === Cut to clipboard ");
        XR_LOG_INFO("Key: Insert           === Toggle mode <Insert> ");
        XR_LOG_INFO("Key: Back / Delete          === Delete symbol left / right ");

        XR_LOG_INFO("Key: Up   / Down            === Prev / Next command in tips list ");
        XR_LOG_INFO("Key: Ctrl + Up / Ctrl + Down === Prev / Next executing command ");
        XR_LOG_INFO("Key: Left, Right, Home, End {+Shift/+Ctrl}       === Navigation in text ");
        XR_LOG_INFO("Key: PageUp / PageDown      === Scrolling history ");
        XR_LOG_INFO("Key: Tab  / Shift + Tab     === Next / Prev possible command from list");
        XR_LOG_INFO("Key: Enter  / NumEnter      === Execute current command ");

        XR_LOG_INFO("--- Command listing: end ----");
    }
};

#ifdef DEBUG
class CCC_DumpOpenFiles final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_DumpOpenFiles, IConsole_Command);

public:
    explicit CCC_DumpOpenFiles(gsl::czstring N) : IConsole_Command{N} {}
    ~CCC_DumpOpenFiles() override = default;

    void Execute(std::string_view args) override
    {
        const auto res = scn::scan_int<s32>(args);
        if (!res)
        {
            InvalidSyntax(res.error().msg(), args);
            return;
        }

        _dump_open_files(res->value());
    }
};
#endif

//-----------------------------------------------------------------------

class CCC_SaveCFG final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_SaveCFG, IConsole_Command);

public:
    explicit CCC_SaveCFG(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_SaveCFG() override = default;

    void Execute(std::string_view args) override
    {
        string_path cfg_full_name;
        xr_strcpy(cfg_full_name, !args.empty() ? args.data() : Console->ConfigFile);

        bool b_abs_name = xr_strlen(cfg_full_name) > 2 && cfg_full_name[1] == ':';
        if (!b_abs_name)
            std::ignore = FS.update_path(cfg_full_name, "$app_data_root$", cfg_full_name);

        if (const auto ext = strext(cfg_full_name); ext != nullptr)
            *ext = '\0';

        xr_strcat(cfg_full_name, ".ltx");

        BOOL b_allow = TRUE;
        if (FS.exist(cfg_full_name))
            b_allow = SetFileAttributesA(cfg_full_name, FILE_ATTRIBUTE_NORMAL);

        if (b_allow)
        {
            IWriter* F = FS.w_open(cfg_full_name);

            for (auto it = Console->Commands.begin(); it != Console->Commands.end(); it++)
                it->second->Save(F);

            FS.w_close(F);
            Msg("Config-file [{}] saved successfully", cfg_full_name);
        }
        else
        {
            InvalidSyntax("failed to save the config to", cfg_full_name);
        }
    }
};
} // namespace

CCC_LoadCFG::CCC_LoadCFG(LPCSTR N) : IConsole_Command{N} {}

void CCC_LoadCFG::Execute(std::string_view args)
{
    Msg("Executing config-script \"{}\"...", args);
    string_path cfg_name;

    xr_strcpy(cfg_name, args.data());

    if (const auto ext = strext(cfg_name); ext != nullptr)
        *ext = '\0';

    xr_strcat(cfg_name, ".ltx");

    string_path cfg_full_name;
    std::ignore = FS.update_path(cfg_full_name, "$app_data_root$", cfg_name);

    if (!FS.exist(cfg_full_name))
        xr_strcpy(cfg_full_name, cfg_name);

    IReader* F = FS.r_open(cfg_full_name);

    string1024 str;
    if (F)
    {
        while (!F->eof())
        {
            if (F->tell() == 0 && strstr(cfg_full_name, "user.ltx"))
            {
                // Костыль от ситуации когда в редких случаях почему-то у игроков бьётся user.ltx - оказывается набит нулями, в результате чего игра не
                // запускается. Не понятно почему так происходит, поэтому сделал тут обработку такой ситуации.

                if (F->elapsed() >= gsl::index{sizeof(u8)})
                {
                    if (F->r_u8() == 0)
                    {
                        InvalidSyntax("broken config file", cfg_full_name);

                        FS.r_close(F);
                        FS.file_delete(cfg_full_name);

                        return;
                    }

                    F->seek(F->tell() - sizeof(u8));
                }
            }

            F->r_string(str, sizeof(str));
            if (allow(str))
                Console->Execute(str);
        }

        FS.r_close(F);

        Msg("[{}] successfully loaded.", cfg_full_name);
    }
    else
    {
        InvalidSyntax("failed to open", cfg_full_name);
    }
}

CCC_LoadCFG_custom::CCC_LoadCFG_custom(LPCSTR cmd) : CCC_LoadCFG{cmd} { xr_strcpy(m_cmd, cmd); }

bool CCC_LoadCFG_custom::allow(LPCSTR cmd) { return (cmd == strstr(cmd, m_cmd)); }

namespace
{
//-----------------------------------------------------------------------
class CCC_Start final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Start, IConsole_Command);

public:
    explicit CCC_Start(gsl::czstring N) : IConsole_Command{N} {}
    ~CCC_Start() override = default;

    void Execute(std::string_view args) override
    {
        const auto start = args.find('(');
        const auto end = args.rfind(')');

        if (start == std::string_view::npos || end == std::string_view::npos)
        {
            InvalidSyntax("invalid expression", args);
            return;
        }

        const xr_string val{args.substr(start + 1, end - start - 1)};

        auto& arg = Device.add_frame_async(CallMe::fromMethod<&CCC_Start::execute_async>(this));
        *reinterpret_cast<gsl::zstring*>(&arg) = xr_strdup(val.c_str());
    }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>& arg) { co_await Engine.Event.Defer("KERNEL:start", *reinterpret_cast<const uintptr_t*>(&arg)); }
};

class CCC_Disconnect final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Disconnect, IConsole_Command);

public:
    explicit CCC_Disconnect(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_Disconnect() override = default;

    void Execute(std::string_view) override { Device.add_frame_async(CallMe::fromMethod<&CCC_Disconnect::execute_async>(this)); }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>&) { co_await Engine.Event.Defer("KERNEL:disconnect"); }
};

//-----------------------------------------------------------------------

class CCC_VID_Reset final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_VID_Reset, IConsole_Command);

public:
    explicit CCC_VID_Reset(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_VID_Reset() override = default;

    void Execute(std::string_view) override
    {
        if (Device.b_is_Ready)
            Device.add_frame_async(CallMe::fromMethod<&CCC_VID_Reset::execute_async>(this));
    }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>&) { co_await Device.Reset(); }
};

class CCC_VidMode final : public CCC_Token
{
    RTTI_DECLARE_TYPEINFO(CCC_VidMode, CCC_Token);

private:
    u32 _dummy{};

public:
    explicit CCC_VidMode(gsl::czstring N) : CCC_Token{N, _dummy, nullptr} { bEmptyArgsHandled = false; }
    ~CCC_VidMode() override = default;

    void Execute(std::string_view args) override
    {
        const auto res = scn::scan<u32, u32>(args, "{}x{}");
        if (!res)
        {
            InvalidSyntax(res.error().msg(), args);
            return;
        }

        const auto& [w, h] = res->values();
        psCurrentVidMode[0] = w;
        psCurrentVidMode[1] = h;
    }

    [[nodiscard]] xr_string Status() const override { return xr::format("{}x{}", psCurrentVidMode[0], psCurrentVidMode[1]); }
    const xr_token* GetToken() override { return vid_mode_token; }
    [[nodiscard]] xr_string Info() const override { return "change screen resolution WxH"; }

    void fill_tips(vecTips& tips) override
    {
        const auto cur = Status();
        bool res = false;
        const xr_token* tok = GetToken();

        while (tok->name && !res)
        {
            if (std::is_eq(xr_strcmp(tok->name, cur)))
            {
                tips.emplace_back(xr::format("{}  (current)", tok->name));
                res = true;
            }

            tok++;
        }

        if (!res)
            tips.emplace_back("---  (current)");

        tok = GetToken();
        while (tok->name)
        {
            tips.emplace_back(tok->name);
            tok++;
        }
    }
};

class CCC_Screenmode final : public CCC_Token
{
    RTTI_DECLARE_TYPEINFO(CCC_Screenmode, CCC_Token);

public:
    explicit CCC_Screenmode(gsl::czstring N) : CCC_Token{N, g_screenmode, screen_mode_tokens} {}
    ~CCC_Screenmode() override = default;

    void Execute(std::string_view args) override
    {
        auto& arg = Device.add_frame_async(CallMe::fromMethod<&CCC_Screenmode::execute_async>(this));
        *reinterpret_cast<gsl::zstring*>(&arg) = xr_strdup(args.data());
    }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>& arg)
    {
        auto args = *reinterpret_cast<gsl::zstring*>(&arg);
        const u32 prev_mode = g_screenmode;

        CCC_Token::Execute(args);
        xr_free(args);

        co_await tmc::resume_on(xr::tmc_cpu_st_executor());

        if ((prev_mode != g_screenmode))
        {
            // TODO: If you enable the debug layer for DX11 and switch between fullscreen and windowed a few times,
            // you'll occasionally see the following error in the output:
            // DXGI ERROR: IDXGISwapChain::Present: The application has not called ResizeBuffers or re-created the SwapChain after a fullscreen or windowed
            // transition. Flip model swapchains (DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL and DXGI_SWAP_EFFECT_FLIP_DISCARD) are required to do so. [ MISCELLANEOUS
            // ERROR #117: ] This shouldn't happen if we're calling reset on FS/Windowed transitions, I tried logging where Present() is called as well as where
            // ResizeBuffers and SetFullscreenState are called, and the debug output looks like this:
            //
            // Present()
            // SetFullscreenState()
            // [ERROR MESSAGE FROM ABOVE]
            // ResizeBuffers()
            // Present()
            //
            // Which makes no sense, it's impossible for us to have called Present between SetFullscreenState and ResizeBuffers
            // so DX11 must be doing it automatically
            //
            // These threads might be relevant for someone looking into this:
            // https://www.gamedev.net/forums/topic/652626-resizebuffers-bug/
            // https://www.gamedev.net/forums/topic/667426-dxgi-altenter-behaves-inconsistently/
            // https://www.gamedev.net/forums/topic/687484-correct-way-of-creating-the-swapchain-fullscreenwindowedvsync-about-refresh-rate/
            //
            // but the fixes make no sense and contradicts MSDN, and this isn't a major priority since ResizeBuffers is called
            // immediately after (before our Present call) so it works, just so stupid

            bool windowed_to_fullscreen = ((prev_mode == 0) || (prev_mode == 1)) && (g_screenmode == 2);
            bool fullscreen_to_windowed = (prev_mode == 2) && ((g_screenmode == 0) || (g_screenmode == 1));
            bool reset_required = windowed_to_fullscreen || fullscreen_to_windowed;
            if (Device.b_is_Ready && reset_required)
                co_await tmc::spawn_clang(Device.Reset(), tmc::cpu_executor());

            if (g_screenmode == 0 || g_screenmode == 1)
            {
                u32 w, h;
                GetMonitorResolution(w, h);
                SetWindowLongPtr(Device.m_hWnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
                SetWindowPos(Device.m_hWnd, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED);

                if (g_screenmode == 0)
                    SetWindowLongPtr(Device.m_hWnd, GWL_STYLE, WS_VISIBLE | WS_OVERLAPPEDWINDOW);
            }
        }

        RECT winRect;
        GetClientRect(Device.m_hWnd, &winRect);

        MapWindowPoints(Device.m_hWnd, nullptr, reinterpret_cast<POINT*>(&winRect), 2);
        ClipCursor(&winRect);
    }
};

//-----------------------------------------------------------------------

class CCC_SND_Restart final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_SND_Restart, IConsole_Command);

public:
    explicit CCC_SND_Restart(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_SND_Restart() override = default;

    void Execute(std::string_view) override { Sound->_restart(); }
};

//-----------------------------------------------------------------------

#if 0
f32 ps_gamma{1.0f}, ps_brightness{1.0f}, ps_contrast{1.0f};

class CCC_Gamma final : public CCC_Float
{
    RTTI_DECLARE_TYPEINFO(CCC_Gamma, CCC_Float);

public:
    explicit CCC_Gamma(gsl::czstring N, f32& V) : CCC_Float(N, V, 0.5f, 1.5f) {}
    ~CCC_Gamma() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Float::Execute(args);
        // Device.Gamma.Gamma		(ps_gamma);
        Device.m_pRender->setGamma(ps_gamma);
        // Device.Gamma.Brightness	(ps_brightness);
        Device.m_pRender->setBrightness(ps_brightness);
        // Device.Gamma.Contrast	(ps_contrast);
        Device.m_pRender->setContrast(ps_contrast);
        // Device.Gamma.Update		();
        Device.m_pRender->updateGamma();
    }
};
#endif

//-----------------------------------------------------------------------

class CCC_r2 final : public CCC_Token
{
    RTTI_DECLARE_TYPEINFO(CCC_r2, CCC_Token);

private:
    typedef CCC_Token inherited;

    u32 renderer_value{};

public:
    explicit CCC_r2(gsl::czstring N) : inherited{N, renderer_value, nullptr} {}
    ~CCC_r2() override = default;

    void Execute(std::string_view args) override
    {
        //	vid_quality_token must be already created!
        tokens = vid_quality_token;
        inherited::Execute(args);
    }

    void Save(IWriter* F) override
    {
        tokens = vid_quality_token;
        inherited::Save(F);
    }

    const xr_token* GetToken() override
    {
        tokens = vid_quality_token;
        return inherited::GetToken();
    }
};

class CCC_soundDevice final : public CCC_Token
{
    RTTI_DECLARE_TYPEINFO(CCC_soundDevice, CCC_Token);

private:
    typedef CCC_Token inherited;

public:
    explicit CCC_soundDevice(gsl::czstring N) : inherited{N, snd_device_id, nullptr} {}
    ~CCC_soundDevice() override = default;

    void Execute(std::string_view args) override
    {
        GetToken();

        if (!tokens)
            return;

        inherited::Execute(args);
    }

    [[nodiscard]] xr_string Status() const override
    {
        xr_string ret;

        if (tokens != nullptr)
            ret = inherited::Status();

        return ret;
    }

    const xr_token* GetToken() override
    {
        tokens = snd_devices_token;
        return inherited::GetToken();
    }

    void Save(IWriter* F) override
    {
        GetToken();
        if (!tokens)
            return;
        inherited::Save(F);
    }
};

class CCC_HideConsole final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_HideConsole, IConsole_Command);

public:
    explicit CCC_HideConsole(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_HideConsole() override = default;

    void Execute(std::string_view) override { Device.add_frame_async(CallMe::fromMethod<&CCC_HideConsole::execute_async>(this)); }
    [[nodiscard]] xr_string Info() const override { return "hide console"; }

private:
    tmc::task<void> execute_async(std::array<std::byte, 16>&) { co_await Console->Hide(); }
};

class CCC_SoundParamsSmoothing final : public CCC_Integer
{
    RTTI_DECLARE_TYPEINFO(CCC_SoundParamsSmoothing, CCC_Integer);

public:
    explicit CCC_SoundParamsSmoothing(gsl::czstring N, s32& V, s32 _min = 0, s32 _max = 999) : CCC_Integer{N, V, _min, _max} {}
    ~CCC_SoundParamsSmoothing() override = default;

    void Execute(std::string_view args) override
    {
        CCC_Integer::Execute(args);
        soundSmoothingParams::alpha = soundSmoothingParams::getAlpha();
    }
};
} // namespace

float psHUD_FOV_def = 0.45f;
float psHUD_FOV = psHUD_FOV_def;

extern int rsDVB_Size;
extern int rsDIB_Size;

float ps_r2_sun_shafts_min = 0.0f;
float ps_r2_sun_shafts_value = 1.0f;

int ps_framelimiter = 0;

Fvector3 ssfx_wetness_multiplier{1.0f, 0.3f, 0.0f};

void CCC_Register()
{
    // General
    XR_CMD(CCC_Help, "help");
    XR_CMD(CCC_Quit, "quit");
    XR_CMD(CCC_Start, "start");
    XR_CMD(CCC_Disconnect, "disconnect");
    XR_CMD(CCC_SaveCFG, "cfg_save");
    XR_CMD(CCC_LoadCFG, "cfg_load");

    XR_CMD(CCC_MotionsStat, "stat_motions");
#ifdef DEBUG
    XR_CMD(CCC_TexturesStat, "stat_textures");
#endif // DEBUG

#ifdef DEBUG_MEMORY_MANAGER
    XR_CMD(CCC_MemStat, "dbg_mem_dump");
    XR_CMD(CCC_DbgMemCheck, "dbg_mem_check");
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG
    XR_CMD(CCC_Mask, "mt_particles", psDeviceFlags, mtParticles);

    XR_CMD(CCC_DbgStrCheck, "dbg_str_check");
    XR_CMD(CCC_DbgStrDump, "dbg_str_dump");

    XR_CMD(CCC_Mask, "mt_sound", psDeviceFlags, mtSound);
    XR_CMD(CCC_Mask, "mt_physics", psDeviceFlags, mtPhysics);
    XR_CMD(CCC_Mask, "mt_network", psDeviceFlags, mtNetwork);

    // Events
    XR_CMD(CCC_E_Dump, "e_list");
    XR_CMD(CCC_E_Signal, "e_signal");

    XR_CMD(CCC_Mask, "rs_wireframe", psDeviceFlags, rsWireframe);
    XR_CMD(CCC_Mask, "rs_clear_bb", psDeviceFlags, rsClearBB);
    XR_CMD(CCC_Mask, "rs_occlusion", psDeviceFlags, rsOcclusion);

    XR_CMD(CCC_Mask, "rs_detail", psDeviceFlags, rsDetails);

    XR_CMD(CCC_Mask, "rs_render_statics", psDeviceFlags, rsDrawStatic);
    XR_CMD(CCC_Mask, "rs_render_dynamics", psDeviceFlags, rsDrawDynamic);
#endif

    // Render device states
    XR_CMD(CCC_Mask, "rs_always_active", psDeviceFlags, rsAlwaysActive);

    XR_CMD(CCC_Bool, "r_lens_flare", ps_lens_flare);

    XR_CMD(CCC_Float, "r2_sunshafts_min", ps_r2_sun_shafts_min, 0.0f, 0.5f);
    XR_CMD(CCC_Float, "r2_sunshafts_value", ps_r2_sun_shafts_value, 0.5f, 2.0f);

    XR_CMD(CCC_Mask, "rs_v_sync", psDeviceFlags, rsVSync);
    XR_CMD(CCC_Mask, "rs_triple_buffering", psDeviceFlags, rs_triple_buffering);
    XR_CMD(CCC_Screenmode, "rs_screenmode");
    XR_CMD(CCC_Mask, "rs_stats", psDeviceFlags, rsStatistic);
    XR_CMD(CCC_Float, "rs_vis_distance", psVisDistance, 0.4f, 1.5f);

    XR_CMD(CCC_Mask, "rs_cam_pos", psDeviceFlags, rsCameraPos);

#ifdef DEBUG
    XR_CMD(CCC_Mask, "rs_occ_draw", psDeviceFlags, rsOcclusionDraw);
    XR_CMD(CCC_Mask, "rs_occ_stats", psDeviceFlags, rsOcclusionStats);
#endif // DEBUG

#if 0
    // Вместо этих настроек теперь используется ES Color Grading
    XR_CMD(CCC_Gamma, "rs_c_gamma", ps_gamma);
    XR_CMD(CCC_Gamma, "rs_c_brightness", ps_brightness);
    XR_CMD(CCC_Gamma, "rs_c_contrast", ps_contrast);
#endif

    XR_CMD(CCC_Mask, "rs_hw_stats", psDeviceFlags, rsHWInfo);

    // General video control
    XR_CMD(CCC_VidMode, "vid_mode");

    XR_CMD(CCC_VID_Reset, "vid_restart");

    // Sound
    XR_CMD(CCC_Float, "snd_volume_eff", psSoundVEffects);
    XR_CMD(CCC_Float, "snd_volume_music", psSoundVMusic);
    XR_CMD(CCC_SND_Restart, "snd_restart");

    XR_CMD(CCC_Mask, "snd_efx", psSoundFlags, ss_EFX);
    XR_CMD(CCC_Integer, "snd_targets", psSoundTargets, 128, 1024);

    XR_CMD(CCC_Float, "snd_rolloff", psSoundRolloff, 0.1f, 2.0f);
    XR_CMD(CCC_Float, "snd_fade_speed", psSoundFadeSpeed, 1.0f, 10.0f);
    XR_CMD(CCC_Float, "snd_occ_scale", psSoundOcclusionScale, 0.1f, 1.0f);
    XR_CMD(CCC_Float, "snd_occ_hf", psSoundOcclusionHf, 0.0f, 1.0f);
    XR_CMD(CCC_Float, "snd_occ_mtl", psSoundOcclusionMtl, 0.0f, 1.0f);

    // Doppler effect power
    XR_CMD(CCC_Float, "snd_doppler_power", soundSmoothingParams::power, 0.0f, 5.0f);
    XR_CMD(CCC_SoundParamsSmoothing, "snd_doppler_smoothing", soundSmoothingParams::steps, 1, 100);

    XR_CMD(CCC_Bool, "senvironment_xr_export", bSenvironmentXrExport);

#ifdef DEBUG
    XR_CMD(CCC_Mask, "snd_stats", g_stats_flags, st_sound);
    XR_CMD(CCC_Mask, "snd_stats_min_dist", g_stats_flags, st_sound_min_dist);
    XR_CMD(CCC_Mask, "snd_stats_max_dist", g_stats_flags, st_sound_max_dist);
    XR_CMD(CCC_Mask, "snd_stats_ai_dist", g_stats_flags, st_sound_ai_dist);
    XR_CMD(CCC_Mask, "snd_stats_info_name", g_stats_flags, st_sound_info_name);
    XR_CMD(CCC_Mask, "snd_stats_info_object", g_stats_flags, st_sound_info_object);

    XR_CMD(CCC_Integer, "error_line_count", g_ErrorLineCount, 6, 1024);
#endif // DEBUG

    // Mouse
    XR_CMD(CCC_Mask, "mouse_invert", psMouseInvert, 1);
    psMouseSens = 0.12f;
    XR_CMD(CCC_Float, "mouse_sens", psMouseSens, 0.001f, 0.6f);

    // Camera
    XR_CMD(CCC_Float, "cam_inert", psCamInert, 0.0f, 0.99f);
    XR_CMD(CCC_Float, "cam_slide_inert", psCamSlideInert);

    XR_CMD(CCC_r2, "renderer");

    XR_CMD(CCC_soundDevice, "snd_device");

#ifdef DEBUG
    XR_CMD(CCC_DumpOpenFiles, "dump_open_files");
#endif

    XR_CMD(CCC_HideConsole, "hide");

    XR_CMD(CCC_Integer, "r__framelimit", ps_framelimiter, 0, 500);

    XR_CMD(CCC_Vector3, "ssfx_wetness_multiplier", ssfx_wetness_multiplier, Fvector3{0.1f, 0.1f, 0.0f}, Fvector3{20.0f, 20.0f, 0.0f});

    XR_CMD(CCC_Float, "g_font_scale_x", g_fontWidthScale, 0.2f, 5.0f);
    XR_CMD(CCC_Float, "g_font_scale_y", g_fontHeightScale, 0.2f, 5.0f);

    XR_CMD(CCC_Bool, "g_prefetch", g_prefetch);

    XR_CMD(CCC_DbgLALibDump, "dbg_lalib_dump");

    xr::detail::discord_register();
}
