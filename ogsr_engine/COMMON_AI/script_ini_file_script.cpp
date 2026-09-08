////////////////////////////////////////////////////////////////////////////
//	Module 		: script_ini_file_script.cpp
//	Created 	: 25.06.2004
//  Modified 	: 25.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Script ini file class export
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "script_ini_file.h"

#include "object_factory.h"

namespace
{
std::tuple<bool, xr_string, xr_string> r_line(const CInifile& self, gsl::czstring sect, gsl::index line)
{
    XR_ASSERT(self.section_exist(sect), "can't find section", sect);
    XR_ASSERT(line < self.line_count(sect), "invalid line number", sect);

    gsl::czstring n{nullptr}, v{nullptr};
    const auto result = self.r_line(sect, line, &n, &v);

    return std::tuple<bool, xr_string, xr_string>(result, n ?: "", v ?: "");
}

void iterate_sections(const CInifile& self, sol::function func)
{
    for (const auto& it : self.sections())
        func(it.first.c_str());
}

CInifile* reload_system_ini()
{
    CInifile::Destroy(pSettings);

    std::unique_ptr<CInifile> tmp;
    string_path fname;

    std::ignore = FS.update_path(fname, "$game_config$", "system_mods.ltx");
    if (FS.exist(fname))
    {
        tmp = std::make_unique<CInifile>(fname, true, false);
        tmp->load_file(true);

        XR_LOG_NOTICE("Applying system_mods.ltx...");
    }

    std::ignore = FS.update_path(fname, "$game_config$", "system.ltx");

    pSettings = xr_new<CInifile>(fname, true, false);
    pSettings->load_file(false, tmp.get());
    XR_ASSERT(!pSettings->sections().empty(), "can't find file", fname);

    return pSettings;
}

[[nodiscard]] inline gsl::czstring r_string(const CInifile& self, gsl::czstring sect, gsl::czstring line) { return self.r_string(sect, line); }

[[nodiscard]] inline s32 r_clsid(const CInifile& self, gsl::czstring sect, gsl::czstring line)
{
    return object_factory().script_clsid(self.r_clsid(sect, line));
}

[[nodiscard]] std::string_view r_string_wb(const CInifile& self, gsl::czstring sect, gsl::czstring line) { return self.r_string_wb(sect, line); }

[[nodiscard]] std::unique_ptr<CInifile> initialize_ini_file(gsl::czstring szFileName, bool updatePath)
{
    string_path full;

    if (updatePath)
    {
        std::ignore = FS.update_path(full, "$game_config$", szFileName);
        szFileName = &full[0];
    }

    if (auto F = absl::WrapUnique(FS.r_open(szFileName)); F)
    {
        // Костыль от ситуации когда в редких случаях почему-то у игроков бьётся создание новых файлов движком - оказывается набит нулями
        // Не понятно почему так происходит, поэтому сделал тут обработку такой ситуации.
        if (F->elapsed() >= gsl::index{sizeof(u8)} && F->r_u8() == 0)
        {
            XR_LOG_ERROR("File [{}] is broken!", szFileName);

            F.reset();
            FS.file_delete(szFileName);
        }
    }

    return std::make_unique<CInifile>(szFileName, true, true, true);
}

[[nodiscard]] inline std::unique_ptr<CInifile> initialize_ini_file(gsl::czstring szFileName) { return initialize_ini_file(szFileName, true); }
} // namespace

template <>
void CScriptIniFile::script_register(sol::state_view& lua)
{
    lua.new_usertype<CInifile>(
        "ini_file", sol::no_constructor, sol::call_constructor,
        sol::factories(sol::resolve<std::unique_ptr<CInifile>(gsl::czstring, bool)>(&initialize_ini_file),
                       sol::resolve<std::unique_ptr<CInifile>(gsl::czstring)>(&initialize_ini_file)),
        "section_exist", sol::resolve<bool(gsl::czstring) const>(&CInifile::section_exist), "line_exist",
        sol::resolve<bool(gsl::czstring, gsl::czstring) const>(&CInifile::line_exist), "line_count",
        sol::resolve<gsl::index(gsl::czstring) const>(&CInifile::line_count), "remove_line", &CInifile::remove_line, "remove_section",
        &CInifile::remove_section, "get_as_string", &CInifile::get_as_string, "name", &CInifile::fname, "save", &CInifile::save_as, "iterate_sections",
        &iterate_sections, "readonly", &CInifile::bReadOnly,

        "r_line", &::r_line, "r_bool", sol::resolve<bool(gsl::czstring, gsl::czstring) const>(&CInifile::r_bool), "r_string", &::r_string, "r_u32",
        sol::resolve<u32(gsl::czstring, gsl::czstring) const>(&CInifile::r_u32), "r_s32",
        sol::resolve<s32(gsl::czstring, gsl::czstring) const>(&CInifile::r_s32), "r_u16",
        sol::resolve<u16(gsl::czstring, gsl::czstring) const>(&CInifile::r_u16), "r_s16",
        sol::resolve<s16(gsl::czstring, gsl::czstring) const>(&CInifile::r_s16), "r_u8", sol::resolve<u8(gsl::czstring, gsl::czstring) const>(&CInifile::r_u8),
        "r_s8", sol::resolve<s8(gsl::czstring, gsl::czstring) const>(&CInifile::r_s8), "r_u32_hex",
        sol::resolve<u32(gsl::czstring, gsl::czstring) const>(&CInifile::r_u32_hex), "r_u16_hex",
        sol::resolve<u16(gsl::czstring, gsl::czstring) const>(&CInifile::r_u16_hex), "r_u8_hex",
        sol::resolve<u8(gsl::czstring, gsl::czstring) const>(&CInifile::r_u8_hex), "r_float",
        sol::resolve<f32(gsl::czstring, gsl::czstring) const>(&CInifile::r_float), "r_vector2",
        sol::resolve<Fvector2(gsl::czstring, gsl::czstring) const>(&CInifile::r_fvector2), "r_vector",
        sol::resolve<Fvector3(gsl::czstring, gsl::czstring) const>(&CInifile::r_fvector3), "r_vector4",
        sol::resolve<Fvector4(gsl::czstring, gsl::czstring) const>(&CInifile::r_fvector4), "r_clsid", &::r_clsid, "r_string_wb", &::r_string_wb,

        "w_bool", &CInifile::w_bool, "w_string", &CInifile::w_string, "w_u32", &CInifile::w_u32, "w_s32", &CInifile::w_s32, "w_u16", &CInifile::w_u16, "w_s16",
        &CInifile::w_s16, "w_u8", &CInifile::w_u8, "w_s8", &CInifile::w_s8, "w_u32_hex", &CInifile::w_u32_hex, "w_u16_hex", &CInifile::w_u16_hex, "w_u8_hex",
        &CInifile::w_u8_hex, "w_float", &CInifile::w_float, "w_vector2", &CInifile::w_fvector2, "w_vector", &CInifile::w_fvector3, "w_vector4",
        &CInifile::w_fvector4);

    lua.set(
        "system_ini", [] { return pSettings; }, "game_ini", [] { return pGameIni; },

        // чтение ini как текста, без возможности сохранить
        "create_ini_file",
        [](gsl::czstring ini_string) {
            IReader reader{static_cast<const void*>(ini_string), xr_strlen(ini_string) + 1};

            return std::make_unique<CInifile>(&reader, FS.get_path("$game_config$")->m_Path);
        },

        "reload_system_ini", &reload_system_ini);
}
