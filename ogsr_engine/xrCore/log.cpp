#include "stdafx.h"

#include "log.h"

#include "cpu.h"

#include <bitmap_object_pool.hpp>

#include <fstream>

xr_vector<xr_string> LogFile;
static string_path logFName;

namespace xr
{
namespace
{
class log_pool final
{
public:
    using msg_vec = xr_vector<xr_string>;

private:
    tzcnt_utils::BitmapObjectPool<msg_vec> pool;
    std::atomic<gsl::index> calls{};
    std::ofstream logfs;

public:
    log_pool();
    ~log_pool();

    void open(gsl::czstring path)
    {
        logfs.open(std::filesystem::path{reinterpret_cast<xr::cu8zstring>(path)}, std::ios::binary);
        flush();
    }

    [[nodiscard]] auto acquire_scoped()
    {
        calls++;
        return pool.acquire_scoped();
    }

    void flush();
};

log_pool::log_pool() { pool.acquire_scoped(); }
log_pool::~log_pool() { flush(); }

void log_pool::flush()
{
    if (calls.load(std::memory_order::relaxed) == 0)
        return;

    msg_vec msgs;

    pool.for_each_available([&msgs](auto&& vec) {
        msgs.append_range(std::views::as_rvalue(vec));
        vec.clear();
    });

    std::ranges::stable_sort(msgs, {}, [] [[nodiscard]] (const auto& msg) { return msg.subview(0, xr::detail::log_pfx_len); });
    const auto open = logfs.is_open();

    for (auto&& msg : msgs)
    {
        // Visual Studio
        if (xr::is_debugger_present())
            ::OutputDebugStringW(sf::String::fromUtf8(msg.begin(), msg.end()).toWideString().c_str() + xr::detail::log_pfx_len);

        // Log file
        if (open)
            logfs.write(msg.c_str(), std::ssize(msg));

        // Console history
        const auto count = msg.size() - xr::detail::log_pfx_len - 1;
        const auto& back = LogFile.emplace_back(std::move(msg), xr::detail::log_pfx_len, count);

        static xr_string last_str;
        static gsl::index last_cnt;

        if (last_str == back)
        {
            *(LogFile.end() - 2) = std::move(*(LogFile.end() - 1));
            LogFile.pop_back();

            LogFile.back() += " [" + std::to_string(++last_cnt) + ']';
        }
        else
        {
            last_str = back;
            last_cnt = 1;
        }
    }

    if (open)
        logfs.flush();

    calls.store(0, std::memory_order::relaxed);
}

std::optional<xr::log_pool> log;
} // namespace

namespace detail
{
void log_init()
{
    logFName[0] = '\0';

    xr::log.emplace();
}
} // namespace detail

void log_flush() { xr::log->flush(); }

namespace
{
void add_one(xr::log_pool::msg_vec& vec, xr_string&& split)
{
    const auto orig = split.size();

    split.resize_and_overwrite(xr::detail::log_pfx_len + orig + 1, [orig] [[nodiscard]] (gsl::zstring p, std::size_t size) noexcept {
        const auto now = std::chrono::system_clock::now();
        const auto lt = xr::localtime(std::chrono::system_clock::to_time_t(now));

        std::memmove(p + xr::detail::log_pfx_len, p, orig);
        xr::format_to(p, "[{:02}.{:02}.{:02} {:02}:{:02}:{:02}.{:03}] [{}] ", lt.tm_mday, lt.tm_mon + 1, lt.tm_year % 100, lt.tm_hour, lt.tm_min, lt.tm_sec,
                      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000, CPU::ID.this_thread());
        p[size - 1] = '\n';

        return size;
    });

    vec.emplace_back(std::move(split));
}

void Log(std::string_view str)
{
    if (str.empty())
        return;

    constexpr std::array<char, 5> color_codes{'-', '~', '!', '*', '#'}; // Зелёный, Жёлтый, Красный, Серый, Бирюзовый
    const char color_s = str.front();
    const bool have_color = std::find(color_codes.begin(), color_codes.end(), color_s) != color_codes.end(); // Ищем в начале строки цветовой код

    // Разбиваем текст по "\n"
    xr_vector<xr_string> substrs;
    size_t beg{};

    for (size_t end{}; (end = str.find("\n", end)) != std::string_view::npos; ++end)
    {
        substrs.emplace_back(str.substr(beg, end - beg));
        beg = end + 1;
    }

    substrs.emplace_back(str.substr(beg));

    const auto obj = xr::log->acquire_scoped();
    bool not_first_line{};

    for (auto&& str : substrs)
    {
        if (not_first_line && have_color)
        {
            // Если надо, перед каждой строкой вставляем спец-символ цвета, чтобы в консоли цветными были все строки текста, а не только первая.
            str = ' ' + str;
            str = color_s + str;
        }

        xr::add_one(obj.value, std::move(str));
        not_first_line = true;
    }
}
} // namespace
} // namespace xr

