#include "stdafx.h"

#include "xr_ini.h"

CInifile* pSettings = nullptr;

CInifile* CInifile::Create(gsl::czstring szFileName, bool ReadOnly) { return xr_new<CInifile>(szFileName, ReadOnly); }
void CInifile::Destroy(CInifile* ini) { xr_delete(ini); }

// Тело функций Inifile

namespace
{
void _parse(gsl::zstring dest, gsl::czstring src)
{
    if (src)
    {
        bool bInsideSTR = false;

        while (*src)
        {
            if (std::isspace(gsl::narrow_cast<u8>(*src)))
            {
                if (bInsideSTR)
                {
                    *dest++ = *src++;
                    continue;
                }

                while (*src && std::iswspace(gsl::narrow_cast<wint_t>(*src)))
                    src++;

                continue;
            }
            else if (*src == '"')
            {
                bInsideSTR = !bInsideSTR;
            }

            *dest++ = *src++;
        }
    }

    *dest = '\0';
}

void _decorate(gsl::zstring dest, gsl::czstring src)
{
    if (src)
    {
        bool bInsideSTR = false;

        while (*src)
        {
            if (*src == ',')
            {
                if (bInsideSTR)
                    *dest++ = *src++;
                else
                {
                    *dest++ = *src++;
                    *dest++ = ' ';
                }

                continue;
            }
            else if (*src == '"')
            {
                bInsideSTR = !bInsideSTR;
            }

            *dest++ = *src++;
        }
    }

    *dest = '\0';
}
} // namespace

bool CInifile::Sect::line_exist(gsl::czstring L, gsl::czstring* val) const
{
    const auto A = Data.find(L);
    if (A != Data.end())
    {
        if (val)
            *val = A->second.c_str();

        return true;
    }

    return false;
}

gsl::czstring CInifile::Sect::r_string(gsl::czstring L) const
{
    //--#SM+#-- [fix for one of "xrDebug - Invalid handler" error log]
    if (L == nullptr || L[0] == '\0')
        XR_LOG_ERROR("Empty line passed: S = [{}], L = [{}]", Name, L);

    return XR_ASSERT_VAL(Data.find(L) != Data.end(), "can't find variable", Name, L)->second.c_str();
}

float CInifile::Sect::r_float(gsl::czstring L) const
{
    const std::string_view val{r_string(L)};

    const auto res = scn::scan_value<f32>(val);
    XR_ASSERT(res, res.error().msg(), Name, L, val);

    return res->value();
}

u32 CInifile::Sect::r_u32(gsl::czstring L) const
{
    const std::string_view val{r_string(L)};

    const auto res = scn::scan_int<u32>(val);
    XR_ASSERT(res, res.error().msg(), Name, L, val);

    return res->value();
}

Fvector3 CInifile::Sect::r_fvector3(gsl::czstring L) const
{
    const std::string_view val{r_string(L)};

    const auto res = scn::scan<f32, f32, f32>(val, "{},{},{}");
    XR_ASSERT(res, res.error().msg(), Name, L, val);

    const auto [x, y, z] = res->values();
    return Fvector3{x, y, z};
}

Ivector2 CInifile::Sect::r_ivector2(gsl::czstring L) const
{
    const std::string_view val{r_string(L)};

    const auto res = scn::scan<s32, s32>(val, "{},{}");
    XR_ASSERT(res, res.error().msg(), Name, L, val);

    const auto [x, y] = res->values();
    return Ivector2{x, y};
}

gsl::index CInifile::Sect::line_count() const { return std::ssize(Data); }

CInifile::CInifile(IReader* F, gsl::czstring path)
{
    fName = nullptr;
    bReadOnly = true;
    bSaveAtEnd = false;

    Load(F, path, false, nullptr, true, "");
}

