add_subdirectory(proto)
add_subdirectory(logging)

# Each half is built by the toolchain that can: a native build makes the
# host, a mingw cross build makes the guest, and neither pretends it can
# make the other.
if(WIN32)
    add_subdirectory(guest)
else()
    add_subdirectory(host)
endif()
