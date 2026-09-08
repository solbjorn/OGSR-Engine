#include "stdafx.h"

#include "FS_internal.h"
#include "trivial_encryptor.h"

#include <filesystem>

#ifdef DEBUG
u32 g_file_mapped_memory = 0;
u32 g_file_mapped_count = 0;
typedef xr_map<u32, std::pair<u32, shared_str>> FILE_MAPPINGS;
FILE_MAPPINGS g_file_mappings;
static std::mutex g_file_mappings_Mutex;

void register_file_mapping(void* address, const u32& size, LPCSTR file_name)
{
    std::scoped_lock<decltype(g_file_mappings_Mutex)> lock(g_file_mappings_Mutex);

    XR_DEBUG_ASSERT(g_file_mappings.find(*(u32*)&address) == g_file_mappings.end());
    g_file_mappings.try_emplace(*(u32*)&address, size, shared_str(file_name));

    g_file_mapped_memory += size;
    ++g_file_mapped_count;
}

void unregister_file_mapping(void* address, const u32& size)
{
    std::scoped_lock<decltype(g_file_mappings_Mutex)> lock(g_file_mappings_Mutex);

    const auto I = XR_DEBUG_ASSERT_VAL(g_file_mappings.find(*(u32*)&address) != g_file_mappings.end());

    g_file_mapped_memory -= (*I).second.first;
    --g_file_mapped_count;

    g_file_mappings.erase(I);
}

void dump_file_mappings()
{
    const std::scoped_lock lock{g_file_mappings_Mutex};

    XR_LOG_TRACE_L1("Active file mappings ({}):", g_file_mappings.size());

    for (const auto& map : g_file_mappings)
        XR_LOG_TRACE_L1("[{:#010x}][{}][{}]", map.first, map.second.first, map.second.second);
}
#endif // DEBUG

//////////////////////////////////////////////////////////////////////
// Tools
//////////////////////////////////////////////////////////////////////

