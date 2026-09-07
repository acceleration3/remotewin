add_library(rwin_logging STATIC logger.cpp)
add_library(rwin::logging ALIAS rwin_logging)
target_include_directories(rwin_logging PUBLIC "${RWIN_INCLUDE_DIR}")
target_compile_features(rwin_logging PUBLIC cxx_std_20)
