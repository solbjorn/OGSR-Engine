#pragma once

#include <chrono>

class CTimer_paused;

class pauseMngr final
{
    xr_vector<CTimer_paused*> m_timers;
    bool paused;

public:
    pauseMngr();

    [[nodiscard]] bool Paused() const { return paused; }
    void Pause(const bool b);
    void Register(CTimer_paused& t);
    void UnRegister(CTimer_paused& t);
};

extern pauseMngr* g_pauseMngr;
extern pauseMngr& g_pauseMngr_get();

class CTimerBase : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(CTimerBase);

public:
    using Clock = std::chrono::high_resolution_clock;
    using Time = std::chrono::time_point<Clock>;
    using Duration = Time::duration;

protected:
    Time startTime{};
    Duration pauseDuration{};
    Duration pauseAccum{};
    bool paused{};

public:
    CTimerBase() noexcept = default;
    ~CTimerBase() override = default;

    void Start()
    {
        if (paused)
            return;

        startTime = Now() - pauseAccum;
    }

    [[nodiscard]] virtual Duration getElapsedTime() const
    {
        if (paused)
            return pauseDuration;

        return Now() - startTime - pauseAccum;
    }

    [[nodiscard]] auto GetElapsed_ns() const
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(getElapsedTime()).count();
    }

    [[nodiscard]] auto GetElapsed_ms() const
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(getElapsedTime()).count();
    }

    [[nodiscard]] f32 GetElapsed_sec() const
    {
        using namespace std::chrono;
        return duration_cast<duration<f32>>(getElapsedTime()).count();
    }

    [[nodiscard]] Time Now() const { return Clock::now(); }

    void Dump() const { XR_LOG_INFO("Elapsed time (sec): {}", GetElapsed_sec()); }
};

class CTimer : public CTimerBase
{
    RTTI_DECLARE_TYPEINFO(CTimer, CTimerBase);

private:
    using inherited = CTimerBase;

    f32 m_time_factor{1.0f};
    Duration realTime{};
    Duration time{};

    [[nodiscard]] Duration getElapsedTime(const Duration& current) const
    {
        const auto delta = current - realTime;
        const double deltaD = gsl::narrow_cast<double>(delta.count());
        const double elapsedTime = deltaD * m_time_factor + 0.5;
        const auto result = gsl::narrow_cast<s64>(std::round(elapsedTime));

        return Duration{this->time.count() + result};
    }

public:
    CTimer() noexcept = default;
    ~CTimer() override = default;

    void Start() noexcept
    {
        if (paused)
            return;

        realTime = std::chrono::nanoseconds{0};
        time = std::chrono::nanoseconds{0};
        inherited::Start();
    }

    [[nodiscard]] f32 time_factor() const noexcept { return m_time_factor; }

    void time_factor(f32 time_factor) noexcept
    {
        const Duration current = inherited::getElapsedTime();
        time = getElapsedTime(current);
        realTime = current;
        m_time_factor = time_factor;
    }

    [[nodiscard]] Duration getElapsedTime() const override { return getElapsedTime(inherited::getElapsedTime()); }
};

class CTimer_paused_ex : public CTimer
{
    RTTI_DECLARE_TYPEINFO(CTimer_paused_ex, CTimer);

private:
    Time save_clock{};

public:
    CTimer_paused_ex() noexcept = default;
    ~CTimer_paused_ex() override = default;

    [[nodiscard]] bool Paused() const noexcept { return paused; }

    void Pause(bool b) noexcept
    {
        if (paused == b)
            return;

        const auto current = Now();
        if (b)
        {
            save_clock = current;
            pauseDuration = CTimerBase::getElapsedTime();
        }
        else
        {
            pauseAccum += current - save_clock;
        }

        paused = b;
    }
};

class CTimer_paused final : public CTimer_paused_ex
{
    RTTI_DECLARE_TYPEINFO(CTimer_paused, CTimer_paused_ex);

public:
    CTimer_paused() { g_pauseMngr_get().Register(*this); }
    ~CTimer_paused() override { g_pauseMngr_get().UnRegister(*this); }
};

extern BOOL g_bEnableStatGather;

class CStatTimer final
{
    using Duration = CTimerBase::Duration;

public:
    CTimer T{};
    Duration accum{};
    f32 result{};
    u32 count{};

    constexpr CStatTimer() noexcept = default;

    void FrameStart();
    void FrameEnd();

    void Begin()
    {
        if (!g_bEnableStatGather)
            return;

        count++;
        T.Start();
    }

    void End()
    {
        if (!g_bEnableStatGather)
            return;

        accum += T.getElapsedTime();
    }

    [[nodiscard]] Duration getElapsedTime() const { return accum; }

    [[nodiscard]] auto GetElapsed_ns() const
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(getElapsedTime()).count();
    }

    [[nodiscard]] auto GetElapsed_ms() const
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(getElapsedTime()).count();
    }

    [[nodiscard]] f32 GetElapsed_sec() const
    {
        using namespace std::chrono;
        return duration_cast<duration<f32>>(getElapsedTime()).count();
    }
};
