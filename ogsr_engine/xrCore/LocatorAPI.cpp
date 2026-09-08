// LocatorAPI.cpp: implementation of the CLocatorAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "FS_internal.h"
#include "stream_reader.h"
#include "file_stream_reader.h"

#include <filesystem>

#include <direct.h>
#include <fcntl.h>
#include <sys/stat.h>

std::unique_ptr<CLocatorAPI> xr_FS;

namespace
{
struct _open_file final
{
    union
    {
        IReader* _reader;
        CStreamReader* _stream_reader;
    };
    shared_str _fn;
    size_t _used;
};

template <typename T>
struct eq_pointer;

template <>
struct eq_pointer<IReader> final
{
    IReader* _val;
    eq_pointer(IReader* p) : _val(p) {}
    bool operator()(_open_file& itm) const { return _val == itm._reader; }
};

template <>
struct eq_pointer<CStreamReader> final
{
    CStreamReader* _val;
    eq_pointer(CStreamReader* p) : _val(p) {}
    bool operator()(_open_file& itm) const { return _val == itm._stream_reader; }
};

struct eq_fname_free final
{
    shared_str _val;
    eq_fname_free(shared_str s) : _val(s) {}
    bool operator()(_open_file& itm) const { return _val == itm._fn && itm._reader == nullptr; }
};

struct eq_fname_check final
{
    shared_str _val;
    eq_fname_check(shared_str s) : _val(s) {}
    bool operator()(_open_file& itm) const { return _val == itm._fn && itm._reader != nullptr; }
};

xr_vector<_open_file> g_open_files;

void _check_open_file(const shared_str& _fname)
{
    auto it = std::find_if(g_open_files.begin(), g_open_files.end(), eq_fname_check(_fname));
    if (it != g_open_files.end())
        XR_LOG_WARNING("File opened at least twice: [{}]", _fname);
}

_open_file& find_free_item(const shared_str& _fname)
{
    auto it = std::find_if(g_open_files.begin(), g_open_files.end(), eq_fname_free(_fname));
    if (it == g_open_files.end())
    {
        g_open_files.resize(g_open_files.size() + 1);
        _open_file& _of = g_open_files.back();
        _of._fn = _fname;
        _of._used = 0;
        return _of;
    }
    return *it;
}

void setup_reader(CStreamReader* _r, _open_file& _of) { _of._stream_reader = _r; }
void setup_reader(IReader* _r, _open_file& _of) { _of._reader = _r; }

template <typename T>
void _register_open_file(T* _r, LPCSTR _fname)
{
    shared_str f{_fname};
    _check_open_file(f);

    _open_file& _of = find_free_item(f);
    setup_reader(_r, _of);
    _of._used += 1;
}

template <typename T>
void _unregister_open_file(T* _r)
{
    XR_DEBUG_ASSERT_VAL(std::ranges::find_if(g_open_files, eq_pointer<T>(_r)) != g_open_files.end())->_reader = nullptr;
}
} // namespace

void _dump_open_files(int mode)
{
    bool bShow = false;
    if (mode == 1)
    {
        for (const auto& file : g_open_files)
        {
            if (file._reader != nullptr)
            {
                if (!bShow)
                    XR_LOG_DYNAMIC(xr::level::TraceL1, "----opened files");

                bShow = true;
                XR_LOG_DYNAMIC(xr::level::TraceL1, "[{}] fname: {}", file._used, file._fn);
            }
        }
    }
    else
    {
        XR_LOG_DYNAMIC(xr::level::TraceL1, "----un-used");

        for (const auto& file : g_open_files)
        {
            if (file._reader == nullptr)
                XR_LOG_DYNAMIC(xr::level::TraceL1, "[{}] fname: {}", file._used, file._fn);
        }
    }

    if (bShow)
        XR_LOG_DYNAMIC(xr::level::TraceL1, "----total count = [{}]", g_open_files.size());
}

CLocatorAPI::CLocatorAPI()
{
    m_Flags.zero();
    // get page size
    SYSTEM_INFO sys_inf;
    GetSystemInfo(&sys_inf);
    dwAllocGranularity = sys_inf.dwAllocationGranularity;
    m_iLockRescan = 0;
    dwOpenCounter = 0;
}

CLocatorAPI::~CLocatorAPI()
{
    XR_ASSERT(m_iLockRescan == 0);
    _dump_open_files(1);
}

