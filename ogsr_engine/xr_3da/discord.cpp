#include "stdafx.h"

#include "IGame_Persistent.h"

#include "x_ray.h"
#include "xr_ioc_cmd.h"

#define DISCORD_API __declspec(dllimport)

#include <discordpp.h>

#undef DISCORD_API

namespace xxh
{
#include <xxhash.h>
}

#ifdef XR_DISCORD
namespace xr
{
namespace
{
class discord final : public xr::social_app
{
    RTTI_DECLARE_TYPEINFO(xr::discord, xr::social_app);

private:
    class meta final
    {
    private:
        struct hdr final
        {
            // u64 xxh, excluded from the hashable data
            u64 alen;
            u64 rlen;
            s64 type;
            s64 exp;
        };
        static_assert(sizeof(hdr) == 32);
        static_assert(std::is_same_v<decltype(hdr::exp), decltype(std::chrono::nanoseconds{}.count())>);

        static constexpr std::string_view token{"discord\\auth.bin"};

        std::string access;
        std::string refresh;
        discordpp::AuthorizationTokenType type;
        decltype(std::chrono::system_clock::now()) expires;

    public:
        [[nodiscard]] constexpr std::pair<discordpp::AuthorizationTokenType, std::string> creds() const { return std::make_pair(type, access); }
        [[nodiscard]] constexpr auto ref() const { return refresh; }

        [[nodiscard]] constexpr bool expired() const { return expires <= std::chrono::system_clock::now(); }

        constexpr void set(std::string access, std::string refresh, discordpp::AuthorizationTokenType type, s32 expires);

        [[nodiscard]] bool load();
        void save() const;
    };

    class rich final
    {
    private:
        shared_str details;
        shared_str state;
        const std::chrono::seconds start;

    public:
        constexpr rich()
            : start{std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - decltype(std::chrono::system_clock::now()){})}
        {}

        [[nodiscard]] constexpr std::tuple<std::string, std::string, u64> data() const
        {
            return std::make_tuple(std::string{details}, std::string{state}, gsl::narrow<u64>(start.count()));
        }

        constexpr void set_details(shared_str details) { this->details = std::move(details); }
        constexpr void set_state(shared_str state) { this->state = std::move(state); }
    };

    static constexpr std::array<std::tuple<std::string_view, discordpp::LoggingSeverity, discordpp::LoggingSeverity>, 2> filter{{
        // substring to search, original severity, target severity
        {"RPC Connect error", discordpp::LoggingSeverity::Warning, discordpp::LoggingSeverity::Info},
        {"RPC manager reset", discordpp::LoggingSeverity::Warning, discordpp::LoggingSeverity::Info},
    }};

#ifdef _DEBUG
    static constexpr auto severity{discordpp::LoggingSeverity::Info};
#else
    static constexpr auto severity{discordpp::LoggingSeverity::Warning};
#endif

    static constexpr u64 id{XR_DISCORD_APPID};

    discordpp::Client client;
    quill::Logger* logger;

    gsl::index nextup{0};
    bool& ready;

    meta meta;
    rich rich;

    void log(std::string_view message, discordpp::LoggingSeverity severity) const;

    void refresh(bool login);
    void refresh_async(discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType type, s32 expires,
                       bool login);

    void login(bool connect);
    void authorize();

    void set_rich();

public:
    explicit discord(bool& ready);
    ~discord() override;

    void connect(bool auth) override;
    void disconnect() override;

    void set_level(shared_str level) override;
    void set_task(shared_str task) override;

