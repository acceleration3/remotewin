add_executable(remotewin-guest
    main.cpp
    capture/capture_clock.cpp
    capture/capture_stream.cpp
    capture/changed_tiles.cpp
    capture/content_bounds.cpp
    capture/frame_signal.cpp
    capture/graphics_device.cpp
    capture/monitor_capture.cpp
    capture/picture_edges.cpp
    capture/slot_ledger.cpp
    capture/surface_id_reader.cpp
    capture/surface_ring.cpp
    capture/window_capture.cpp
    cursor/cursor_decoder.cpp
    cursor/cursor_image.cpp
    cursor/cursor_watcher.cpp
    cursor/monochrome_picture.cpp
    cursor/sent_cursors.cpp
    input/key_input.cpp
    input/pointer_grab_watcher.cpp
    input/input_target.cpp
    input/pointer_input.cpp
    session/guest_session.cpp
    session/multimedia_thread.cpp
    session/outbox.cpp
    session/tcp_client.cpp
    windows/app_id_resolver.cpp
    windows/desktop_settings.cpp
    windows/marker_window.cpp
    windows/min_max_reader.cpp
    windows/move_size_report.cpp
    windows/shell_views.cpp
    windows/wide_text.cpp
    windows/window_commands.cpp
    windows/window_directory.cpp
    windows/window_reader.cpp
    windows/window_surface.cpp
    windows/popup_intent.cpp
    windows/window_icons.cpp
    windows/window_tracker.cpp
    windows/window_watch.cpp
    windows/z_order_report.cpp
)
target_link_libraries(remotewin-guest PRIVATE
    rwin::proto rwin::logging ws2_32 gdi32 user32 dwmapi d3d11 dxgi runtimeobject ole32 avrt advapi32 shlwapi)

# Statically linked: this drops onto a stock Windows install with no
# runtime to ship beside it.
target_link_options(remotewin-guest PRIVATE -static -static-libgcc -static-libstdc++)

# How steadily WGC hands over the monitor (bench/wgc_bench.cpp), through the
# same capture the mirror runs. Built on request, not with the guest half.
add_executable(remotewin-wgcbench EXCLUDE_FROM_ALL
    bench/wgc_bench.cpp
    capture/capture_clock.cpp
    capture/capture_stream.cpp
    capture/frame_signal.cpp
    capture/graphics_device.cpp
)
target_link_libraries(remotewin-wgcbench PRIVATE
    rwin::proto rwin::logging user32 gdi32 dwmapi d3d11 dxgi runtimeobject ole32)
target_link_options(remotewin-wgcbench PRIVATE -static -static-libgcc -static-libstdc++)
