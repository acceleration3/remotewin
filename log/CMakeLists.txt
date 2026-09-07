add_library(rwin_log STATIC logger.cpp)
add_library(rwin::log ALIAS rwin_log)
target_include_directories(rwin_log PUBLIC "${PROJECT_SOURCE_DIR}")
target_compile_features(rwin_log PUBLIC cxx_std_20)
