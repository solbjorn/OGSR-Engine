#include "stdafx.h"

#ifdef USE_MEMORY_VALIDATOR

#include "xrMemoryDebug.h"

static std::map<void*, PointerInfo, std::less<>, PointerAllocator<std::pair<void* const, PointerInfo>>> gPointerRegistry;
static std::recursive_mutex gPointerRegistryProtector;

bool g_enable_memory_debug{true};
static bool g_enable_double_free_check = g_enable_memory_debug;

void PointerRegistryAdd(void* ptr, PointerInfo&& info)
{
    if (gModulesLoaded && g_enable_memory_debug && ptr != nullptr)
    {
        const std::scoped_lock lock{gPointerRegistryProtector};
        gPointerRegistry.emplace(ptr, std::move(info));
    }
}

void PointerRegistryRelease(const void* ptr, const std::source_location& loc, const bool is_class)
{
    if (ExitFromWinMain)
        return;

    if (gModulesLoaded && g_enable_memory_debug && ptr != nullptr)
    {
        const std::scoped_lock lock{gPointerRegistryProtector};

        if (const auto search = gPointerRegistry.find(ptr); search != gPointerRegistry.end())
        {
            if (search->second.is_class && !is_class)
                XR_LOG_ERROR("Wrong release call [{}.{} ({})]! xr_free called for class ?. ID [{}]", loc.file_name(), loc.line(), loc.function_name(),
                             search->second.identity);

            gPointerRegistry.erase(search);
        }
        else if (g_enable_double_free_check)
        {
            XR_LOG_ERROR("Ptr [{}] not found in gPointerRegistry! Potential double-free! Called from: [{}.{} ({})]", ptr, loc.file_name(), loc.line(),
                         loc.function_name());
        }
    }
}

void PointerRegistryClear()
{
    if (gModulesLoaded)
    {
        const std::scoped_lock lock{gPointerRegistryProtector};

        gPointerRegistry.clear();
        g_enable_double_free_check = false;
    }
}

void PointerRegistryDump(float thresholdInKb)
{
    if (gModulesLoaded)
    {
        const std::scoped_lock lock{gPointerRegistryProtector};

        // cnt,size
        std::vector<std::pair<const char*, std::tuple<size_t, size_t>>, PointerAllocator<std::pair<const char*, std::tuple<size_t, size_t>>>> tmp;

        size_t cnt = gPointerRegistry.size();
        size_t size = 0;

        for (const auto& pair : gPointerRegistry)
        {
            if (const auto search = std::ranges::find_if(tmp, [&pair](const auto& v) { return std::is_eq(xr_strcmp(v.first, pair.second.identity)); });
                search != tmp.end())
            {
                std::get<0>(search->second) += 1;
                std::get<1>(search->second) += pair.second.size;
            }
            else
            {
                tmp.emplace_back(pair.second.identity.c_str(), std::make_tuple(1, pair.second.size));
            }

            size += pair.second.size;
        }

        XR_LOG_TRACE_L1("Instance count [{}]. total size [{:.2} Kb]", cnt, gsl::narrow_cast<f32>(size) / 1024.0f);
        XR_LOG_TRACE_L1("Dump (large that [{} Kb]):", thresholdInKb);

        std::ranges::sort(tmp, [](const auto& a, const auto& b) { return std::get<1>(a.second) > std::get<1>(b.second); });

        for (const auto& [name, typle] : tmp)
        {
            if (const auto total_size = gsl::narrow_cast<f32>(std::get<1>(typle)) / 1024.0f; total_size > thresholdInKb)
                XR_LOG_TRACE_L1(" Total size:[{:.4} Kb], instance count:[{}], id: {}", total_size, std::get<0>(typle), name);
        }
    }
}

void PointerRegistryInfo()
{
    if (gModulesLoaded)
    {
        const std::scoped_lock lock{gPointerRegistryProtector};

        size_t cnt = gPointerRegistry.size();
        size_t size = 0;

        for (const auto& pair : gPointerRegistry)
            size += pair.second.size;

        XR_LOG_DEBUG("Instance count [{}]. total size [{:.2} Kb]", cnt, gsl::narrow_cast<f32>(size) / 1024.0f);
    }
}

#endif
