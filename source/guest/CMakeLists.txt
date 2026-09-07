add_executable(remotewin-guest
    main.cpp
    capture/graphics_device.cpp
    capture/window_capture.cpp
    input/keyboard_injector.cpp
    input/pointer_injector.cpp
    session/guest_session.cpp
    cursor/cursor_image.cpp
    cursor/cursor_watcher.cpp
    session/tcp_client.cpp
    windows/window_enumerator.cpp
    windows/window_focus.cpp
    windows/window_drag_watcher.cpp
    windows/launch_scope.cpp
    windows/window_list_watcher.cpp
    windows/window_resizer.cpp
    windows/window_surface.cpp
)
target_link_libraries(remotewin-guest PRIVATE
    rwin::proto rwin::logging ws2_32 gdi32 user32 dwmapi d3d11 dxgi runtimeobject ole32)

# Statically linked: this drops onto a stock Windows install with no
# runtime to ship beside it.
target_link_options(remotewin-guest PRIVATE -static -static-libgcc -static-libstdc++)
