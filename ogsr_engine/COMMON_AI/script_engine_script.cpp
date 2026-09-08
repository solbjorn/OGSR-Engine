////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine_script.cpp
//	Created 	: 25.12.2002
//  Modified 	: 13.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator script engine export
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "script_engine.h"

#include "ai_space.h"

#include "../xr_3da/xr_input.h"

namespace xr
{
namespace
{
quill::Logger* lua_logger{nullptr};
}
} // namespace xr

namespace
{
struct profile_timer_script final
{
    using Clock = std::chrono::high_resolution_clock;
    using Time = Clock::time_point;
    using Duration = Clock::duration;

    Time start_time{};
    Duration accumulator{};
    u64 count{};
    int recurse_mark{};

    profile_timer_script() = default;

    profile_timer_script operator+(const profile_timer_script& that) const
    {
        profile_timer_script result;
        result.accumulator = accumulator + that.accumulator;
        result.count = count + that.count;
        return result;
    }

    bool operator<(const profile_timer_script& profile_timer) const { return accumulator < profile_timer.accumulator; }

    void start()
    {
        if (recurse_mark)
        {
            ++recurse_mark;
            return;
        }

        ++recurse_mark;
        ++count;
        start_time = Clock::now();
    }

    void stop()
    {
        --recurse_mark;

        if (recurse_mark)
            return;

        const auto finish = Clock::now();
        if (finish > start_time)
        {
            accumulator += finish - start_time;
        }
    }

    decltype(auto) time() const
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(accumulator).count();
    }
};

void take_screenshot(IRender_interface::ScreenshotMode mode, LPCSTR name) { ::Render->Screenshot(mode, name); }

[[nodiscard]] bool GetLAlt() { return pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::LAlt}); }
[[nodiscard]] bool GetRAlt() { return pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::RAlt}); }

[[nodiscard]] bool GetAlt()
{
    return pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::LAlt}) || pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::RAlt});
}

[[nodiscard]] bool GetShift()
{
    return pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::LShift}) || pInput->iGetAsyncKeyState(xr::key_id{sf::Keyboard::Scancode::RShift});
}
} // namespace

void CScriptEngine::script_register(sol::state_view& lua)
{
    xr::lua_logger = xr::logger_init("Lua");

    lua.new_enum("log_level", "trace_l3", xr::level::TraceL3, "trace_l2", xr::level::TraceL2, "trace_l1", xr::level::TraceL1, "debug", xr::level::Debug, "info",
                 xr::level::Info, "notice", xr::level::Notice, "warning", xr::level::Warning, "error", xr::level::Error, "critical", xr::level::Critical);

    lua.set("log1",
            sol::overload([](xr::level lvl, std::string_view msg) { XR_LOG__DYNAMIC(xr::lua_logger, lvl, "{}", msg); },
                          [](std::string_view msg) { XR_LOG__NOTICE(xr::lua_logger, "{}", msg); }),
            "screenshot", &take_screenshot);

    lua.new_enum("modes", "normal", IRender_interface::ScreenshotMode::SM_NORMAL, "cubemap", IRender_interface::ScreenshotMode::SM_FOR_CUBEMAP, "gamesave",
                 IRender_interface::ScreenshotMode::SM_FOR_GAMESAVE, "levelmap", IRender_interface::ScreenshotMode::SM_FOR_LEVELMAP);

    lua.new_usertype<profile_timer_script>("profile_timer", sol::no_constructor, sol::call_constructor,
                                           sol::constructors<profile_timer_script(), profile_timer_script(const profile_timer_script&)>(),
                                           sol::meta_function::addition, &profile_timer_script::operator+, "start", &profile_timer_script::start, "stop",
                                           &profile_timer_script::stop, "time", &profile_timer_script::time);

    lua.set(
        "user_name", [] { return Core.UserName; }, "time_global", [] { return Device.dwTimeGlobal; }, "GetShift", &GetShift, "GetLAlt", &GetLAlt, "GetRAlt",
        &GetRAlt, "GetAlt", &GetAlt, "device", [] { return &Device; }, "__debugbreak", &xr::breakpoint);
}