void Log(xr::detail::string_view fmt, xr::detail::format_args args) { xr::Log(xr::detail::vformat(fmt, args)); }

void CreateLog(BOOL nl)
{
    if (nl)
        return;

    if (std::strstr(Core.Params, "-no_unique_logs") == nullptr)
    {
        using namespace std::chrono;

        string32 TimeBuf;
        const auto now = system_clock::now();
        const auto time = system_clock::to_time_t(now);
        const auto lt = xr::localtime(time);

        std::strftime(TimeBuf, sizeof(TimeBuf), "%d-%m-%y_%H-%M-%S", &lt);
        xr_strconcat(logFName, Core.ApplicationName, "_", Core.UserName, "_", TimeBuf, ".log");
    }
    else
    {
        xr_strconcat(logFName, Core.ApplicationName, "_", Core.UserName, ".log");
    }

    std::ignore = FS.update_path(logFName, "$logs$", logFName);
    VerifyPath(logFName);

    xr::log->open(logFName);
}

#include <SFML/System/Err.hpp>

#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <absl/log/log_sink_registry.h>

#include <boost/asio/readable_pipe.hpp>

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wextra-semi");
XR_DIAG_IGNORE("-Wnewline-eof");
XR_DIAG_IGNORE("-Wnrvo");

#include <quill/Frontend.h>

XR_DIAG_POP();

#include <quill/sinks/FileSink.h>

#include <fcntl.h>

namespace xr
{
namespace
{
class redirect final
{
private:
    class std_redir final
    {
    private:
        std::variant<::HANDLE, boost::asio::readable_pipe> read{INVALID_HANDLE_VALUE};
        xr_string buf;

        quill::Logger* logger{nullptr};
        xr::level lvl;

        s32 orig_fd{-1};
        ::HANDLE orig_write{INVALID_HANDLE_VALUE};

    public:
        explicit std_redir(s32 fd);
        ~std_redir();

        tmc::task<void> poll();

    private:
        void print(std::string_view line) const { XR_LOG__DYNAMIC(logger, lvl, "{}", xr::redirect::strip(line)); }
    };

    class sbuf_redir final : public std::streambuf
    {
    private:
        quill::Logger* logger;
        xr_string buf;

    public:
        explicit sbuf_redir(std::string_view name)
        {
            logger = xr::logger_init(name);
            buf.reserve(64);
        }

        ~sbuf_redir() override
        {
            if (!buf.empty())
                print(std::move(buf));
        }

    protected:
        [[nodiscard]] int_type overflow(int_type ch) override;
        [[nodiscard]] std::streamsize xsputn(gsl::czstring s, std::streamsize n) override;
        [[nodiscard]] s32 sync() override;

    private:
        void print(std::string_view line) const { XR_LOG__ERROR(logger, "{}", xr::redirect::strip(line)); }
    };

    class absl_redir final : public absl::LogSink
    {
    private:
        quill::Logger* logger;

    public:
        absl_redir() { logger = xr::logger_init("Abseil"); }
        ~absl_redir() override = default;

        void Send(const absl::LogEntry& entry) override;
    };

    std_redir srout;
    std_redir srerr;

    sbuf_redir sfmls;
    absl_redir absls;

    std::streambuf* sfmlb;

public:
    redirect();
    ~redirect();

