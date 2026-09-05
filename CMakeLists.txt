cmake_minimum_required(VERSION 3.20)
project(SpecOpsTheLineHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "SpecOpsTheLineHeadTracking must be built as Win32 (x86). Configure with -A Win32.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

set(MOD_SOURCES
    src/dllmain.cpp
    src/aim_marker.cpp
    src/config.cpp
    src/path_utils.cpp
    src/hotkeys.cpp
    src/tracking_runtime.cpp
    src/build_profile.cpp
    src/aim_trace.cpp
    src/camera_hook.cpp
    src/crosshair_hook.cpp
    src/reticle_overlay.cpp
    src/game_state.cpp
)

add_library(SpecOpsTheLineHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${MOD_SOURCES}
)

set_target_properties(SpecOpsTheLineHeadTracking PROPERTIES
    OUTPUT_NAME "SpecOpsTheLineHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(SpecOpsTheLineHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

target_link_libraries(SpecOpsTheLineHeadTracking PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(SpecOpsTheLineHeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_FLAGS "/W0")
endif()

if(WIN32)
    target_link_libraries(SpecOpsTheLineHeadTracking PRIVATE ws2_32)
endif()

option(SPECOPS_BUILD_TESTS "Build the mod's own unit tests" ON)
if(SPECOPS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