CInifile::CInifile(gsl::czstring szFileName, bool ReadOnly, bool bLoad, bool SaveAtEnd)
{
    fName = szFileName ? xr_strdup(szFileName) : nullptr;
    bReadOnly = !!ReadOnly;
    bSaveAtEnd = SaveAtEnd;

    if (bLoad)
    {
        load_file(false);
    }
}

CInifile::~CInifile()
{
    if (!bReadOnly && bSaveAtEnd)
    {
        if (!save_as())
            XR_LOG_ERROR("Can't save inifile: [{}]", fName);
    }

    xr_free(fName);

    Ordered_DATA.clear();

    for (auto& I : DATA)
        xr_delete(I.second);

    DATA.clear();
}

namespace
{
void insert_item(CInifile::Sect* tgt, const CInifile::Item& I)
{
    auto sect_it = tgt->Data.find(I.first);
    if (sect_it != tgt->Data.end() && sect_it->first.equal(I.first))
    {
        sect_it->second = I.second;
        auto found = std::find_if(tgt->Ordered_Data.begin(), tgt->Ordered_Data.end(), [&](const auto& it) { return std::is_eq(xr_strcmp(it.first, I.first)); });
        if (found != tgt->Ordered_Data.end())
            found->second = I.second;
    }
    else
    {
        tgt->Data.emplace(I.first, I.second);
        tgt->Ordered_Data.push_back(I);
    }
}
} // namespace

void CInifile::Load(IReader* F, gsl::czstring path, bool allow_dup_sections, const CInifile* override, bool root_level, gsl::czstring current_file)
{
    XR_ASSERT(F != nullptr);

    string4096 str;
    string4096 str2;

    auto AddOrOverride = [&]() {
        auto I = DATA.find(Current->Name);
        if (I != DATA.end())
        {
            XR_ASSERT(allow_dup_sections, "duplicate section found", Current->Name);

            Sect* existing = (I->second);
            for (auto& it : Current->Ordered_Data)
            {
                Item I = {it.first, it.second};
                insert_item(existing, I);
            }

            xr_delete(Current);
        }
        else
        {
            if (override)
            {
                auto O = override->DATA.find(Current->Name);
                if (O != override->DATA.end())
                {
                    XR_LOG_NOTICE("Override section [{}]", Current->Name);

                    Sect* override = (O->second);
                    for (auto& it : override->Ordered_Data)
                    {
                        Item v = {it.first, it.second};
                        insert_item(Current, v);
                    }
                }
            }

            Current->Index = std::ssize(DATA);
            DATA.emplace(Current->Name, Current);
            Ordered_DATA.emplace_back(Current->Name, Current);
        }
    };

    while (!F->eof())
    {
        F->r_string(str, sizeof(str));
        std::ignore = _Trim(str);
        gsl::zstring semi = strchr(str, ';');
        gsl::zstring semi_1 = strchr(str, '/');

        if (semi_1 && (*(semi_1 + 1) == '/') && ((!semi) || (semi && (semi_1 < semi))))
            semi = semi_1;

        if (semi)
            *semi = 0;

        if (str[0] && (str[0] == '#') && strstr(str, "#include"))
        {
            XR_ASSERT(path != nullptr && path[0] != '\0' && bReadOnly);

            if (string64 inc_name; _GetItem(str, 1, inc_name, '"') != nullptr)
            {
                string_path fn, inc_path, folder;
                strconcat(sizeof(fn), fn, path, inc_name);
                _fullpath(fn, fn, sizeof(fn));
                _splitpath(fn, inc_path, folder, nullptr, nullptr);
                strcat_s(inc_path, folder);

                const auto loadFile = [&](const string_path _fn) {
                    const auto I = XR_ASSERT_VAL(absl::WrapUnique(FS.r_open(_fn)), "can't find include file", path, _fn);

                    I->skip_bom(_fn);
                    Load(I.get(), inc_path, allow_dup_sections, override, false, _fn);
                };

                if (strstr(inc_name, "*.ltx"))
                {
                    FS_FileSet fset;
                    std::ignore = FS.file_list(fset, inc_path, FS_ListFiles, strrchr(fn, '\\') + 1);

                    for (FS_FileSet::iterator it = fset.begin(); it != fset.end(); it++)
                    {
                        gsl::czstring _name = it->name.c_str();
                        string_path _fn;
                        strconcat(sizeof(_fn), _fn, inc_path, _name);

                        if (std::is_eq(xr_strcmp(current_file, _fn)))
                            continue;

                        loadFile(_fn);
                    }
                }
                else
                {
                    loadFile(fn);
                }
            }
        }
        else if (str[0] && (str[0] == '['))
        {
            // insert previous filled section
            if (Current)
            {
                AddOrOverride();
                Current = nullptr;
            }

            Current = xr_new<Sect>();

            // start new section
            XR_ASSERT(std::strchr(str, ']') != nullptr, "bad ini section found", str);

            gsl::czstring inherited_names = strstr(str, "]:");
            if (inherited_names)
            {
                inherited_names += 2;

                if (bReadOnly)
                {
                    const auto cnt = _GetItemCount(inherited_names);

                    for (gsl::index k{}; k < cnt; ++k)
                    {
                        xr_string tmp;
                        std::ignore = _GetItem(inherited_names, k, tmp);

                        Sect& inherited = r_section(tmp.c_str());
                        for (auto& it : inherited.Ordered_Data)
                        {
                            Item I{it.first, it.second};
                            insert_item(Current, I);
                        }
                    }
                }
                else
                {
                    Current->ParentNames._set(inherited_names);
                }
            }

            *strchr(str, ']') = 0;

            Current->Name._set(_strlwr(str + 1));
        }
        else
        {
            if (Current)
            {
                gsl::zstring name = str;
                gsl::zstring t = strchr(name, '=');

                if (t)
                {
                    *t = 0;
                    std::ignore = _Trim(name);
                    _parse(str2, ++t);
                }
                else
                {
                    std::ignore = _Trim(name);
                    str2[0] = 0;
                }

                if (name[0])
                {
                    Item I{name, str2[0] ? str2 : nullptr};
                    if (bReadOnly)
                    {
                        if (I.first.c_str() != nullptr)
                            insert_item(Current, I);
                    }
                    else
                    {
                        if (I.first.c_str() != nullptr || I.second.c_str() != nullptr)
                            insert_item(Current, I);
                    }
                }
            }
        }
    }

    if (Current && root_level)
    {
        AddOrOverride();
        Current = nullptr;
    }
}