    [[nodiscard]] std::tuple<tmc::task<void>, tmc::task<void>> poll() { return {srout.poll(), srerr.poll()}; }

    [[nodiscard]] static constexpr std::string_view strip(std::string_view line)
    {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.remove_suffix(1);

        return line;
    }
};

redirect::std_redir::std_redir(s32 fd)
{
    const bool err = fd == 2;
    const auto name = std::array<std::string_view, 2>{"stdout", "stderr"}[err];

    logger = xr::logger_init(name);
    lvl = err ? xr::level::Error : xr::level::Info;

    const auto path = std::filesystem::path{"\\\\.\\pipe"} / xr::format("redir_{}_{}", std::this_thread::get_id(), name);

    const auto rh =
        ::CreateNamedPipeW(path.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 64 * 1024, 64 * 1024, 0, nullptr);
    if (rh == INVALID_HANDLE_VALUE)
    {
        XR_LOG_ERROR("Failed to create read pipe for {}: {}", name, xr::GetLastError());
        return;
    }

    ::SECURITY_ATTRIBUTES attr{};
    attr.nLength = sizeof(attr);
    attr.bInheritHandle = true;

    const auto write = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, &attr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (write == INVALID_HANDLE_VALUE)
    {
        XR_LOG_ERROR("Failed to create write pipe for {}: {}", name, xr::GetLastError());

        ::CloseHandle(rh);
        return;
    }

    orig_write = ::GetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    orig_fd = ::_dup(fd);

    (err ? std::cerr : std::cout) << std::flush;
    ::fflush(err ? stderr : stdout);

    ::SetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE, write);

    const auto pfd = ::_open_osfhandle(std::bit_cast<std::intptr_t>(write), _O_WRONLY);
    if (pfd == -1)
    {
        XR_LOG_ERROR("Failed to replace write pipe for {}: {}", name, std::error_code{errno, std::generic_category()});

        ::CloseHandle(rh);
        return;
    }

    ::_dup2(pfd, fd);
    ::_close(pfd);

    read.emplace<::HANDLE>(rh);
}

redirect::std_redir::~std_redir()
{
    const bool err = lvl == xr::level::Error;

    (err ? std::cerr : std::cout) << std::flush;
    ::fflush(err ? stderr : stdout);

    if (const auto stream = std::get_if<boost::asio::readable_pipe>(&read); stream != nullptr)
        stream->close();

    if (orig_write != INVALID_HANDLE_VALUE)
    {
        ::SetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE, orig_write);

        ::_dup2(orig_fd, err ? 2 : 1);
        ::_close(orig_fd);
    }

    if (const auto handle = std::get_if<::HANDLE>(&read); handle != nullptr && *handle != INVALID_HANDLE_VALUE)
        ::CloseHandle(*handle);

    if (!buf.empty())
        print(std::move(buf));
}

tmc::task<void> redirect::std_redir::poll()
{
    const auto hp = std::get_if<::HANDLE>(&read);
    XR_ASSERT(hp != nullptr && *hp != INVALID_HANDLE_VALUE);
    const auto handle = *hp;

    auto& stream = read.emplace<boost::asio::readable_pipe>(tmc::asio_executor());
    boost::system::error_code ec;

    stream.assign(handle, ec);
    XR_ASSERT(!ec, ec.message(), ec.category().name());

    // Minus hidden \0-term
    buf.reserve(4095);

    while (true)
    {
        const auto old = buf.size();
        auto avail = buf.capacity();

        if (avail == old)
        {
            buf.reserve(XR_ASSERT_VAL(avail * 2 <= 64 * 1024));
            avail = buf.capacity();
        }

        buf.resize(avail);

        const auto [ec, len] = co_await stream.async_read_some(boost::asio::buffer(buf.data() + old, avail - old), tmc::aw_asio);
        switch (ec.value())
        {
        case boost::asio::error::eof:
        case boost::asio::error::broken_pipe:
        case boost::asio::error::operation_aborted: co_return;
        default: XR_ASSERT(!ec, ec.message(), ec.category().name());
        }

        buf.resize(old + len);
        if (len == 0)
            continue;

        const auto sz = buf.size();
        std::size_t off{0};

        while (off < sz)
        {
            auto pos = buf.find('\n', off);
            if (pos == xr_string::npos)
                break;

            ++pos;

            print(buf.subview(off, pos - off));
            off = pos;
        }

        if (off == sz)
            buf.clear();
        else if (off > 0)
            buf.erase(0, off);
    }
}

