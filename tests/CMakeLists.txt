add_executable(rwin_proto_test proto_test.cpp)
target_link_libraries(rwin_proto_test PRIVATE rwin::proto)
add_test(NAME rwin_proto_test COMMAND rwin_proto_test)

add_executable(rwin_frame_timing_test frame_timing_test.cpp)
target_link_libraries(rwin_frame_timing_test PRIVATE rwin::logging)
add_test(NAME rwin_frame_timing_test COMMAND rwin_frame_timing_test)

add_executable(rwin_probe_wire_test probe_wire_test.cpp)
target_link_libraries(rwin_probe_wire_test PRIVATE rwin::proto)
add_test(NAME rwin_probe_wire_test COMMAND rwin_probe_wire_test)

add_executable(rwin_pointer_grab_rule_test pointer_grab_rule_test.cpp)
target_link_libraries(rwin_pointer_grab_rule_test PRIVATE rwin::proto)
add_test(NAME rwin_pointer_grab_rule_test COMMAND rwin_pointer_grab_rule_test)

# The guest half's logic that needs no Windows, built here.
add_executable(rwin_monochrome_picture_test
    monochrome_picture_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/cursor/monochrome_picture.cpp")
target_include_directories(rwin_monochrome_picture_test PRIVATE "${RWIN_INCLUDE_DIR}")
target_compile_features(rwin_monochrome_picture_test PRIVATE cxx_std_20)
add_test(NAME rwin_monochrome_picture_test COMMAND rwin_monochrome_picture_test)

add_executable(rwin_changed_tiles_test
    changed_tiles_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/capture/changed_tiles.cpp")
target_link_libraries(rwin_changed_tiles_test PRIVATE rwin::proto)
add_test(NAME rwin_changed_tiles_test COMMAND rwin_changed_tiles_test)

add_executable(rwin_picture_room_test
    picture_room_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/capture/picture_room.cpp")
target_link_libraries(rwin_picture_room_test PRIVATE rwin::logging)
target_include_directories(rwin_picture_room_test PRIVATE "${RWIN_INCLUDE_DIR}")
add_test(NAME rwin_picture_room_test COMMAND rwin_picture_room_test)

add_executable(rwin_slot_ledger_test
    slot_ledger_test.cpp
    "${PROJECT_SOURCE_DIR}/source/guest/capture/slot_ledger.cpp")
target_link_libraries(rwin_slot_ledger_test PRIVATE rwin::proto)
add_test(NAME rwin_slot_ledger_test COMMAND rwin_slot_ledger_test)

# The host half: what knows nothing of a window system against the link
# library, what does against the host library.
if(TARGET rwin_link)
    foreach(name export_wire latency mirror_scope picture_limits emulated_cursor unit_padding screen_mapping
                 native_cursor_shape
                 view_mapping)
        add_executable(rwin_${name}_test ${name}_test.cpp)
        target_link_libraries(rwin_${name}_test PRIVATE rwin::link)
        add_test(NAME rwin_${name}_test COMMAND rwin_${name}_test)
    endforeach()
endif()

if(TARGET rwin_host)
    foreach(name win32_style guest_button key_translator popup_layout picture_cutout link_lifecycle state_sync
                 move_requests kwin_tag)
        add_executable(rwin_${name}_test ${name}_test.cpp)
        target_link_libraries(rwin_${name}_test PRIVATE rwin::host)
        add_test(NAME rwin_${name}_test COMMAND rwin_${name}_test)
    endforeach()
endif()

if(TARGET rwin_host)
    add_subdirectory(integration)
endif()
