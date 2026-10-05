add_library(rwin_proto STATIC
    cursor_picture.cpp
    frame_decoder.cpp
    frame_in_slot.cpp
    frame_route.cpp
    guest_cursor.cpp
    guest_desktop.cpp
    key_stroke.cpp
    message_reader.cpp
    message_writer.cpp
    picture_rect.cpp
    pointer_delta.cpp
    pointer_wheel.cpp
    pointer_grab.cpp
    pointer_grab_rule.cpp
    slot_free.cpp
    slot_ring.cpp
    system_cursor.cpp
    wire_hello.cpp
    probe/cursor_truth.cpp
    probe/input_record.cpp
    probe/probe_action.cpp
    probe/probe_hello.cpp
    probe/screen_pixels.cpp
    probe/window_spec.cpp
    probe/window_truth.cpp
)
add_library(rwin::proto ALIAS rwin_proto)
target_include_directories(rwin_proto PUBLIC "${RWIN_INCLUDE_DIR}")
target_compile_features(rwin_proto PUBLIC cxx_std_20)
