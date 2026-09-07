find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)

# The headers are listed as sources because AUTOMOC only looks for a
# Q_OBJECT beside the .cpp that includes it, and here they live in a
# tree of their own.
add_executable(remotewin
    main.cpp
    chrome/non_client_area.cpp
    chrome/window_style.cpp
    cursor/emulated_cursor.cpp
    link/guest_link.cpp
    mirror/mirror_session.cpp
    mirror/mirror_window.cpp
    "${RWIN_INCLUDE_DIR}/host/link/guest_link.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/mirror_session.hpp"
    "${RWIN_INCLUDE_DIR}/host/mirror/mirror_window.hpp"
)
target_link_libraries(remotewin PRIVATE
    rwin::proto rwin::logging Qt6::Widgets Qt6::Network)
target_include_directories(remotewin PRIVATE "${RWIN_INCLUDE_DIR}")
