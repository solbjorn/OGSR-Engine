#ifndef __XRCORE_FORMAT_H
#define __XRCORE_FORMAT_H

#include <SFML/System/String.hpp>

#ifdef XR_USE_FMT

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wfloat-equal");
XR_DIAG_IGNORE("-Wnan-infinity-disabled");
XR_DIAG_IGNORE("-Wnewline-eof");
XR_DIAG_IGNORE("-Wnrvo");

#include <enchantum/fmt_format.hpp>

XR_DIAG_POP();

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wnrvo");

#include <fmt/ranges.h>

XR_DIAG_POP();

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wfloat-equal");

#include <fmt/std.h>

XR_DIAG_POP();

namespace xr
{
using fmt::format;
using fmt::format_to;
using fmt::format_to_n;

namespace detail
{
using fmt::format_args;
using fmt::format_string;
using fmt::make_format_args;
using fmt::string_view;
using fmt::vformat;

struct unformattable final
{};
} // namespace detail

// Disable formatting of a type when unwanted, e.g. when the type is incomplete.
#define XR_UNFORMATTABLE(x) \
    template <> \
    struct fmt::formatter<x> final : fmt::formatter<xr::detail::unformattable> \
    {}; \
    static_assert(!fmt::is_formattable<x>::value)
} // namespace xr

#else // !XR_USE_FMT

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wnewline-eof");
XR_DIAG_IGNORE("-Wnrvo");

#include <enchantum/std_format.hpp>

XR_DIAG_POP();

namespace xr
{
using std::format;
using std::format_to;
using std::format_to_n;

namespace detail
{
using std::format_args;
using std::format_string;
using std::make_format_args;
using std::string_view;
using std::vformat;

struct unformattable final
{};
} // namespace detail

// Disable formatting of a type when unwanted, e.g. when the type is incomplete.
#define XR_UNFORMATTABLE(x) \
    template <> \
    struct std::formatter<x> final : std::formatter<xr::detail::unformattable> \
    {}; \
    static_assert(!std::formattable<x, char>)
} // namespace xr

template <typename T>
struct std::formatter<std::atomic<T>> final : std::formatter<T>
{
    [[nodiscard]] constexpr auto format(const std::atomic<T>& val, std::format_context& ctx) const { return std::formatter<T>::format(val.load(), ctx); }
};

template <>
struct std::formatter<Fvector2> final : std::formatter<std::array<f32, 2>>
{
    [[nodiscard]] constexpr auto format(const Fvector2& vec, std::format_context& ctx) const
    {
        return std::formatter<std::array<f32, 2>>::format(*reinterpret_cast<const std::array<f32, 2>*>(&vec), ctx);
    }
};

template <>
struct std::formatter<Fvector3> final : std::formatter<std::array<f32, 3>>
{
    [[nodiscard]] constexpr auto format(const Fvector3& vec, std::format_context& ctx) const
    {
        return std::formatter<std::array<f32, 3>>::format(*reinterpret_cast<const std::array<f32, 3>*>(&vec), ctx);
    }
};

template <>
struct std::formatter<Fvector4> final : std::formatter<std::array<f32, 4>>
{
    [[nodiscard]] constexpr auto format(const Fvector4& vec, std::format_context& ctx) const
    {
        return std::formatter<std::array<f32, 4>>::format(*reinterpret_cast<const std::array<f32, 4>*>(&vec), ctx);
    }
};

template <>
struct std::formatter<shared_str> final : std::formatter<std::string_view>
{
    [[nodiscard]] constexpr auto format(const shared_str& str, std::format_context& ctx) const
    {
        return std::formatter<std::string_view>::format(std::string_view{str}, ctx);
    }
};

template <>
struct std::formatter<sf::String> final : std::formatter<xr_string>
{
    [[nodiscard]] constexpr auto format(const sf::String& str, std::format_context& ctx) const
    {
        auto utf = str.toUtf8();
        return std::formatter<xr_string>::format(*reinterpret_cast<xr_string*>(&utf), ctx);
    }
};

template <>
struct std::formatter<xr::hresult> final : std::formatter<xr_string>
{
    [[nodiscard]] constexpr auto format(const xr::hresult& hr, std::format_context& ctx) const
    {
        return std::formatter<xr_string>::format(xr::format("{:#x} \"{}\"", hr.code(), hr.what()), ctx);
    }
};

