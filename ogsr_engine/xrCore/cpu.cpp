#include "stdafx.h"

#include "cpu.h"

namespace xr
{
namespace
{
constexpr std::array<std::string_view, cpu_features::X86_MICROARCHITECTURE_LAST_> mc_names{"unknown microarchitecture",
                                                                                           "ZX-C Zhangjiang",
                                                                                           "ZX-D Wudaokou",
                                                                                           "ZX-E Lujiazui",
                                                                                           "ZX-E Yongfeng",
                                                                                           "80486",
                                                                                           "P5 Pentium",
                                                                                           "Lakemont",
                                                                                           "Core's Merom",
                                                                                           "Core's Penryn",
                                                                                           "Nehalem",
                                                                                           "Bonnell",
                                                                                           "Nehalem's Westmere",
                                                                                           "Sandy Bridge",
                                                                                           "Sandy Bridge's Ivy Bridge",
                                                                                           "Silvermont's Bay Trail",
                                                                                           "Haswell",
                                                                                           "Haswell's Broadwell",
                                                                                           "Skylake",
                                                                                           "Skylake's Cascade Lake",
                                                                                           "Goldmont's Apollo Lake",
                                                                                           "Goldmont Plus' Gemini Lake",
                                                                                           "Tremont's Lakefield",
                                                                                           "Skylake's Kaby Lake",
                                                                                           "Skylake's Coffee Lake",
                                                                                           "Skylake's Whiskey Lake",
                                                                                           "Skylake's Comet Lake",
                                                                                           "Palm Cove's Cannon Lake",
                                                                                           "Sunny Cove's Ice Lake",
                                                                                           "Willow Cove's Tiger Lake",
                                                                                           "Golden Cove's Sapphire Rapids",
                                                                                           "Golden Cove's Alder Lake",
                                                                                           "Cypress Cove's Rocket Lake",
                                                                                           "Raptor Cove's Raptor Lake",
                                                                                           "Lion Cove's Lunar Lake",
                                                                                           "Lion Cove's Arrow Lake",
                                                                                           "Knights Mill",
                                                                                           "Knights Landing",
                                                                                           "Knights Ferry",
                                                                                           "Knights Corner",
                                                                                           "NetBurst",
                                                                                           "K8 Hammer",
                                                                                           "K10",
                                                                                           "K10 Husky",
                                                                                           "K10 Llano",
                                                                                           "K14 Bobcat",
                                                                                           "K15 Piledriver",
                                                                                           "K15 Steamroller",
                                                                                           "K15 Excavator",
                                                                                           "K15 Bulldozer",
                                                                                           "K16 Jaguar",
                                                                                           "K16 Puma",
                                                                                           "K17 Zen",
                                                                                           "K17 Zen+",
                                                                                           "K17 Zen 2",
                                                                                           "K19 Zen 3",
                                                                                           "K19 Zen 4",
                                                                                           "K1A Zen 5"};
constexpr std::array<std::string_view, cpu_features::CPU_FEATURE_CACHE_PREFETCH + 1> cache_names{"unknown", "data",  "instruction", "unified",
                                                                                                 "TLB",     "D-TLB", "S-TLB",       "prefetch"};
} // namespace
} // namespace xr

namespace CPU
{
_processor_info ID;

s64 qpc_freq;
u32 qpc_counter{0};

s64 QPC()
{
    ++qpc_counter;

    ::LARGE_INTEGER dest;
    ::QueryPerformanceCounter(&dest);

    return dest.QuadPart;
}
} // namespace CPU

void _processor_info::init()
{
    std::ranges::sort(threads, {}, &tmc::topology::thread_info::index);
    features = cpu_features::GetX86Info().features;

    ::SYSTEM_INFO sysInfo;
    ::GetSystemInfo(&sysInfo);

    m_dwNumberOfProcessors = sysInfo.dwNumberOfProcessors;
    fUsage = std::make_unique<float[]>(m_dwNumberOfProcessors);
    m_idleTime = std::make_unique<LARGE_INTEGER[]>(m_dwNumberOfProcessors);
    perfomanceInfo = std::make_unique<::SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION[]>(m_dwNumberOfProcessors);

    ::LARGE_INTEGER Freq;
    ::QueryPerformanceFrequency(&Freq);
    CPU::qpc_freq = Freq.QuadPart;
}

