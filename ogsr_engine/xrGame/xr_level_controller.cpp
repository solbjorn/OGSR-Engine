#include "stdafx.h"

#include "xr_level_controller.h"

#include "string_table.h"

#include "../xr_3da/xr_input.h"
#include "../xr_3da/xr_ioc_cmd.h"

#include <SFML/Window/Joystick.hpp>

xr_vector<_action> actions{{
#define DEF_ACTION(a1, a2) {a1, #a2}
    DEF_ACTION("left", kLEFT),
    DEF_ACTION("right", kRIGHT),
    DEF_ACTION("up", kUP),
    DEF_ACTION("down", kDOWN),
    DEF_ACTION("jump", kJUMP),
    DEF_ACTION("crouch", kCROUCH),
    DEF_ACTION("crouch_toggle", kCROUCH_TOGGLE),
    DEF_ACTION("accel", kACCEL),
    DEF_ACTION("sprint_toggle", kSPRINT_TOGGLE),

    DEF_ACTION("forward", kFWD),
    DEF_ACTION("back", kBACK),
    DEF_ACTION("lstrafe", kL_STRAFE),
    DEF_ACTION("rstrafe", kR_STRAFE),

    DEF_ACTION("llookout", kL_LOOKOUT),
    DEF_ACTION("rlookout", kR_LOOKOUT),

    DEF_ACTION("turn_engine", kENGINE),

    DEF_ACTION("cam_1", kCAM_1),
    DEF_ACTION("cam_2", kCAM_2),
    DEF_ACTION("cam_3", kCAM_3),

    DEF_ACTION("torch", kTORCH),
    DEF_ACTION("night_vision", kNIGHT_VISION),
    DEF_ACTION("wpn_1", kWPN_1),
    DEF_ACTION("wpn_2", kWPN_2),
    DEF_ACTION("wpn_3", kWPN_3),
    DEF_ACTION("wpn_4", kWPN_4),
    DEF_ACTION("wpn_5", kWPN_5),
    DEF_ACTION("wpn_6", kWPN_6),
    DEF_ACTION("wpn_8", kWPN_8),
    DEF_ACTION("laser_on", kLASER_ON),
    DEF_ACTION("wpn_next", kWPN_NEXT),
    DEF_ACTION("wpn_fire", kWPN_FIRE),
    DEF_ACTION("wpn_zoom", kWPN_ZOOM),
    DEF_ACTION("wpn_zoom_inc", kWPN_ZOOM_INC),
    DEF_ACTION("wpn_zoom_dec", kWPN_ZOOM_DEC),
    DEF_ACTION("wpn_reload", kWPN_RELOAD),
    DEF_ACTION("wpn_func", kWPN_FUNC),
    DEF_ACTION("wpn_firemode_prev", kWPN_FIREMODE_PREV),
    DEF_ACTION("wpn_firemode_next", kWPN_FIREMODE_NEXT),

    DEF_ACTION("pause", kPAUSE),
    DEF_ACTION("drop", kDROP),
    DEF_ACTION("use", kUSE),
    DEF_ACTION("scores", kSCORES),
    DEF_ACTION("flashlight", kFLASHLIGHT),
    DEF_ACTION("screenshot", kSCREENSHOT),
    DEF_ACTION("quit", kQUIT),
    DEF_ACTION("console", kCONSOLE),
    DEF_ACTION("inventory", kINVENTORY),
    DEF_ACTION("active_jobs", kACTIVE_JOBS),
    DEF_ACTION("map", kMAP),
    DEF_ACTION("contacts", kCONTACTS),
    DEF_ACTION("ext_1", kEXT_1),

    DEF_ACTION("next_slot", kNEXT_SLOT),
    DEF_ACTION("prev_slot", kPREV_SLOT),

    DEF_ACTION("use_bandage", kUSE_BANDAGE),
    DEF_ACTION("use_medkit", kUSE_MEDKIT),

    DEF_ACTION("quick_save", kQUICK_SAVE),
    DEF_ACTION("quick_load", kQUICK_LOAD),

    DEF_ACTION("hide_hud", kHIDEHUD),
    DEF_ACTION("show_hud", kSHOWHUD),
#undef DEF_ACTION
}};

xr_vector<_binding> g_key_bindings;

