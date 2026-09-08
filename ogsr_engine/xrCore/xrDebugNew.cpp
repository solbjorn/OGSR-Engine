#include "stdafx.h"

#include "xrDebug.h"

#include "sentry.h"

#include <cpptrace/from_current.hpp>
#include <cpptrace/utils.hpp>

#include <csignal>

#include <new.h>
#include <shellapi.h>

xrDebug Debug;

HWND gGameWindow{};
bool ExitFromWinMain{};

namespace xr
{
namespace
{
#ifdef XR_SENTRY
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define XR_SENTRY_ASAN
#endif

// VC Runtime's UEF to invoke std::terminate() and rethrow uncaught C++ exceptions
long (*term)(::EXCEPTION_POINTERS*){nullptr};
// Crashpad's SIGABRT handler for artificial crashes (no exception pointers)
void (*abrt)(s32){nullptr};

#ifndef XR_SENTRY_ASAN
// Engine's VEH to catch STATUS_HEAP_CORRUPTION before Crashpad
void* veh{nullptr};
#endif

// Exception pointers to pass to Crashpad from UEF
::EXCEPTION_POINTERS* maybe_info{nullptr};
#endif // XR_SENTRY

// Crashpad's UEF when Sentry is enabled, VC Runtime's UEF for std::terminate() otherwise
long (*orig)(::EXCEPTION_POINTERS*){nullptr};
// Formatted stacktrace when captured inside UEF
xr_string maybe_trace;

quill::Logger* cpptrace_logger{nullptr};

void log_callback(cpptrace::log_level lvl, gsl::czstring msg)
{
    xr::level qlvl;

    switch (lvl)
    {
    case cpptrace::log_level::debug: qlvl = xr::level::Debug; break;
    case cpptrace::log_level::info: qlvl = xr::level::Info; break;
    case cpptrace::log_level::warning: qlvl = xr::level::Warning; break;
    case cpptrace::log_level::error: qlvl = xr::level::Error; break;
    default: qlvl = xr::level::Notice; break;
    }

    XR_LOG__DYNAMIC(xr::cpptrace_logger, qlvl, "{}", msg);
}

void show(std::string_view msg)
{
    static std::atomic<bool> hit{false};

    if (bool exp{false}; !hit.compare_exchange_strong(exp, true))
        return;

    if (Debug.to_log() != nullptr)
        XR_LOG_CRITICAL("{}", msg);

    xr::log_flush();
    const auto back = xr::detail::log_flush();

    if (gGameWindow != nullptr)
    {
        tmc::post(xr::tmc_cpu_st_executor(), [] -> tmc::task<void> {
            ::ShowWindow(gGameWindow, SW_HIDE);
            co_return;
        }());
    }

    while (::ShowCursor(true) < 0)
        continue;

    if (xr::is_debugger_present())
        return;

    if (const auto log = Debug.to_log(); log != nullptr && !back)
        ::ShellExecuteW(nullptr, L"open", std::filesystem::path{reinterpret_cast<xr::cu8zstring>(log)}.c_str(), nullptr, nullptr, SW_SHOW);
    else
        ::MessageBoxW(nullptr, sf::String::fromUtf8(msg.begin(), msg.end()).toWideString().c_str(), L"FATAL ERROR",
                      MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND | MB_TOPMOST);
}

[[noreturn]] void failure_handler(const libassert::assertion_info& info)
{
    auto msg = xr::format("\n{}\nStack trace:\n{}", info.header(xr::detail::log_width),
                          xr::maybe_trace.empty() ? info.print_stacktrace(xr::detail::log_width) : xr::maybe_trace);

    if (auto trace = Debug.get_lua_trace(); trace != nullptr)
        msg += trace();

    while (msg.back() == '\n')
        msg.pop_back();

    xr::show(msg);
#ifdef XR_SENTRY
    xr::sentry().event_msg(msg.subview(1, msg.find("\nStack trace:\n") - 2));

    if (xr::maybe_trace.empty())
        xr::sentry().event_trace(info.get_stacktrace());
#endif

    if (xr::is_debugger_present())
        xr::breakpoint();

#ifdef XR_SENTRY
    // Uncaught C++ exceptions: unhandled_filter() -> VC Runtime's UEF -> std::terminate() -> failure_handler() -> Crashpad's UEF
    // SEH, Lua errors: unhandled_filter() -> failure_handler() -> Crashpad's UEF
    // Assertions, signals: failure_handler() -> Crashpad's SIGABRT handler
    if (xr::maybe_info != nullptr)
        xr::orig(xr::maybe_info);
    else
        xr::abrt(SIGABRT);

    xr::unreachable();
#else
    std::quick_exit(EXIT_FAILURE);
#endif
}

[[noreturn]] void terminate()
{
    xr_string description;
    xr_string type;

    try
    {
        if (auto ep = std::current_exception(); ep)
            std::rethrow_exception(ep);
    }
    catch (const std::exception& ex)
    {
        description = ex.what();
        type = cpptrace::demangle(typeid(ex).name());
    }
    catch (const std::string& msg)
    {
        description = msg;
        type = cpptrace::demangle(typeid(msg).name());
    }
    catch (gsl::czstring msg)
    {
        description = msg;
        type = cpptrace::demangle(typeid(msg).name());
    }
    catch (...)
    {
        description = "unknown exception";
        type = "not available";
    }

    if (!type.empty())
        XR_PANIC(description, type);
    else
        XR_PANIC("unexpected application termination");
}

[[noreturn]] void signal_handler(s32 sig)
{
    std::string_view desc;

    switch (sig)
    {
    case SIGINT: desc = "execution interrupt"; break;
    case SIGILL: desc = "illegal instruction"; break;
    case SIGFPE: desc = "floating point exception"; break;
    case SIGSEGV: desc = "segment violation"; break;
    case SIGTERM: desc = "process termination"; break;
    case SIGBREAK: desc = "break sequence"; break;
    case SIGABRT:
    case SIGABRT_COMPAT: desc = "abnormal termination"; break;
    default: break;
    }

    XR_PANIC(desc, sig);
}

[[noreturn]] s32 new_handler(std::size_t size)
{
    Memory.mem_compact();

    const auto process_heap = mem_usage_impl(nullptr, nullptr) / 1024;
    const auto str_economy = str_container::stat_economy() / 1024;
    const auto smem_economy = smem_container::stat_economy() / 1024;

    XR_PANIC("out of memory", size, process_heap, str_economy, smem_economy);
}

[[noreturn]] void invalid_parameter_handler(gsl::cwzstring expr, gsl::cwzstring fn, gsl::cwzstring f, u32 line, std::uintptr_t reserved)
{
    if (expr == nullptr || fn == nullptr || f == nullptr)
        XR_PANIC("invalid parameter", line, reserved);

    const sf::String expression{expr};
    const sf::String function{fn};
    const sf::String file{f};

    XR_PANIC("invalid parameter", expression, function, file, line, reserved);
}

// LuaJIT always throws a custom structured exception instead of calling to
// the user-defined panic handler. It is used for unwinding when inside a
// protected call, otherwise the exception sneaks out and finishes in the
// unhandled exception handler.
// If the code matches, redirect it to the Lua panic handler, which will
// print the error off the Lua stack, then eventually the stack trace and
// the locals if accessible.
class lua_panic_handler final
{
private:
    static constexpr unsigned long lua_exception_base{0xe24c4a00};

