#pragma once

#include <cpu_features/cpuinfo_x86.h>

#include <winternl.h>

struct _processor_info final
{
private:
    cpu_features::X86Features features;

public:
    std::unique_ptr<float[]> fUsage;
    DWORD m_dwNumberOfProcessors;

    [[nodiscard]] bool getCPULoad(f64& val);
    void MTCPULoad();

private:
    DWORD m_dwCount = 0;
    FILETIME prevSysIdle{}, prevSysKernel{}, prevSysUser{};
    std::unique_ptr<LARGE_INTEGER[]> m_idleTime;
    std::unique_ptr<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION[]> perfomanceInfo;

    const tmc::ex_any* const cpu{tmc::cpu_executor().type_erased()};
    const tmc::ex_any* const st{xr::tmc_cpu_st_executor().type_erased()};
    const tmc::ex_any* const asio{tmc::asio_executor().type_erased()};

    void print_features(std::string_view pfx, std::span<const std::string_view> list);
    void print_features();
    void print_cache();
    void print_topology(std::string_view pfx, std::span<const std::size_t> list);
    void print_topology();

public:
    xr_vector<tmc::topology::thread_info> threads;
    tmc::topology::cpu_topology topo;

    [[nodiscard]] constexpr auto this_thread() const
    {
        if (tmc::current_executor() == cpu)
            return xr::format("{}{:02}P{}", threads[tmc::current_thread_index()].group.cpu_kind == tmc::topology::cpu_kind::PERFORMANCE ? "PE" : "EF",
                              tmc::current_thread_index(), tmc::current_priority());
        else if (tmc::current_executor() == st)
            return xr::format("ST00P{}", tmc::current_priority());
        else if (tmc::current_executor() == asio)
            return xr::format("AS00P{}", tmc::current_priority());
        else
            return xr::format("E{:X>5}", std::this_thread::get_id());
    }

    void init();
    void print();
};
