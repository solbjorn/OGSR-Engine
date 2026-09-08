#include "stdafx.h"

#include "LocatorAPI.h"

#include "FS_internal.h"
#include "stream_reader.h"

// wingdi.h
#undef ERROR

#include <dwarfs/reader/filesystem_options.h>

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wdeprecated-copy-with-user-provided-dtor");
XR_DIAG_IGNORE("-Wfloat-equal");
XR_DIAG_IGNORE("-Wmicrosoft-cpp-macro");
XR_DIAG_IGNORE("-Wnan-infinity-disabled");
XR_DIAG_IGNORE("-Wold-style-cast");
XR_DIAG_IGNORE("-Wsign-conversion");
XR_DIAG_IGNORE("-Wzero-as-null-pointer-constant");

#include <dwarfs/reader/filesystem_v2.h>

XR_DIAG_POP();

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>

namespace xr
{
namespace
{
class dwfs_logger final : public dwarfs::logger
{
private:
#ifdef _DEBUG
    static constexpr auto thresh = dwarfs::logger::level_type::INFO;
    using policy = dwarfs::debug_logger_policy;
#else
    static constexpr auto thresh = dwarfs::logger::level_type::WARN;
    using policy = dwarfs::prod_logger_policy;
#endif

    quill::Logger* logger;

public:
    dwfs_logger()
    {
        logger = xr::logger_init("DwarFS");
        set_policy<policy>();
    }

    ~dwfs_logger() override = default;

    [[nodiscard]] dwarfs::logger::level_type threshold() const override { return thresh; }

    void write(dwarfs::logger::level_type level, std::string_view output, dwarfs::source_location loc) override;
};

void dwfs_logger::write(dwarfs::logger::level_type level, std::string_view output, dwarfs::source_location loc)
{
    if (level > thresh)
        return;

    xr::level lvl;

    switch (level)
    {
    case dwarfs::logger::level_type::FATAL: lvl = xr::level::Critical; break;
    case dwarfs::logger::level_type::ERROR: lvl = xr::level::Error; break;
    case dwarfs::logger::level_type::WARN: lvl = xr::level::Warning; break;
    case dwarfs::logger::level_type::INFO: lvl = xr::level::Info; break;
    case dwarfs::logger::level_type::VERBOSE:
    case dwarfs::logger::level_type::DEBUG: lvl = xr::level::Debug; break;
    case dwarfs::logger::level_type::TRACE: lvl = xr::level::TraceL1; break;
    default: lvl = xr::level::Notice; break;
    }

    const auto ctx = dwarfs::get_logger_context(loc);

    XR_LOG__DYNAMIC(logger, lvl, "{}{}", ctx, output);
    XR_ASSERT(level != dwarfs::logger::level_type::FATAL, "DwarFS fatal error", ctx, output);
}

static_assert(sizeof(dwarfs::reader::filesystem_v2) == sizeof(uintptr_t));

[[nodiscard]] constexpr auto& dwfs_cb(uintptr_t& cb) { return *reinterpret_cast<dwarfs::reader::filesystem_v2*>(&cb); }
[[nodiscard]] constexpr const auto& dwfs_cb(const uintptr_t& cb) { return *reinterpret_cast<const dwarfs::reader::filesystem_v2*>(&cb); }

// Stream reader for a file inside DwarFS

class dwfs_stream final : public CStreamReader
{
    RTTI_DECLARE_TYPEINFO(xr::dwfs_stream, CStreamReader);

private:
    static constexpr std::array<gsl::index, 4> wnd{0, 1024, 4096, 16384};

    const uintptr_t cb;
    const u64 inode;

    xr_vector<std::byte> buf;

    const gsl::index fbase;
    const gsl::index fsize;

    gsl::index foff{};
    gsl::index last{};

    gsl::index bsize;
    gsl::index boff{};

public:
    dwfs_stream() = delete;
    explicit dwfs_stream(uintptr_t cb, u64 inode, gsl::index base, gsl::index sz);
    ~dwfs_stream() override = default;

    void close() override;

    [[nodiscard]] gsl::index elapsed() const override { return fsize - tell(); }
    [[nodiscard]] gsl::index length() const override { return fsize; }
    [[nodiscard]] gsl::index tell() const override { return foff + boff; }
    void seek(gsl::index offset) override { advance(offset - tell()); }