    decltype(std::declval<xrDebug>().get_lua_panic()) handler{};
    s32 code{};

    [[noreturn]] void execute()
    {
        handler(code);
        xr::unreachable();
    }

public:
    constexpr explicit lua_panic_handler(const ::EXCEPTION_RECORD& rec)
    {
        const auto maybe_code = rec.ExceptionCode ^ lua_exception_base;
        if (maybe_code > std::numeric_limits<u8>::max())
            return;

        handler = Debug.get_lua_panic();
        code = gsl::narrow_cast<s32>(maybe_code);
    }

    constexpr ~lua_panic_handler()
    {
        if (handler != nullptr)
            execute();
    }

    [[nodiscard]] constexpr explicit operator bool() const { return handler != nullptr; }
};

// An uncaught C++ exception finishes as an unhandled structured exception in
// the handler with the particular codes. If that's the case, the handler
// shouldn't exit immediately -- pass it to the default handler, which will
// call std::terminate() where it can be rethrown and logged.
[[nodiscard]] constexpr bool is_cpp_exception(const ::EXCEPTION_RECORD& rec)
{
    if (rec.ExceptionCode != 0xe06d7363 || rec.NumberParameters != 4)
        return false;

    switch (rec.ExceptionInformation[0])
    {
    case 0x19930520:
    case 0x19930521:
    case 0x19930522:
    case 0x01994000: return true;
    default: return false;
    }
}

[[nodiscard]] long unhandled_filter(::EXCEPTION_POINTERS* info)
{
    static std::atomic<bool> hit{false};

out:
    if (bool exp{false}; !hit.compare_exchange_strong(exp, true))
        return xr::orig != nullptr ? xr::orig(info) : EXCEPTION_CONTINUE_SEARCH;

    const auto& rec = *info->ExceptionRecord;
    const auto cpp = xr::is_cpp_exception(rec);
    const xr::lua_panic_handler panic{rec};

    cpptrace::detail::maybe_collect_trace(info, EXCEPTION_EXECUTE_HANDLER);

    if (const auto& trace = cpptrace::from_current_exception(); !trace.empty())
    {
        xr::maybe_trace = libassert::print_stacktrace(trace, xr::detail::log_width);
#ifdef XR_SENTRY
        xr::sentry().event_trace(trace);
    }

    xr::maybe_info = info;

    if (cpp)
        return xr::term != nullptr ? xr::term(info) : EXCEPTION_CONTINUE_SEARCH;
#else
    }
#endif