void CInifile::load_file(bool allow_dup_sections, const CInifile* f)
{
    string_path path, folder;
    _splitpath(fName, path, folder, nullptr, nullptr);
    strcat_s(path, folder);

    IReader* R = FS.r_open(fName);
    if (R)
    {
        R->skip_bom(fName);

        Load(R, path, allow_dup_sections, f, true, fName);

        FS.r_close(R);
    }
}

bool CInifile::save_as(gsl::czstring new_fname)
{
    // save if needed
    if (new_fname && new_fname[0])
    {
        xr_free(fName);
        fName = xr_strdup(new_fname);
    }

    XR_ASSERT(fName != nullptr && fName[0] != '\0');

    IWriter* F = FS.w_open_ex(fName);
    if (F)
    {
        xr_vector<Sect*> sorted_List;

        for (const auto& r_it : DATA)
        {
            sorted_List.push_back(r_it.second);
        }

        struct
        {
            bool operator()(Sect* a, Sect* b) const { return a->Index < b->Index; }
        } pred;

        std::ranges::sort(sorted_List, pred);

        for (const auto& r_it : sorted_List)
        {
            const Sect* second = r_it;

            string512 temp, val;
            if (second->ParentNames.size())
            {
                sprintf_s(temp, "[%s]:%s", second->Name.c_str(), second->ParentNames.c_str());
            }
            else
            {
                sprintf_s(temp, "[%s]", second->Name.c_str());
            }

            F->w_string(temp);

            for (const auto& I : second->Ordered_Data)
            {
                if (I.first.c_str() != nullptr)
                {
                    if (I.second.c_str() != nullptr)
                    {
                        _decorate(val, I.second.c_str());

                        // only name and value
                        sprintf_s(temp, "%s = %s", I.first.c_str(), val);
                    }
                    else
                    {
                        // only name
                        sprintf_s(temp, "%s = ", I.first.c_str());
                    }
                }
                else
                {
                    // no name, so no value
                    temp[0] = '\0';
                }

                std::ignore = _TrimRight(temp);

                if (temp[0])
                    F->w_string(temp);
            }

            F->w_string(" ");
        }
        FS.w_close(F);
        return true;
    }
    return false;
}

