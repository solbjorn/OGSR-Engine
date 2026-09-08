set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 26)

# https://github.com/llvm/llvm-project/commit/2ba7f11bce7f
if(NOT CMAKE_DISABLE_PRECOMPILE_HEADERS)
  set(CMAKE_PCH_PROLOGUE "")
endif()

set(conformance_options "/Brepro /bigobj /clang:-fconstexpr-depth=2147483647 /clang:-fconstexpr-steps=2147483647 /permissive- /volatile:iso /Zc:inline /Zc:preprocessor /Zc:enumTypes /Zc:lambda /Zc:__STDC__ /Zc:__cplusplus /Zc:externConstexpr /Zc:throwingNew /Zc:checkGwOdr /Zc:forScope /Zc:templateScope /Zc:u8EscapeEncoding /Zc:wchar_t /Zc:alignedNew /Zc:noexceptTypes /Zc:nrvo /Zc:tlsGuards /JMC- /EHsc- /DNOMINMAX /DSTRICT -fstrict-aliasing -fno-delayed-template-parsing -fno-wrapv /D_CRT_STDIO_ISO_WIDE_SPECIFIERS -fno-ms-compatibility -fgnuc-version=0 /utf-8")

set(llvm_options "-fuse-ld=lld-link -gcodeview-ghash -march=x86-64-v3 -mavx2 -mbmi -mbmi2 -mfma -mlzcnt -flto -fmerge-all-constants -fforce-emit-vtables -fwhole-program-vtables /clang:-fcoro-aligned-allocation")

set(warning_options "-Wextra -Wmost -Wno-error=unused-command-line-argument -Werror=format -Wformat-nonliteral -Werror=format-pedantic -Werror=format-signedness -Werror=format-type-confusion -Werror=inconsistent-missing-override -Werror=microsoft -Werror=move -Werror=nan-infinity-disabled -Werror=parentheses -Werror=strict-aliasing -Werror=tautological-compare -Werror=typename-missing -Werror=weak-vtables")

# hwloc and ASIO SDK don't behave
# DwarFS uses `UNICODE` as an enumeration
if(NOT (HWLOC_SKIP_LSTOPO OR PA_USE_ASIO))
  cmake_policy(SET CMP0204 NEW)
  add_compile_definitions(_UNICODE)

  if(DEFINED WITH_FUSE_DRIVER)
    add_compile_definitions(UNICODE=UNICODE)
  else()
    add_compile_definitions(UNICODE)
  endif()
endif()

# DirectXTex
if(ENABLE_OPENEXR_SUPPORT)
  set(conformance_options "${conformance_options} -D_FORCENAMELESSUNION")
endif()

# OpenJPH
if(DEFINED OJPH_BUILD_EXECUTABLES)
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# SFML
if(SFML_USE_SYSTEM_DEPS)
  set(conformance_options "${conformance_options} -D_WIN32_WINDOWS=0x0A00 -DHB_DISABLE_DEPRECATED")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# Skribidi
if(DEFINED SKRIBIDI_EXAMPLE)
  set(conformance_options "${conformance_options} -DHB_DISABLE_DEPRECATED")
  set(warning_options "${warning_options} -Wno-error=deprecated-declarations -Wno-error=logical-op-parentheses -Wno-error=missing-format-attribute -Wno-error=sign-compare -Wno-error=unused-function -Wno-error=unused-parameter -Wno-error=unused-variable")
endif()

# abseil
if(ABSL_PROPAGATE_CXX_STD)
  set(warning_options "${warning_options} -Wno-error=format -Wno-error=format-signedness")
endif()

# archive
if(DEFINED ENABLE_CPIO)
  set(conformance_options "${conformance_options} -DHAVE_VPRINTF -DHAVE_WMEMCMP -DHAVE_WMEMCPY -DHAVE_WMEMMOVE -DLZMA_API_STATIC -DS_IFCHR=_S_IFCHR -DS_IFDIR=_S_IFDIR -DS_IFMT=_S_IFMT -DS_IFREG=_S_IFREG -DWINCRYPT_USE_SYMBOL_PREFIX -Ddev_t=_dev_t -Dino_t=_ino_t -Doff_t=_off_t -Dstat=_stat")
  set(warning_options "${warning_options} -Wno-error=format")
