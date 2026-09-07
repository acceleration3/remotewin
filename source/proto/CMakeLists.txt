add_library(rwin_proto STATIC
    frame_decoder.cpp
    message_reader.cpp
    message_writer.cpp
)
add_library(rwin::proto ALIAS rwin_proto)
target_include_directories(rwin_proto PUBLIC "${RWIN_INCLUDE_DIR}")
target_compile_features(rwin_proto PUBLIC cxx_std_20)