void CLocatorAPI::Register(gsl::czstring entry, gsl::czstring name, gsl::index vfs, u64 cb, s64 size, s64 modif)
{
    string_path temp_file_name;
    xr_strconcat(temp_file_name, entry, name);

    for (auto pos = &temp_file_name[0];; ++pos)
    {
        pos = std::strchr(pos, '/');
        if (pos == nullptr)
            break;

        *pos = '\\';
    }

    xr_strlwr(temp_file_name);

    // Register file
    file desc;
    //	desc.name			= xr_strlwr(xr_strdup(name));
    desc.name = temp_file_name;
    desc.vfs = vfs;
    desc.size_real = size;
    desc.cb = cb;
    desc.modif = modif;

    //	if file already exist - update info
    files_it I = files.find(desc);
    if (I != files.end())
    {
        desc.name = I->name;
        // sad but true, performance option
        // correct way is to erase and then insert new record:
        const_cast<file&>(*I) = desc;

        return;
    }
    else
    {
        desc.name = xr_strdup(desc.name);
    }

    // otherwise insert file
    files.insert(desc);

    // Try to register folder(s)
    string_path temp;
    strcpy_s(temp, desc.name);
    string_path path;
    string_path folder;
    while (temp[0])
    {
        _splitpath(temp, path, folder, nullptr, nullptr);
        if (!folder[0])
            break;

        strcat_s(path, folder);
        if (!exist(path))
        {
            desc.name = xr_strdup(path);
            desc.vfs = VFS_STANDARD_FILE;
            desc.size_real = 0;
            desc.cb = 0;
            desc.modif = modif;
            desc.folder = true;

            XR_ASSERT(files.insert(desc).second, "", path);
        }

        strcpy_s(temp, path); // strcpy_s(temp, folder);
        if (xr_strlen(temp) > 0)
            temp[xr_strlen(temp) - 1] = 0;
    }
}

// Archives

void CLocatorAPI::archive::open()
{
    // Open the file
    if (hSrcFile != nullptr && cb != 0)
        return;

    hSrcFile =
        XR_ASSERT_VAL(::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) != INVALID_HANDLE_VALUE,
                      "", path, xr::GetLastError());

    LARGE_INTEGER sz;
    GetFileSizeEx(hSrcFile, &sz);
    size = XR_ASSERT_VAL(sz.QuadPart > 0, "", path);

    XR_ASSERT(::SetFilePointer(hSrcFile, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER, "", path, xr::GetLastError());

    switch (fmt)
    {
    case format::dwfs: open_dwfs(); break;
    case format::sqfs: open_sqfs(); break;
    case format::zip: open_zip(); break;
    case format::ar: open_ar(); break;
    case format::db: open_db(); break;
    default: xr::unreachable();
    }
}

IC bool CLocatorAPI::archive::autoload()
{
    switch (fmt)
    {
    case format::dwfs: autoload_dwfs(); break;
    case format::sqfs: autoload_sqfs(); break;
    case format::zip: autoload_zip(); break;
    case format::ar: autoload_ar(); break;
    case format::db: autoload_db(); break;
    default: xr::unreachable();
    }

    if (!header || !header->section_exist("header"))
    {
        header.reset();
        return true;
    }

    return header->line_exist("header", "auto_load") ? header->r_bool("header", "auto_load") : true;
}

IC gsl::czstring CLocatorAPI::archive::entry_point() const
{
    if (!header || !header->line_exist("header", "entry_point"))
        return nullptr;

    return header->r_string("header", "entry_point");
}

IC void CLocatorAPI::archive::index(CLocatorAPI& loc, gsl::czstring fs_entry_point) const
{
    switch (fmt)
    {
    case format::dwfs: index_dwfs(loc, fs_entry_point); break;
    case format::sqfs: index_sqfs(loc, fs_entry_point); break;
    case format::zip: index_zip(loc, fs_entry_point); break;
    case format::ar: index_ar(loc, fs_entry_point); break;
    case format::db: index_db(loc, fs_entry_point); break;
    default: xr::unreachable();
    }
}

IC IReader* CLocatorAPI::archive::read(const struct file& desc, u32 gran) const
{
    switch (fmt)
    {
    case format::dwfs: return read_dwfs(desc, gran);
    case format::sqfs: return read_sqfs(desc, gran);
    case format::zip: return read_zip(desc, gran);
    case format::ar: return read_ar(desc, gran);
    case format::db: return read_db(desc, gran);
    default: xr::unreachable();
    }
}

IC CStreamReader* CLocatorAPI::archive::stream(const struct file& desc) const
{
    switch (fmt)
    {
    case format::dwfs: return stream_dwfs(desc);
    case format::sqfs: return stream_sqfs(desc);
    case format::zip: return stream_zip(desc);
    case format::ar: return stream_ar(desc);
    case format::db: return stream_db(desc);
    default: xr::unreachable();
    }
}

void CLocatorAPI::archive::close()
{
    switch (fmt)
    {
    case format::dwfs: close_dwfs(); break;
    case format::sqfs: close_sqfs(); break;
    case format::zip: close_zip(); break;
    case format::ar: close_ar(); break;
    case format::db: close_db(); break;
    default: xr::unreachable();
    }

    CloseHandle(hSrcFile);
    hSrcFile = nullptr;
}

