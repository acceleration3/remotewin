find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test GuiPrivate)

set(CMAKE_AUTOMOC ON)

# The conformance harness: runs the mirror in its own process against
# windows the probe (source/probe) makes in the guest, and reports where
# the two disagree. A library and a main, so the parts that are logic -
# the expectation table, the pixel comparison, the input expectation, the
# scenario file - are tested on their own.
#
# Out of the default build: it is a tool for whoever works on the mirror,
# and a release that ships remotewin has no use for it. Built by name
# (remotewin-conform), or as what the tests need.
add_library(rwin_conform STATIC EXCLUDE_FROM_ALL
    cursor_picture_on_glass.cpp
    expectation.cpp
    guest_agent.cpp
    host_scene.cpp
    input_expectation.cpp
    motion_expectation.cpp
    pixel_comparison.cpp
    pointer_expectation.cpp
    probe_link.cpp
    run_report.cpp
    scenario_book.cpp
    scenario_matrix.cpp
    scenario_outcome.cpp
    scenario_runner.cpp
    "${RWIN_INCLUDE_DIR}/conform/probe_link.hpp"
)
target_link_libraries(rwin_conform PUBLIC rwin::host Qt6::Test PRIVATE Qt6::GuiPrivate)

add_executable(remotewin-conform EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(remotewin-conform PRIVATE rwin_conform)
target_compile_definitions(remotewin-conform PRIVATE
    RWIN_CONFORM_SCENARIOS="${PROJECT_SOURCE_DIR}/tests/conformance/scenarios.json")
