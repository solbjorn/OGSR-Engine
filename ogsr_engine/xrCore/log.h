#ifndef __XRCORE_LOG_H
#define __XRCORE_LOG_H

void Log(xr::detail::string_view fmt, xr::detail::format_args args);

template <typename... Args>
constexpr void Msg(xr::detail::format_string<Args...> fmt, Args&&... args)
{
    Log(fmt.get(), xr::detail::make_format_args(args...));
}

void CreateLog(BOOL no_log = FALSE);

extern xr_vector<xr_string> LogFile;

namespace xr
{
namespace detail
{
constexpr inline auto log_pfx_len{33uz};

void log_init();
} // namespace detail

void log_flush();
} // namespace xr

#include "../xrExternal/quill.h"

#define XR__STRINGIFY(x) #x
#define XR_STRINGIFY(x) XR__STRINGIFY(x)
#define XR_LOGGER_SUBSYSTEM_NAME XR_STRINGIFY(XR_SUBSYSTEM)

#define XR__CONCAT(x, y) x##y
#define XR_CONCAT(x, y) XR__CONCAT(x, y)
#define XR__LOGGER_SUBSYSTEM XR_CONCAT(XR_SUBSYSTEM, _logger)
#define XR_LOGGER_SUBSYSTEM xr::detail::XR__LOGGER_SUBSYSTEM

