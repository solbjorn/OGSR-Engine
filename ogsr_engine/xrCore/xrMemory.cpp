#include "stdafx.h"

#include "xrsharedmem.h"

#include <psapi.h>

#ifndef __SANITIZE_ADDRESS__
#define USE_MIMALLOC
#endif

#ifdef USE_MIMALLOC
// string.h
#undef _strdup

#define MI_SHARED_LIB

XR_DIAG_PUSH();
XR_DIAG_IGNORE("-Wc++98-compat-extra-semi");
XR_DIAG_IGNORE("-Wzero-as-null-pointer-constant");

#include <mimalloc-override.h>
#include <mimalloc-new-delete.h>

XR_DIAG_POP();

#undef MI_SHARED_LIB

#pragma comment(lib, "mimalloc.lib")

namespace xr
{
namespace
{
quill::Logger* mimalloc_logger{nullptr};

void mimalloc_print(gsl::czstring msg, void* arg)
{
    XR_LOG__DYNAMIC(xr::mimalloc_logger, gsl::narrow<xr::level>(std::bit_cast<std::uintptr_t>(arg)), "{}", msg);
}
} // namespace
} // namespace xr
#endif // USE_MIMALLOC

void xrMemory::_initialize()
{
#ifdef USE_MIMALLOC
    xr::mimalloc_logger = xr::logger_init("mimalloc");

    mi_register_output(&xr::mimalloc_print, std::bit_cast<void*>(std::uintptr_t{std::to_underlying(xr::level::Error)}));
    mi_options_print_out(&xr::mimalloc_print, std::bit_cast<void*>(std::uintptr_t{std::to_underlying(xr::level::Debug)}));
#endif

    SProcessMemInfo memCounters;
    GetProcessMemInfo(memCounters);
}

void xrMemory::mem_compact()
{
#ifndef USE_MIMALLOC
    /*
    Следующая команда, в целом, не нужна.
    Современные аллокаторы достаточно грамотно и когда нужно возвращают память операционной системе.
    Эта строчка нужна, скорее всего, в определённых ситуациях, вроде использования файлов отображаемых в память,
    которые требуют большие свободные области памяти.
    */
    _heapmin(); //-V530
    HeapCompact(GetProcessHeap(), 0);
#endif

    str_container::clean();
    smem_container::clean();
}

XR_RESTRICT void* xrMemory::mem_alloc_aligned(gsl::index size, gsl::index align) noexcept
{
    const auto sz = gsl::narrow<size_t>(size);

    void* ptr = _aligned_malloc(sz, gsl::narrow<size_t>(align));
    TracyAlloc(ptr, sz);

    return ptr;
}

void* xrMemory::mem_realloc_aligned(void* P, gsl::index size, gsl::index align) noexcept
{
    const auto sz = gsl::narrow<size_t>(size);

    TracyFree(P);
    P = _aligned_realloc(P, sz, gsl::narrow<size_t>(align));
    TracyAlloc(P, sz);

    return P;
}

void xrMemory::mem_free_aligned(void* P) noexcept
{
    TracyFree(P);
    _aligned_free(P);
}

gsl::index xrMemory::mem_usage(gsl::index* pBlocksUsed, gsl::index* pBlocksFree) { return mem_usage_impl(pBlocksUsed, pBlocksFree); }

void GetProcessMemInfo(SProcessMemInfo& minfo)
{
    std::memset(&minfo, 0, sizeof(SProcessMemInfo));

    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    minfo.TotalPhysicalMemory = mem.ullTotalPhys;
    minfo.FreePhysicalMemory = mem.ullAvailPhys;

    minfo.TotalPageFile = mem.ullTotalPageFile - mem.ullTotalPhys;

    if (minfo.TotalPageFile > 0l)
    {
        // эта херня погоду показывает на самом деле. надо найти способ как получить свободный размер файла подкачки
        minfo.FreePageFile = mem.ullAvailPageFile > mem.ullAvailPhys ? mem.ullAvailPageFile - mem.ullAvailPhys : mem.ullAvailPageFile;
    }

    minfo.MemoryLoad = mem.dwMemoryLoad;

    PROCESS_MEMORY_COUNTERS pc;
    std::memset(&pc, 0, sizeof(PROCESS_MEMORY_COUNTERS));
    pc.cb = sizeof(pc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pc, sizeof(pc)))
    {
        minfo.WorkingSetSize = pc.WorkingSetSize;
        minfo.PeakWorkingSetSize = pc.PeakWorkingSetSize;

        minfo.PagefileUsage = pc.PagefileUsage;
        minfo.PeakPagefileUsage = pc.PeakPagefileUsage;
    }

#ifdef USE_MIMALLOC
    mi_stats_print_out(&xr::mimalloc_print, std::bit_cast<void*>(std::uintptr_t{std::to_underlying(xr::level::Info)}));
#endif
}

gsl::index mem_usage_impl(gsl::index* pBlocksUsed, gsl::index* pBlocksFree)
{
    static const bool no_memory_usage = !strstr(Core.Params, "-memory_usage");
    if (no_memory_usage)
        return 0;

    _HEAPINFO hinfo;
    int heapstatus;
    hinfo._pentry = nullptr;

    gsl::index total{};
    gsl::index blocks_free{};
    gsl::index blocks_used{};

    while ((heapstatus = _heapwalk(&hinfo)) == _HEAPOK)
    {
        if (hinfo._useflag == _USEDENTRY)
        {
            total += hinfo._size;
            blocks_used += 1;
        }
        else
        {
            blocks_free += 1;
        }
    }

    if (pBlocksFree)
        *pBlocksFree = 1024 * blocks_free;
    if (pBlocksUsed)
        *pBlocksUsed = 1024 * blocks_used;

    switch (heapstatus)
    {
    case _HEAPBADPTR: XR_LOG_CRITICAL("Bad pointer to heap"); break;
    case _HEAPBADBEGIN: XR_LOG_CRITICAL("Bad start of heap"); break;
    case _HEAPBADNODE: XR_LOG_CRITICAL("Bad node in heap"); break;
    default: break;
    }

    return total;
}