    void update() override;
};

// Authorization metadata

constexpr void discord::meta::set(std::string access, std::string refresh, discordpp::AuthorizationTokenType type, s32 expires)
{
    this->access = std::move(access);
    this->refresh = std::move(refresh);

    this->type = type;
    this->expires = std::chrono::system_clock::now() + std::chrono::seconds{expires - 24 * 3600};
}

bool discord::meta::load()
{
    string_path path;

    if (FS.exist(path, xr::fsgame::app_data_root.data(), token.data()) == nullptr)
        return false;

    const auto rd = XR_ASSERT_VAL(absl::WrapUnique(FS.r_open(path)), "", path);

    if (const auto xxh = rd->r_u64(); xxh::XXH3_64bits(rd->pointer(), gsl::narrow_cast<size_t>(rd->elapsed())) != xxh)
        return false;

    hdr hdr;
    rd->r(&hdr, sizeof(hdr));

    if (hdr.alen == 0 || hdr.rlen == 0)
        return false;

    const auto type = gsl::narrow_cast<discordpp::AuthorizationTokenType>(hdr.type);
    switch (type)
    {
    case discordpp::AuthorizationTokenType::User:
    case discordpp::AuthorizationTokenType::Bearer: break;
    default: return false;
    }

    access.resize(hdr.alen);
    rd->r(&access[0], hdr.alen);

    refresh.resize(hdr.rlen);
    rd->r(&refresh[0], hdr.rlen);

    this->type = type;
    expires = decltype(expires){} + std::chrono::duration_cast<decltype(expires)::duration>(std::chrono::nanoseconds{hdr.exp});

    return true;
}

void discord::meta::save() const
{
    auto wr = XR_ASSERT_VAL(FS.w_open(xr::fsgame::app_data_root.data(), token.data()) != nullptr);
    const auto _ = gsl::finally([wr] {
        auto ptr = wr;
        FS.w_close(ptr);
    });

    const hdr hdr{access.size(), refresh.size(), std::to_underlying(type),
                  std::chrono::duration_cast<std::chrono::nanoseconds>(expires - decltype(expires){}).count()};
    xr_vector<std::byte> data(sizeof(hdr) + hdr.alen + hdr.rlen);
    size_t pos{0};

    std::memcpy(&data[0], &hdr, sizeof(hdr));
    pos += sizeof(hdr);

    std::memcpy(&data[pos], access.c_str(), hdr.alen);
    pos += hdr.alen;

    std::memcpy(&data[pos], refresh.c_str(), hdr.rlen);
    pos += hdr.rlen;

    wr->w_u64(xxh::XXH3_64bits(&data[0], pos));
    wr->w(&data[0], gsl::narrow_cast<gsl::index>(pos));
}

// Local helpers

discord::discord(bool& ready) : ready{ready}
{
    ready = false;

    logger = xr::logger_init("Discord");
    client.AddLogCallback([this](std::string message, discordpp::LoggingSeverity severity) { log(std::move(message), severity); }, severity);

    client.SetStatusChangedCallback([this](discordpp::Client::Status status, discordpp::Client::Error error, s32 detail) {
        if (error != discordpp::Client::Error::None)
        {
            XR_LOG__ERROR(logger, "Connection error: {} ({})", error, detail);
            disconnect();
        }
        else if (status == discordpp::Client::Status::Ready)
        {
            this->ready = true;

            if (const auto user = client.GetCurrentUserV2(); user)
                XR_LOG__INFO(logger, "Hi, {}", user->DisplayName());

            set_rich();
        }
        else
        {
            this->ready = false;
        }
    });
}

discord::~discord() = default;

void discord::log(std::string_view message, discordpp::LoggingSeverity severity) const
{
    if (const auto iter = std::ranges::find_if(filter,
                                               [severity, message] [[nodiscard]] (const auto& item) {
                                                   return severity == std::get<1>(item) && message.find(std::get<0>(item)) != std::string_view::npos;
                                               });
        iter != filter.end())
    {
        severity = std::get<2>(*iter);
        if (severity < discord::severity)
            return;
    }

    xr::level lvl;

    switch (severity)
    {
    case discordpp::LoggingSeverity::Verbose: lvl = xr::level::Debug; break;
    case discordpp::LoggingSeverity::Info: lvl = xr::level::Info; break;
    case discordpp::LoggingSeverity::Warning: lvl = xr::level::Warning; break;
    case discordpp::LoggingSeverity::Error: lvl = xr::level::Error; break;
    case discordpp::LoggingSeverity::None:
    default: lvl = xr::level::Notice; break;
    }

    if (const auto pos = message.find("): "); pos != std::string_view::npos)
        message = message.subview(pos + 3);

    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.remove_suffix(1);

    XR_LOG__DYNAMIC(logger, lvl, "{}", message);
}

void discord::refresh(bool login)
{
    client.RefreshToken(id, meta.ref(),
                        [this, login](discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType type,
                                      s32 expires,
                                      std::string) { refresh_async(std::move(result), std::move(access), std::move(refresh), type, expires, login); });
}

void discord::refresh_async(discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType type, s32 expires,
                            bool login)
{
    if (!result.Successful())
    {
        XR_LOG__ERROR(logger, "Failed to get token: {}", result.Error());

        disconnect();
        return;
    }

    meta.set(std::move(access), std::move(refresh), type, expires);
    this->login(login);
}

void discord::login(bool connect)
{
    auto [type, access] = meta.creds();

    client.UpdateToken(type, std::move(access), [this, connect](discordpp::ClientResult result) {
        if (!result.Successful())
        {
            XR_LOG__ERROR(logger, "Failed to update token: {}", result.Error());

            disconnect();
            return;
        }

        meta.save();

        if (connect)
            client.Connect();
    });
}

void discord::authorize()
{
    auto verifier = client.CreateAuthorizationCodeVerifier();

    discordpp::AuthorizationArgs args;
    args.SetClientId(id);
    args.SetCodeChallenge(verifier.Challenge());
    args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());

