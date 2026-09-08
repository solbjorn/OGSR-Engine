////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine.cpp
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "script_engine.h"

#include "MainMenu.h"
#include "ai_space.h"
#include "object_factory.h"

#include "ui/UIWpnParams.h"

#include "../xrExternal/imgui.h"

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wcomma");
XR_DIAG_IGNORE("-Wextra-semi-stmt");
XR_DIAG_IGNORE("-Wunused-parameter");

#include <tracy/TracyLua.hpp>

XR_DIAG_POP();

namespace
{
// FILE_HEADER нужен для того, чтобы классы регистрировались внутри модуля в котором находятся, а не в _G
constexpr std::string_view FILE_HEADER{
    "\
local function script_name() \
return '{0}' \
end; \
local this; \
module('{0}', package.seeall, function(m) this = m end); \
{1}"};

void lua_panic(s32 code)
{
    auto L = ai().script_engine().lua().lua_state();
    xr_string description;
    std::size_t len;

    if (const auto desc = lua_tolstring(L, -1, &len); desc != nullptr)
    {
        description.assign(desc, len);
        lua_pop(L, 1);
    }
    else
    {
        description = "An unexpected error occurred and panic has been invoked";
    }

    XR_PANIC(xr::format("script {} error", sol::to_string(sol::call_status{code})), code, description);
}

[[nodiscard]] std::string lua_trace()
{
    auto trace = ai().script_engine().print_stack();
    if (trace.empty())
        return trace;

    const auto dump = ai().script_engine().dump_state();
    if (!dump.empty())
        trace = xr::format("\nLua {}\n\nLua state dump:{}", trace, dump);
    else
        trace = xr::format("\nLua {}", trace);

    return trace;
}

int auto_load(lua_State* L)
{
    if (lua_gettop(L) < 2 || !lua_istable(L, 1) || !lua_isstring(L, 2))
    {
        lua_pushnil(L);
        return 1;
    }

    std::ignore = ai().script_engine().process_file_if_exists(lua_tostring(L, 2), false);
    lua_rawget(L, 1);

    return 1;
}

bool initialized{false};
} // namespace

CScriptEngine::CScriptEngine() = default;
CScriptEngine::~CScriptEngine() { close(); }

void CScriptEngine::setup_auto_load()
{
    auto L = lua().lua_state();

    lua_pushstring(L, GlobalNamespace.data());
    lua_gettable(L, LUA_GLOBALSINDEX);
    int value_index = lua_gettop(L); // alpet: во избежания оставления в стеке лишней метатаблицы

    luaL_newmetatable(L, "XRAY_AutoLoadMetaTable");
    lua_pushstring(L, "__index");
    lua_pushcfunction(L, auto_load);
    lua_settable(L, -3);

    // luaL_getmetatable(L, "XRAY_AutoLoadMetaTable");
    lua_setmetatable(L, value_index);

    xray_scripts.clear();
}

void CScriptEngine::register_script_classes()
{
    sol::function result = lua()["class_registrator"]["register"];
    result(const_cast<CObjectFactory*>(&object_factory()));
}

void CScriptEngine::reinit()
{
    close();

    m_virtual_machine.emplace();
    initialized = !!m_virtual_machine;

    std::ignore = Debug.set_lua_panic(&lua_panic);
    std::ignore = Debug.set_lua_trace(&lua_trace);
}

void CScriptEngine::init()
{
    reinit();
    XR_ASSERT(xr::script_engine_initialized(), "can't initialize Lua VM!");

    lua().open_libraries();
    tracy::LuaRegister(lua().lua_state());
    sol_ImGui::Init(*m_virtual_machine);

    //-----------------------------------------------------//
    export_classes(lua()); // Тут регистрируются все движковые функции, импортированные в скрипты
    setup_auto_load(); // Построение метатаблицы

    const bool save = m_reload_modules;
    m_reload_modules = true;
    std::ignore = process_file_if_exists(GlobalNamespace.data(), false); // Компиляция _G.script
    m_reload_modules = save;

    register_script_classes(); // Походу, запуск class_registrator.script
    object_factory().register_script(); // Регистрация классов
}

void CScriptEngine::unload()
{
    destroy_lua_wpn_params();

    if (MainMenu())
        MainMenu()->DestroyInternal(true);

    xr_delete(g_object_factory);

    close(); // вызовем тут явно

    no_files.clear();
}

void CScriptEngine::close()
{
    if (!initialized)
        return;

    XR_ASSERT(Debug.set_lua_trace(nullptr) == &lua_trace);
    XR_ASSERT(Debug.set_lua_panic(nullptr) == &lua_panic);

    initialized = false;
    m_virtual_machine.reset();
}

namespace xr
{
bool script_engine_initialized() { return initialized; }
} // namespace xr

