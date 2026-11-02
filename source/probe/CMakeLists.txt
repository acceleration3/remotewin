add_executable(remotewin-probe EXCLUDE_FROM_ALL
    main.cpp
    ani_file.cpp
    backdrop_process.cpp
    backdrop_window.cpp
    capture_screen.cpp
    command_reader.cpp
    cursor_reader.cpp
    dialog_boxes.cpp
    input_journal.cpp
    pointer_actions.cpp
    probe_cursors.cpp
    probe_session.cpp
    probe_window.cpp
    probe_windows.cpp
    test_pattern.cpp
    window_actions.cpp
    window_reading.cpp
    ../guest/session/tcp_client.cpp
)
target_link_libraries(remotewin-probe PRIVATE
    rwin::proto rwin::logging ws2_32 gdi32 user32 dwmapi)
target_link_options(remotewin-probe PRIVATE -static -static-libgcc -static-libstdc++)