void _processor_info::print_features(std::string_view pfx, std::span<const std::string_view> list)
{
    if (list.empty())
    {
        XR_LOG_INFO("{}:", pfx);
        return;
    }

    const auto indent = pfx.size() + 2;
    const xr_string wrap(indent - 1, ' ');

    xr_string out;
    out.reserve(512);
    auto it = std::back_inserter(out);

    xr::format_to(it, "{}: {}", pfx, list.front());
    auto line = indent + list.front().size();

    for (auto feat : list | std::views::drop(1))
    {
        if (const auto entry = feat.size() + 2; line + entry > xr::detail::log_width)
        {
            xr::format_to(it, ",\n{} {}", wrap, feat);
            line = indent + entry;
        }
        else
        {
            xr::format_to(it, ", {}", feat);
            line += entry;
        }
    }

    XR_LOG_INFO("{}", std::move(out));
}

void _processor_info::print_features()
{
    const auto info = cpu_features::GetX86Info();

    XR_LOG_INFO("CPU: {} ({}: family {}, model {}, stepping {})", info.brand_string, xr::mc_names[cpu_features::GetX86Microarchitecture(&info)], info.family,
                info.model, info.stepping);

    xr_vector<std::string_view> avail;
    avail.reserve(64);

    xr_vector<std::string_view> na;
    na.reserve(64);

    for (auto i = gsl::narrow_cast<cpu_features::X86FeaturesEnum>(0); i < cpu_features::X86_LAST_;
         i = gsl::narrow_cast<cpu_features::X86FeaturesEnum>(s32{i} + 1))
        (cpu_features::GetX86FeaturesEnumValue(&info.features, i) != 0 ? avail : na).emplace_back(cpu_features::GetX86FeaturesEnumName(i));

    print_features(" Features", std::move(avail));
    print_features(" Not present", std::move(na));
}

void _processor_info::print_cache()
{
    XR_LOG_INFO("Caches:");

    auto info = cpu_features::GetX86CacheInfo();
    const std::span<cpu_features::CacheLevelInfo> list{info.levels, gsl::narrow_cast<std::size_t>(info.size)};

    std::ranges::sort(list, {}, [] [[nodiscard]] (const auto& elem) { return std::forward_as_tuple(elem.level, elem.cache_type); });

    for (const auto& elem : list)
        XR_LOG_INFO(" L{} {} cache: {} KiB {}-way, line size {} bytes, {} TLB entries, {} line(s) per sector", elem.level, xr::cache_names[elem.cache_type],
                    elem.cache_size / 1024, elem.ways, elem.line_size, elem.tlb_entries, elem.partitioning);
}

void _processor_info::print_topology(std::string_view pfx, std::span<const std::size_t> list)
{
    xr_string out;
    out.reserve(64);
    auto it = std::back_inserter(out);

    xr::format_to(it, "{}: {}", pfx, list.front());

    for (auto idx : list | std::views::drop(1))
        xr::format_to(it, ", {}", idx);

    XR_LOG_INFO("{}", std::move(out));
}

void _processor_info::print_topology()
{
    const bool hybrid = topo.is_hybrid();

    XR_LOG_INFO("NUMA nodes: {}", topo.numa_count());
    XR_LOG_INFO("Hybrid architecture: {}", hybrid ? "yes" : "no");
    XR_LOG_INFO("Physical cores: {}", topo.core_count());

    if (hybrid)
    {
        XR_LOG_INFO(" Performance cores: {}", topo.cpu_kind_counts[0]);
        XR_LOG_INFO(" Efficiency cores: {}", topo.cpu_kind_counts[1]);
    }

    XR_LOG_INFO("Logical processors: {}", topo.pu_count());
    XR_LOG_INFO(" Container CPU quota: {}", topo.container_cpu_quota);
    XR_LOG_INFO("Core groups: {}", topo.group_count());

    for (const auto& group : topo.groups)
    {
        XR_LOG_INFO(" Group {}: NUMA {}, kind: {}, SMT: {}", group.index, group.numa_index,
                    group.cpu_kind == tmc::topology::cpu_kind::PERFORMANCE ? "performance" : "efficiency", group.smt_level);
        print_topology("  Cores", group.core_indexes);
    }

    XR_LOG_INFO("TMC threads: {} (main) + {} (ST) + {} (Asio)", tmc::cpu_executor().thread_count(), xr::tmc_cpu_st_executor().thread_count(), 1);

    if (!hybrid)
        return;

    xr_vector<std::size_t> perf, eff;

    for (const auto& thread : threads)
        (thread.group.cpu_kind == tmc::topology::cpu_kind::PERFORMANCE ? perf : eff).emplace_back(thread.index);

    print_topology(" Performance", std::move(perf));
    print_topology(" Efficiency", std::move(eff));
}

