add_subdirectory(proto)
add_subdirectory(logging)

# Each half is built by the toolchain that can: a native build makes the
# host, a mingw cross build makes the guest, and neither pretends it can
# make the other.
if(WIN32)
    add_subdirectory(guest)
    add_subdirectory(probe)
else()
    # The host reaches below Qt for what Qt has no public API for: a
    # window's wl_surface (QPA's native interface) and, for pixel cursors at
    # a fractional scale, Qt's own cursor surface (QtWaylandClient's private
    # classes, used only on the Qt it was built against). Known, so not
    # warned about at every configure.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    add_subdirectory(host)
    add_subdirectory(conform)
endif()
