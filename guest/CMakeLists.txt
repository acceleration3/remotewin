add_executable(remotewin-guest
    guest_session.cpp
    input_injector.cpp
    main.cpp
    tcp_client.cpp
    window_capture.cpp
    window_enumerator.cpp
)
target_link_libraries(remotewin-guest PRIVATE rwin::proto rwin::log ws2_32 gdi32 user32)

# Statically linked: this drops onto a stock Windows install with no
# runtime to ship beside it.
target_link_options(remotewin-guest PRIVATE -static -static-libgcc -static-libstdc++)