void CLocatorAPI::LoadArchive(archive& A)
{
    // Create base path
    string_path fs_entry_point;
    fs_entry_point[0] = '\0';

    gsl::czstring read_path{A.entry_point() ?: "gamedata"};
    if (std::is_eq(xr::strcasecmp(read_path, "gamedata")))
    {
        read_path = "$fs_root$";

        const auto P = pathes.find(read_path);
        if (P != pathes.end())
            xr_strcpy(fs_entry_point, sizeof(fs_entry_point), P->second->m_Path);

        xr_strcat(fs_entry_point, "gamedata\\");
    }
    else
    {
        XR_ASSERT(read_path[0] == '$', "", A.path, read_path);

        const auto res = scn::scan<std::string_view>(std::string_view{read_path}, "{:[^\\]}");
        XR_ASSERT(res, res.error().msg(), A.path, read_path);
        const auto alias_name = res->value();

        const auto P = pathes.find(alias_name);
        if (P != pathes.end())
            xr_strcpy(fs_entry_point, sizeof(fs_entry_point), P->second->m_Path);

        xr_strcat(fs_entry_point, sizeof(fs_entry_point), read_path + xr_strlen(alias_name) + 1);
    }

    // Read FileSystem
    A.open();
    A.index(*this, fs_entry_point);
}

void CLocatorAPI::ProcessArchive(gsl::czstring path, CLocatorAPI::archive::format fmt)
{
    const std::string_view pv{path};

    if (std::ranges::find_if(archives, [pv](const auto& arc) { return std::is_eq(xr_strcmp(arc.path, pv)); }) != archives.end())
        return;

    // open archive
    auto& A = archives.emplace_back(fmt);
    A.vfs_idx = std::ssize(archives) - 1;
    A.path._set(path);

    A.open();

    bool bProcessArchiveLoading = A.autoload();
    if (bProcessArchiveLoading || strstr(Core.Params, "-auto_load_arch"))
        LoadArchive(A);
    else
        A.close();
}

void CLocatorAPI::ProcessOne(LPCSTR path, const _FINDDATA_T& F, bool bNoRecurse)
{
    string_path N;
    strcpy_s(N, path);
    strcat_s(N, F.name);
    xr_strlwr(N);

    if (F.attrib & _A_HIDDEN)
        return;

    if (F.attrib & _A_SUBDIR)
    {
        if (bNoRecurse)
            return;

        if (std::is_eq(xr_strcmp(F.name, ".")))
            return;
        if (std::is_eq(xr_strcmp(F.name, "..")))
            return;

        strcat_s(N, "\\");

        std::ignore = RecurseScanPhysicalPath(N, false, bNoRecurse);
        return;
    }

    constexpr auto match_arch = [] [[nodiscard]] (gsl::czstring ext) {
        static constexpr std::array<std::pair<std::string_view, CLocatorAPI::archive::format>, 5> formats{{{".dw", CLocatorAPI::archive::format::dwfs},
                                                                                                           {".sq", CLocatorAPI::archive::format::sqfs},
                                                                                                           {".z", CLocatorAPI::archive::format::zip},
                                                                                                           {".xdb", CLocatorAPI::archive::format::db},
                                                                                                           {".db", CLocatorAPI::archive::format::db}}};

        if (ext == nullptr)
            return CLocatorAPI::archive::format::unknown;

        const std::string_view our{ext};

        if (std::ranges::find_if(xr::fsgame::formats::archive, [our](auto fmt) { return our.starts_with(fmt); }) == xr::fsgame::formats::archive.end())
            return CLocatorAPI::archive::format::unknown;

        if (const auto it = std::ranges::find_if(formats, [our](auto& fmt) { return our.starts_with(fmt.first); }); it != formats.end())
            return it->second;

        return CLocatorAPI::archive::format::ar;
    };

    if (!m_Flags.is(flTargetFolderOnly))
    {
        gsl::czstring ext = strext(N);

        if (ext != nullptr && ext - &N[0] >= 4)
        {
            const std::string_view pref{&ext[-4], 4};

            if (std::is_eq(xr_strcmp(pref, ".tar")))
                ext = pref.data();
        }

        if (const auto fmt = match_arch(ext); fmt != CLocatorAPI::archive::format::unknown)
        {
            XR_LOG_INFO("  Found base arch: [{}], size: [{}]", N, F.size);

            ProcessArchive(N, fmt);
            return;
        }
    }

    Register("", N, VFS_STANDARD_FILE, 0, F.size, F.time_write);
}

namespace
{
bool ignore_name(const char* _name)
{
    // ignore windows hidden Thumbs.db
    if (std::is_eq(xr_strcmp(_name, "Thumbs.db")))
        return true;

    // ignore processing ".svn" folders
    if (std::is_eq(xr_strcmp(_name, ".svn")))
        return true;

    // ignore processing ".git" folders
    if (std::is_eq(xr_strcmp(_name, ".git")))
        return true;

    return false;
}

// we need to check for file existance
// because Unicode file names can
// be interpolated by FindNextFile()

bool ignore_path(const char* _path) { return !std::filesystem::exists(_path); }
} // namespace

