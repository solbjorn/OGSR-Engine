#include "stdafx.h"

#include "sentry.h"

#ifdef XR_SENTRY
#include "cpu.h"

#include <cpptrace/basic.hpp>

namespace sentry
{
#define SENTRY_BUILD_STATIC

#include <sentry.h>

#undef SENTRY_BUILD_STATIC
} // namespace sentry

#include <cstdarg>

namespace xr
{
namespace
{
[[nodiscard]] constexpr auto& sentry_value_cast(u64& val) { return *reinterpret_cast<sentry::sentry_value_t*>(&val); }

[[gnu::format(printf, 2, 0)]] void sentry_logger(sentry::sentry_level_t level, gsl::czstring message, std::va_list args, void* userdata)
{
    xr::level lvl;

    switch (level)
    {
    case sentry::sentry_level_t::SENTRY_LEVEL_FATAL: lvl = xr::level::Critical; break;
    case sentry::sentry_level_t::SENTRY_LEVEL_ERROR: lvl = xr::level::Error; break;
    case sentry::sentry_level_t::SENTRY_LEVEL_WARNING: lvl = xr::level::Warning; break;
    case sentry::sentry_level_t::SENTRY_LEVEL_INFO: lvl = xr::level::Info; break;
    case sentry::sentry_level_t::SENTRY_LEVEL_DEBUG: lvl = xr::level::Debug; break;
    case sentry::sentry_level_t::SENTRY_LEVEL_TRACE: lvl = xr::level::TraceL1; break;
    default: lvl = xr::level::Notice; break;
    }

    std::va_list copy;
    va_copy(copy, args);

    const auto sz = std::vsnprintf(nullptr, 0, message, args);
    if (sz <= 0)
    {
        va_end(copy);
        return;
    }

    xr_string res;

    res.resize_and_overwrite(gsl::narrow_cast<std::size_t>(sz), [message, &copy] [[nodiscard]] (gsl::zstring buf, std::size_t size) noexcept {
        std::vsnprintf(buf, size + 1, message, copy);
        va_end(copy);

        return size;
    });

    XR_LOG__DYNAMIC(static_cast<quill::Logger*>(userdata), lvl, "{}", std::move(res));
}

[[nodiscard]] auto sentry_on_crash(const sentry::sentry_ucontext_t*, sentry::sentry_value_t event, void* user_data)
{
    auto& helper = *static_cast<xr::sentry_helper*>(user_data);

    auto maybe_msg = sentry::sentry_value_new_null();
    std::swap(maybe_msg, xr::sentry_value_cast(helper.maybe_msg));

    if (!sentry::sentry_value_is_null(maybe_msg))
        sentry::sentry_value_set_by_key_n(event, "message", 7, maybe_msg);

    auto maybe_trace = sentry::sentry_value_new_null();
    std::swap(maybe_trace, xr::sentry_value_cast(helper.maybe_trace));

    if (!sentry::sentry_value_is_null(maybe_trace))
        sentry::sentry_event_add_thread(event, maybe_trace);

    return event;
}
} // namespace

sentry_helper::sentry_helper()
{
    xr::sentry_value_cast(maybe_msg) = sentry::sentry_value_new_null();
    xr::sentry_value_cast(maybe_trace) = sentry::sentry_value_new_null();
}

sentry_helper::~sentry_helper()
{
    sentry::sentry_value_decref(xr::sentry_value_cast(maybe_trace));
    sentry::sentry_value_decref(xr::sentry_value_cast(maybe_msg));
}

s32 sentry_helper::init(gsl::czstring log)
{
#ifdef _DEBUG
    static constexpr sentry::sentry_level_t severity{sentry::sentry_level_t::SENTRY_LEVEL_INFO};
#else
    static constexpr sentry::sentry_level_t severity{sentry::sentry_level_t::SENTRY_LEVEL_WARNING};
#endif

    std::filesystem::path fs;

    if (log != nullptr)
    {
        string_path dbc;
        std::ignore = FS.update_path(dbc, xr::fsgame::app_data_root.data(), "sentry");
        fs.assign(reinterpret_cast<xr::cu8zstring>(dbc));
    }
    else
    {
        fs = std::filesystem::temp_directory_path() / xr::format("{}", std::this_thread::get_id());
        tmp = fs;
    }

    const auto opts = sentry::sentry_options_new();
    sentry::sentry_options_set_debug(opts, true);
    sentry::sentry_options_set_logger_level(opts, severity);
    sentry::sentry_options_set_logger(opts, &xr::sentry_logger, xr::logger_init("Sentry"));

    sentry::sentry_options_set_dsn(opts, XR_SENTRY_DSN);
    sentry::sentry_options_set_attach_screenshot(opts, true);
    sentry::sentry_options_set_on_crash(opts, &xr::sentry_on_crash, this);
    sentry::sentry_options_set_database_pathw_n(opts, fs.c_str(), fs.native().size());

    if (log != nullptr)
    {
        sentry::sentry_options_set_cache_keep(opts, sentry::sentry_cache_keep_t::SENTRY_CACHE_KEEP_ALWAYS);
        sentry::sentry_options_set_cache_max_size(opts, 160 * 1024 * 1024);

        fs.assign(reinterpret_cast<xr::cu8zstring>(log));
        sentry::sentry_options_add_attachmentw_n(opts, fs.c_str(), fs.native().size());
    }

    return sentry::sentry_init(opts);
}

void sentry_helper::close()
{
    sentry::sentry_close();

    if (tmp.empty())
        return;

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);