    client.Authorize(std::move(args), [this, verifier = std::move(verifier)](discordpp::ClientResult result, std::string code, std::string uri) {
        if (!result.Successful())
        {
            XR_LOG__ERROR(logger, "Authentication error: {}", result.Error());

            ready = false;
            return;
        }

        client.GetToken(id, std::move(code), verifier.Verifier(), std::move(uri),
                        [this](discordpp::ClientResult result, std::string access, std::string refresh, discordpp::AuthorizationTokenType type, s32 expires,
                               std::string) { refresh_async(std::move(result), std::move(access), std::move(refresh), type, expires, true); });
    });
}

void discord::set_rich()
{
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Playing);
    activity.SetSupportedPlatforms(discordpp::ActivityGamePlatforms::Desktop);

    discordpp::ActivityAssets assets;
    assets.SetLargeImage("main_image");

    auto [details, state, start] = rich.data();

    std::string large;
    if (!details.empty() && !state.empty())
        large = xr::format("{} | {}", details, state);
    else if (!details.empty())
        large = details;
    else if (!state.empty())
        large = state;

    if (!large.empty())
        assets.SetLargeText(std::move(large));

    assets.SetSmallImage("main_image_small");
    assets.SetSmallText(Core.GetEngineVersion());
    activity.SetAssets(std::move(assets));

    if (!details.empty())
        activity.SetDetails(std::move(details));
    if (!state.empty())
        activity.SetState(std::move(state));

    discordpp::ActivityTimestamps timestamps;
    timestamps.SetStart(start);
    activity.SetTimestamps(std::move(timestamps));

    discordpp::ActivityButton gh;
    gh.SetLabel(XR_DISCORD_GITHUB_LABEL);
    gh.SetUrl(XR_DISCORD_GITHUB_URL);
    activity.AddButton(std::move(gh));

    discordpp::ActivityButton ds;
    ds.SetLabel(XR_DISCORD_SERVER_LABEL);
    ds.SetUrl(XR_DISCORD_SERVER_URL);
    activity.AddButton(std::move(ds));

    client.UpdateRichPresence(std::move(activity), [this](discordpp::ClientResult result) {
        if (!result.Successful())
            XR_LOG__ERROR(logger, "Failed to update rich presence: {}", result.Error());
    });
}

// Public interface

void discord::connect(bool auth)
{
    if (!meta.load())
    {
        if (auth)
            authorize();

        return;
    }

    if (meta.expired())
        refresh(true);
    else
        login(true);
}

void discord::disconnect()
{
    if (!ready)
        return;

    ready = false;
    client.Disconnect();
}

void discord::set_level(shared_str level)
{
    rich.set_details(std::move(level));

    if (ready)
        set_rich();
}

void discord::set_task(shared_str task)
{
    rich.set_state(std::move(task));

    if (ready)
        set_rich();
}

void discord::update()
{
    discordpp::RunCallbacks();

    if (!ready || nextup > Device.dwTimeGlobal)
        return;

    if (meta.expired())
        refresh(false);

    nextup = Device.dwTimeGlobal + 1000;
}

// Integration switch

class ccc_social final : public CCC_Bool
{
    RTTI_DECLARE_TYPEINFO(ccc_social, CCC_Bool);

private:
    const std::unique_ptr<xr::social_app>& app;

public:
    explicit ccc_social(gsl::czstring name, const std::unique_ptr<xr::social_app>& app, bool& val) : CCC_Bool{name, val}, app{app} {}
    ~ccc_social() override = default;

    void Execute(std::string_view args) override
    {
        const auto old = GetValue();
        CCC_Bool::Execute(args);

        if (!app)
            return;

        const auto val = GetValue();
        if (val == old)
            return;

        if (val)
            app->connect(true);
        else
            app->disconnect();
    }
};

bool enable{false};
} // namespace

namespace detail
{
void discord_register() { XR_CMD(ccc_social, "rs_discord", xr::social(), xr::enable); }

void discord_init()
{
    const auto on = xr::enable;
    xr::detail::social = std::make_unique<xr::discord>(xr::enable);

    if (on)
        xr::social()->connect(false);
}
} // namespace detail
} // namespace xr
#endif // XR_DISCORD