void _processor_info::print()
{
    print_features();
    print_cache();
    print_topology();
}

namespace
{
[[nodiscard]] constexpr s64 SubtractTimes(FILETIME one, FILETIME two)
{
    ULARGE_INTEGER a, b;
    a.LowPart = one.dwLowDateTime;
    a.HighPart = one.dwHighDateTime;

    b.LowPart = two.dwLowDateTime;
    b.HighPart = two.dwHighDateTime;

    return gsl::narrow_cast<s64>(a.QuadPart) - gsl::narrow_cast<s64>(b.QuadPart);
}
} // namespace

bool _processor_info::getCPULoad(f64& val)
{
    FILETIME sysIdle, sysKernel, sysUser;
    // sysKernel include IdleTime
    if (GetSystemTimes(&sysIdle, &sysKernel, &sysUser) == 0) // GetSystemTimes func FAILED return value is zero;
        return false;

    if (prevSysIdle.dwLowDateTime != 0 && prevSysIdle.dwHighDateTime != 0)
    {
        s64 sysIdleDiff, sysKernelDiff, sysUserDiff;
        sysIdleDiff = SubtractTimes(sysIdle, prevSysIdle);
        sysKernelDiff = SubtractTimes(sysKernel, prevSysKernel);
        sysUserDiff = SubtractTimes(sysUser, prevSysUser);

        s64 sysTotal = sysKernelDiff + sysUserDiff;
        s64 kernelTotal = sysKernelDiff - sysIdleDiff; // kernelTime - IdleTime = kernelTime, because sysKernel include IdleTime

        if (sysTotal > 0) // sometimes kernelTime > idleTime
            val = gsl::narrow_cast<f64>(kernelTotal + sysUserDiff) * 100.0 / gsl::narrow_cast<f64>(sysTotal);
    }

    prevSysIdle = sysIdle;
    prevSysKernel = sysKernel;
    prevSysUser = sysUser;

    return true;
}

void _processor_info::MTCPULoad()
{
    using NTQUERYSYSTEMINFORMATION = NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    static const auto m_pNtQuerySystemInformation =
        reinterpret_cast<NTQUERYSYSTEMINFORMATION>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation"));

    if (!NT_SUCCESS(m_pNtQuerySystemInformation(SystemProcessorPerformanceInformation, perfomanceInfo.get(),
                                                sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * m_dwNumberOfProcessors, nullptr)))
        XR_LOG_ERROR("Can't get NtQuerySystemInformation");

    DWORD dwTickCount = GetTickCount();
    if (!m_dwCount)
        m_dwCount = dwTickCount;

    for (DWORD i = 0; i < m_dwNumberOfProcessors; i++)
    {
        auto& cpuPerfInfo = perfomanceInfo[i];
        cpuPerfInfo.KernelTime.QuadPart -= cpuPerfInfo.IdleTime.QuadPart;

        fUsage[i] =
            100.0f - 0.01f * gsl::narrow_cast<f32>(cpuPerfInfo.IdleTime.QuadPart - m_idleTime[i].QuadPart) / gsl::narrow_cast<f32>(dwTickCount - m_dwCount);
        fUsage[i] = std::clamp(fUsage[i], 0.0f, 100.0f);

        m_idleTime[i] = cpuPerfInfo.IdleTime;
    }

    m_dwCount = dwTickCount;
}
