find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)

add_executable(remotewin
    guest_link.cpp
    main.cpp
    mirror_session.cpp
    mirror_window.cpp
    window_style.cpp
)
target_link_libraries(remotewin PRIVATE rwin::proto rwin::log Qt6::Widgets Qt6::Network)
