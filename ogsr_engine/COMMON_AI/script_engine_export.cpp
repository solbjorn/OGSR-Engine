////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine_export.cpp
//	Created 	: 01.04.2004
//  Modified 	: 22.06.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine export
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#define SCRIPT_REGISTRATOR
#include "script_export_space.h"
#include "script_engine_export.h"

template <typename List>
struct ts_converter;

template <typename... Ts>
struct ts_converter<imdexlib::typelist<Ts...>>
{
private:
#ifdef DEBUG
    static constexpr auto names{std::array {(RTTI::TypeName<Ts>())... }};
#endif
    static constexpr auto arr{std::array{(&Ts::script_register)...}};

public:
    static void script_register(sol::state_view& lua)
    {
#ifdef DEBUG
        for (auto [name, handler] : std::views::zip(names, arr))
#else
        for (auto handler : arr)
#endif
        {
#ifdef DEBUG
            XR_LOG_TRACE_L1("Exporting [{}]", name);
#endif
            handler(lua);
        }
    }
};

void export_classes(sol::state_view& lua) { ts_converter<imdexlib::ts_reverse_t<script_type_list>>::script_register(lua); }