    if (!cpp && !panic)
    {
        const xr::ntstatus code{std::bit_cast<long>(rec.ExceptionCode)};
        XR_PANIC(code.what(), code, errno, xr::GetLastError());
    }

    // Falls to Lua error handler if detected
    goto out;
}

#ifdef XR_SENTRY
#ifndef XR_SENTRY_ASAN
[[nodiscard]] long vectored_handler(::EXCEPTION_POINTERS* info)
{
    return info->ExceptionRecord->ExceptionCode == STATUS_HEAP_CORRUPTION ? xr::unhandled_filter(info) : EXCEPTION_CONTINUE_SEARCH;
}
#endif

void sentry_close()
{
#ifndef XR_SENTRY_ASAN
    ::RemoveVectoredExceptionHandler(xr::veh);
#endif
    xr::sentry().close();
}

void sentry_init(gsl::czstring log = nullptr)
{
    if (log == nullptr)
    {
        xr::term = ::SetUnhandledExceptionFilter(nullptr);
        ::SetUnhandledExceptionFilter(xr::term);
    }
    else
    {
        xr::sentry_close();
    }

    if (const auto ret = xr::sentry().init(log); ret != 0)
    {
        xr::show(xr::format("Failed to initialize Sentry: {}", ret));

        if (xr::is_debugger_present())
            xr::breakpoint();

        std::quick_exit(EXIT_FAILURE);
    }

    // Restore for the 2nd time, initially restored below
    if (log != nullptr)
        xr::orig = ::SetUnhandledExceptionFilter(&xr::unhandled_filter);

#ifndef XR_SENTRY_ASAN
    xr::veh = ::AddVectoredExceptionHandler(true, xr::vectored_handler);
#endif
    xr::abrt = std::signal(SIGABRT, &xr::signal_handler);
}
#endif // XR_SENTRY
} // namespace
} // namespace xr

