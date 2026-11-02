add_subdirectory(proto)
add_subdirectory(logging)

if(WIN32)
    add_subdirectory(guest)
    add_subdirectory(probe)
else()
    add_subdirectory(host)
endif()
