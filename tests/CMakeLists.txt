add_executable(rwin_proto_test proto_test.cpp)
target_link_libraries(rwin_proto_test PRIVATE rwin::proto)
add_test(NAME rwin_proto_test COMMAND rwin_proto_test)