#define XR_LOG__DYNAMIC(logger, lvl, fmt, ...) \
    QUILL_LOG_RUNTIME_METADATA_HYBRID(logger, lvl, QUILL_FILE_NAME, QUILL_LINE_NO, QUILL_FUNCTION_NAME, xr::detail::this_thread().encode().data(), fmt, \
                                      ##__VA_ARGS__)

#define XR_LOG__NOOP(logger, lvl, fmt, ...) \
\
    do \
    { \
        if constexpr (false) \
            XR_LOG__DYNAMIC(logger, lvl, fmt, ##__VA_ARGS__); \
    } while (0)

#ifdef _DEBUG
#define XR_LOG__DYNAMIC_DEBUG(logger, lvl, fmt, ...) XR_LOG__DYNAMIC(logger, lvl, fmt, ##__VA_ARGS__)
#else
#define XR_LOG__DYNAMIC_DEBUG(logger, lvl, fmt, ...) XR_LOG__NOOP(logger, lvl, fmt, ##__VA_ARGS__)
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL < QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO

#define XR_LOG__TRACE_L3(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::TraceL3, fmt, ##__VA_ARGS__)
#define XR_LOG__TRACE_L2(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::TraceL2, fmt, ##__VA_ARGS__)
#define XR_LOG__TRACE_L1(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::TraceL1, fmt, ##__VA_ARGS__)
#define XR_LOG__DEBUG(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Debug, fmt, ##__VA_ARGS__)

#else

#define XR_LOG__TRACE_L3(logger, fmt, ...) XR_LOG__NOOP(logger, xr::level::TraceL3, fmt, ##__VA_ARGS__)
#define XR_LOG__TRACE_L2(logger, fmt, ...) XR_LOG__NOOP(logger, xr::level::TraceL2, fmt, ##__VA_ARGS__)
#define XR_LOG__TRACE_L1(logger, fmt, ...) XR_LOG__NOOP(logger, xr::level::TraceL1, fmt, ##__VA_ARGS__)
#define XR_LOG__DEBUG(logger, fmt, ...) XR_LOG__NOOP(logger, xr::level::Debug, fmt, ##__VA_ARGS__)

#endif

#define XR_LOG__INFO(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Info, fmt, ##__VA_ARGS__)
#define XR_LOG__NOTICE(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Notice, fmt, ##__VA_ARGS__)
#define XR_LOG__WARNING(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Warning, fmt, ##__VA_ARGS__)
#define XR_LOG__ERROR(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Error, fmt, ##__VA_ARGS__)
#define XR_LOG__CRITICAL(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Critical, fmt, ##__VA_ARGS__)
#define XR_LOG__BACKTRACE(logger, fmt, ...) XR_LOG__DYNAMIC(logger, xr::level::Backtrace, fmt, ##__VA_ARGS__)

#define XR_LOG_TRACE_L3(fmt, ...) XR_LOG__TRACE_L3(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_TRACE_L2(fmt, ...) XR_LOG__TRACE_L2(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_TRACE_L1(fmt, ...) XR_LOG__TRACE_L1(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_DEBUG(fmt, ...) XR_LOG__DEBUG(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_INFO(fmt, ...) XR_LOG__INFO(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_NOTICE(fmt, ...) XR_LOG__NOTICE(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_WARNING(fmt, ...) XR_LOG__WARNING(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_ERROR(fmt, ...) XR_LOG__ERROR(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_CRITICAL(fmt, ...) XR_LOG__CRITICAL(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_BACKTRACE(fmt, ...) XR_LOG__BACKTRACE(XR_LOGGER_SUBSYSTEM, fmt, ##__VA_ARGS__)
#define XR_LOG_DYNAMIC(lvl, fmt, ...) XR_LOG__DYNAMIC(XR_LOGGER_SUBSYSTEM, lvl, fmt, ##__VA_ARGS__)

#define XR_LOG_DYNAMIC_DEBUG(lvl, fmt, ...) XR_LOG__DYNAMIC_DEBUG(XR_LOGGER_SUBSYSTEM, lvl, fmt, ##__VA_ARGS__)

namespace xr
{
using level = quill::LogLevel;

namespace detail
{
constexpr inline auto log_width{100uz};

inline quill::Logger* XR__LOGGER_SUBSYSTEM;

// Some Beyond Berklee. Quill expects a valid '\0'-terminated string for runtime tags.
// We don't want to format thread identificator on hotpath, instead, we encode 8-byte
// structure representing the identificator into a valid 8-byte (7 + '\0') string.
// The backend then does reverse-engineering and formats the actual ID to log prefix.

class alignas(sizeof(u64)) thread_tag final
{
private:
    static constexpr auto base{0x0001010101010101ull};

public:
    enum class exec : u8
    {
        cpu = 0,
        st,
        asio,
        ext
    };

    static const inline tmc::ex_any* const ex_cpu{tmc::cpu_executor().type_erased()};
    static const inline tmc::ex_any* const ex_st{xr::tmc_cpu_st_executor().type_erased()};
    static const inline tmc::ex_any* const ex_asio{tmc::asio_executor().type_erased()};

    u32 tid;
    u16 prio;
    exec ex;
    u8 pad;

    constexpr explicit thread_tag(exec ex, std::size_t prio, std::size_t tid) noexcept
        : tid{gsl::narrow_cast<u32>(tid)}, prio{gsl::narrow_cast<u16>(prio)}, ex{ex}, pad{0}
    {}

    constexpr explicit thread_tag(std::array<char, 8> tag) noexcept { *this = std::bit_cast<thread_tag>(std::bit_cast<u64>(tag) - base); }

    [[nodiscard]] constexpr ICF auto encode() const noexcept { return std::bit_cast<std::array<char, 8>>(std::bit_cast<u64>(*this) + base); }
};

static_assert(sizeof(xr::detail::thread_tag) == sizeof(u64));
static_assert(xr::detail::thread_tag{xr::detail::thread_tag::exec::ext, 0xfefe, 0xfefefefe}.encode() == std::array<char, 8>{-1, -1, -1, -1, -1, -1, 4, 0});

[[nodiscard]] constexpr ICF auto this_thread() noexcept
{
    const auto ex = tmc::current_executor();
    const auto prio = tmc::current_priority();

    if (ex == xr::detail::thread_tag::ex_cpu) [[likely]]
        return xr::detail::thread_tag{xr::detail::thread_tag::exec::cpu, prio, tmc::current_thread_index()};
    else if (ex == xr::detail::thread_tag::ex_st)
        return xr::detail::thread_tag{xr::detail::thread_tag::exec::st, prio, 0};
    else if (ex == xr::detail::thread_tag::ex_asio)
        return xr::detail::thread_tag{xr::detail::thread_tag::exec::asio, prio, 0};
    else [[unlikely]]
        return xr::detail::thread_tag{xr::detail::thread_tag::exec::ext, 0, ::__readgsdword(0x48)};
}

void log_init_new();
tmc::task<void> log_run();
void log_create();

[[nodiscard]] bool log_flush();
} // namespace detail

[[nodiscard]] quill::Logger* logger_init(std::string_view name);

inline void logger_init_subsystem() { XR_LOGGER_SUBSYSTEM = xr::logger_init(XR_LOGGER_SUBSYSTEM_NAME); }
} // namespace xr

// Custom formatters

template <enchantum::Enum E>
struct fmtquill::formatter<E> final : fmtquill::formatter<enchantum::string_view>
{
    template <typename FormatContext>
    constexpr auto format(const E e, FormatContext& ctx) const
    {
        return fmtquill::formatter<enchantum::string_view>::format(enchantum::details::format(e), ctx);
    }
};

// Custom codecs

template <>
struct quill::Codec<shared_str> final : quill::Codec<std::string_view>
{};

template <>
struct quill::Codec<xr::last_error> final
{
private:
    using base = quill::Codec<decltype(std::declval<xr::last_error>().code())>;

public:
    [[nodiscard]] static constexpr auto compute_encoded_size(quill::detail::SizeCacheVector& conditional_arg_size_cache, const xr::last_error& err) noexcept
    {
        return base::compute_encoded_size(conditional_arg_size_cache, err.code());
    }

    static constexpr void encode(std::byte*& buffer, const quill::detail::SizeCacheVector& conditional_arg_size_cache, u32& conditional_arg_size_cache_index,
                                 const xr::last_error& err) noexcept
    {
        base::encode(buffer, conditional_arg_size_cache, conditional_arg_size_cache_index, err.code());
    }

    [[nodiscard]] static constexpr auto decode_arg(std::byte*& buffer) { return xr::last_error{base::decode_arg(buffer)}; }
    static constexpr void decode_and_store_arg(std::byte*& buffer, quill::DynamicFormatArgStore* args_store) { args_store->push_back(decode_arg(buffer)); }
};

#endif // !__XRCORE_LOG_H