bool CLocatorAPI::RecurseScanPhysicalPath(const char* path, const bool log_if_found, bool bNoRecurse)
{
    _FINDDATA_T sFile;
    intptr_t hFile;

    string_path N;
    strcpy_s(N, path);
    strcat_s(N, "*.*");

    using FFVec = xr_vector<_FINDDATA_T>;
    FFVec rec_files;
    rec_files.reserve(1224);

    // find all files
    if (-1 == (hFile = _findfirst64(N, &sFile)))
        return false;

    if (log_if_found)
        XR_LOG_INFO(" Found FS dir: [{}]", path);

    string1024 full_path;
    if (m_Flags.test(flNeedCheck))
    {
        strcpy_s(full_path, path);
        strcat_s(full_path, sFile.name);

        // загоняем в вектор для того *.db* приходили в сортированном порядке
        if (!ignore_name(sFile.name) && !ignore_path(full_path))
            rec_files.push_back(sFile);

        while (_findnext64(hFile, &sFile) == 0)
        {
            strcpy_s(full_path, path);
            strcat_s(full_path, sFile.name);
            if (!ignore_name(sFile.name) && !ignore_path(full_path))
                rec_files.push_back(sFile);
        }
    }
    else
    {
        // загоняем в вектор для того *.db* приходили в сортированном порядке
        if (!ignore_name(sFile.name))
            rec_files.push_back(sFile);

        while (_findnext64(hFile, &sFile) == 0)
        {
            if (!ignore_name(sFile.name))
                rec_files.push_back(sFile);
        }
    }

    _findclose(hFile);

    if (log_if_found)
        XR_LOG_INFO("  Files: [{}]", rec_files.size());

    std::ranges::sort(rec_files, [](const _FINDDATA_T& x, const _FINDDATA_T& y) { return std::is_lt(xr_strcmp(x.name, y.name)); });

    for (const auto& el : rec_files)
        ProcessOne(path, el, bNoRecurse);

    // insert self
    if (path && path[0])
        Register("", path, VFS_STANDARD_FILE, 0, 0, 0);

    return true;
}

