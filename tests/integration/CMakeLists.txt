enable_language(C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RWIN_IT_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(RWIN_IT_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_get_variable(RWIN_IT_SCANNER wayland-scanner wayland_scanner)

set(fake_input_dir "${CMAKE_CURRENT_BINARY_DIR}/protocol")
file(MAKE_DIRECTORY "${fake_input_dir}")
add_custom_command(
    OUTPUT "${fake_input_dir}/fake-input-client-protocol.h" "${fake_input_dir}/fake-input-protocol.c"
    COMMAND "${RWIN_IT_SCANNER}" client-header "${CMAKE_CURRENT_SOURCE_DIR}/fake-input.xml"
            "${fake_input_dir}/fake-input-client-protocol.h"
    COMMAND "${RWIN_IT_SCANNER}" private-code "${CMAKE_CURRENT_SOURCE_DIR}/fake-input.xml"
            "${fake_input_dir}/fake-input-protocol.c"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fake-input.xml"
    VERBATIM)

add_executable(rwin_resize_integration
    resize_integration.cpp
    child_process.cpp
    fake_export_device.cpp
    surface_buffer.cpp
    fake_guest.cpp
    fake_pointer.cpp
    host_report.cpp
    kwin_view.cpp
    screen_capture.cpp
    model_window.cpp
    test_desktop.cpp
    "${fake_input_dir}/fake-input-client-protocol.h"
    "${fake_input_dir}/fake-input-protocol.c")
target_include_directories(rwin_resize_integration PRIVATE "${fake_input_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rwin_resize_integration PRIVATE rwin::host PkgConfig::RWIN_IT_WAYLAND PkgConfig::RWIN_IT_SYSTEMD)
target_compile_definitions(rwin_resize_integration PRIVATE RWIN_HOST_BINARY="$<TARGET_FILE:remotewin>")
add_dependencies(rwin_resize_integration remotewin)

foreach(case maximize-here maximize-in-guest minimize-here minimize-in-guest drag-corner drag-corner-slow-guest
             drag-corner-sluggish-guest drag-corner-in drag-top drag-left resize-by-compositor resize-in-guest
             dialog-in-front menus-in-front winui-popup activation-both-ways map-keeps-guest-active
             taskbar-presence welcome-in-front welcome-in-front-rounded close-confirmation-in-front
             close-confirmation-in-front-rounded)
    add_test(NAME rwin_integration_${case} COMMAND rwin_resize_integration ${case})
    set_tests_properties(rwin_integration_${case} PROPERTIES LABELS integration SKIP_RETURN_CODE 77 TIMEOUT 30)
endforeach()