void xrDebug::_initialize()
{
    xr::detail::log_init();
    xr::detail::log_init_new();

    xr::cpptrace_logger = xr::logger_init("cpptrace");
    cpptrace::set_log_callback(&xr::log_callback);

    libassert::set_color_scheme(libassert::color_scheme::blank);
    libassert::set_failure_handler(&xr::failure_handler);

    std::atexit([] { XR_ASSERT(ExitFromWinMain, "unexpected application exit"); });
    std::set_terminate(&xr::terminate);

#ifdef XR_SENTRY
    xr::sentry_init();
#else
    ::_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    for (auto sig : std::array{SIGILL, SIGFPE, SIGTERM, SIGABRT, SIGABRT_COMPAT})
        std::signal(sig, &xr::signal_handler);

    ::_set_new_mode(1);
    ::_set_new_handler(&xr::new_handler);
    std::set_new_handler([] { xr::new_handler(0); });

    ::_set_invalid_parameter_handler(&xr::invalid_parameter_handler);
    ::_set_purecall_handler([] { XR_PANIC("pure virtual function call"); });

    xr::orig = ::SetUnhandledExceptionFilter(&xr::unhandled_filter);
}

void xrDebug::thread_init(std::size_t) { std::set_terminate(&xr::terminate); }

void xrDebug::to_log(gsl::czstring path)
{
    log = path;

#ifdef XR_SENTRY
    if (path != nullptr)
        xr::sentry_init(path);
    else
        xr::sentry_close();
#endif
}

std::string xrDebug::format_system(unsigned long code, bool module)
{
    constexpr auto drop = [] [[nodiscard]] (char ch) { return ch == ' ' || ch == '.' || ch == '\n' || ch == '\r'; };
    constexpr unsigned long lang{0x409}; // en-US

    unsigned long flags{FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS};
    ::HINSTANCE__* handle{nullptr};
    gsl::zstring str{nullptr};

    if (module)
    {
        handle = ::LoadLibraryA("ntdll.dll");
        flags |= FORMAT_MESSAGE_FROM_HMODULE;
    }

    const auto _ = gsl::finally([str, handle] {
        ::LocalFree(str);

        if (handle != nullptr)
            ::FreeLibrary(handle);
    });

    auto n = ::FormatMessageA(flags, handle, code, lang, reinterpret_cast<gsl::zstring>(&str), 0, nullptr);
    while (n > 0 && drop(str[n - 1]))
        --n;

    if (n == 0)
        return "unknown error";

    std::string ret{str, n};
    std::ranges::replace(ret, '\n', ' ');

    return ret;
}

// Legacy, to be removed when xrGame is converted to [lib]assert