void CLocatorAPI::_initialize(u32 flags, LPCSTR target_folder, LPCSTR fs_name)
{
    if (m_Flags.is(flReady))
        return;

    const char _delimiter = '|'; //','

    CTimer t;
    t.Start();

    XR_LOG_NOTICE("Initializing File System...");
    const auto M1 = Memory.mem_usage();

    m_Flags.set(flags, TRUE);

    // scan root directory
    string4096 buf;
    IReader* pFSltx{};

    // append working folder
    LPCSTR fs_ltx{};

    // append application path
    if (m_Flags.is(flScanAppRoot))
        std::ignore = append_path("$app_root$", Core.ApplicationPath, nullptr, FALSE);

    if (m_Flags.is(flTargetFolderOnly))
    {
        std::ignore = append_path("$fs_root$", "", nullptr, FALSE);
    }
    else
    {
        // find nearest fs.ltx and set fs_root correctly
        fs_ltx = (fs_name && fs_name[0]) ? fs_name : FSLTX;
        pFSltx = r_open(fs_ltx);

        if (!pFSltx && m_Flags.is(flScanAppRoot))
            pFSltx = r_open("$app_root$", fs_ltx);

        if (!pFSltx)
        {
            if (strstr(Core.Params, "-use-work-dir"))
            {
                string_path currentDir;
                GetCurrentDirectoryA(std::size(currentDir) - 1, currentDir);
                currentDir[std::size(currentDir) - 1] = '\0';

                std::ignore = append_path("$fs_root$", currentDir, nullptr, FALSE);
            }
            else
            {
                string_path tmpAppPath{};
                strcpy_s(tmpAppPath, Core.ApplicationPath);
                if (xr_strlen(tmpAppPath) > 0)
                {
                    tmpAppPath[xr_strlen(tmpAppPath) - 1] = 0;
                    if (strrchr(tmpAppPath, '\\'))
                        *(strrchr(tmpAppPath, '\\') + 1) = 0;

                    std::ignore = append_path("$fs_root$", tmpAppPath, nullptr, FALSE);
                }
                else
                {
                    std::ignore = append_path("$fs_root$", "", nullptr, FALSE);
                }
            }

            pFSltx = r_open("$fs_root$", fs_ltx);
        }
        else
        {
            std::ignore = append_path("$fs_root$", "", nullptr, FALSE);
        }

        XR_LOG_NOTICE("Using fs-ltx: [{}]", fs_ltx);
    }

    //-----------------------------------------------------------
    // append application data path
    // target folder
    if (m_Flags.is(flTargetFolderOnly))
    {
        std::ignore = append_path("$target_folder$", target_folder, nullptr, TRUE);
    }
    else
    {
        XR_ASSERT(pFSltx != nullptr, "cannot open filesystem ltx", fs_ltx);

        // append all pathes
        string_path id, root, add, def, capt;
        LPCSTR lp_add, lp_def, lp_capt;
        string16 b_v;
        string4096 temp;

        while (!pFSltx->eof())
        {
            pFSltx->r_string(buf, sizeof(buf));
            if (buf[0] == ';')
                continue;

            std::ignore = _GetItem(buf, 0, id, '=');

            if (!m_Flags.is(flBuildCopy) && std::is_eq(xr_strcmp(id, "$build_copy$")))
                continue;

            std::ignore = _GetItem(buf, 1, temp, '=');
            const auto cnt = XR_ASSERT_VAL(_GetItemCount(temp, _delimiter) >= 3, "", temp);
            u32 fl{};

            std::ignore = _GetItem(temp, 0, b_v, _delimiter);
            if (CInifile::IsBOOL(b_v))
                fl |= FS_Path::flRecurse;

            std::ignore = _GetItem(temp, 1, b_v, _delimiter);
            if (CInifile::IsBOOL(b_v))
                fl |= FS_Path::flNotif;

            std::ignore = _GetItem(temp, 2, root, _delimiter);
            std::ignore = _GetItem(temp, 3, add, _delimiter);
            std::ignore = _GetItem(temp, 4, def, _delimiter);
            std::ignore = _GetItem(temp, 5, capt, _delimiter);
            xr_strlwr(id);

            xr_strlwr(root);
            lp_add = (cnt >= 4) ? xr_strlwr(add) : nullptr;
            lp_def = (cnt >= 5) ? def : nullptr;
            lp_capt = (cnt >= 6) ? capt : nullptr;

            auto p_it = pathes.find(root);
            FS_Path* P = xr_new<FS_Path>((p_it != pathes.end()) ? p_it->second->m_Path : root, lp_add, lp_def, lp_capt, fl);

            XR_ASSERT(pathes.emplace(xr_strdup(id), P).second, "duplicate lines in filesystem ltx", fs_ltx, id);

#ifndef DEBUG
            m_Flags.set(flCacheFiles, FALSE);
#endif // DEBUG
        }

        r_close(pFSltx);

        XR_ASSERT(path_exist("$app_data_root$"));

        for (auto p_it = pathes.begin(); p_it != pathes.end(); p_it++)
        {
            FS_Path* P = p_it->second;

#ifdef RESTRICT_GAMEDATA
            shared_str id = p_it->first;
            if (std::is_eq(xr_strcmp(id, "$app_data_root$")) || std::is_eq(xr_strcmp(id, "$game_saves$")) || std::is_eq(xr_strcmp(id, "$logs$")) ||
                std::is_eq(xr_strcmp(id, "$screenshots$")))
                std::ignore = RecurseScanPhysicalPath(P->m_Path);
#else
            std::ignore = RecurseScanPhysicalPath(P->m_Path, true, !P->m_Flags.test(FS_Path::flRecurse));
#endif
        }
    }

    XR_LOG_INFO("FS: {} files cached, {} Kb memory used", std::ssize(files), (Memory.mem_usage() - M1) / 1024);

    m_Flags.set(flReady, TRUE);

    XR_LOG_NOTICE("Init FileSystem {} sec", t.GetElapsed_sec());

    const std::string_view params{Core.Params};

    if (const auto pos = params.rfind("-overlaypath"); pos != std::string_view::npos)
    {
        if (pos + 13 < params.size())
        {
            if (const auto res = scn::scan_value<xr_string>(params.substr(pos + 13)); res)
            {
                auto& root = res->value();

                if (auto pLogsPath = FS.get_path("$logs$"); pLogsPath != nullptr)
                    pLogsPath->_set_root(root.c_str());

                if (auto pAppdataPath = FS.get_path("$app_data_root$"); pAppdataPath != nullptr)
                {
                    pAppdataPath->_set_root(root.c_str());
                    rescan_physical_path(pAppdataPath->m_Path, pAppdataPath->m_Flags.is(FS_Path::flRecurse));
                }
            }
            else
            {
                XR_LOG_ERROR("Invalid -overlaypath parameter argument: {}", res.error().msg());
            }
        }
        else
        {
            XR_LOG_ERROR("The -overlaypath parameter requires an argument");
        }
    }

    CreateLog(params.contains("-nolog"));
    xr::detail::log_create();
}

void CLocatorAPI::_destroy()
{
    for (auto& file : files)
    {
        char* str = const_cast<char*>(file.name);
        xr_free(str);
    }

    files.clear();

    for (auto& path : pathes)
    {
        char* str = const_cast<char*>(path.first);
        xr_free(str);

        xr_delete(path.second);
    }

    pathes.clear();

    for (auto& arc : archives)
        arc.close();

    archives.clear();
}

const CLocatorAPI::file* CLocatorAPI::exist(const char* fn)
{
    files_it it = file_find_it(fn);
    return (it != files.end()) ? &(*it) : nullptr;
}

const CLocatorAPI::file* CLocatorAPI::exist(const char* path, const char* name)
{
    string_path temp;
    std::ignore = update_path(temp, path, name);

    return exist(temp);
}

const CLocatorAPI::file* CLocatorAPI::exist(string_path& fn, LPCSTR path, LPCSTR name)
{
    std::ignore = update_path(fn, path, name);
    return exist(fn);
}

const CLocatorAPI::file* CLocatorAPI::exist(string_path& fn, LPCSTR path, LPCSTR name, LPCSTR ext)
{
    string_path nm;
    strconcat(sizeof(nm), nm, name, ext);
    std::ignore = update_path(fn, path, nm);

    return exist(fn);
}

