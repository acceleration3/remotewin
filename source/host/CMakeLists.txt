find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
# DRM syncobj timelines for explicit sync (drmSyncobjEventfd, libdrm 2.4.116),
# and the compositor's main device named as a PCI address.
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm>=2.4.116)
# sd-bus: KWin's scripting, for placement, and the window report.
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.38)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_check_modules(WAYLAND_SCANNER_PC REQUIRED wayland-scanner)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_PROTOCOLS_DIR OR NOT WAYLAND_SCANNER)
    message(FATAL_ERROR "wayland-protocols and wayland-scanner are needed for the protocols remotewin speaks")
endif()

# Every protocol a mirror speaks, client code generated from the system's
# protocol files; it is C. cursor-shape names the tablet tool, so the
# tablet protocol comes too.
enable_language(C)
set(RWIN_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland")
file(MAKE_DIRECTORY "${RWIN_PROTOCOL_DIR}")
set(RWIN_PROTOCOL_SOURCES)
foreach(protocol
        stable/xdg-shell/xdg-shell
        stable/viewporter/viewporter
        stable/presentation-time/presentation-time
        stable/linux-dmabuf/linux-dmabuf-v1
        stable/tablet/tablet-v2
        staging/fractional-scale/fractional-scale-v1
        staging/linux-drm-syncobj/linux-drm-syncobj-v1
        staging/cursor-shape/cursor-shape-v1
        staging/xdg-activation/xdg-activation-v1
        staging/xdg-toplevel-icon/xdg-toplevel-icon-v1
        staging/xdg-dialog/xdg-dialog-v1
        staging/alpha-modifier/alpha-modifier-v1
        unstable/pointer-constraints/pointer-constraints-unstable-v1
        unstable/relative-pointer/relative-pointer-unstable-v1
        unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1
        unstable/xdg-decoration/xdg-decoration-unstable-v1)
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

# The guest link, its frames and its cursor, apart from any window system:
# what a program with a loop of its own embeds to show the guest - LSW's
# manager, for its desktop preview - with that loop under it (EventLoop).
add_library(rwin_link STATIC
    cursor/cursor_animation.cpp
    cursor/cursor_player.cpp
    cursor/emulated_cursor.cpp
    cursor/native_cursor_shape.cpp
    cursor/unit_padding.cpp
    export/copy_wait.cpp
    export/export_answer.cpp
    export/export_hello.cpp
    export/export_request.cpp
    export/export_socket.cpp
    export/pci_address.cpp
    export/shared_frame_feed.cpp
    export/slot_lease.cpp
    export/unique_fd.cpp
    export/window_ring.cpp
    geometry/view_mapping.cpp
    latency/cadence_meter.cpp
    latency/clock_offset.cpp
    latency/display_lock.cpp
    latency/frame_stamps.cpp
    latency/guest_clock.cpp
    latency/latency_log.cpp
    latency/latency_meter.cpp
    link/guest_link.cpp
    link/guest_window.cpp
    link/guest_windows.cpp
    loop/deferred.cpp
    mirror/mirror_scope.cpp
)
add_library(rwin::link ALIAS rwin_link)
target_link_libraries(rwin_link PUBLIC rwin::proto rwin::logging)
target_include_directories(rwin_link PUBLIC "${RWIN_INCLUDE_DIR}")

# The mirrors: a Wayland client of its own, every mirror a surface in the
# role its window has, the guest's buffers shown as they are.
add_library(rwin_host STATIC
    chrome/resize_border.cpp
    chrome/win32_style.cpp
    dbus/bus_connection.cpp
    input/guest_button.cpp
    input/held_keys.cpp
    input/key_translator.cpp
    input/pointer_grabber.cpp
    input/scan_code.cpp
    link/parent_control.cpp
    loop/poll_loop.cpp
    mirror/activation_sync.cpp
    mirror/json_text.cpp
    mirror/mirror_role.cpp
    mirror/mirror_session.cpp
    mirror/monitor_session.cpp
    mirror/popup_layout.cpp
    mirror/popup_stack.cpp
    mirror/size_requests.cpp
    mirror/window_mirror.cpp
    mirror/window_report.cpp
    placement/kwin_placement.cpp
    placement/kwin_script.cpp
    placement/placement_sync.cpp
    wayland/activation.cpp
    wayland/backdrop.cpp
    wayland/compositor_gpu.cpp
    wayland/confined_pointer.cpp
    wayland/dmabuf_buffers.cpp
    wayland/drm_sync_device.cpp
    wayland/globals.cpp
    wayland/keyboard_events.cpp
    wayland/locked_pointer.cpp
    wayland/mirror_surface.cpp
    wayland/picture_cutout.cpp
    wayland/pointer_cursor.cpp
    wayland/pointer_events.cpp
    wayland/popup_role.cpp
    wayland/presentation_times.cpp
    wayland/relative_pointer.cpp
    wayland/seat.cpp
    wayland/shm_buffer.cpp
    wayland/shm_buffers.cpp
    wayland/shortcuts_inhibitor.cpp
    wayland/subsurface_role.cpp
    wayland/sync_timeline.cpp
    wayland/toplevel_icon.cpp
    wayland/toplevel_role.cpp
    wayland/wayland_display.cpp
    ${RWIN_PROTOCOL_SOURCES}
)
add_library(rwin::host ALIAS rwin_host)
target_link_libraries(rwin_host
    PUBLIC rwin::link
    PRIVATE PkgConfig::WAYLAND_CLIENT PkgConfig::XKBCOMMON PkgConfig::LIBDRM PkgConfig::LIBSYSTEMD)
target_include_directories(rwin_host PUBLIC "${RWIN_INCLUDE_DIR}" PRIVATE "${RWIN_PROTOCOL_DIR}")

add_executable(remotewin main.cpp)
target_link_libraries(remotewin PRIVATE rwin::host PkgConfig::WAYLAND_CLIENT)
