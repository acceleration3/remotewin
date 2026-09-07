# remotewin

Mirror a guest Windows application's windows on a Linux host.

One process per guest application: `remotewin-app` opens a real host
window for every window that application has, forwards input back, and
exits when the application does. The windows are the host's own — they
group, alt-tab, minimize and get an icon like anything else on the
desktop, because to the compositor they *are* an application's windows.

This is the window half of running Windows software on Linux. The
graphics half — Vulkan, OpenGL and OpenCL across the same boundary — is
[render-server](https://github.com/acceleration3/render-server), and the
two know nothing about each other.

## Shape

    remotewin-guest.exe   in the VM: capture, enumeration, input, icons, UIA
    remotewin-host        one per VM: owns the transport and the MCP surface,
                          spawns and reaps the per-application processes
    remotewin-app         one per launched guest application: its windows,
                          its popups, its taskbar identity

`remotewin-app` is one process per *application*, not per window. That is
not an optimisation — a popup menu must be an `xdg_popup` on its owner's
surface, and xdg-shell has no cross-process form, so a window and the
menus it opens have to live in the same process to be positioned at all.

## Transports

The wire is the same either way; only the carrier differs.

  * **shared memory** — an ivshmem region: a frame ring the guest writes
    pixels into directly, plus duplex message rings. No copy between the
    guest's captured frame and the host's window.
  * **TCP** — a socket. Control works today; pixels over TCP need
    encoding that shared memory does not, and that is not written yet.

## Status

Early. The protocol layer is here; the guest and host halves are being
moved in from the project this was extracted out of.

## Building

    cmake -S . -B build -G Ninja
    cmake --build build

The guest half cross-builds from Linux with mingw-w64, the same way the
host half builds natively.
