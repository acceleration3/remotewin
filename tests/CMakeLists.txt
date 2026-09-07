add_executable(rwin_proto_test proto_test.cpp)
target_link_libraries(rwin_proto_test PRIVATE rwin::proto)
add_test(NAME rwin_proto_test COMMAND rwin_proto_test)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(rwin_win32_style_test
    win32_style_test.cpp
    "${CMAKE_SOURCE_DIR}/source/host/chrome/win32_style.cpp")
target_link_libraries(rwin_win32_style_test PRIVATE Qt6::Core)
target_include_directories(rwin_win32_style_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_win32_style_test COMMAND rwin_win32_style_test)
