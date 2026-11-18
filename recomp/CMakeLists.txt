cmake_minimum_required(VERSION 3.25)
project(wfc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)
add_executable(wfc WIN32 src/main.cpp)
rexglue_setup_target(wfc GPU_PLUGINS xenos)

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND DEFINED ENV{WFC_WINDOWS_DEBUG_RUNTIME_DIR})
    set(debug_runtime_dir "$ENV{WFC_WINDOWS_DEBUG_RUNTIME_DIR}")
    set(debug_runtime_dlls
        "${debug_runtime_dir}/msvcp140d.dll"
        "${debug_runtime_dir}/msvcp140d_atomic_wait.dll"
        "${debug_runtime_dir}/vcruntime140d.dll"
        "${debug_runtime_dir}/vcruntime140_1d.dll"
        "${debug_runtime_dir}/ucrtbased.dll"
    )
    foreach(dll IN LISTS debug_runtime_dlls)
        if(NOT EXISTS "${dll}")
            message(FATAL_ERROR "Missing Windows debug runtime DLL: ${dll}")
        endif()
    endforeach()
    add_custom_command(TARGET wfc POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${debug_runtime_dlls} "$<TARGET_FILE_DIR:wfc>"
        VERBATIM
    )
endif()