namespace
{
std::array<_keyboard, sf::Keyboard::ScancodeCount + sf::Mouse::ButtonCount + sf::Joystick::ButtonCount> keyboards{{
#define XR_KEY(k) \
    _keyboard \
    { \
        "Keyboard::" #k, xr::key_id { sf::Keyboard::Scancode::k } \
    }
    XR_KEY(A),
    XR_KEY(B),
    XR_KEY(C),
    XR_KEY(D),
    XR_KEY(E),
    XR_KEY(F),
    XR_KEY(G),
    XR_KEY(H),
    XR_KEY(I),
    XR_KEY(J),
    XR_KEY(K),
    XR_KEY(L),
    XR_KEY(M),
    XR_KEY(N),
    XR_KEY(O),
    XR_KEY(P),
    XR_KEY(Q),
    XR_KEY(R),
    XR_KEY(S),
    XR_KEY(T),
    XR_KEY(U),
    XR_KEY(V),
    XR_KEY(W),
    XR_KEY(X),
    XR_KEY(Y),
    XR_KEY(Z),
    XR_KEY(Num1),
    XR_KEY(Num2),
    XR_KEY(Num3),
    XR_KEY(Num4),
    XR_KEY(Num5),
    XR_KEY(Num6),
    XR_KEY(Num7),
    XR_KEY(Num8),
    XR_KEY(Num9),
    XR_KEY(Num0),
    XR_KEY(Enter),
    XR_KEY(Escape),
    XR_KEY(Backspace),
    XR_KEY(Tab),
    XR_KEY(Space),
    XR_KEY(Hyphen),
    XR_KEY(Equal),
    XR_KEY(LBracket),
    XR_KEY(RBracket),
    XR_KEY(Backslash),
    XR_KEY(Semicolon),
    XR_KEY(Apostrophe),
    XR_KEY(Grave),
    XR_KEY(Comma),
    XR_KEY(Period),
    XR_KEY(Slash),
    XR_KEY(F1),
    XR_KEY(F2),
    XR_KEY(F3),
    XR_KEY(F4),
    XR_KEY(F5),
    XR_KEY(F6),
    XR_KEY(F7),
    XR_KEY(F8),
    XR_KEY(F9),
    XR_KEY(F10),
    XR_KEY(F11),
    XR_KEY(F12),
    XR_KEY(F13),
    XR_KEY(F14),
    XR_KEY(F15),
    XR_KEY(F16),
    XR_KEY(F17),
    XR_KEY(F18),
    XR_KEY(F19),
    XR_KEY(F20),
    XR_KEY(F21),
    XR_KEY(F22),
    XR_KEY(F23),
    XR_KEY(F24),
    XR_KEY(CapsLock),
    XR_KEY(PrintScreen),
    XR_KEY(ScrollLock),
    XR_KEY(Pause),
    XR_KEY(Insert),
    XR_KEY(Home),
    XR_KEY(PageUp),
    XR_KEY(Delete),
    XR_KEY(End),
    XR_KEY(PageDown),
    XR_KEY(Right),
    XR_KEY(Left),
    XR_KEY(Down),
    XR_KEY(Up),
    XR_KEY(NumLock),
    XR_KEY(NumpadDivide),
    XR_KEY(NumpadMultiply),
    XR_KEY(NumpadMinus),
    XR_KEY(NumpadPlus),
    XR_KEY(NumpadEqual),
    XR_KEY(NumpadEnter),
    XR_KEY(NumpadDecimal),
    XR_KEY(Numpad1),
    XR_KEY(Numpad2),
    XR_KEY(Numpad3),
    XR_KEY(Numpad4),
    XR_KEY(Numpad5),
    XR_KEY(Numpad6),
    XR_KEY(Numpad7),
    XR_KEY(Numpad8),
    XR_KEY(Numpad9),
    XR_KEY(Numpad0),
    XR_KEY(NonUsBackslash),
    XR_KEY(Application),
    XR_KEY(Execute),
    XR_KEY(ModeChange),
    XR_KEY(Help),
    XR_KEY(Menu),
    XR_KEY(Select),
    XR_KEY(Redo),
    XR_KEY(Undo),
    XR_KEY(Cut),
    XR_KEY(Copy),
    XR_KEY(Paste),
    XR_KEY(VolumeMute),
    XR_KEY(VolumeUp),
    XR_KEY(VolumeDown),
    XR_KEY(MediaPlayPause),
    XR_KEY(MediaStop),
    XR_KEY(MediaNextTrack),
    XR_KEY(MediaPreviousTrack),
    XR_KEY(LControl),
    XR_KEY(LShift),
    XR_KEY(LAlt),
    XR_KEY(LSystem),
    XR_KEY(RControl),
    XR_KEY(RShift),
    XR_KEY(RAlt),
    XR_KEY(RSystem),
    XR_KEY(Back),
    XR_KEY(Forward),
    XR_KEY(Refresh),
    XR_KEY(Stop),
    XR_KEY(Search),
    XR_KEY(Favorites),
    XR_KEY(HomePage),
    XR_KEY(LaunchApplication1),
    XR_KEY(LaunchApplication2),
    XR_KEY(LaunchMail),
    XR_KEY(LaunchMediaSelect),
#undef XR_KEY

#define XR_KEY(k) \
    _keyboard \
    { \
        "Mouse::" #k, xr::key_id { sf::Mouse::Button::k } \
    }
    XR_KEY(Left),
    XR_KEY(Right),
    XR_KEY(Middle),
    XR_KEY(Extra1),
    XR_KEY(Extra2),
#undef XR_KEY

#define XR_KEY(k) \
    _keyboard \
    { \
        "Joystick::Button" #k, xr::key_id { xr::key_id::joystick::button##k } \
    }
    XR_KEY(1),
    XR_KEY(2),
    XR_KEY(3),
    XR_KEY(4),
    XR_KEY(5),
    XR_KEY(6),
    XR_KEY(7),
    XR_KEY(8),
    XR_KEY(9),
    XR_KEY(10),
    XR_KEY(11),
    XR_KEY(12),
    XR_KEY(13),
    XR_KEY(14),
    XR_KEY(15),
    XR_KEY(16),
    XR_KEY(17),
    XR_KEY(18),
    XR_KEY(19),
    XR_KEY(20),
    XR_KEY(21),
    XR_KEY(22),
    XR_KEY(23),
    XR_KEY(24),
    XR_KEY(25),
    XR_KEY(26),
    XR_KEY(27),
    XR_KEY(28),
    XR_KEY(29),
    XR_KEY(30),
    XR_KEY(31),
    XR_KEY(32),
#undef XR_KEY
}};
static_assert(std::to_underlying(xr::key_id::joystick::button32) + 1 == sf::Joystick::ButtonCount);

void initialize_bindings()
{
#ifdef DEBUG
    for (auto [i1, _k1] : xr::enumerate_views(std::as_const(keyboards)))
    {
        for (auto i2{i1}; i2 < std::ssize(keyboards); ++i2)
        {
            const auto& _k2 = keyboards[i2];

            if (_k1.dik == _k2.dik && i1 != i2)
                Msg("{}=={}", _k1.key_name, _k2.key_name);
        }
    }
#endif

    constexpr gsl::czstring keyboard_section = "custom_keyboard_action";

    if (pSettings->section_exist(keyboard_section))
    {
        const auto action_count = pSettings->line_count(keyboard_section);

        for (u32 i = 0; i < action_count; ++i)
        {
            gsl::czstring name, value;
            std::ignore = pSettings->r_line(keyboard_section, i, &name, &value);

            actions.emplace_back(name, value);
        }
    }

    for (auto& act : actions)
        g_key_bindings.emplace_back(&act);
}

void remap_keys()
{
    for (auto& key : keyboards)
    {
        if (!key.dik.is<sf::Keyboard::Scancode>())
        {
        alias:
            key.key_local_name = std::string_view{key.key_name} | std::views::split(std::string_view{"::"}) | std::views::join_with(std::string_view{" "}) |
                std::ranges::to<xr_string>();
            continue;
        }

        *reinterpret_cast<sf::U8String*>(&key.key_local_name) = sf::Keyboard::getDescription(key.dik.get<sf::Keyboard::Scancode>()).toUtf8();
        if (key.key_local_name == "Unknown")
            goto alias;
    }
}
} // namespace