endif()

# assert
if(LIBASSERT_USE_ENCHANTUM)
  set(conformance_options "${conformance_options} -DLIBASSERT_PREFIX_ASSERTIONS -DLIBASSERT_SAFE_COMPARISONS -DLIBASSERT_USE_FMT")
endif()

# boost
if(BOOST_INCLUDE_LIBRARIES)
  set(conformance_options "${conformance_options} -DBOOST_FILESYSTEM_NO_LIB -DBOOST_NO_CXX23_HDR_SPANSTREAM -DBOOST_NO_CXX23_HDR_STACKTRACE -DBOOST_NO_CXX23_HDR_STDFLOAT -Denviron=_environ -Dstat=_stat")
  set(warning_options "${warning_options} -Wno-error=microsoft-cpp-macro")
endif()

# boringssl
if(EXISTS "${CMAKE_SOURCE_DIR}/decrepit")
  set(conformance_options "${conformance_options} -DO_BINARY=_O_BINARY -DO_RDONLY=_O_RDONLY -Dfstat=_fstat -Dstat=_stat")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# bzip2
if(ENABLE_LIB_ONLY)
  set(conformance_options "${conformance_options} -DO_BINARY=_O_BINARY -Dfdopen=_fdopen -Dfileno=_fileno -Dsetmode=_setmode")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# cpptrace
if(CPPTRACE_DISABLE_CXX_20_MODULES)
  set(conformance_options "${conformance_options} -Doff_t=_off_t")
endif()

# dwarfs
if(DEFINED WITH_FUSE_DRIVER)
  set(conformance_options "${conformance_options} -DBOOST_NO_CXX23_HDR_SPANSTREAM -DBOOST_NO_CXX23_HDR_STACKTRACE -DBOOST_NO_CXX23_HDR_STDFLOAT -DFLAC__NO_DLL -DLZMA_API_STATIC -DZXC_STATIC_DEFINE -Dclose=_close -Ddev_t=_dev_t -Dgetpid=_getpid -Doff_t=_off_t -Dread=_read -Dumask=_umask")
  set(warning_options "${warning_options} -Wno-c++98-compat")
endif()

# flac
if(DEFINED INSTALL_MANPAGES)
  set(conformance_options "${conformance_options} -DS_IWRITE=_S_IWRITE -Dfileno=_fileno -Dgetenv=getenv -Doff_t=_off_t -Dstrdup=_strdup -Dutimbuf=_utimbuf")
endif()

# freetype
if(FT_ENABLE_ERROR_STRINGS)
  set(conformance_options "${conformance_options} -DFT_CONFIG_OPTION_SUBPIXEL_RENDERING -DHB_DISABLE_DEPRECATED -DPCF_CONFIG_OPTION_LONG_FAMILY_NAMES")
endif()

# graphite
if(EXISTS "${CMAKE_SOURCE_DIR}/Graphite.cmake")
  set(conformance_options "${conformance_options} -DGRAPHITE2_STATIC")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# harfbuzz
if(HB_HAVE_FREETYPE)
  set(conformance_options "${conformance_options} -DGRAPHITE2_STATIC -DHB_DISABLE_DEPRECATED")
  set(warning_options "${warning_options} -Wno-error=microsoft-enum-value -Wno-error=microsoft-exception-spec")
endif()

# hwloc
if(HWLOC_SKIP_LSTOPO)
  set(conformance_options "${conformance_options} -DHWLOC_HAVE_DECL_STRNCASECMP -DHWLOC_HAVE_MSVC_CPUIDEX -DO_RDONLY=_O_RDONLY -DS_IFREG=_S_IFREG -Dfstat=_fstat -Dstat=_stat")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# ktx
if(DEFINED KTX_FEATURE_TESTS)
  set(conformance_options "${conformance_options} -DS_IFCHR=_S_IFCHR -DS_IFMT=_S_IFMT")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# lame
