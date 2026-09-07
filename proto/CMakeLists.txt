add_library(rwin_proto STATIC
    frame_decoder.cpp
    message_reader.cpp
    message_writer.cpp
)
add_library(rwin::proto ALIAS rwin_proto)

# The include root is the project, so a header says where it lives:
# #include "proto/message_writer.hpp".
target_include_directories(rwin_proto PUBLIC "${PROJECT_SOURCE_DIR}")
target_compile_features(rwin_proto PUBLIC cxx_std_20)

if(RWIN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