namespace xr
{
EGameActions action_id(const _action& act) { return EGameActions{gsl::narrow_cast<s32>(&act - actions.data())}; }
std::span<const _keyboard> key_ids() { return keyboards; }
} // namespace xr

EGameActions action_name_to_id(gsl::czstring _name)
{
    const auto action = action_name_to_ptr(_name);
    if (action != nullptr)
        return xr::action_id(*action);

    return EGameActions::kNOTBINDED;
}

_action* action_name_to_ptr(gsl::czstring _name)
{
    if (const auto it = std::ranges::find_if(actions, [_name](const auto& act) { return std::is_eq(xr::strcasecmp(_name, act.action_name)); });
        it != actions.end())
        return std::to_address(it);

    Msg("! Can't find corresponding [id] for action_name [{}]", _name);

    return nullptr;
}

gsl::czstring dik_to_keyname(xr::key_id _dik)
{
    if (const auto kb = dik_to_ptr(_dik, true); kb != nullptr)
        return kb->key_name;

    return nullptr;
}

_keyboard* dik_to_ptr(xr::key_id _dik, bool bSafe)
{
    if (const auto it = std::ranges::find(keyboards, _dik, &_keyboard::dik); it != keyboards.end())
        return std::to_address(it);

    if (!bSafe)
        XR_LOG_ERROR("Can't find corresponding [_keyboard] for dik");

    return nullptr;
}

