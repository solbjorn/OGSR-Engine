#include "stdafx.h"

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

#include "../xrCore/stream_reader.h"

namespace sf
{
#include <sndfile.h>
}

namespace xr
{
namespace
{
constexpr sf::SF_VIRTUAL_IO vio{
    // get_filelen
    [] [[nodiscard]] (void* user_data) { return sf::sf_count_t{static_cast<CStreamReader*>(user_data)->length()}; },

    // seek
    [] [[nodiscard]] (sf::sf_count_t offset, s32 whence, void* user_data) {
        auto& file = *static_cast<CStreamReader*>(user_data);
        gsl::index real;

        switch (whence)
        {
        case sf::SF_SEEK_SET: real = offset; break;
        case sf::SF_SEEK_CUR: real = file.tell() + offset; break;
        case sf::SF_SEEK_END: real = file.length() + offset; break;
        default: xr::unreachable();
        }

        real = std::min(real, file.length());
        file.seek(real);

        return sf::sf_count_t{real};
    },

    // read
    [] [[nodiscard]] (void* ptr, sf::sf_count_t count, void* user_data) {
        auto& file = *static_cast<CStreamReader*>(user_data);
        const auto real = std::min(count, file.elapsed());

        file.r(ptr, real);

        return sf::sf_count_t{real};
    },

    // write
    [] [[nodiscard]] (const void*, sf::sf_count_t, void*) -> sf::sf_count_t { XR_PANIC("can't write to a read-only stream"); },
    // tell
    [] [[nodiscard]] (void* user_data) { return sf::sf_count_t{static_cast<CStreamReader*>(user_data)->tell()}; },
};

[[nodiscard]] constexpr CStreamReader* sf_stream(const sf::SNDFILE* snd)
{
    return *reinterpret_cast<CStreamReader* const*>(reinterpret_cast<const std::byte*>(snd) + 8056);
}

[[nodiscard]] bool find_sound(string_path& fn, std::span<const std::string_view> places, std::string_view path)
{
    for (auto& ext : xr::fsgame::formats::sound)
    {
        for (auto& place : places)
        {
            if (FS.exist(fn, place.data(), path.data(), ext.data()))
                return true;
        }
    }

    return false;
}
} // namespace

bool sound_exists(string_path& fn, std::string_view path) { return find_sound(fn, std::array{xr::fsgame::game_sounds}, path); }
} // namespace xr

CSoundRender_Source::~CSoundRender_Source() { unload(); }

void CSoundRender_Source::decompress(void* dest, s64 byte_offset, s64 size, sf::SNDFILE* snd) const
{
    const auto sample_offset = byte_offset / m_wformat.item_size;
    const auto cur_pos = sf::sf_seek(snd, 0, sf::SF_SEEK_CUR);

    if (cur_pos != sample_offset && sf::sf_seek(snd, sample_offset, sf::SF_SEEK_SET) < 0)
        XR_LOG_ERROR("{} File: [{}]", sf::sf_strerror(snd), pname);

    if (const auto frames = size / m_wformat.item_size; sf::sf_readf_float(snd, static_cast<f32*>(dest), frames) < frames)
        XR_LOG_ERROR("{} File: [{}]", sf::sf_strerror(snd), pname);
}

sf::SNDFILE* CSoundRender_Source::open() const
{
    auto file = FS.rs_open(pname.c_str());
    XR_ASSERT(file != nullptr && file->length() > 0, "can't open or invalid sound file", pname);

    sf::SF_INFO info{};
    auto snd = sf::sf_open_virtual(const_cast<sf::SF_VIRTUAL_IO*>(&xr::vio), sf::SFM_READ, &info, file);
    if (snd == nullptr)
    {
        XR_LOG_ERROR("{} File: [{}]", sf::sf_strerror(snd), pname);
        file->close();
    }

    return snd;
}

void CSoundRender_Source::close(sf::SNDFILE*& snd) const
{
    if (snd == nullptr)
        return;

    auto file = xr::sf_stream(snd);

    if (const auto ret = sf::sf_close(snd); ret != 0)
        XR_LOG_ERROR("{} File: [{}]", sf::sf_error_number(ret), pname);

    file->close();
    snd = nullptr;
}

void CSoundRender_Source::LoadWave(LPCSTR pName)
{
    pname._set(pName);

    auto file = FS.rs_open(pName);
    XR_ASSERT(file != nullptr && file->length() > 0, "can't open or invalid sound file", pname);

    sf::SF_INFO info{};
    auto snd = sf::sf_open_virtual(const_cast<sf::SF_VIRTUAL_IO*>(&xr::vio), sf::SFM_READ, &info, file);
    XR_ASSERT(snd != nullptr, sf::sf_strerror(snd), pname);
    XR_ASSERT(xr::sf_stream(snd) == file);

    m_wformat.samplerate = info.samplerate;
    m_wformat.channels = info.channels;
    m_wformat.item_size = sizeof(f32) * info.channels;

    const auto byterate = info.samplerate * m_wformat.item_size;
    bytesPerBuffer = sdef_target_block * byterate / 1000;

    dwBytesTotal = info.frames * m_wformat.item_size;
    fTimeTotal = gsl::narrow_cast<f32>(dwBytesTotal) / gsl::narrow_cast<f32>(byterate);

    const auto fallback = (info.format & sf::SF_FORMAT_SUBMASK) == sf::SF_FORMAT_VORBIS;
    if (!parse_comment(snd, fallback) && fallback)
        parse_legacy_comment(*file);

    XR_ASSERT(m_fMaxDist >= 0.1f && m_fMaxAIDist >= 0.1f, "invalid max distance", pname, m_fMaxDist, m_fMaxAIDist);
    close(snd);
}