namespace
{
gsl::czstring ExtractFileName(gsl::czstring fname)
{
    const auto pos = std::string_view{fname}.find_last_of('\\');

    return pos == std::string_view::npos ? fname : &fname[pos + 1];
}
} // namespace

void CScriptEngine::CollectScriptFiles(gsl::czstring path)
{
    if (xr_strlen(path) == 0)
        return;

    auto folders = FS.file_list_open(path, FS_ListFolders);
    if (folders == nullptr)
        goto files;

    std::ranges::for_each(*folders, [&](gsl::czstring folder) {
        if (std::strchr(folder, '.') != nullptr)
        {
            string_path fname;
            strconcat(sizeof(fname), fname, path, folder);

            CollectScriptFiles(fname);
        }
    });

    FS.file_list_close(folders);

files:
    auto files = FS.file_list_open(path, FS_ListFiles);
    if (files == nullptr)
        return;

    std::ranges::for_each(*files, [&](gsl::czstring file) {
        string_path fname;
        strconcat(sizeof(fname), fname, path, file);

        if (std::strstr(fname, ".script") != nullptr && FS.exist(fname) && FS.file_length(fname) > 0)
        {
            string_path buff;
            strcpy_s(buff, ExtractFileName(fname));
            _strlwr_s(buff);

            std::string_view nspace{buff};
            nspace = nspace.substr(0, nspace.find_last_of('.'));

            const auto it = xray_scripts.find(nspace);
            XR_ASSERT(it == xray_scripts.end(), "script namespace conflict", nspace, fname, it->second);

            xray_scripts.emplace(nspace, fname);
        }
    });

    FS.file_list_close(files);
}

bool CScriptEngine::LookupScript(string_path& fname, gsl::czstring base)
{
    string_path lc_base;

    if (xray_scripts.empty())
    {
        std::ignore = FS.update_path(lc_base, "$game_scripts$", "");
        CollectScriptFiles(lc_base);
    }

    strcpy_s(lc_base, base);
    _strlwr_s(lc_base);

    auto it = xray_scripts.find(lc_base);
    if (it != xray_scripts.end())
    {
        strcpy_s(fname, it->second.c_str());
        return true;
    }

    return false;
}

bool CScriptEngine::namespace_loaded(gsl::czstring name, bool remove_from_stack)
{
    auto L = lua().lua_state();
    int start{
#ifdef DEBUG
        lua_gettop(L)
#endif
    };

    lua_pushstring(L, GlobalNamespace.data());
    lua_rawget(L, LUA_GLOBALSINDEX);

    string256 S2;
    xr_strcpy(S2, name);
    auto S = S2;

    for (;;)
    {
        if (!xr_strlen(S))
        {
            XR_DEBUG_ASSERT(lua_gettop(L) >= 1);
            lua_pop(L, 1);
            XR_DEBUG_ASSERT(lua_gettop(L) == start);

            return false;
        }

        auto S1 = strchr(S, '.');
        if (S1)
            *S1 = 0;

        lua_pushstring(L, S);
        lua_rawget(L, -2);

        if (lua_isnil(L, -1))
        {
            XR_DEBUG_ASSERT(lua_gettop(L) >= 2);
            lua_pop(L, 2);
            XR_DEBUG_ASSERT(lua_gettop(L) == start);

            return false;
        }
        else if (!lua_istable(L, -1))
        {
            XR_DEBUG_ASSERT(lua_gettop(L) >= 1);
            lua_pop(L, 1);
            XR_DEBUG_ASSERT(lua_gettop(L) == start);

            XR_PANIC("namespace is already used by a non-table object", name, S);
        }

        lua_remove(L, -2);

        if (S1)
            S = ++S1;
        else
            break;
    }

    if (remove_from_stack)
    {
        XR_DEBUG_ASSERT(lua_gettop(L) >= 1);
        lua_pop(L, 1);
        XR_DEBUG_ASSERT(lua_gettop(L) == start);
    }
    else
    {
        XR_DEBUG_ASSERT(lua_gettop(L) == start + 1);
    }

    return true;
}

bool CScriptEngine::do_file(gsl::czstring caScriptName, gsl::czstring caNameSpaceName)
{
    // KRodin: обращаться к _G только с большой буквы! Иначе он загрузится ещё раз и это неизвестно к чему приведёт!
    // Глобальное пространство инитится один раз после запуска луаджита, и никогда больше.
    if (std::is_eq(xr_strcmp(caNameSpaceName, "_g")))
        return false;

    const auto l_tpFileReader = absl::WrapUnique(FS.r_open(caScriptName));
    if (!l_tpFileReader)
    {
        // заменить на ассерт?
        XR_LOG_ERROR("Cannot open file [{}]", caScriptName);
        return false;
    }

    l_tpFileReader->skip_bom(caScriptName);

    string_path l_caLuaFileName;
    xr_strconcat(l_caLuaFileName, "@", caScriptName); // KRodin: приводит путь к виду @f:\games\s.t.a.l.k.e.r\gamedata\scripts\***.script

    std::string_view strbuf{static_cast<gsl::czstring>(l_tpFileReader->pointer()), gsl::narrow_cast<size_t>(l_tpFileReader->elapsed())};
    xr_string script;

    if (std::is_neq(xr_strcmp(caNameSpaceName, GlobalNamespace)))
    {
        script = xr::format(FILE_HEADER, caNameSpaceName, strbuf);
        strbuf = script;
    }

    lua().script(strbuf, l_caLuaFileName);

    return true;
}

