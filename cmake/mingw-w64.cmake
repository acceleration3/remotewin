set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_rwin_prefix x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${_rwin_prefix}-gcc)
set(CMAKE_CXX_COMPILER ${_rwin_prefix}-g++)
set(CMAKE_RC_COMPILER  ${_rwin_prefix}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${_rwin_prefix})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