template <>
struct std::formatter<xr::last_error> final : std::formatter<xr_string>
{
    [[nodiscard]] constexpr auto format(const xr::last_error& le, std::format_context& ctx) const
    {
        return std::formatter<xr_string>::format(xr::format("{:2} \"{}\"", le.code(), le.what()), ctx);
    }
};

template <>
struct std::formatter<xr::ntstatus> final : std::formatter<xr_string>
{
    [[nodiscard]] constexpr auto format(const xr::ntstatus& nt, std::format_context& ctx) const
    {
        return std::formatter<xr_string>::format(xr::format("{:#x} \"{}\"", nt.code(), nt.what()), ctx);
    }
};

#endif // !XR_USE_FMT

// Define format_as() regardless of XR_USE_FMT: used by both {fmt} and Quill, which is {fmt}-exclusive

[[nodiscard]] constexpr auto format_as(const Fvector2& vec) { return *reinterpret_cast<const std::array<f32, 2>*>(&vec); }
[[nodiscard]] constexpr auto format_as(const Fvector3& vec) { return *reinterpret_cast<const std::array<f32, 3>*>(&vec); }
[[nodiscard]] constexpr auto format_as(const Fvector4& vec) { return *reinterpret_cast<const std::array<f32, 4>*>(&vec); }

[[nodiscard]] constexpr auto format_as(const shared_str& str) { return std::string_view{str}; }

namespace sf
{
[[nodiscard]] constexpr auto format_as(const sf::String& str)
{
    auto utf = str.toUtf8();
    return *reinterpret_cast<xr_string*>(&utf);
}
} // namespace sf

namespace xr
{
[[nodiscard]] constexpr auto format_as(const xr::hresult& hr) { return xr::format("{:#x} \"{}\"", hr.code(), hr.what()); }
[[nodiscard]] constexpr auto format_as(const xr::last_error& le) { return xr::format("{:2} \"{}\"", le.code(), le.what()); }
[[nodiscard]] constexpr auto format_as(const xr::ntstatus& nt) { return xr::format("{:#x} \"{}\"", nt.code(), nt.what()); }
} // namespace xr

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wsign-conversion");

#include <scn/ranges.h>

XR_DIAG_POP();

template <typename T>
struct scn::scanner<Fvector2, T> final : scn::scanner<std::inplace_vector<f32, 2>, T>
{
    template <typename Context>
    [[nodiscard]] constexpr auto scan(Fvector2& val, Context& ctx) -> scn::scan_expected<typename Context::iterator>
    {
        std::inplace_vector<f32, 2> in;

        auto res = scn::scanner<std::inplace_vector<f32, 2>, T>::scan(in, ctx);
        if (!res)
            return res;

        if (in.size() != 2)
        {
            res = scn::unexpected(scn::scan_error{scn::scan_error::invalid_scanned_value, "Not a 2D vector"});
            return res;
        }

        val.set(in[0], in[1]);

        return res;
    }
};

template <typename T>
struct scn::scanner<Fvector3, T> final : scn::scanner<std::inplace_vector<f32, 3>, T>
{
    template <typename Context>
    [[nodiscard]] constexpr auto scan(Fvector3& val, Context& ctx) -> scn::scan_expected<typename Context::iterator>
    {
        std::inplace_vector<f32, 3> in;

        auto res = scn::scanner<std::inplace_vector<f32, 3>, T>::scan(in, ctx);
        if (!res)
            return res;

        if (in.size() != 3)
        {
            res = scn::unexpected(scn::scan_error{scn::scan_error::invalid_scanned_value, "Not a 3D vector"});
            return res;
        }

        val.set(in[0], in[1], in[2]);

        return res;
    }
};

template <typename T>
struct scn::scanner<Fvector4, T> final : scn::scanner<std::inplace_vector<f32, 4>, T>
{
    template <typename Context>
    [[nodiscard]] constexpr auto scan(Fvector4& val, Context& ctx) -> scn::scan_expected<typename Context::iterator>
    {
        std::inplace_vector<f32, 4> in;

        auto res = scn::scanner<std::inplace_vector<f32, 4>, T>::scan(in, ctx);
        if (!res)
            return res;

        if (in.size() != 4)
        {
            res = scn::unexpected(scn::scan_error{scn::scan_error::invalid_scanned_value, "Not a 4D vector"});
            return res;
        }

        val.set(in[0], in[1], in[2], in[3]);

        return res;
    }
};

#endif // !__XRCORE_FORMAT_H