redirect::sbuf_redir::int_type redirect::sbuf_redir::overflow(redirect::sbuf_redir::int_type ch)
{
    if (ch == traits_type::eof())
        return traits_type::not_eof(ch);

    if (const auto c = traits_type::to_char_type(ch); c == '\n')
    {
        print(buf);
        buf.clear();
    }
    else if (c != '\r')
    {
        buf.push_back(c);
    }

    return ch;
}

std::streamsize redirect::sbuf_redir::xsputn(gsl::czstring s, std::streamsize n)
{
    const std::string_view block{s, gsl::narrow_cast<std::size_t>(n)};
    const auto sz = block.size();
    std::size_t off{0};

    while (off < sz)
    {
        auto pos = block.find('\n', off);
        if (pos == std::string_view::npos)
        {
            buf.append_range(block.subview(off, sz - off));
            break;
        }

        ++pos;
        const auto line = block.subview(off, pos - off);

        if (!buf.empty())
        {
            buf.append_range(line);
            print(buf);
            buf.clear();
        }
        else
        {
            print(line);
        }

        off = pos;
    }

    return n;
}

s32 redirect::sbuf_redir::sync()
{
    if (buf.empty())
        return 0;

    print(buf);
    buf.clear();

    return 0;
}

void redirect::absl_redir::Send(const absl::LogEntry& entry)
{
    xr::level lvl;

    switch (const auto sev = entry.log_severity(); sev)
    {
    case absl::LogSeverity::kInfo: lvl = xr::level::Info; break;
    case absl::LogSeverity::kWarning: lvl = xr::level::Warning; break;
    case absl::LogSeverity::kError: lvl = xr::level::Error; break;
    case absl::LogSeverity::kFatal: lvl = xr::level::Critical; break;
    default: lvl = xr::level::Notice; break;
    }

    XR_LOG__DYNAMIC(logger, lvl, "{}", !entry.stacktrace().empty() ? entry.stacktrace() : entry.text_message());
}

enum class backend_state : xr::tmc_atomic_wait_t
{
    initial = 0,
    running,
    exiting,
    exited
};
std::atomic<xr::backend_state> stop{xr::backend_state::initial};

redirect::redirect() : srout{1}, srerr{2}, sfmls{"SFML"}
{
    sfmlb = sf::err().rdbuf(&sfmls);

    // Disable std::cerr sink
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
    absl::InitializeLog();

    absl::AddLogSink(&absls);
}

redirect::~redirect()
{
    Debug.to_log(nullptr);
    xr::stop.store(xr::backend_state::exiting, std::memory_order_release);

    absl::RemoveLogSink(&absls);
    sf::err().rdbuf(sfmlb);
}

class relocatable_file_sink final : public quill::FileSink
{
public:
    using quill::FileSink::FileSink;

    void relocate(std::string_view src, std::filesystem::path dest)
    {
        std::error_code ec;

        std::filesystem::create_directories(dest.parent_path(), ec);
        XR_ASSERT(!ec, "", dest);

        close_file();

        std::filesystem::rename(*reinterpret_cast<std::u8string_view*>(&src), dest, ec);
        XR_ASSERT(!ec, "", dest, src);

        open_file(std::move(dest), "a");
    }
};

const auto pattern = [] {
    quill::PatternFormatterOptions pattern{"%(time) [%(tags)] %(short_source_location:<28) LOG_%(log_level:<9) %(logger:<12) %(message)"};

    pattern.process_tags = [] [[nodiscard]] (gsl::czstring tags) {
        const xr::detail::thread_tag tag{*reinterpret_cast<const std::array<char, 8>*>(tags)};

        if (tag.ex == xr::detail::thread_tag::exec::ext)
            return xr::format("X{:T>5}", tag.tid);

        std::string_view ex;

        switch (tag.ex)
        {
        case xr::detail::thread_tag::exec::cpu: ex = CPU::ID.threads[tag.tid].group.cpu_kind == tmc::topology::cpu_kind::PERFORMANCE ? "PE" : "EF"; break;
        case xr::detail::thread_tag::exec::st: ex = "ST"; break;
        case xr::detail::thread_tag::exec::asio: ex = "AS"; break;
        default: xr::unreachable();
        }

        return xr::format("{}{:02}P{}", ex, tag.tid, tag.prio);
    };

    return pattern;
}();