namespace
{
[[nodiscard]] _keyboard* keyname_to_ptr(gsl::czstring _name)
{
    if (const auto it = std::ranges::find_if(keyboards, [_name](const auto& key) { return std::is_eq(xr::strcasecmp(_name, key.key_name)); });
        it != keyboards.end())
        return std::to_address(it);

    Msg("! cant find corresponding [_keyboard*] for keyname {}", _name);

    return nullptr;
}
} // namespace

xr::key_id keyname_to_dik(gsl::czstring _name)
{
    if (const auto kb = keyname_to_ptr(_name); kb != nullptr)
        return kb->dik;

    return xr::key_id{sf::Keyboard::Scancode::Unknown};
}

bool is_binded(EGameActions _action_id, xr::key_id _dik)
{
    if (_action_id == EGameActions::kNOTBINDED)
        return false;

    const auto pbinding = &g_key_bindings[std::to_underlying(_action_id)];

    if (pbinding->m_keyboard[0] != nullptr && pbinding->m_keyboard[0]->dik == _dik)
        return true;

    if (pbinding->m_keyboard[1] != nullptr && pbinding->m_keyboard[1]->dik == _dik)
        return true;

    return false;
}

xr::key_id get_action_dik(EGameActions _action_id)
{
    const auto pbinding = &g_key_bindings[std::to_underlying(_action_id)];

    if (pbinding->m_keyboard[0] != nullptr)
        return pbinding->m_keyboard[0]->dik;

    if (pbinding->m_keyboard[1] != nullptr)
        return pbinding->m_keyboard[1]->dik;

    return xr::key_id{sf::Keyboard::Scancode::Unknown};
}

EGameActions get_binded_action(xr::key_id _dik)
{
    if (const auto it = std::ranges::find_if(g_key_bindings,
                                             [_dik](const auto& binding) {
                                                 if (binding.m_keyboard[0] != nullptr && binding.m_keyboard[0]->dik == _dik)
                                                     return true;

                                                 if (binding.m_keyboard[1] != nullptr && binding.m_keyboard[1]->dik == _dik)
                                                     return true;

                                                 return false;
                                             });
        it != g_key_bindings.end())
        return xr::action_id(*it->m_action);

    return EGameActions::kNOTBINDED;
}

void GetActionAllBinding(gsl::czstring _action, gsl::zstring dst_buff, gsl::index dst_buff_sz)
{
    const EGameActions action_id = action_name_to_id(_action);
    const _binding* pbinding{};

    if (action_id == EGameActions::kNOTBINDED)
        Msg("!![{}] Action [{}] not found! Fix it or remove from text!", std::source_location::current().function_name(), _action);
    else
        pbinding = &g_key_bindings[std::to_underlying(action_id)];

    string128 prim;
    string128 sec;
    prim[0] = '\0';
    sec[0] = '\0';

    if (pbinding != nullptr && pbinding->m_keyboard[0] != nullptr)
        strcpy_s(prim, pbinding->m_keyboard[0]->key_local_name.c_str());

    if (pbinding != nullptr && pbinding->m_keyboard[1] != nullptr)
        strcpy_s(sec, pbinding->m_keyboard[1]->key_local_name.c_str());

    sprintf_s(dst_buff, dst_buff_sz, "%s%s%s", prim[0] ? prim : "", (sec[0] && prim[0]) ? " , " : "", sec[0] ? sec : "");
}