bool CInifile::section_exist(gsl::czstring S) const { return DATA.find(S) != DATA.end(); }

bool CInifile::line_exist(gsl::czstring S, gsl::czstring L) const
{
    if (!section_exist(S))
        return false;

    Sect& I = r_section(S);
    const auto A = I.Data.find(L);

    return A != I.Data.end();
}

gsl::index CInifile::line_count(gsl::czstring Sname) const { return r_section(Sname).line_count(); }

CInifile::Sect& CInifile::r_section(const shared_str& S) const { return r_section(S.c_str()); }

bool CInifile::line_exist(const shared_str& S, const shared_str& L) const { return line_exist(S.c_str(), L.c_str()); }

gsl::index CInifile::line_count(const shared_str& S) const { return line_count(S.c_str()); }

bool CInifile::section_exist(const shared_str& S) const { return section_exist(S.c_str()); }

CInifile::Sect& CInifile::r_section(gsl::czstring S) const
{
    //--#SM+#--
    XR_ASSERT(S != nullptr && S[0] != '\0', "", fName);

    char section[256];
    strcpy_s(section, S);
    _strlwr(section);

    return *XR_ASSERT_VAL(DATA.find(section) != DATA.end(), "can't open section", fName, S)->second;
}

gsl::czstring CInifile::r_string(gsl::czstring S, gsl::czstring L) const
{
    //--#SM+#-- [fix for one of "xrDebug - Invalid handler" error log]
    if (S == nullptr || L == nullptr || S[0] == '\0' || L[0] == '\0')
        XR_LOG_ERROR("Empty section or line passed: S = [{}], L = [{}]", S, L);

    auto& I = r_section(S);

    return XR_ASSERT_VAL(I.Data.find(L) != I.Data.end(), "can't find variable", fName, S, L)->second.c_str();
}

shared_str CInifile::r_string_wb(gsl::czstring S, gsl::czstring L) const
{
    gsl::czstring _base = r_string(S, L);
    if (!_base)
        return shared_str{};

    string512 _original;
    strcpy_s(_original, _base);

    const auto _len = xr_strlen(_original);
    if (_len == 0)
        return shared_str{""};
    if (_original[_len - 1] == '"')
        _original[_len - 1] = 0; // skip end
    if (_original[0] == '"')
        return shared_str{&_original[0] + 1}; // skip begin

    return shared_str{_original};
}