namespace
{
void LogStackTrace(const char* header)
{
    __try
    {
        XR_LOG_CRITICAL("********************************************************************************");
        XR_LOG_CRITICAL("[{}]", std::source_location::current().function_name());
        XR_LOG_CRITICAL("{}", header);
        XR_LOG_CRITICAL("********************************************************************************");
    }
    __finally
    {}
}

void gather_info(const char* expression, const char* description, const char* argument0, const char* argument1, const char* file, gsl::index line,
                 const char* function, char* assertion_info)
{
    xr_string unhandled;

    try
    {
        auto ep = std::current_exception();
        if (ep)
            std::rethrow_exception(ep);
    }
    catch (const std::exception& ex)
    {
        unhandled = ex.what();
    }
    catch (const std::string& msg)
    {
        unhandled = msg;
    }
    catch (gsl::czstring msg)
    {
        unhandled = msg;
    }
    catch (...)
    {
        unhandled = "Unknown exception type";
    }

    auto buffer = assertion_info;
    auto endline = "\n";
    auto prefix = "[error]";
    const bool extended_description = strchr(description, '\n') != nullptr;
    const bool extended_unhandled = unhandled.contains('\n');

    for (int i = 0; i < 2; ++i)
    {
        if (!i)
            buffer += sprintf(buffer, "%sFATAL ERROR%s%s", endline, endline, endline);

        buffer += sprintf(buffer, "%sExpression    : %s%s", prefix, expression, endline);
        buffer += sprintf(buffer, "%sFunction      : %s%s", prefix, function, endline);
        buffer += sprintf(buffer, "%sFile          : %s%s", prefix, file, endline);
        buffer += sprintf(buffer, "%sLine          : %zd%s", prefix, line, endline);

        if (extended_description)
            buffer += sprintf(buffer, "%sDescription   :%s%s%s%s", prefix, endline, endline, description, endline);
        else
            buffer += sprintf(buffer, "%sDescription   : %s%s", prefix, description, endline);

        if (argument0)
        {
            if (extended_description)
                buffer += sprintf(buffer, "%s", endline);

            if (argument1)
            {
                buffer += sprintf(buffer, "%sArgument 0    : %s%s", prefix, argument0, endline);
                buffer += sprintf(buffer, "%sArgument 1    : %s%s", prefix, argument1, endline);
            }
            else
            {
                buffer += sprintf(buffer, "%sArguments     : %s%s", prefix, argument0, endline);
            }
        }

        buffer += sprintf(buffer, "%s", endline);

        if (!unhandled.empty())
        {
            buffer += sprintf(buffer, "%sExpression    : %s%s", prefix, "unhandled exception", endline);
            buffer += sprintf(buffer, "%sFunction      : %s%s", prefix, function, endline);
            buffer += sprintf(buffer, "%sFile          : %s%s", prefix, file, endline);
            buffer += sprintf(buffer, "%sLine          : %zd%s", prefix, line, endline);

            if (extended_unhandled)
                buffer += sprintf(buffer, "%sDescription   :%s%s%s%s", prefix, endline, endline, unhandled.c_str(), endline);
            else
                buffer += sprintf(buffer, "%sDescription   : %s%s", prefix, unhandled.c_str(), endline);

            buffer += sprintf(buffer, "%s", endline);
        }

        if (!i)
        {
            XR_LOG_CRITICAL("{}", assertion_info);
            buffer = assertion_info;
            endline = "\r\n";
            prefix = "";
        }
    }

    buffer += sprintf(buffer, "See log file for detailed information\r\n");
    LogStackTrace("!!stack trace:\n");
}
} // namespace

void xrDebug::backend(const char* expression, const char* description, const char* argument0, const char* argument1, const char* file, gsl::index line,
                      const char* function)
{
    static std::atomic<bool> hit{false};
    bool exp{false};

    if (!hit.compare_exchange_strong(exp, true))
        return;

    string4096 assertion_info;
    gather_info(expression, description, argument0, argument1, file, line, function, assertion_info);

    auto endline = "\r\n";
    auto buffer = assertion_info + xr_strlen(assertion_info);
    buffer += sprintf(buffer, "%sPress OK to abort execution%s", endline, endline);

    xr::show({});

    if (xr::is_debugger_present())
        xr::breakpoint();

    std::quick_exit(EXIT_FAILURE);
}

void xrDebug::fail(const char* e1, const char* file, gsl::index line, const char* function)
{
    backend("assertion failed", e1, nullptr, nullptr, file, line, function);
    xr::unreachable();
}

void xrDebug::fail(const char* e1, const std::string& e2, const char* file, gsl::index line, const char* function)
{
    backend(e1, e2.c_str(), nullptr, nullptr, file, line, function);
    xr::unreachable();
}

void xrDebug::fail(const char* e1, const char* e2, const char* file, gsl::index line, const char* function)
{
    backend(e1, e2, nullptr, nullptr, file, line, function);
    xr::unreachable();
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3, const char* file, gsl::index line, const char* function)
{
    backend(e1, e2, e3, nullptr, file, line, function);
    xr::unreachable();
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3, const char* e4, const char* file, gsl::index line, const char* function)
{
    backend(e1, e2, e3, e4, file, line, function);
    xr::unreachable();
}
