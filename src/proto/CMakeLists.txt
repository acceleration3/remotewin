# Header-only on purpose.
#
# Both ends compile this: the host natively, the guest through
# mingw-w64. Anything that needs a library — a logger, a JSON parser, an
# allocator — would have to be available to both, so nothing here needs
# one. The layout is arithmetic and the framing is bytes.
add_library(rwin_proto INTERFACE)
add_library(rwin::proto ALIAS rwin_proto)

target_include_directories(rwin_proto INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_features(rwin_proto INTERFACE cxx_std_20)

if(RWIN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