bool CScriptEngine::process_file_if_exists(gsl::czstring file_name, bool warn_if_not_exist)
{
    // Это для оптимизации, чтоб постоянно не проверять, отсутствует ли этот файл.
    if (!warn_if_not_exist && no_file_exists(file_name))
        return false;

    if (m_reload_modules || (*file_name && !namespace_loaded(file_name)))
    {
        string_path S;
        if (!LookupScript(S, file_name))
        {
            if (warn_if_not_exist)
            {
                XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "Variable {} not found; No script by this name exists, either", file_name);
            }
            else
            {
                XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "-------------------------");
                XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "Access to nonexistent variable or loading nonexistent script '{}'", file_name);
                XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "{}", print_stack());
                XR_LOG_DYNAMIC_DEBUG(xr::level::Warning, "-------------------------");

                add_no_file(file_name);
            }

            return false;
        }

        XR_LOG_TRACE_L1("Loading script: [{}]", file_name);

        m_reload_modules = false;
        return do_file(S, file_name);
    }

    return true;
}

bool CScriptEngine::process_file(gsl::czstring file_name, bool reload_modules)
{
    m_reload_modules = reload_modules;
    bool result = process_file_if_exists(file_name, true);
    m_reload_modules = false;

    return result;
}

void CScriptEngine::parse_script_namespace(gsl::czstring name, gsl::zstring ns, u32 nsSize, gsl::zstring func, u32 funcSize)
{
    auto p = std::strrchr(name, '.');
    if (p != nullptr)
    {
        XR_ASSERT(p - name + 1 <= nsSize);

        std::strncpy(ns, name, p - name);
        ns[p - name] = '\0';
    }
    else
    {
        xr_strcpy(ns, nsSize, GlobalNamespace.data());
        p = name - 1;
    }

    xr_strcpy(func, funcSize, p + 1);
}

bool CScriptEngine::function(gsl::czstring function_to_call, sol::function& func)
{
    if (xr_strlen(function_to_call) == 0)
        return false;

    string256 name_space, function;
    parse_script_namespace(function_to_call, name_space, sizeof(name_space), function, sizeof(function));

    if (std::is_neq(xr_strcmp(name_space, GlobalNamespace)))
    {
        auto file_name = std::strchr(name_space, '.');
        if (file_name == nullptr)
        {
            std::ignore = process_file(name_space);
        }
        else
        {
            *file_name = '\0';
            std::ignore = process_file(name_space);
            *file_name = '.';
        }
    }

    gsl::czstring ns = name_space;
    gsl::czstring fn = function;

    auto x = lua().get<sol::optional<sol::function>>(std::tie(ns, fn));
    if (!x)
        return false;

    func = x.value();
    return true;
}

void CScriptEngine::collect_all_garbage()
{
    for (gsl::index i{0}; i < 4; ++i)
        lua().collect_gc();
}

//*********************************************************************************************

xr_string CScriptEngine::print_stack()
{
    auto L = lua().lua_state();
    luaL_traceback(L, L, nullptr, 0);

    std::size_t len;
    xr_string data;

    const auto stack = lua_tolstring(L, -1, &len);
    if (stack == nullptr)
        return data;

    if (len < xr_strlen("stack traceback:") + 2)
        return data;

    data.assign(stack, len);
    lua_pop(L, 1);

    return data;
}

xr_string CScriptEngine::dump_state()
{
    static std::atomic<bool> hit{false};
    xr_string ret;

    if (bool exp{false}; !hit.compare_exchange_strong(exp, true))
        return ret;

    auto L = lua().lua_state();
    lua_Debug dbg;

    for (s32 i{0}; lua_getstack(L, i, &dbg) != 0; ++i)
    {
        lua_getinfo(L, "nSlu", &dbg);

        if (std::is_neq(xr_strcmp(dbg.what, "C")))
        {
            if (dbg.name != nullptr)
                ret += xr::format("\n{:2}: [{:3}]: {}:{}: in function '{}':{}", i, dbg.what, dbg.short_src, dbg.currentline, dbg.name, dbg.linedefined);
            else
                ret += xr::format("\n{:2}: [{:3}]: {}:{}: in function <{}:{}>", i, dbg.what, dbg.short_src, dbg.currentline, dbg.short_src, dbg.linedefined);
        }
        else
        {
            ret += xr::format("\n{:2}: [C++]: in function '{}'", i, dbg.name != nullptr ? dbg.name : "");
        }

        gsl::czstring name;
        s32 var{1};

        while ((name = lua_getlocal(L, &dbg, var++)) != nullptr)
        {
            ret += LogVariable(L, name, 2);
            lua_pop(L, 1); /* remove variable value */
        }
    }

    hit = false;

    return ret;
}