xr_vector<char*>* CLocatorAPI::file_list_open(const char* initial, const char* folder, u32 flags)
{
    XR_ASSERT(initial != nullptr && initial[0] != '\0');

    string_path N;
    std::ignore = update_path(N, initial, folder);

    return file_list_open(N, flags);
}

xr_vector<char*>* CLocatorAPI::file_list_open(const char* _path, u32 flags)
{
    XR_ASSERT(_path != nullptr && _path[0] != '\0');
    XR_DEBUG_ASSERT(flags != 0);

    // проверить нужно ли пересканировать пути
    check_pathes();

    string_path N;

    if (path_exist(_path))
        std::ignore = update_path(N, _path, "");
    else
        strcpy_s(N, _path);

    file desc;
    desc.name = N;
    files_it I = files.find(desc);
    if (I == files.end())
        return nullptr;

    xr_vector<char*>* dest = xr_new<xr_vector<char*>>();
    const auto base_len = xr_strlen(N);

    for (++I; I != files.end(); I++)
    {
        const file& entry = *I;
        if (!std::string_view{entry.name}.starts_with(std::string_view{N, gsl::narrow_cast<size_t>(base_len)}))
            break; // end of list

        const char* end_symbol = entry.name + xr_strlen(entry.name) - 1;
        if ((*end_symbol) != '\\')
        {
            // file
            if ((flags & FS_ListFiles) == 0)
                continue;

            const char* entry_begin = entry.name + base_len;
            if ((flags & FS_RootOnly) && strchr(entry_begin, '\\'))
                continue; // folder in folder

            dest->push_back(xr_strdup(entry_begin));
            LPSTR fname = dest->back();

            if (flags & FS_ClampExt)
            {
                if (const auto ext = strext(fname); ext != nullptr)
                    *ext = '\0';
            }
        }
        else
        {
            // folder
            if ((flags & FS_ListFolders) == 0)
                continue;

            const char* entry_begin = entry.name + base_len;
            if ((flags & FS_RootOnly) && (strchr(entry_begin, '\\') != end_symbol))
                continue; // folder in folder

            dest->push_back(xr_strdup(entry_begin));
        }
    }

    return dest;
}

void CLocatorAPI::file_list_close(xr_vector<char*>*& lst)
{
    if (lst)
    {
        for (xr_vector<char*>::iterator I = lst->begin(); I != lst->end(); I++)
            xr_free(*I);
        xr_delete(lst);
    }
}

gsl::index CLocatorAPI::file_list(FS_FileSet& dest, LPCSTR path, u32 flags, LPCSTR mask)
{
    XR_ASSERT(path != nullptr && path[0] != '\0' && flags != 0, "", flags);

    // проверить нужно ли пересканировать пути
    check_pathes();

    string_path N;
    if (path_exist(path))
        std::ignore = update_path(N, path, "");
    else
        strcpy_s(N, path);

    file desc;
    desc.name = N;
    files_it I = files.find(desc);
    if (I == files.end())
        return 0;

    SStringVec masks;
    _SequenceToList(masks, mask);
    BOOL b_mask = !masks.empty();

    const auto base_len = xr_strlen(N);
    for (++I; I != files.end(); I++)
    {
        const file& entry = *I;
        if (!std::string_view{entry.name}.starts_with(std::string_view{N, gsl::narrow_cast<size_t>(base_len)}))
            break; // end of list

        LPCSTR end_symbol = entry.name + xr_strlen(entry.name) - 1;
        if ((*end_symbol) != '\\')
        {
            // file
            if ((flags & FS_ListFiles) == 0)
                continue;

            LPCSTR entry_begin = entry.name + base_len;
            if ((flags & FS_RootOnly) && strchr(entry_begin, '\\'))
                continue; // folder in folder

            // check extension
            if (b_mask)
            {
                bool bOK = false;
                for (SStringVecIt it = masks.begin(); it != masks.end(); it++)
                {
                    if (PatternMatch(entry_begin, it->c_str()))
                    {
                        bOK = true;
                        break;
                    }
                }
                if (!bOK)
                    continue;
            }

            xr_string fn = entry_begin;

            // insert file entry
            if (flags & FS_ClampExt)
            {
                if (LPCSTR src_ext = strext(entry_begin); src_ext != nullptr)
                {
                    const auto ext_pos = src_ext - entry_begin;
                    fn.replace(gsl::narrow_cast<size_t>(ext_pos), gsl::narrow_cast<size_t>(xr_strlen(src_ext)), "");
                }
            }

            u32 fl = (entry.vfs != VFS_STANDARD_FILE ? FS_File::flVFS : 0);
            dest.emplace(fn, entry.size_real, entry.modif, fl, !(flags & FS_NoLower));
        }
        else
        {
            // folder
            if ((flags & FS_ListFolders) == 0)
                continue;

            LPCSTR entry_begin = entry.name + base_len;
            if ((flags & FS_RootOnly) && (strchr(entry_begin, '\\') != end_symbol))
                continue; // folder in folder

            u32 fl = FS_File::flSubDir | (entry.vfs ? FS_File::flVFS : 0);
            dest.emplace(entry_begin, entry.size_real, entry.modif, fl, !(flags & FS_NoLower));
        }
    }

    return std::ssize(dest);
}