bool CSoundRender_Source::parse_comment(sf::SNDFILE* snd, bool fallback)
{
    auto comment = sf::sf_get_string(snd, sf::SF_STR_COMMENT);
    if (comment == nullptr)
    {
        if (!fallback)
            XR_LOG_ERROR("Missing UTF-8 comment, file: [{}]", pname);

        return false;
    }

    IReader rd{comment, xr_strlen(comment) + 1};
    const auto ini = std::make_unique<CInifile>(&rd, pname.c_str());

    if (!ini->section_exist("sound"))
    {
    inv:
        if (!fallback)
            XR_LOG_ERROR("Invalid UTF-8 comment, file: [{}]", pname);

        return false;
    }

    for (auto& line : std::array<std::string_view, 5>{"min_dist", "max_dist", "base_volume", "game_type", "max_ai_dist"})
    {
        if (!ini->line_exist("sound", line.data()))
            goto inv;
    }

    m_fMinDist = ini->r_float("sound", "min_dist");
    m_fMaxDist = ini->r_float("sound", "max_dist");
    m_fBaseVolume = ini->r_float("sound", "base_volume");
    m_uGameType = ini->r_u32("sound", "game_type");
    m_fMaxAIDist = ini->r_float("sound", "max_ai_dist");

    return true;
}

void CSoundRender_Source::parse_legacy_comment(CStreamReader& file)
{
    if (file.length() < 128)
    {
    miss:
        XR_LOG_ERROR("Missing legacy comment, file: [{}]", pname);
        return;
    }

    const auto orig = file.tell();
    const auto rewind = gsl::finally([&file, orig] { file.seek(orig); });

    // Read page 1 segment number and skip their sizes
    file.seek(84);
    file.advance(file.r_u8());

    // Check page 1 magic (0x3 "vorbis")
    static constexpr std::array<u8, 7> magic{0x3, 0x76, 0x6f, 0x72, 0x62, 0x69, 0x73};
    std::array<std::byte, 24> buf;
    file.r(buf.data(), 7);

    if (std::memcmp(buf.data(), magic.data(), magic.size()) != 0)
    {
    inv:
        XR_LOG_ERROR("Invalid legacy comment, file: [{}]", pname);
        return;
    }

    // Read vendor string size and skip it
    file.advance(file.r_u32());

    // The number of OGG comments, must be at least 1
    if (file.r_u32() == 0)
        goto miss;

    // Read the size of comment 0 and validate
    const auto sz = file.r_u32();
    if (sz < 16)
        goto inv;

    file.r(buf.data(), std::min(size_t{sz}, buf.size()));
    IReader comment{buf.data(), sz};

    switch (comment.r_u32())
    {
    case 0x1:
        m_fMinDist = comment.r_float();
        m_fMaxDist = comment.r_float();
        m_fBaseVolume = 1.0f;
        m_uGameType = comment.r_u32();
        m_fMaxAIDist = m_fMaxDist;

        break;
    case 0x2:
        if (sz < 20)
            goto inv;

        m_fMinDist = comment.r_float();
        m_fMaxDist = comment.r_float();
        m_fBaseVolume = comment.r_float();
        m_uGameType = comment.r_u32();
        m_fMaxAIDist = m_fMaxDist;

        break;
    case OGG_COMMENT_VERSION:
        if (sz < 24)
            goto inv;

        m_fMinDist = comment.r_float();
        m_fMaxDist = comment.r_float();
        m_fBaseVolume = comment.r_float();
        m_uGameType = comment.r_u32();
        m_fMaxAIDist = comment.r_float();

        break;
    default: XR_LOG_ERROR("Invalid legacy comment version, file: [{}]", pname); break;
    }
}

void CSoundRender_Source::load(LPCSTR name)
{
    string_path fn, N;
    xr_strcpy(N, name);
    _strlwr(N);

    if (const auto ext = strext(N); ext != nullptr)
        *ext = '\0';

    fname._set(N);

    if (!xr::find_sound(fn, std::array{xr::fsgame::level, xr::fsgame::game_sounds}, fname))
    {
        XR_LOG_ERROR("Can't find sound [{}]", fname);
        XR_ASSERT(xr::find_sound(fn, std::array{xr::fsgame::game_sounds}, "$no_sound"));
    }

    LoadWave(fn);
}

void CSoundRender_Source::unload()
{
    fTimeTotal = 0.0f;
    dwBytesTotal = 0;
}