xr_string CScriptEngine::LogTable(lua_State* l, std::string_view S, s32 level)
{
    xr_string ret;

    if (!lua_istable(l, -1))
        return ret;

    gsl::index i{0};
    lua_pushnil(l);

    while (lua_next(l, -2) != 0)
    {
        // конвертирование не строковых значений в строки, поставит в тупик lua_next
        // https://github.com/defold/defold/issues/9778
        xr_string key;

        const auto type = lua_type(l, -2);
        switch (sol::type{type})
        {
        case sol::type::nil: key = xr::format("{}[nil]", S); break;
        case sol::type::string: {
            std::size_t len;
            key = xr::format("{}.{}", S, std::string_view{lua_tolstring(l, -2, &len), len});
            break;
        }
        case sol::type::number: key = xr::format("{}[{}]", S, lua_tonumber(l, -2)); break;
        case sol::type::boolean: key = xr::format("{}[{}]", S, lua_toboolean(l, -2) ? "true" : "false"); break;
        case sol::type::userdata: key = xr::format("{}[{}]", S, sol::associated_type_name(l, -2, sol::type::userdata)); break;
        default: key = xr::format("{}[{}{}]", S, lua_typename(l, type), i); break;
        }

        ret += LogVariable(l, std::move(key), level);

        lua_pop(l, 1);
        ++i;
    }

    return ret;
}

xr_string CScriptEngine::LogVariable(lua_State* l, std::string_view name, s32 level)
{
    const auto ntype = lua_type(l, -1);
    const auto type = lua_typename(l, ntype);
    xr_string val;

    switch (sol::type{ntype})
    {
    case sol::type::nil: val = "nil"; break;
    case sol::type::string: {
        std::size_t len;
        val = xr::format("\"{}\"", std::string_view{lua_tolstring(l, -1, &len), len});
        break;
    }
    case sol::type::number: val = xr::format("{}", lua_tonumber(l, -1)); break;
    case sol::type::boolean: val = lua_toboolean(l, -1) ? "true" : "false"; break;
    case sol::type::userdata: val = sol::associated_type_name(l, -1, sol::type::userdata); break;
    case sol::type::table: val = level < 5 ? LogTable(l, name, level + 1) : "[...]"; break;
    case sol::type::none:
    case sol::type::thread:
    case sol::type::function:
    case sol::type::lightuserdata: val = xr::format("[{}]", type); break;
    default: val = "[not available]"; break;
    }

    return xr::format("\n{}{} {}: {}", xr_string(level * 4, ' '), type, name, val);
}

#ifdef DEBUG
// Используется в очень многих местах //Очень много пишет в лог.
void CScriptEngine::vscript_log(ScriptStorage::ELuaMessageType message, xr::detail::string_view fmt, xr::detail::format_args args)
{
    xr::level lvl{xr::level::TraceL1};
    std::string_view S;

    switch (message)
    {
    case ScriptStorage::eLuaMessageTypeInfo:
        lvl = xr::level::Info;
        S = "[LUA INFO]";
        break;
    case ScriptStorage::eLuaMessageTypeError:
        lvl = xr::level::Error;
        S = "[LUA ERROR]";
        break;
    case ScriptStorage::eLuaMessageTypeMessage:
        lvl = xr::level::Notice;
        S = "[LUA MESSAGE]";
        break;
    case ScriptStorage::eLuaMessageTypeHookCall: S = "[LUA HOOK_CALL]"; break;
    case ScriptStorage::eLuaMessageTypeHookReturn: S = "[LUA HOOK_RETURN]"; break;
    case ScriptStorage::eLuaMessageTypeHookLine: S = "[LUA HOOK_LINE]"; break;
    case ScriptStorage::eLuaMessageTypeHookCount: S = "[LUA HOOK_COUNT]"; break;
    default: xr::unreachable();
    }

    XR_LOG_DYNAMIC(lvl, "-----------------------------------------");
    XR_LOG_DYNAMIC(lvl, "{} {}", S, xr::detail::vformat(fmt, args));
    XR_LOG_DYNAMIC(lvl, "{}", print_stack());
    XR_LOG_DYNAMIC(lvl, "-----------------------------------------");
}
#endif
