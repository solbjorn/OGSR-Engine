#include "StdAfx.h"

#include "r__occlusion.h"

#include "QueryHelper.h"

#include "sleep.h"

R_occlusion::~R_occlusion() { occq_destroy(true); }

void R_occlusion::occq_create()
{
    enabled = !strstr(Core.Params, "-no_occq");
    last_frame = Device.dwFrame;
}

void R_occlusion::occq_destroy(bool silent)
{
    const auto p_cnt = std::ssize(pool);

    if (!silent)
        Msg("* [{}]: fids[{}] used[{}] pool[{}]", std::source_location::current().function_name(), std::ssize(fids), std::ssize(used), p_cnt);

    gsl::index u_cnt{};

    for (const auto& it : used)
    {
        if (it.Q)
            u_cnt++;
    }

    used.clear();
    pool.clear();
    fids.clear();

    if (!silent)
        Msg("* [{}]: released [{}] used and [{}] pool queries", std::source_location::current().function_name(), u_cnt, p_cnt);
}

void R_occlusion::cleanup_lost()
{
    u32 cnt = 0;
    for (u32 ID = 0; ID < used.size(); ID++)
    {
        if (used[ID].Q && used[ID].ttl && used[ID].ttl < Device.dwFrame)
        {
            occq_free(ID);
            cnt++;
        }
    }

    if (cnt > 0)
        XR_LOG_DYNAMIC_DEBUG(xr::level::Error, "Cleanup {} lost queries", cnt);
}

u32 R_occlusion::occq_begin(u32& ID, ctx_id_t context_id)
{
    if (!enabled)
    {
        ID = iInvalidHandle;
        return 0;
    }

    std::scoped_lock slock{lock};

    if (last_frame != Device.dwFrame)
    {
        cleanup_lost();
        last_frame = Device.dwFrame;
    }

    RImplementation.stats.o_queries++;
    if (fids.empty())
    {
        ID = u32(used.size());
        _Q q{};
        q.order = ID;
        if (FAILED(CreateQuery(q.Q.GetAddressOf(), D3D_QUERY_OCCLUSION)))
        {
            if (Device.dwFrame % 100 == 0)
                Msg("RENDER [Warning]: Too many occlusion queries were issued: {} !!!", used.size());
            ID = iInvalidHandle;
            return 0;
        }
        used.push_back(std::move(q));
    }
    else
    {
        XR_ASSERT(pool.size() == fids.size());

        ID = fids.back();
        fids.pop_back();

        used[ID].Q = pool.back().Q;
        pool.pop_back();
    }

    used[ID].ttl = Device.dwFrame + 1;
    XR_ASSERT(xr::hr(BeginQuery(used[ID].Q.Get(), context_id)));

    return used[ID].order;
}

void R_occlusion::occq_end(u32& ID, ctx_id_t context_id)
{
    if (!enabled || ID == iInvalidHandle)
        return;

    std::scoped_lock slock{lock};

    if (!used[ID].Q)
        return;

    XR_ASSERT(xr::hr(EndQuery(used[ID].Q.Get(), context_id)));
    used[ID].ttl = Device.dwFrame + 1;
}

R_occlusion::occq_result R_occlusion::occq_get(u32& ID)
{
    if (!enabled || ID == iInvalidHandle)
        return std::numeric_limits<occq_result>::max();

    std::scoped_lock slock{lock};

    if (!used[ID].Q)
        return std::numeric_limits<occq_result>::max();

    XR_TRACY_ZONE_SCOPED();

    occq_result fragments;
    HRESULT hr;

    // здесь нужно дождаться результата, т.к. отладка показывает, что
    // очень редко когда он готов немедленно
    Device.Statistic->RenderDUMP_Wait.Begin();

    if (std::__libcpp_thread_poll_with_backoff(
            [this, &hr, ID, &fragments] -> bool {
                hr = GetData(used[ID].Q.Get(), &fragments, sizeof(fragments));
                return hr != S_FALSE;
            },
            xr::backoff, std::chrono::milliseconds{ps_r2_wait_timeout}) == std::__poll_with_backoff_results::__timeout)
        fragments = std::numeric_limits<occq_result>::max();

    Device.Statistic->RenderDUMP_Wait.End();

    if (hr == D3DERR_DEVICELOST)
        fragments = std::numeric_limits<occq_result>::max();

    if (fragments == 0)
        RImplementation.stats.o_culled++;

    // remove from used and shrink as nesessary
    occq_free(ID);
    ID = 0;

    return fragments;
}

void R_occlusion::occq_free(u32 ID)
{
    std::scoped_lock slock{lock};

    if (used[ID].Q)
    {
        pool.push_back(used[ID]);
        used[ID].Q.Reset();
        fids.push_back(std::move(ID));
    }
}
