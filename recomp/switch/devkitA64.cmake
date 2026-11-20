set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(DEVKITPRO "${DEVKITPRO}" CACHE PATH "devkitPro prefix")
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DEVKITPRO)
set(REXGLUE_PLATFORM_SWITCH ON CACHE BOOL "Use the Horizon runtime")
set(REXGLUE_SWITCH_DEKO3D ON CACHE BOOL "Use deko3d without a Vulkan driver")
set(REXGLUE_USE_VULKAN OFF CACHE BOOL "")
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "")
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "")
set(REXGLUE_ENABLE_PERF_COUNTERS OFF CACHE BOOL "")

set(prefix "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-")
if(CMAKE_HOST_WIN32)
    set(suffix ".exe")
endif()
set(CMAKE_C_COMPILER "${prefix}gcc${suffix}")
set(CMAKE_CXX_COMPILER "${prefix}g++${suffix}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_STRIP "${prefix}strip${suffix}")

set(arch_flags "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -fPIE -D__SWITCH__ -DNX -D_GNU_SOURCE -DSPDLOG_NO_TZ_OFFSET")
set(CMAKE_C_FLAGS_INIT "${arch_flags} -ffunction-sections -fdata-sections -Wno-psabi")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_ASM_FLAGS_INIT "${arch_flags}")

# The linker specs normally read DEVKITPRO from the environment. Resolve the
# path here so the explicit --devkitpro argument is sufficient on either host.
file(READ "${DEVKITPRO}/libnx/switch.specs" specs)
string(REPLACE "%:getenv(DEVKITPRO /libnx/switch.ld)" "${DEVKITPRO}/libnx/switch.ld" specs "${specs}")
file(WRITE "${CMAKE_BINARY_DIR}/switch.specs" "${specs}")
# The Windows desktop environment can export -fuse-ld=lld for Clang.
# devkitA64 must link with its matching GNU binutils and the libnx specs.
unset(ENV{LDFLAGS})
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=\"${CMAKE_BINARY_DIR}/switch.specs\" -Wl,--gc-sections,--no-relax")

include_directories(SYSTEM
    "${DEVKITPRO}/libnx/include"
    "${DEVKITPRO}/portlibs/switch/include")
link_directories("${DEVKITPRO}/libnx/lib" "${DEVKITPRO}/portlibs/switch/lib")
