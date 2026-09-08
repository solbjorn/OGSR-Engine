// xrCore.cpp : Defines the entry point for the DLL application.
//
#include "stdafx.h"

#include "xrCore.h"

#include "cpu.h"
#include "sleep.h"
#include "xrXMLParser.h"

#include <mmsystem.h>
#include <objbase.h>

xrCore Core;

// indicate that we reach WinMain, and all static variables are initialized
bool gModulesLoaded = false;

static u32 init_counter = 0;

void xrCore::_initialize(gsl::czstring _ApplicationName, bool init_fs, gsl::czstring fs_fname)
{
    strcpy_s(ApplicationName, _ApplicationName);
    if (0 == init_counter)
    {
        /*
            По сути это не рекомендуемый Microsoft, но повсеместно используемый
           способ повышения точности соблюдения и измерения временных интревалов
           функциями Sleep, QueryPerformanceCounter, timeGetTime и GetTickCount.
            Функция действует на всю операционную систему в целом (!) и нет
           необходимости вызывать её при старте нового потока. Вызов
           timeEndPeriod специалисты Microsoft считают обязательным. Есть
           подозрения, что Windows сама устанавливает максимальную точность при
           старте таких приложений как, например, игры. Тогда есть шанс, что
           вызов timeBeginPeriod здесь бессмысленен. Недостатком данного способа
           является то, что он приводит к общему замедлению работы как текущего
           приложения, так и всей операционной системы. Ещё можно посмотреть
           ссылки:
            https://msdn.microsoft.com/en-us/library/vs/alm/dd757624(v=vs.85).aspx
            https://users.livejournal.com/-winnie/151099.html
            https://github.com/tebjan/TimerTool
        */
        timeBeginPeriod(1);
        xr::detail::timer_create();

        strcpy_s(Params, sizeof(Params), GetCommandLineA());

        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        if (strstr(Params, "-dbg"))
            ParamFlags.set(ParamFlag::dbg, TRUE);

        // application path
        string_path fn, dr, di;
        GetModuleFileNameA(nullptr, fn, sizeof(fn));
        _splitpath(fn, dr, di, nullptr, nullptr);
        strconcat(sizeof(ApplicationPath), ApplicationPath, dr, di);

        // -use-work-dir используется, когда при отладке в Working Directory указывают путь к ресурсам игры
        // в таком случае не надо вызывать SetCurrentDirectory :)
        if (strstr(Core.Params, "-use-work-dir") == nullptr)
        {
            // Рабочий каталог для процесса надо устанавливать принудительно
            // в папку с движком, независимо откуда запустили. Иначе начинаются
            // чудеса типа игнорирования движком символов для стектрейсинга.
            SetCurrentDirectoryA(ApplicationPath);
            GetCurrentDirectoryA(sizeof(WorkingPath), WorkingPath);
        }

        // User/Comp Name
        DWORD sz_user = sizeof(UserName);
        GetUserNameA(UserName, &sz_user);

        DWORD sz_comp = sizeof(CompName);
        GetComputerNameA(CompName, &sz_comp);

        xrMemory::_initialize();
        CPU::ID.print();

        xr::detail::xml_init();
        xr_FS = std::make_unique<CLocatorAPI>();
    }

    if (init_fs)
    {
        u32 flags = 0;
        if (strstr(Params, "-build"))
            flags |= CLocatorAPI::flBuildCopy;
        if (strstr(Params, "-ebuild"))
            flags |= CLocatorAPI::flBuildCopy | CLocatorAPI::flEBuildCopy;
#ifdef DEBUG
        if constexpr (false) /*(strstr(Params,"-cache"))*/
            flags |= CLocatorAPI::flCacheFiles;
        else
            flags &= ~CLocatorAPI::flCacheFiles;
#endif // DEBUG
        flags |= CLocatorAPI::flScanAppRoot;

        if (strstr(Params, "-file_activity"))
            flags |= CLocatorAPI::flDumpFileActivity;

        FS._initialize(flags, nullptr, fs_fname);

        XR_LOG_INFO("[ReapeR Engine ({})] build date: [" __DATE__ " " __TIME__ "]", GetBuildConfiguration());

        if (xr_strlen(APPVEYOR_BUILD_VERSION) > 0)
            XR_LOG_INFO("[AppVeyor] build version: [" APPVEYOR_BUILD_VERSION "], repo: [" APPVEYOR_REPO_NAME "]");

#ifdef __clang__
        XR_LOG_INFO("Clang/LLVM version: [" __clang_version__ "], C++ standard version: [" _CRT_STRINGIZE(__cplusplus) "]");
#else
        XR_LOG_INFO("MSVC version: [" _CRT_STRINGIZE(_MSC_FULL_VER) "], MSVC++ standard version: [" _CRT_STRINGIZE(_MSVC_LANG) "]");
#endif

        XR_LOG_NOTICE("Working Directory: [{}]", WorkingPath);
        XR_LOG_INFO("Command Line: [{}]", Core.Params);

#ifdef DEBUG
        XR_LOG_NOTICE("CRT heap {:#010x}", _get_heap_handle());
        XR_LOG_NOTICE("Process heap {:#010x}", GetProcessHeap());
#endif // DEBUG
    }

    init_counter++;
}

void xrCore::_destroy()
{
    if (--init_counter == 0)
    {
        FS._destroy();
        xr_FS.reset();

        CoUninitialize();

        xr::detail::timer_destroy();
        timeEndPeriod(1);
    }
}

xr_string xrCore::GetEngineVersion()
{
    if (xr_strlen(APPVEYOR_BUILD_VERSION) > 0)
        return xr::format(APPVEYOR_BUILD_VERSION " ({}) from repo: [" APPVEYOR_REPO_NAME "]", GetBuildConfiguration());
    else
        return xr::format("[ReapeR Engine {} (build: " __DATE__ " " __TIME__ ")]", GetBuildConfiguration());
}