    void advance(gsl::index offset) override;
    void r(void* buffer, gsl::index buffer_size) override;
    [[nodiscard]] CStreamReader* open_chunk(u32 chunk_id) override;
};

dwfs_stream::dwfs_stream(uintptr_t cb, u64 inode, gsl::index base, gsl::index sz) : cb{cb}, inode{inode}, fbase{base}, fsize{sz}
{
    bsize = std::min(wnd[3], fsize);
    buf.reserve(gsl::narrow_cast<size_t>(bsize));
}

void dwfs_stream::close()
{
    auto self = this;
    xr_delete(self);
}

void dwfs_stream::advance(gsl::index offset)
{
    const auto noff = boff + offset;
    if (noff >= 0 && noff < bsize)
    {
        boff = noff;
        return;
    }

    foff += noff;

    const auto woff = xr::rounddown(foff, wnd[3]);
    boff = foff - woff;
    foff = woff;
}

void dwfs_stream::r(void* buffer, gsl::index buffer_size)
{
    auto out = std::span{static_cast<std::byte*>(buffer), gsl::narrow_cast<size_t>(std::min(buffer_size, elapsed()))};

    while (!out.empty())
    {
        if (foff != last)
            buf.resize(wnd[0]);

        auto read = std::ssize(buf);
        if (read == bsize)
            goto copy;

        if (boff == 0 && read == wnd[0])
        {
            const auto direct = xr::rounddown(std::ssize(out), wnd[3]);
            if (direct > 0)
            {
                const auto sz = gsl::narrow_cast<size_t>(direct);
                std::error_code ec;

                const auto ret = xr::dwfs_cb(cb).read(gsl::narrow_cast<u32>(inode), reinterpret_cast<char*>(&out[0]), sz, fbase + foff, ec);
                XR_ASSERT(ret == sz && !ec, "", ret, sz, ec);

                out = out.subspan(sz);
                last = foff;
                advance(direct);

                if (out.empty())
                    return;
            }
        }

        if (const auto end = boff + std::ssize(out); end > read)
        {
            gsl::index precache;

            if (read == wnd[0] && fsize <= wnd[3])
                precache = fsize;
            else if (read == wnd[0] && end <= wnd[1])
                precache = wnd[1];
            else if (read <= wnd[1] && end <= wnd[2])
                precache = wnd[2] - read;
            else
                precache = wnd[3] - read;

            buf.resize(gsl::narrow_cast<size_t>(read + precache));
            precache = std::min(precache, fsize - foff - read);

            const auto sz = gsl::narrow_cast<size_t>(precache);
            std::error_code ec;

            const auto ret =
                xr::dwfs_cb(cb).read(gsl::narrow_cast<u32>(inode), reinterpret_cast<char*>(&buf[gsl::narrow_cast<size_t>(read)]), sz, fbase + foff + read, ec);
            XR_ASSERT(ret == sz && !ec, "", ret, sz, ec);

            read += precache;
        }

    copy:
        const auto copy = std::min(std::ssize(out), read - boff);
        std::memcpy(&out[0], &buf[gsl::narrow_cast<size_t>(boff)], gsl::narrow_cast<size_t>(copy));

        out = out.subspan(gsl::narrow_cast<size_t>(copy));
        last = foff;
        advance(copy);
    }
}

CStreamReader* dwfs_stream::open_chunk(u32 chunk_id)
{
    ::BOOL compressed;

    const auto size = find_chunk(chunk_id, &compressed);
    if (size == 0)
        return nullptr;

    XR_ASSERT(!compressed, "cannot stream compressed chunks", chunk_id);

    return xr_new<xr::dwfs_stream>(cb, inode, tell(), size);
}

std::optional<xr::dwfs_logger> lg;
const dwarfs::os_access_generic os;

constexpr dwarfs::reader::filesystem_options opts{.image_offset = dwarfs::reader::filesystem_options::IMAGE_OFFSET_AUTO,
                                                  .block_cache{
                                                      .decompress_ratio = 0.8,
                                                      .sequential_access_detector_threshold = 4,
                                                      .allocation_mode = dwarfs::reader::block_cache_allocation_mode::MMAP,
                                                  },
                                                  .metadata{
                                                      .block_size = 512 * 1024,
                                                  }};
} // namespace
} // namespace xr

// DwarFS

void CLocatorAPI::archive::open_dwfs()
{
    if (!xr::lg)
        xr::lg.emplace();

    xr::dwfs_cb(cb) = dwarfs::reader::filesystem_v2{
        *xr::lg, xr::os, std::u8string_view{reinterpret_cast<xr::cu8zstring>(path.c_str()), gsl::narrow_cast<std::size_t>(path.size())}, xr::opts};
}

void CLocatorAPI::archive::autoload_dwfs()
{
    const auto head = xr::dwfs_cb(cb).header();
    if (!head)
        return;

    const auto data = head->as_string();
    IReader rd{data.c_str(), std::ssize(data) + 1};

    header = std::make_unique<CInifile>(&rd, path.c_str());
}

void CLocatorAPI::archive::index_dwfs(CLocatorAPI& loc, gsl::czstring fs_entry_point) const
{
    xr::dwfs_cb(cb).walk([this, &loc, fs_entry_point](dwarfs::reader::dir_entry_view entry) {
        const auto iv = entry.inode();
        if (!iv.is_regular_file())
            return;

        const auto st = xr::dwfs_cb(cb).getattr(iv);

        loc.Register(fs_entry_point, entry.path().c_str(), vfs_idx, iv.inode_num(), st.size(), st.mtime());
    });
}

IReader* CLocatorAPI::archive::read_dwfs(const struct file& desc, u32) const
{
    const auto sz = gsl::narrow_cast<size_t>(desc.size_real);
    std::error_code ec;

    auto dest = xr_alloc<std::byte>(desc.size_real);
    const auto ret = xr::dwfs_cb(cb).read(gsl::narrow_cast<u32>(desc.cb), reinterpret_cast<char*>(dest), sz, ec);
    XR_ASSERT(ret == sz && !ec, "", ret, sz, ec);

    return xr_new<CTempReader>(dest, desc.size_real, 0z);
}

CStreamReader* CLocatorAPI::archive::stream_dwfs(const struct file& desc) const { return xr_new<xr::dwfs_stream>(cb, desc.cb, 0, desc.size_real); }

void CLocatorAPI::archive::close_dwfs() { const auto fs = std::move(xr::dwfs_cb(cb)); }