ConsoleBindCmds bindConsoleCmds;

namespace
{
BOOL bRemapped = FALSE;

class CCC_Bind final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_Bind, IConsole_Command);

private:
    s32 m_work_idx;

public:
    explicit CCC_Bind(gsl::czstring N, s32 idx) : IConsole_Command{N}, m_work_idx{idx} {}
    ~CCC_Bind() override = default;

    void Execute(std::string_view args) override
    {
        if (Core.Features.test(xrCore::Feature::remove_alt_keybinding) && m_work_idx > 0)
        {
            InvalidSyntax("secondary keybindings are disabled", args);
            return;
        }

        const auto res = scn::scan<xr_string, xr_string>(args, "{} {}");
        if (!res)
        {
            InvalidSyntax(res.error().msg(), args);
            return;
        }

        if (!bRemapped)
        {
            remap_keys();
            bRemapped = TRUE;
        }

        const auto& [action, key] = res->values();

        if (action_name_to_ptr(action.c_str()) == nullptr)
        {
        inv:
            InvalidSyntax("invalid action", action);
            return;
        }

        const EGameActions action_id = action_name_to_id(action.c_str());
        if (action_id == EGameActions::kNOTBINDED)
            goto inv;

        auto pkeyboard = keyname_to_ptr(key.c_str());
        if (pkeyboard == nullptr)
        {
            InvalidSyntax("invalid keycode", key);
            return;
        }

        auto& curr_pbinding = g_key_bindings[std::to_underlying(action_id)];
        curr_pbinding.m_keyboard[m_work_idx] = pkeyboard;

        for (auto& binding : g_key_bindings)
        {
            if (&binding == &curr_pbinding)
                continue;

            if (binding.m_keyboard[0] == pkeyboard)
                binding.m_keyboard[0] = nullptr;

            if (binding.m_keyboard[1] == pkeyboard)
                binding.m_keyboard[1] = nullptr;
        }

        CStringTable::ReparseKeyBindings();
    }

    void Save(IWriter* F) override
    {
        if (m_work_idx == 0)
            F->w_printf("unbindall\r\n");

        for (const auto& pbinding : g_key_bindings)
        {
            if (pbinding.m_keyboard[m_work_idx])
                F->w_printf("{} {} {}\r\n", cName, pbinding.m_action->action_name, pbinding.m_keyboard[m_work_idx]->key_name);
        }
    }
};

class CCC_UnBind final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_UnBind, IConsole_Command);

private:
    s32 m_work_idx;

public:
    explicit CCC_UnBind(gsl::czstring N, s32 idx) : IConsole_Command{N, true}, m_work_idx{idx} {}
    ~CCC_UnBind() override = default;

    void Execute(std::string_view args) override
    {
        if (action_name_to_ptr(args.data()) == nullptr)
        {
        inv:
            InvalidSyntax("invalid action", args);
            return;
        }

        const auto act = action_name_to_id(args.data());
        if (act == EGameActions::kNOTBINDED)
            goto inv;

        g_key_bindings[std::to_underlying(act)].m_keyboard[m_work_idx] = nullptr;

        CStringTable::ReparseKeyBindings();
    }
};

class CCC_ListActions final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_ListActions, IConsole_Command);

public:
    explicit CCC_ListActions(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_ListActions() override = default;

    void Execute(std::string_view) override
    {
        XR_LOG_INFO("--- Action list start ---");

        for (const auto& pbinding : g_key_bindings)
            Msg("- {}", pbinding.m_action->action_name);

        XR_LOG_INFO("--- Action list end   ---");
    }
};

class CCC_UnBindAll final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_UnBindAll, IConsole_Command);