void CLocatorAPI::file_from_cache_impl(IReader*& R, LPSTR fname, const file&) { R = xr_new<CVirtualFileReader>(fname); }

void CLocatorAPI::file_from_cache_impl(CStreamReader*& R, LPSTR fname, const file&)
{
    CFileStreamReader* r = xr_new<CFileStreamReader>();
    r->construct(fname, BIG_FILE_READER_WINDOW_SIZE);
    R = r;
}

template <typename T>
void CLocatorAPI::file_from_cache(T*& R, LPSTR fname, const file& desc, LPCSTR&)
{
    file_from_cache_impl(R, fname, desc);
}

void CLocatorAPI::file_from_archive(IReader*& R, const file& desc) { R = archives[gsl::narrow_cast<size_t>(desc.vfs)].read(desc, dwAllocGranularity); }
void CLocatorAPI::file_from_archive(CStreamReader*& R, const file& desc) { R = archives[gsl::narrow_cast<size_t>(desc.vfs)].stream(desc); }

bool CLocatorAPI::check_for_file(LPCSTR path, LPCSTR _fname, string_path& fname, const file*& desc)
{
    // проверить нужно ли пересканировать пути
    check_pathes();

    // correct path
    strcpy_s(fname, _fname);
    xr_strlwr(fname);

    if (path && path[0])
        std::ignore = update_path(fname, path, fname);

    // Search entry
    file desc_f;
    desc_f.name = fname;

    const auto I = files.find(desc_f);
    if (I == files.end())
        return false;

    ++dwOpenCounter;
    desc = std::to_address(I);

    return true;
}

template <typename T>
T* CLocatorAPI::r_open_impl(LPCSTR path, LPCSTR _fname)
{
    T* R = nullptr;
    string_path fname;
    const file* desc = nullptr;
    LPCSTR source_name = &fname[0];

    if (!check_for_file(path, _fname, fname, desc))
        return nullptr;

    // OK, analyse
    if (VFS_STANDARD_FILE == desc->vfs)
        file_from_cache(R, fname, *desc, source_name);
    else
        file_from_archive(R, *desc);

    if (m_Flags.test(flDumpFileActivity))
        _register_open_file(R, fname);

    return (R);
}

CStreamReader* CLocatorAPI::rs_open(LPCSTR path, LPCSTR _fname) { return (r_open_impl<CStreamReader>(path, _fname)); }
IReader* CLocatorAPI::r_open(LPCSTR path, LPCSTR _fname) { return (r_open_impl<IReader>(path, _fname)); }

void CLocatorAPI::r_close(IReader*& fs)
{
    if (m_Flags.test(flDumpFileActivity))
        _unregister_open_file(fs);

    xr_delete(fs);
}

void CLocatorAPI::r_close(CStreamReader*& fs)
{
    if (m_Flags.test(flDumpFileActivity))
        _unregister_open_file(fs);

    fs->close();
}

IWriter* CLocatorAPI::w_open(LPCSTR path, LPCSTR _fname)
{
    string_path fname;
    strcpy_s(fname, _fname);
    xr_strlwr(fname); //,".$");

    if (path && path[0])
        std::ignore = update_path(fname, path, fname);

    return xr_new<CFileWriter>(fname, false);
}

IWriter* CLocatorAPI::w_open_ex(LPCSTR path, LPCSTR _fname)
{
    string_path fname;
    strcpy_s(fname, _fname);
    xr_strlwr(fname); //,".$");

    if (path && path[0])
        std::ignore = update_path(fname, path, fname);

    return xr_new<CFileWriter>(fname, true);
}

void CLocatorAPI::w_close(IWriter*& S)
{
    if (S)
    {
        XR_ASSERT(!S->fName.empty(), "", S->fName);

        string_path fname;
        strcpy_s(fname, S->fName.c_str());
        bool bReg = S->valid();
        xr_delete(S);

        if (bReg)
        {
            struct _stat64 st;
            _stat64(fname, &st);
            Register("", fname, VFS_STANDARD_FILE, 0, st.st_size, st.st_mtime);
        }
    }
}

CLocatorAPI::files_it CLocatorAPI::file_find_it(LPCSTR fname)
{
    // проверить нужно ли пересканировать пути
    check_pathes();

    file desc_f;
    desc_f.name = fname;

    return files.find(desc_f);
}

void CLocatorAPI::file_delete(LPCSTR path, LPCSTR nm)
{
    string_path fname;
    if (path && path[0])
        std::ignore = update_path(fname, path, nm);
    else
        strcpy_s(fname, nm);

    const files_it I = file_find_it(fname);
    if (I != files.end())
    {
        // remove file
        _unlink(I->name);

        char* str = const_cast<char*>(I->name);
        xr_free(str);

        files.erase(I);
    }
}