u8 CInifile::r_u8(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_int<u8>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

u8 CInifile::r_u8_hex(gsl::czstring sect, gsl::czstring line) const
{
    const std::string_view val{r_string(sect, line)};

    const auto res = scn::scan_int<u8>(val, 16);
    XR_ASSERT(res, res.error().msg(), fName, sect, line, val);

    return res->value();
}

u16 CInifile::r_u16(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_int<u16>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

u16 CInifile::r_u16_hex(gsl::czstring sect, gsl::czstring line) const
{
    const std::string_view val{r_string(sect, line)};

    const auto res = scn::scan_int<u16>(val, 16);
    XR_ASSERT(res, res.error().msg(), fName, sect, line, val);

    return res->value();
}

u32 CInifile::r_u32(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_int<u32>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

u32 CInifile::r_u32_hex(gsl::czstring sect, gsl::czstring line) const
{
    const std::string_view val{r_string(sect, line)};

    const auto res = scn::scan_int<u32>(val, 16);
    XR_ASSERT(res, res.error().msg(), fName, sect, line, val);

    return res->value();
}

s8 CInifile::r_s8(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_int<s8>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

s16 CInifile::r_s16(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_int<s16>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

s32 CInifile::r_s32(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_int<s32>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

float CInifile::r_float(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan_value<f32>(val);
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    return res->value();
}

Fcolor CInifile::r_fcolor(gsl::czstring S, gsl::czstring L) const
{
    if (const std::string_view val{r_string(S, L)}; std::ranges::count(val, ',') == 3)
    {
        const auto res = scn::scan<f32, f32, f32, f32>(val, "{},{},{},{}");
        XR_ASSERT(res, res.error().msg(), fName, S, L, val);

        const auto [r, g, b, a] = res->values();
        return Fcolor{r, g, b, a};
    }
    else
    {
        const auto res = scn::scan<f32, f32, f32>(val, "{},{},{}");
        XR_ASSERT(res, res.error().msg(), fName, S, L, val);

        const auto [r, g, b] = res->values();
        return Fcolor{r, g, b, 1.0f};
    }
}

u32 CInifile::r_color(gsl::czstring S, gsl::czstring L) const
{
    if (const std::string_view val{r_string(S, L)}; std::ranges::count(val, ',') == 3)
    {
        const auto res = scn::scan<u8, u8, u8, u8>(val, "{},{},{},{}");
        XR_ASSERT(res, res.error().msg(), fName, S, L, val);

        const auto [r, g, b, a] = res->values();
        return color_rgba(r, g, b, a);
    }
    else
    {
        const auto res = scn::scan<u8, u8, u8>(val, "{},{},{}");
        XR_ASSERT(res, res.error().msg(), fName, S, L, val);

        const auto [r, g, b] = res->values();
        return color_rgba(r, g, b, 255);
    }
}

Ivector2 CInifile::r_ivector2(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan<s32, s32>(val, "{},{}");
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    const auto [x, y] = res->values();
    return Ivector2{x, y};
}

Ivector3 CInifile::r_ivector3(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan<s32, s32, s32>(val, "{},{},{}");
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    const auto [x, y, z] = res->values();
    return Ivector3{x, y, z};
}

Ivector4 CInifile::r_ivector4(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan<s32, s32, s32, s32>(val, "{},{},{},{}");
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    const auto [x, y, z, w] = res->values();
    return Ivector4{x, y, z, w};
}

Fvector2 CInifile::r_fvector2(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan<f32, f32>(val, "{},{}");
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    const auto [x, y] = res->values();
    return Fvector2{x, y};
}

Fvector3 CInifile::r_fvector3(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan<f32, f32, f32>(val, "{},{},{}");
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    const auto [x, y, z] = res->values();
    return Fvector3{x, y, z};
}

Fvector4 CInifile::r_fvector4(gsl::czstring S, gsl::czstring L) const
{
    const std::string_view val{r_string(S, L)};

    const auto res = scn::scan<f32, f32, f32, f32>(val, "{},{},{},{}");
    XR_ASSERT(res, res.error().msg(), fName, S, L, val);

    const auto [x, y, z, w] = res->values();
    return Fvector4{x, y, z, w};
}

bool CInifile::r_bool(gsl::czstring S, gsl::czstring L) const { return IsBOOL(r_string(S, L)); }

CLASS_ID CInifile::r_clsid(gsl::czstring S, gsl::czstring L) const
{
    gsl::czstring C = r_string(S, L);
    return TEXT2CLSID(C);
}

gsl::index CInifile::r_token(gsl::czstring S, gsl::czstring L, const xr_token* token_list) const
{
    gsl::czstring C = r_string(S, L);

    for (int i = 0; token_list[i].name; i++)
    {
        if (std::is_eq(xr::strcasecmp(C, token_list[i].name)))
            return token_list[i].id;
    }

    return 0;
}

bool CInifile::r_line(gsl::czstring S, gsl::index L, gsl::czstring* N, gsl::czstring* V) const
{
    Sect& SS = r_section(S);
    if (L >= std::ssize(SS.Ordered_Data) || L < 0)
        return false;

    const auto& I = SS.Ordered_Data[gsl::narrow_cast<size_t>(L)];
    *N = I.first.c_str();
    *V = I.second.c_str();

    return true;
}

bool CInifile::r_line(const shared_str& S, gsl::index L, gsl::czstring* N, gsl::czstring* V) const { return r_line(S.c_str(), L, N, V); }

void CInifile::w_clsid(gsl::czstring S, gsl::czstring L, CLASS_ID V)
{
    string16 temp;
    CLSID2TEXT(V, temp);
    w_string(S, L, temp);
}

void CInifile::w_string(gsl::czstring S, gsl::czstring L, gsl::czstring V)
{
    XR_ASSERT(!bReadOnly, "file is read-only", fName, S, L, V);

    // section
    char sect[256];
    _parse(sect, S);
    _strlwr(sect);
    XR_ASSERT(sect[0] != '\0', "invalid section name", fName, S, L, V);

    if (!section_exist(sect))
    {
        // create _new_ section
        Sect* NEW = xr_new<Sect>();
        NEW->Name._set(sect);
        NEW->Index = std::ssize(DATA);
        DATA.emplace(NEW->Name, NEW);
        Ordered_DATA.emplace_back(NEW->Name, NEW);
    }

    // parse line/value
    char line[256];
    _parse(line, L);
    XR_ASSERT(line[0] != '\0', "invalid param name", fName, S, L, V);

    char value[256];
    _parse(value, V);

    // duplicate & insert
    Item I{line, value[0] ? value : nullptr};

    Sect* data = &r_section(sect);
    insert_item(data, I);

    bWasChanged = true;
}

void CInifile::w_u8(gsl::czstring S, gsl::czstring L, u8 V) { w_string(S, L, xr::format("{}", V).c_str()); }
void CInifile::w_u8_hex(gsl::czstring sect, gsl::czstring line, u8 val) { w_string(sect, line, xr::format("{:#x}", val).c_str()); }

void CInifile::w_u16(gsl::czstring S, gsl::czstring L, u16 V) { w_string(S, L, xr::format("{}", V).c_str()); }
void CInifile::w_u16_hex(gsl::czstring sect, gsl::czstring line, u16 val) { w_string(sect, line, xr::format("{:#x}", val).c_str()); }

void CInifile::w_u32(gsl::czstring S, gsl::czstring L, u32 V) { w_string(S, L, xr::format("{}", V).c_str()); }
void CInifile::w_u32_hex(gsl::czstring sect, gsl::czstring line, u32 val) { w_string(sect, line, xr::format("{:#x}", val).c_str()); }

void CInifile::w_s8(gsl::czstring S, gsl::czstring L, s8 V) { w_string(S, L, xr::format("{}", V).c_str()); }
void CInifile::w_s16(gsl::czstring S, gsl::czstring L, s16 V) { w_string(S, L, xr::format("{}", V).c_str()); }
void CInifile::w_s32(gsl::czstring S, gsl::czstring L, s32 V) { w_string(S, L, xr::format("{}", V).c_str()); }

void CInifile::w_float(gsl::czstring S, gsl::czstring L, float V) { w_string(S, L, xr::format("{}", V).c_str()); }
void CInifile::w_fcolor(gsl::czstring S, gsl::czstring L, const Fcolor& V) { w_string(S, L, xr::format("{}, {}, {}, {}", V.r, V.g, V.b, V.a).c_str()); }

void CInifile::w_color(gsl::czstring S, gsl::czstring L, u32 V)
{
    w_string(S, L, xr::format("{}, {}, {}, {}", color_get_R(V), color_get_G(V), color_get_B(V), color_get_A(V)).c_str());
}

void CInifile::w_ivector2(gsl::czstring S, gsl::czstring L, const Ivector2& V) { w_string(S, L, xr::format("{}, {}", V.x, V.y).c_str()); }
void CInifile::w_ivector3(gsl::czstring S, gsl::czstring L, const Ivector3& V) { w_string(S, L, xr::format("{}, {}, {}", V.x, V.y, V.z).c_str()); }
void CInifile::w_ivector4(gsl::czstring S, gsl::czstring L, const Ivector4& V) { w_string(S, L, xr::format("{}, {}, {}, {}", V.x, V.y, V.z, V.w).c_str()); }

void CInifile::w_fvector2(gsl::czstring S, gsl::czstring L, const Fvector2& V) { w_string(S, L, xr::format("{}, {}", V.x, V.y).c_str()); }
void CInifile::w_fvector3(gsl::czstring S, gsl::czstring L, const Fvector3& V) { w_string(S, L, xr::format("{}, {}, {}", V.x, V.y, V.z).c_str()); }
void CInifile::w_fvector4(gsl::czstring S, gsl::czstring L, const Fvector4& V) { w_string(S, L, xr::format("{}, {}, {}, {}", V.x, V.y, V.z, V.w).c_str()); }

void CInifile::w_bool(gsl::czstring S, gsl::czstring L, bool V) { w_string(S, L, V ? "true" : "false"); }

void CInifile::remove_line(gsl::czstring S, gsl::czstring L)
{
    if (!line_exist(S, L))
        return;

    auto& data = r_section(S);
    data.Data.erase(XR_ASSERT_VAL(data.Data.find(L) != data.Data.end(), "", fName, S, L));

    const auto found =
        XR_ASSERT_VAL(std::ranges::find_if(data.Ordered_Data, [L](const auto& it) { return std::is_eq(xr_strcmp(it.first, L)); }) != data.Ordered_Data.end(),
                      "", fName, S, L);
    data.Ordered_Data.erase(found);
}

void CInifile::remove_section(gsl::czstring S)
{
    if (!section_exist(S))
        return;

    DATA.erase(XR_ASSERT_VAL(DATA.find(S) != DATA.end(), "", fName, S));

    const auto found = XR_ASSERT_VAL(
        std::ranges::find_if(Ordered_DATA, [S](const auto& it) { return std::is_eq(xr_strcmp(it.first, S)); }) != Ordered_DATA.end(), "", fName, S);
    Ordered_DATA.erase(found);
}

CInifile::Sect& CInifile::append_section(gsl::czstring name, Sect* base)
{
    Sect* new_sect = xr_new<Sect>();

    if (base)
    {
        // copy section to own DATA
        Sect old_sect = *base;
        for (auto& it : old_sect.Ordered_Data)
        {
            Item v = {it.first, it.second};
            insert_item(new_sect, v);
        }
    }

    new_sect->Name._set(name);
    new_sect->Index = std::ssize(DATA);

    DATA.emplace(new_sect->Name, new_sect);
    Ordered_DATA.emplace_back(new_sect->Name, new_sect);

    return r_section(name);
}

std::string CInifile::get_as_string() const
{
    std::stringstream str;

    bool first_sect = true;
    for (const auto& r_it : DATA)
    {
        if (!first_sect)
            str << "\r\n";
        first_sect = false;
        str << "[" << r_it.first.c_str() << "]\r\n";
        for (const auto& I : r_it.second->Ordered_Data)
        {
            if (I.first.c_str())
            {
                if (I.second.c_str())
                {
                    string512 val;
                    _decorate(val, I.second.c_str());
                    std::ignore = _TrimRight(val);

                    // only name and value
                    str << I.first.c_str() << " = " << val << "\r\n";
                }
                else
                {
                    // only name
                    str << I.first.c_str() << " =\r\n";
                }
            }
        }
    }

    return str.str();
}