    if (ec)
        XR_LOG_ERROR("Failed to remove {}: {}", tmp, ec);

    tmp = std::filesystem::path{};
}

void sentry_helper::event_msg(std::string_view msg)
{
    auto container = sentry::sentry_value_new_object();
    sentry::sentry_value_set_by_key_n(container, "formatted", 9, sentry::sentry_value_new_string_n(msg.data(), msg.size()));

    std::swap(container, xr::sentry_value_cast(maybe_msg));
    sentry::sentry_value_decref(container);
}

void sentry_helper::event_trace(const cpptrace::stacktrace& trace)
{
    auto thread = sentry::sentry_value_new_object();

    auto str = xr::format("{}", std::this_thread::get_id());
    sentry::sentry_value_set_by_key_n(thread, "id", 2, sentry::sentry_value_new_string_n(str.c_str(), str.size()));

    str = CPU::ID.this_thread();
    sentry::sentry_value_set_by_key_n(thread, "name", 4, sentry::sentry_value_new_string_n(str.c_str(), str.size()));

    sentry::sentry_value_set_by_key_n(thread, "crashed", 7, sentry::sentry_value_new_bool(true));
    sentry::sentry_value_set_by_key_n(thread, "current", 7, sentry::sentry_value_new_bool(true));

    const auto frames = sentry::sentry_value_new_list();

    for (auto& elem : std::views::reverse(trace))
    {
        auto frame = sentry::sentry_value_new_object();
        sentry::sentry_value_set_by_key_n(frame, "in_app", 6, sentry::sentry_value_new_bool(true));

        str.clear();
        xr::format_to(std::back_inserter(str), "{:#x}", elem.raw_address);
        sentry::sentry_value_set_by_key_n(frame, "instruction_addr", 16, sentry::sentry_value_new_string_n(str.c_str(), str.size()));

        sentry::sentry_value_append(frames, frame);
    }

    const auto stacktrace = sentry::sentry_value_new_object();
    sentry::sentry_value_set_by_key(stacktrace, "frames", frames);
    sentry::sentry_value_set_by_key(thread, "stacktrace", stacktrace);

    std::swap(thread, xr::sentry_value_cast(maybe_trace));
    sentry::sentry_value_decref(thread);
}
} // namespace xr
#endif // XR_SENTRY