xr_vector<std::shared_ptr<quill::Sink>> sinks;
std::optional<xr::redirect> redir;
xr_string path;
} // namespace

quill::Logger* logger_init(std::string_view name)
{
    const auto ret = quill::Frontend::create_or_get_logger(xr_string{name}, xr::sinks, xr::pattern);

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL < QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO
    ret->set_log_level(xr::level::TraceL3);
#endif

    return ret;
}

namespace detail
{
void log_init_new()
{
    auto path = (std::filesystem::temp_directory_path() / xr::format("{}", std::this_thread::get_id()) / "tmp.log").u8string();

    sinks.emplace_back(quill::Frontend::create_or_get_sink<xr::relocatable_file_sink>(std::move(*reinterpret_cast<xr_string*>(&path)), [] [[nodiscard]] {
        quill::FileSinkConfig cfg;

        cfg.set_open_mode('w');
        cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);

        return cfg;
    }()));

    *reinterpret_cast<std::u8string*>(&xr::path) = dynamic_cast<xr::relocatable_file_sink*>(xr::sinks[0].get())->get_filename().u8string();

    xr::logger_init_subsystem();
    xr::redir.emplace();
}

tmc::task<void> log_run()
{
    tmc::spawn([] -> tmc::task<void> {
        tmc::asio_safe_timer timer{boost::asio::steady_timer{tmc::asio_executor()}};
        xr::quill_manual_backend worker;

        worker.init([] [[nodiscard]] {
            quill::BackendOptions opts;

            opts.check_printable_char = {};

            return opts;
        }());

        xr::stop.store(xr::backend_state::running, std::memory_order_release);

        while (xr::stop.load(std::memory_order_acquire) == xr::backend_state::running)
        {
            worker.poll(std::chrono::microseconds{50});
            co_await tmc::yield_if_requested();

            const auto [ec] = co_await timer.async_wait_for(std::chrono::microseconds{100});
            XR_ASSERT(!ec, ec.message(), ec.category().name());
        }

        worker.shutdown();

        xr::stop.store(xr::backend_state::exited, std::memory_order_release);
        xr::stop.notify_all();
    }())
        .run_on(tmc::asio_executor())
        .with_priority(xr::tmc_priority_low)
        .detach();

    tmc::spawn_tuple(xr::redir->poll()).run_on(tmc::asio_executor()).with_priority(xr::tmc_priority_low).detach();
    co_return;
}

void log_create()
{
    xr_string name;

    if (!std::string_view{Core.Params}.contains("-no_unique_logs"))
    {
        const auto pos = xr::path.rfind("tmp") + 3;
        const auto len = xr::path.size() - pos - 4;

        name = xr::format("{}_{}{}.log", Core.ApplicationName, Core.UserName, xr::path.subview(pos, len));
    }
    else
    {
        name = xr::format("{}_{}.log", Core.ApplicationName, Core.UserName);
    }

    string_path path;
    std::ignore = FS.update_path(path, "$logs$", name.c_str());

    dynamic_cast<xr::relocatable_file_sink*>(xr::sinks[0].get())->relocate(xr::path, reinterpret_cast<xr::cu8zstring>(path));
    xr::path.assign(path);

    Debug.to_log(xr::path.c_str());
}

bool log_flush()
{
    if (xr::stop.load(std::memory_order_relaxed) != xr::backend_state::running)
        return false;

    xr::stop.store(xr::backend_state::exiting, std::memory_order_release);

    const auto back = quill::detail::LoggerBase::is_current_thread_backend_thread();
    if (!back)
        xr::stop.wait(xr::backend_state::exiting);

    return back;
}
} // namespace detail
} // namespace xr