void CLocatorAPI::file_copy(LPCSTR src, LPCSTR dest)
{
#if __has_include("../build_config_overrides/trivial_encryptor_ovr.h")
    XR_PANIC("file copying is prohibited");
#else
    if (exist(src))
    {
        IReader* S = r_open(src);
        if (S)
        {
            IWriter* D = w_open(dest);
            if (D)
            {
                D->w(S->pointer(), S->length());
                w_close(D);
            }
            r_close(S);
        }
    }
#endif
}

void CLocatorAPI::file_rename(LPCSTR src, LPCSTR dest, bool bOwerwrite)
{
    files_it S = file_find_it(src);
    if (S != files.end())
    {
        files_it D = file_find_it(dest);
        if (D != files.end())
        {
            if (!bOwerwrite)
                return;

            _unlink(D->name);

            char* str = const_cast<char*>(D->name);
            xr_free(str);

            files.erase(D);

            // Обновим снова, потому что после erase итератор может быть невалидным
            S = XR_ASSERT_VAL(file_find_it(src) != files.end(), "", src);
        }

        file new_desc = *S;

        // remove existing item
        char* str = const_cast<char*>(S->name);
        xr_free(str);

        files.erase(S);

        // insert updated item
        new_desc.name = xr_strlwr(xr_strdup(dest));
        files.insert(new_desc);

        // physically rename file
        VerifyPath(dest);
        rename(src, dest);
    }
}

s64 CLocatorAPI::file_length(LPCSTR src)
{
    auto I = file_find_it(src);
    return I != files.end() ? I->size_real : -1;
}

bool CLocatorAPI::path_exist(LPCSTR path) { return pathes.contains(path); }

FS_Path* CLocatorAPI::append_path(LPCSTR path_alias, LPCSTR root, LPCSTR add, BOOL recursive)
{
    XR_ASSERT(root != nullptr);
    XR_DEBUG_ASSERT(!path_exist(path_alias));

    FS_Path* P = xr_new<FS_Path>(root, add, nullptr, nullptr, 0u);
    bool bNoRecurse = !recursive;

    std::ignore = RecurseScanPhysicalPath(P->m_Path, false, bNoRecurse);
    pathes.emplace(xr_strdup(path_alias), P);

    return P;
}

FS_Path* CLocatorAPI::get_path(LPCSTR path) { return XR_ASSERT_VAL(pathes.find(path) != pathes.end(), "", path)->second; }
LPCSTR CLocatorAPI::update_path(string_path& dest, LPCSTR initial, LPCSTR src) { return get_path(initial)->_update(dest, src); }

s64 CLocatorAPI::get_file_age(LPCSTR nm)
{
    // проверить нужно ли пересканировать пути
    check_pathes();

    const auto I = file_find_it(nm);

    return I != files.end() ? I->modif : 0;
}

void CLocatorAPI::rescan_physical_path(LPCSTR full_path, BOOL bRecurse)
{
    auto I = file_find_it(full_path);
    if (I == files.end())
        return;

    XR_LOG_DEBUG("Files count before rescan: [{}]", files.size());

    const auto base_len = xr_strlen(full_path);

    while (I != files.end())
    {
        const file& entry = *I;
        if (!std::string_view{entry.name}.starts_with(std::string_view{full_path, gsl::narrow_cast<size_t>(base_len)}))
            break; // end of list

        const char* entry_begin = entry.name + base_len;

        if (entry.vfs != VFS_STANDARD_FILE || entry.folder || (!bRecurse && strchr(entry_begin, '\\')))
        {
            I++;
        }
        else
        {
            //  erase item
            char* str = const_cast<char*>(entry.name);
            xr_free(str);

            I = files.erase(I);
        }
    }

    XR_LOG_DEBUG("Files count after cleanup: [{}]", files.size());

    bool bNoRecurse = !bRecurse;
    std::ignore = RecurseScanPhysicalPath(full_path, false, bNoRecurse);

    XR_LOG_DEBUG("Files count after rescan: [{}]", files.size());
}

void CLocatorAPI::rescan_physical_pathes()
{
    m_Flags.set(flNeedRescan, FALSE);

    for (auto p_it = pathes.begin(); p_it != pathes.end(); p_it++)
    {
        FS_Path* P = p_it->second;
        if (P->m_Flags.is(FS_Path::flNeedRescan))
        {
            std::string filepath{P->m_Path};
            bool filepathExists = std::filesystem::exists(filepath);

            // рескан нужно делать только для реальных каталогов на диске
            if (filepathExists)
            {
                rescan_physical_path(P->m_Path, P->m_Flags.is(FS_Path::flRecurse));
            }

            P->m_Flags.set(FS_Path::flNeedRescan, FALSE);
        }
    }
}

void CLocatorAPI::lock_rescan() { m_iLockRescan++; }

void CLocatorAPI::unlock_rescan()
{
    m_iLockRescan--;
    XR_ASSERT(m_iLockRescan >= 0);

    if ((0 == m_iLockRescan) && m_Flags.is(flNeedRescan))
        rescan_physical_pathes();
}

void CLocatorAPI::check_pathes()
{
    if (m_Flags.is(flNeedRescan) && (0 == m_iLockRescan))
    {
        lock_rescan();
        rescan_physical_pathes();
        unlock_rescan();
    }
}