public:
    explicit CCC_UnBindAll(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_UnBindAll() override = default;

    void Execute(std::string_view) override
    {
        for (auto& pbinding : g_key_bindings)
        {
            pbinding.m_keyboard[0] = nullptr;
            pbinding.m_keyboard[1] = nullptr;
        }

        bindConsoleCmds.clear();

        string_path _cfg;
        string_path cmd;

        std::ignore = FS.update_path(_cfg, "$game_config$", "default_controls.ltx");
        xr_strconcat(cmd, "cfg_load", " ", _cfg);

        Console->Execute(cmd);
    }
};

class CCC_BindList final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_BindList, IConsole_Command);

public:
    explicit CCC_BindList(gsl::czstring N) : IConsole_Command{N, true} {}
    ~CCC_BindList() override = default;

    void Execute(std::string_view) override
    {
        XR_LOG_INFO("--- Bind list start ---");

        for (const auto& pbinding : g_key_bindings)
        {
            Msg("[{}] primary is[{}] secondary is[{}]", pbinding.m_action->action_name,
                pbinding.m_keyboard[0] != nullptr ? std::string_view{pbinding.m_keyboard[0]->key_local_name} : std::string_view{"None"},
                pbinding.m_keyboard[1] != nullptr ? std::string_view{pbinding.m_keyboard[1]->key_local_name} : std::string_view{"None"});
        }

        XR_LOG_INFO("--- Bind list end   ---");
    }
};

class CCC_BindConsoleCmd final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_BindConsoleCmd, IConsole_Command);

public:
    explicit CCC_BindConsoleCmd(gsl::czstring N) : IConsole_Command{N} {}
    ~CCC_BindConsoleCmd() override = default;

    void Execute(std::string_view args) override
    {
        const auto space = args.rfind(' ');
        if (space == std::string_view::npos)
        {
            InvalidSyntax("insufficient args", args);
            return;
        }

        const xr_string console_command{args.substr(0, space)};
        const xr_string key{args.substr(space + 1)};

        if (keyname_to_ptr(key.c_str()) == nullptr)
        {
            InvalidSyntax("invalid keycode", key);
            return;
        }

        bindConsoleCmds.bind(keyname_to_dik(key.c_str()), console_command.c_str());
    }

    void Save(IWriter* F) override { bindConsoleCmds.save(F); }
};

class CCC_UnBindConsoleCmd final : public IConsole_Command
{
    RTTI_DECLARE_TYPEINFO(CCC_UnBindConsoleCmd, IConsole_Command);

public:
    explicit CCC_UnBindConsoleCmd(gsl::czstring N) : IConsole_Command{N} {}
    ~CCC_UnBindConsoleCmd() override = default;

    void Execute(std::string_view args) override { bindConsoleCmds.unbind(keyname_to_dik(args.data())); }
};
} // namespace

void ConsoleBindCmds::bind(xr::key_id dik, gsl::czstring N) { m_bindConsoleCmds[dik]._set(N); }

void ConsoleBindCmds::unbind(xr::key_id dik)
{
    auto it = m_bindConsoleCmds.find(dik);
    if (it == m_bindConsoleCmds.end())
        return;

    m_bindConsoleCmds.erase(it);
}

void ConsoleBindCmds::clear() { m_bindConsoleCmds.clear(); }

bool ConsoleBindCmds::execute(xr::key_id dik) const
{
    const auto it = m_bindConsoleCmds.find(dik);
    if (it == m_bindConsoleCmds.end())
        return false;

    Console->Execute(it->second.c_str());

    return true;
}

void ConsoleBindCmds::save(IWriter* F) const
{
    for (const auto& bind : m_bindConsoleCmds)
        F->w_printf("bind_console {} {}\n", bind.second, dik_to_keyname(bind.first));
}

void CCC_RegisterInput()
{
    initialize_bindings();

    XR_CMD(CCC_Bind, "bind", 0);
    XR_CMD(CCC_Bind, "bind_sec", 1);
    XR_CMD(CCC_UnBind, "unbind", 0);
    XR_CMD(CCC_UnBind, "unbind_sec", 1);
    XR_CMD(CCC_UnBindAll, "unbindall");
    XR_CMD(CCC_ListActions, "list_actions");

    XR_CMD(CCC_BindList, "bind_list");
    XR_CMD(CCC_BindConsoleCmd, "bind_console");
    XR_CMD(CCC_UnBindConsoleCmd, "unbind_console");
}
