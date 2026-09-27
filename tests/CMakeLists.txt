cmake_minimum_required(VERSION 3.20)

# Behaviour-locking tests for the parts of the mod that are pure logic: the UE3
# rotation maths the camera injection and the reticle projection share, the aim
# projection, and the INI boundary checks. The hooks themselves need a running game
# and are covered by the in-game checklist, not here.
add_executable(SpecOpsTheLineHeadTracking_tests
    test_main.cpp
    rotation_tests.cpp
    projection_tests.cpp
    config_sanitize_tests.cpp
    config_load_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/aim_marker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_config.cpp
)

target_include_directories(SpecOpsTheLineHeadTracking_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(SpecOpsTheLineHeadTracking_tests PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(SpecOpsTheLineHeadTracking_tests PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME SpecOpsTheLineHeadTracking_tests
         COMMAND SpecOpsTheLineHeadTracking_tests)

# The config differential test. The oracle is the newest published build's reader and hotkey
# registration with the core sources they compiled, copied verbatim into
# config_differential/oracle. It is two libraries because Hotkeys::Start has to run on
# oracle_fake's recording poller; each renames the two namespaces its own way, so neither
# collides with the other or with the current core.
set(SPECOPS_DIFF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config_differential)
set(SPECOPS_ORACLE_DIR ${SPECOPS_DIFF_DIR}/oracle)
add_library(specops_config_oracle STATIC
    ${SPECOPS_ORACLE_DIR}/src/config.cpp
    ${SPECOPS_ORACLE_DIR}/core/src/config/ini_reader.cpp
    ${SPECOPS_ORACLE_DIR}/core/src/logging/file_log.cpp
    ${SPECOPS_DIFF_DIR}/oracle_config_adapter.cpp
)
target_include_directories(specops_config_oracle PRIVATE
    ${SPECOPS_ORACLE_DIR}/src
    ${SPECOPS_ORACLE_DIR}/core/include
    ${SPECOPS_DIFF_DIR})
target_compile_definitions(specops_config_oracle PRIVATE
    cameraunlock=specops_oracle_core
    SpecOpsTheLineHeadTracking=specops_oracle)

add_library(specops_hotkeys_oracle STATIC
    ${SPECOPS_ORACLE_DIR}/src/hotkeys.cpp
    ${SPECOPS_ORACLE_DIR}/core/src/logging/file_log.cpp
    ${SPECOPS_DIFF_DIR}/oracle_hotkeys_adapter.cpp
)
target_include_directories(specops_hotkeys_oracle PRIVATE
    ${SPECOPS_DIFF_DIR}/oracle_fake
    ${SPECOPS_ORACLE_DIR}/src
    ${SPECOPS_ORACLE_DIR}/core/include
    ${SPECOPS_DIFF_DIR})
target_compile_definitions(specops_hotkeys_oracle PRIVATE
    cameraunlock=specops_oracle_hk_core
    SpecOpsTheLineHeadTracking=specops_oracle_hk)

add_executable(config_differential_tests
    ${SPECOPS_DIFF_DIR}/differential_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_config.cpp
)
target_include_directories(config_differential_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${SPECOPS_DIFF_DIR})
target_compile_definitions(config_differential_tests PRIVATE
    SPECOPS_DIFFERENTIAL_DATA="${SPECOPS_DIFF_DIR}/data")
target_link_libraries(config_differential_tests PRIVATE
    cameraunlock specops_config_oracle specops_hotkeys_oracle)
if(MSVC)
    target_compile_options(config_differential_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME config_differential_tests COMMAND config_differential_tests)