// Проверяет путь до файла. Если папки в пути отсутствуют - создаёт их.
void VerifyPath(std::string_view path)
{
    const auto lastSepPos = path.find_last_of('\\');
    const auto foldersPath = (lastSepPos != std::string_view::npos) ? path.substr(0, lastSepPos) : path;
    std::error_code e;
    namespace stdfs = std::filesystem;
    stdfs::create_directories(stdfs::path(foldersPath.begin(), foldersPath.end()), e);
    (void)e;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
//---------------------------------------------------
// memory
CMemoryWriter::~CMemoryWriter() { xr_free(data); }

void CMemoryWriter::w(const void* ptr, gsl::index count)
{
    if (position + count > mem_size)
    {
        // reallocate
        if (mem_size == 0)
            mem_size = 1024 * 1024;
        while (mem_size <= (position + count))
            mem_size *= 2;

        if (!data)
            data = static_cast<std::byte*>(xr_malloc(mem_size));
        else
            data = static_cast<std::byte*>(xr_realloc(data, mem_size));
    }

    std::memcpy(data + position, ptr, gsl::narrow_cast<size_t>(count));
    position += count;

    if (position > file_size)
        file_size = position;
}

void CMemoryWriter::reserve(gsl::index count)
{
    mem_size = count;
    data = static_cast<std::byte*>(xr_malloc(mem_size));
}

bool CMemoryWriter::save_to(LPCSTR fn) const
{
    IWriter* F = FS.w_open(fn);
    if (F)
    {
        F->w(pointer(), size());
        FS.w_close(F);
        return true;
    }
    return false;
}

void IWriter::open_chunk(u32 type)
{
    w_u32(type);
    chunk_pos.push(tell());
    w_u32(0); // the place for 'size'
}

void IWriter::close_chunk()
{
    XR_ASSERT(!chunk_pos.empty());

    const auto pos = tell();
    seek(chunk_pos.top());
    w_u32(gsl::narrow<u32>(pos - chunk_pos.top() - 4));
    seek(pos);
    chunk_pos.pop();
}

gsl::index IWriter::chunk_size()
{
    if (chunk_pos.empty())
        return 0;

    return tell() - chunk_pos.top() - 4;
}

void IWriter::w_compressed(void* ptr, gsl::index count, bool encrypt, bool is_ww)
{
    BYTE* dest{};
    gsl::index dest_sz{};
    _compressLZ(&dest, &dest_sz, ptr, count);

    if (encrypt)
        g_trivial_encryptor.encode(dest, dest_sz, dest, is_ww ? trivial_encryptor::key_flag::worldwide : trivial_encryptor::key_flag::russian);

    if (dest && dest_sz)
        w(dest, dest_sz);
    xr_free(dest);
}

void IWriter::w_chunk(u32 type, void* data, gsl::index size, bool encrypt, bool is_ww)
{
    open_chunk(type);

    if (type & CFS_CompressMark)
        w_compressed(data, size, encrypt, is_ww);
    else
        w(data, size);

    close_chunk();
}

void IWriter::w_sdir(const Fvector& D)
{
    Fvector C;
    float mag = D.magnitude();
    if (mag > EPS_S)
    {
        C.div(D, mag);
    }
    else
    {
        C.set(0, 0, 1);
        mag = 0;
    }
    w_dir(C);
    w_float(mag);
}

void IWriter::w_vprintf(xr::detail::string_view fmt, xr::detail::format_args args)
{
    const auto str = xr::detail::vformat(fmt, args);
    w(str.data(), std::ssize(str));
}

//---------------------------------------------------
// base stream
IReader* IReader::open_chunk(u32 ID)
{
    BOOL bCompressed;
    const auto dwSize = find_chunk(ID, &bCompressed);
    if (dwSize != 0)
    {
        if (bCompressed)
        {
            BYTE* dest{};
            gsl::index dest_sz{};

            std::ignore = _decompressLZ(&dest, &dest_sz, pointer(), dwSize);
            return xr_new<CTempReader>(dest, dest_sz, tell() + dwSize);
        }
        else
        {
            return xr_new<IReader>(pointer(), dwSize, tell() + dwSize);
        }
    }

    return nullptr;
}

void IReader::close()
{
    auto* temp = this;
    xr_delete(temp);
}

IReader* IReader::open_chunk_iterator(u32& ID, IReader* _prev)
{
    if (!_prev)
    {
        // first
        rewind();
    }
    else
    {
        // next
        seek(_prev->iterpos);
        _prev->close();
    }

    //	open
    if (elapsed() < gsl::index{sizeof(u32) * 2})
        return nullptr;

    ID = r_u32();
    gsl::index _size{r_u32()};

    // На всякий случай тут тоже так сделаем по аналогии с find_chunk()
    if (elapsed() < _size)
    {
        XR_LOG_ERROR("Chunk [{}] has invalid size [{}], return elapsed size [{}]", ID, _size, elapsed());
        _size = elapsed();
    }

    if (ID & CFS_CompressMark)
    {
        // compressed
        u8* dest{};
        gsl::index dest_sz{};

        std::ignore = _decompressLZ(&dest, &dest_sz, pointer(), _size);
        return xr_new<CTempReader>(dest, dest_sz, tell() + _size);
    }
    else
    {
        // normal
        return xr_new<IReader>(pointer(), _size, tell() + _size);
    }
}

constexpr unsigned char boms[]{0xef, 0xbb, 0xbf};

void IReader::skip_bom(const char* dbg_name)
{
    if (elapsed() < 3)
        return;

    for (const auto& bom : boms)
    {
        if (static_cast<unsigned char>(data[Pos]) != bom)
        {
            seek(0);
            return;
        }

        Pos++;
    }

    XR_LOG_WARNING("Skip BOM for file [{}]", dbg_name);
}

void IReader::r(void* p, gsl::index cnt)
{
    XR_ASSERT(Pos + cnt <= Size);
    std::memcpy(p, pointer(), gsl::narrow_cast<size_t>(cnt));
    advance(cnt);

#ifdef DEBUG
    BOOL bShow = FALSE;
    if (smart_cast<CVirtualFileReader*>(this))
        bShow = TRUE;
    if (bShow)
    {
        FS.dwOpenCounter++;
    }
#endif
}

namespace
{
constexpr bool is_term(char a) { return a == '\r' || a == '\n'; }
} // namespace

gsl::index IReader::advance_term_string()
{
    const char* src = reinterpret_cast<const char*>(data);
    gsl::index sz{};

    while (!eof())
    {
        Pos++;
        sz++;
        if (!eof() && is_term(src[Pos]))
        {
            while (!eof() && is_term(src[Pos]))
                Pos++;

            break;
        }
    }

    return sz;
}

void IReader::r_string(char* dest, gsl::index tgt_sz)
{
    const char* src = reinterpret_cast<const char*>(data) + Pos;
    const auto sz = advance_term_string();
    XR_ASSERT(sz < tgt_sz - 1, "destination buffer overflow", src);

    strncpy(dest, src, gsl::narrow_cast<size_t>(sz));
    dest[sz] = '\0';
}

void IReader::r_string(xr_string& dest)
{
    const char* src = reinterpret_cast<const char*>(data) + Pos;
    const auto sz = advance_term_string();
    dest.assign(src, gsl::narrow_cast<size_t>(sz));
}

void IReader::r_stringZ(char* dest, gsl::index tgt_sz)
{
    const char* src = reinterpret_cast<const char*>(data);
    gsl::index sz{};

    while ((src[Pos] != 0) && (!eof()))
    {
        sz++;
        XR_ASSERT(sz < tgt_sz - 1, "destination buffer overflow", src);
        *dest++ = src[Pos++];
    }

    *dest = 0;

    if (!eof())
        Pos++;
}

void IReader::r_stringZ(shared_str& dest)
{
    const char* src = reinterpret_cast<const char*>(data) + Pos;
    gsl::index size{};

    while ((src[size] != 0) && (!eof()))
    {
        size++;
        Pos++;
    }

    std::string tmp;
    tmp.assign(src, gsl::narrow_cast<size_t>(size));

    dest._set(tmp.c_str());

    // advance(size);

    if (!eof())
        Pos++;
}

void IReader::r_stringZ(xr_string& dest)
{
    const char* src = reinterpret_cast<const char*>(data) + Pos;
    gsl::index size{};

    while ((src[size] != 0) && (!eof()))
    {
        size++;
        Pos++;
    }

    dest.assign(src, gsl::narrow_cast<size_t>(size));

    if (!eof())
        Pos++;
}

void IReader::skip_stringZ()
{
    const char* src = reinterpret_cast<const char*>(data);

    while ((src[Pos] != 0) && (!eof()))
        Pos++;

    Pos++;
}

//---------------------------------------------------
// temp stream
CTempReader::~CTempReader()
{
    auto ptr = const_cast<std::byte*>(data);
    xr_free(ptr);

    data = nullptr;
}

//---------------------------------------------------
// pack stream
CPackReader::~CPackReader()
{
#ifdef DEBUG
    unregister_file_mapping(base_address, Size);
#endif // DEBUG

    UnmapViewOfFile(base_address);
}
//---------------------------------------------------

CVirtualFileReader::CVirtualFileReader(gsl::czstring cFileName)
{
    Pos = 0;

    // Open the file
    hSrcFile =
        XR_ASSERT_VAL(::CreateFileA(cFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) != INVALID_HANDLE_VALUE,
                      "", cFileName, xr::GetLastError());

    LARGE_INTEGER sz;
    GetFileSizeEx(hSrcFile, &sz);
    Size = sz.QuadPart;

    if (Size == 0)
        XR_LOG_WARNING("Found empty file: [{}]", cFileName);

    hSrcMap = XR_ASSERT_VAL(::CreateFileMapping(hSrcFile, nullptr, PAGE_READONLY, 0, 0, nullptr) != INVALID_HANDLE_VALUE, "", cFileName, xr::GetLastError());

    data = static_cast<const std::byte*>(::MapViewOfFile(hSrcMap, FILE_MAP_READ, 0, 0, 0));
    XR_ASSERT(data != nullptr || Size == 0, "", cFileName, xr::GetLastError());

#ifdef DEBUG
    register_file_mapping(data, Size, cFileName);
#endif // DEBUG
}

CVirtualFileReader::~CVirtualFileReader()
{
#ifdef DEBUG
    unregister_file_mapping(data, Size);
#endif // DEBUG

    UnmapViewOfFile(data);
    CloseHandle(hSrcMap);
    CloseHandle(hSrcFile);
}
