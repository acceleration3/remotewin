find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus OpenGL OpenGLWidgets GuiPrivate WaylandClientPrivate)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
# EGL, for the frames that stay on the GPU: a dma-buf imported as an image,
# and the fences either side of a draw from it. Qt draws through EGL here.
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_check_modules(WAYLAND_SCANNER_PC REQUIRED wayland-scanner)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_PROTOCOLS_DIR OR NOT WAYLAND_SCANNER)
    message(FATAL_ERROR "wayland-protocols and wayland-scanner are needed for the pointer constraint, "
                        "relative pointer and viewport protocols")
endif()

# The compositor protocols below Qt that a mirror speaks itself: holding
# the pointer (pointer constraints), its unaccelerated motion (relative
# pointer), and the size of its cursor surface (viewporter). Client code is
# generated from the system's protocol files; it is C.
enable_language(C)
set(RWIN_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland")
file(MAKE_DIRECTORY "${RWIN_PROTOCOL_DIR}")
set(RWIN_PROTOCOL_SOURCES)
foreach(protocol
        unstable/pointer-constraints/pointer-constraints-unstable-v1
        unstable/relative-pointer/relative-pointer-unstable-v1
        stable/viewporter/viewporter)
    get_filename_component(name "${protocol}" NAME)
    set(xml "${WAYLAND_PROTOCOLS_DIR}/${protocol}.xml")
    add_custom_command(
        OUTPUT "${RWIN_PROTOCOL_DIR}/${name}-client-protocol.h" "${RWIN_PROTOCOL_DIR}/${name}-protocol.c"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${RWIN_PROTOCOL_DIR}/${name}-client-protocol.h"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${RWIN_PROTOCOL_DIR}/${name}-protocol.c"
        DEPENDS "${xml}"
        VERBATIM)
    list(APPEND RWIN_PROTOCOL_SOURCES
         "${RWIN_PROTOCOL_DIR}/${name}-client-protocol.h" "${RWIN_PROTOCOL_DIR}/${name}-protocol.c")
endforeach()

set(CMAKE_AUTOMOC ON)

# Everything but main, as a library: the remotewin executable is this and
# a command line, and the conformance harness runs the same mirror in its
# own process to look at it from outside.
#
# The headers are listed as sources because AUTOMOC only looks for a
# Q_OBJECT beside the .cpp that includes it, and here they live in a
# tree of their own.
add_library(rwin_host STATIC
    chrome/resize_border.cpp
    chrome/win32_style.cpp
    cursor/cursor_animation.cpp
    cursor/cursor_player.cpp
    cursor/emulated_cursor.cpp
    cursor/native_cursor_shape.cpp
    cursor/pixel_cursor.cpp
    cursor/unit_padding.cpp
    cursor/worn_cursor.cpp
    export/copy_wait.cpp
    export/export_answer.cpp
    export/export_request.cpp
    export/export_socket.cpp
    export/pci_address.cpp
    export/rendering_gpu.cpp
    export/shared_frame_feed.cpp
    export/slot_lease.cpp
    export/unique_fd.cpp
    export/window_ring.cpp
    input/guest_button.cpp
    input/held_keys.cpp
    input/key_translator.cpp
    input/pointer_grabber.cpp
    input/scan_code.cpp
    link/guest_link.cpp
    link/parent_control.cpp
    mirror/draw_picture.cpp
    mirror/gpu_picture.cpp
    mirror/mirror_session.cpp
    mirror/mirror_window.cpp
    mirror/monitor_session.cpp
    mirror/monitor_window.cpp
    mirror/pixel_mapping.cpp
    placement/kwin_placement.cpp
    placement/kwin_script.cpp
    placement/placement_sync.cpp
    wayland/confined_pointer.cpp
    wayland/cursor_surface_viewport.cpp
    wayland/locked_pointer.cpp
    wayland/pointer_globals.cpp
    wayland/relative_pointer.cpp
    ${RWIN_PROTOCOL_SOURCES}
    "${RWIN_INCLUDE_DIR}/host/cursor/cursor_animation.hpp"
    "${RWIN_INCLUDE_DIR}/host/cursor/cursor_player.hpp"
    "${RWIN_INCLUDE_DIR}/host/export/shared_frame_feed.hpp"
    "${RWIN_INCLUDE_DIR}/host/input/pointer_grabber.hpp"
    "${RWIN_INCLUDE_DIR}/host/wayland/relative_pointer.hpp"
    "${RWIN_INCLUDE_DIR}/host/link/guest_link.hpp"
    "${RWIN_INCLUDE_DIR}/host/link/parent_control.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/gpu_picture.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/mirror_session.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/mirror_window.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/monitor_session.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/monitor_window.hpp"
    "${RWIN_INCLUDE_DIR}/host/placement/kwin_placement.hpp"
    "${RWIN_INCLUDE_DIR}/host/placement/placement_sync.hpp"
)
add_library(rwin::host ALIAS rwin_host)
target_link_libraries(rwin_host PUBLIC
    rwin::proto rwin::logging Qt6::Widgets Qt6::Network Qt6::DBus Qt6::OpenGL Qt6::OpenGLWidgets
    PRIVATE Qt6::GuiPrivate Qt6::WaylandClientPrivate PkgConfig::WAYLAND_CLIENT PkgConfig::EGL)
# EGL's platform header would otherwise bring in Xlib, whose macros (Status,
# None, Bool) collide with names here and in Qt.
target_compile_definitions(rwin_host PRIVATE EGL_NO_X11)
target_include_directories(rwin_host PUBLIC "${RWIN_INCLUDE_DIR}" PRIVATE "${RWIN_PROTOCOL_DIR}")

add_executable(remotewin main.cpp)
target_link_libraries(remotewin PRIVATE rwin::host)