if(EXISTS "${CMAKE_SOURCE_DIR}/lame")
  set(conformance_options "${conformance_options} -Doff_t=_off_t")
  set(warning_options "${warning_options} -Wno-error=incompatible-pointer-types -Wno-error=tautological-pointer-compare")
endif()

# llvm-libc, libc++
if(LLVM_ENABLE_RUNTIMES)
  set(conformance_options "${conformance_options} -DLIBC_TYPES_HAS_INT128 -DO_BINARY=_O_BINARY -DO_CREAT=_O_CREAT -DO_RDONLY=_O_RDONLY -DO_WRONLY=_O_WRONLY -Dalloca=_alloca -Dfdopen=_fdopen -Dfileno=_fileno -Doff_t=_off_t")
  set(warning_options "${warning_options} -Wno-error=microsoft-enum-value")
else()
  set(llvm_options "${llvm_options} /clang:-fopenmp /clang:-fopenmp-extensions")
endif()

# luajit2
if(LUAJIT_DIR)
  set(warning_options "${warning_options} -Wno-error=format -Wno-error=format-pedantic -Wno-error=format-signedness -Wno-error=microsoft-enum-value")
endif()

# minizip
if(DEFINED MZ_FETCH_LIBS)
  set(conformance_options "${conformance_options} -Dstrdup=_strdup")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# mpg123
if(DEFINED BUILD_LIBOUT123)
  set(conformance_options "${conformance_options} -DO_RDONLY=_O_RDONLY -Doff_t=_off_t -Dread=_read -Dwrite=_write")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# openexr
if(OPENEXR_ENABLE_LARGE_STACK)
  set(conformance_options "${conformance_options} -D_LIBCPP_ENABLE_CXX26_REMOVED_CODECVT -D_LIBCPP_ENABLE_CXX26_REMOVED_WSTRING_CONVERT")
  set(warning_options "${warning_options} -Wno-error=format -Wno-error=format-signedness -Wno-error=microsoft-redeclare-static -Wno-error=parentheses")
endif()

# opus
if(OPUS_CUSTOM_MODES)
  set(conformance_options "${conformance_options} -DENABLE_OPUS_CUSTOM_API -DENABLE_QEXT -Dalloca=_alloca")
endif()

# portaudio
if(PA_USE_ASIO)
  set(conformance_options "${conformance_options} -Dalloca=_alloca -UNOMINMAX")
endif()

# sentry
if(SENTRY_TRANSPORT_COMPRESSION)
  set(conformance_options "${conformance_options} -DLK_UNLCK=_LK_UNLCK")
  set(warning_options "${warning_options} -Wno-error=c++-keyword -Wno-error=gnu-include-next -Wno-error=implicit-int-enum-cast -Wno-error=implicit-void-ptr-cast -Wno-error=jump-misses-init -Wno-error=language-extension-token -Wno-error=lifetime-safety-intra-tu-suggestions -Wno-error=microsoft-enum-value -Wno-error=missing-format-attribute -Wno-error=nrvo -Wno-error=padded -Wno-error=pre-c23-compat -Wno-error=used-but-marked-unused")
endif()

# sndfile
if(EXISTS "${CMAKE_SOURCE_DIR}/sndfile.pc.in")
  set(conformance_options "${conformance_options} -DENABLE_OPUS_CUSTOM_API -DENABLE_QEXT -DFLAC__NO_DLL -Daccess=_access -Dclose=_close -Dfstat=_fstat -Dfstat64=_fstat64 -Dlseek=_lseek -Dmemccpy=_memccpy -Doff_t=_off_t -Dopen=_open -Dpipe=_pipe -Dread=_read -Dstat=_stat -Dwrite=_write")
  set(warning_options "${warning_options} -Wno-error=format-signedness")
endif()

# squashfs
if(EXISTS "${CMAKE_SOURCE_DIR}/squashfs-tools-ng")
  set(conformance_options "${conformance_options} -Dstrdup=_strdup")
