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