endif()

# vorbis
if(CMAKE_POLICY_VERSION_MINIMUM)
  set(conformance_options "${conformance_options} -Dalloca=_alloca")
  set(warning_options "${warning_options} -Wno-error=incompatible-pointer-types")
endif()

# zlib
if(ZLIB_COMPAT)
  set(conformance_options "${conformance_options} -DO_APPEND=_O_APPEND -DO_BINARY=_O_BINARY -DO_CREAT=_O_CREAT -DO_RDONLY=_O_RDONLY -DO_TRUNC=_O_TRUNC -DO_WRONLY=_O_WRONLY -Dclose=_close -Dfileno=_fileno -Dopen=_open -Dread=_read -Dsetmode=_setmode -Dunlink=_unlink -Dwrite=_write")
  set(warning_options "${warning_options} -Wno-error=format-signedness -Wno-error=missing-format-attribute")
endif()

# zstd
if(ZSTD_BUILD_TESTS)
  set(conformance_options "${conformance_options} -DS_IFREG=_S_IFREG -Dfileno=_fileno -Dutimbuf=_utimbuf -Dutime=_utime")
  set(warning_options "${warning_options} -Wno-format-signedness")
endif()

set(CMAKE_ASM_FLAGS_RELEASE "${CMAKE_ASM_FLAGS_RELEASE} /DWIN32 /D_WINDOWS /MP /O2 /Ob2 /Oi /Oy /Ot /GL /Gy /Gw /W4 /GF /GS /Zi /GT ${conformance_options} ${llvm_options} ${warning_options} /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_MSVC_STL_HARDENING=1 /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MP /O2 /Ob2 /Oi /Oy /Ot /GL /Gy /Gw /W4 /GF /GS /Zi /GT ${conformance_options} ${llvm_options} $ENV{LIBC_OPTIONS} ${warning_options} /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_MSVC_STL_HARDENING=1 /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MP /O2 /Ob2 /Oi /Oy /Ot /GL /Gy /Gw /W4 /GF /GS /Zi /GT ${conformance_options} ${llvm_options} $ENV{LIBCXX_OPTIONS} ${warning_options} /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_MSVC_STL_HARDENING=1 /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /Brepro /DEBUG /DEBUG:GHASH /RELEASE /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG /opt:lldlto=3 /opt:lldltocgo=3 /opt:lldtailmerge")
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /Brepro /DEBUG /DEBUG:GHASH /RELEASE /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG /opt:lldlto=3 /opt:lldltocgo=3 /opt:lldtailmerge")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /Brepro /DEBUG /DEBUG:GHASH /RELEASE /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG /opt:lldlto=3 /opt:lldltocgo=3 /opt:lldtailmerge")

set(CMAKE_ASM_FLAGS_DEBUG "${CMAKE_ASM_FLAGS_DEBUG} /DWIN32 /D_WINDOWS /MP /Od -fno-builtin /Oy /Ot /GL /Gy /Gw /W4 /GF /GS /Zi /GT ${conformance_options} ${llvm_options} ${warning_options} /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_MSVC_STL_HARDENING=1 /D_ITERATOR_DEBUG_LEVEL=0")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MP /Od -fno-builtin /Oy /Ot /GL /Gy /Gw /W4 /GF /GS /Zi /GT ${conformance_options} ${llvm_options} $ENV{LIBC_OPTIONS} ${warning_options} /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_MSVC_STL_HARDENING=1 /D_ITERATOR_DEBUG_LEVEL=0")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MP /Od -fno-builtin /Oy /Ot /GL /Gy /Gw /W4 /GF /GS /Zi /GT ${conformance_options} ${llvm_options} $ENV{LIBCXX_OPTIONS} ${warning_options} /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_MSVC_STL_HARDENING=1 /D_ITERATOR_DEBUG_LEVEL=0")

set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /Brepro /DEBUG /DEBUG:GHASH /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG")
set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG} /Brepro /DEBUG /DEBUG:GHASH /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /Brepro /DEBUG /DEBUG:GHASH /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG")
