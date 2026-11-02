# remotewin

Mirror a guest Windows application's windows on a Linux host.

One process per guest application: `remotewin` opens a real host
window for every window that application has, forwards input back, and
exits when the application does. The windows are the host's own — they
group, alt-tab, minimize and get an icon like anything else on the
desktop, because to the compositor they *are* an application's windows.

This is the window half of running Windows software on Linux. LSW starts
the two ends and supplies accel-virt's export socket when GPU-backed
frames are available; RemoteWin itself does not manage virtual machines.

## Shape

    remotewin-guest.exe   in Windows: capture, enumeration, input and cursors
    remotewin            on Linux: one application's windows, popups and
                         desktop identity, or the whole guest monitor

`remotewin` is one process per *application*, not per window. That is
not an optimisation — a popup menu must be an `xdg_popup` on its owner's
surface, and xdg-shell has no cross-process form, so a window and the
menus it opens have to live in the same process to be positioned at all.

## Transports

Control and fallback pixels travel over TCP. With accel-virt, window
frames can instead stay on the GPU through shared surfaces exported as
dma-bufs (below). Changed-tile patches reduce traffic on the pixel path.

## Launcher integration

`remotewin --managed --port 0` binds an OS-selected IPv4 loopback port and
reports `LISTENING <port>\n` on stdout after it is listening. Only then
should the launcher start `remotewin-guest.exe --host <host> --port <port>`.
`READY\n` reports the first application/monitor frame; process creation
alone is not successful mirroring. Diagnostics stay on stderr.

The launcher can write `RAISE\n` to stdin to restore and focus an existing
mirror, including its guest window. Closing stdin detaches the mirror
without closing the Windows application. Clicking a mirrored window's
close button still asks the application to close normally. LSW shares its
running-app registry between its grid and desktop launchers, coalesces
pending opens, and reports startup failures/timeouts in the UI.

Without `--managed`, the listener remains on all IPv4 interfaces for
standalone use. The transport is not authenticated: use only on trusted
networks, or protect it with a tunnel/firewall. Exit status 3 means a wire
version mismatch; 4 means the guest connection was lost.

## The whole desktop

`remotewin --desktop --title <title>` mirrors the guest's primary monitor
as one window instead of an application's windows (`WatchMonitor` on the
wire). The guest half captures the monitor with Windows.Graphics.Capture,
the same frame path a window takes, cursor left out of the picture; the
host draws it one guest pixel to one device pixel when the window has room
and scaled down, letterboxed, when it has not, and wears the guest's cursor
as the window's own. After the first frame only the 64x64 tiles that
changed are sent (`FramePatch`), unless more than half of the screen did:
QEMU's user-mode network carries a little over 100 MB a second, which is
eight whole 2560x1440 frames. Pointer positions are the monitor's pixels and keys go
to whatever the guest has in front. Nothing holds or confines the pointer
in this mode.

## Frames on the GPU

Under accel-virt's GPU (virtio-gpu-av), a window's frames need not cross
as pixels at all. `remotewin --export-socket <path>` (or
`$RWIN_EXPORT_SOCKET`) connects to the device's export socket;
when it answers and its GPU is
the one the compositor composes on (the main device of its
`zwp_linux_dmabuf_v1` feedback, by PCI address), the guest is told so at
its hello (`FrameRoute`). Then, per window and
for the desktop:

  * the guest half keeps a ring of three shared B8G8R8A8 textures with
    room for the picture (`SurfaceRing`; the picture rounded up to 128
    pixels and 128 more, so a resize fills the same ring for a while,
    `picture_room.hpp`), and reads the id the GPU driver gave each from
    the texture's allocation data through D3DKMT (`SurfaceIdReader`) -
    announced as `SlotRing`;
  * each frame is one `CopySubresourceRegion` on the guest's GPU to the
    top left of a slot the host is not reading, flushed, and named by its
    copy count and the picture's size (`FrameInSlot`);
  * the host half exports each surface once, asks the device until its
    count of copies reaches the frame's, and attaches the dma-buf it was
    given to the window's surface as a `wl_buffer`, with the copy's
    sync_file as the acquire point of a `wp_linux_drm_syncobj_v1`
    timeline (or, where the compositor has no explicit sync, once the
    sync_file signals);
  * when the compositor is done with it (the release point, or
    `wl_buffer.release`) the slot goes back to the guest (`SlotFree`).

Nothing of a frame is copied by either CPU, and no GL context exists on
the host: the compositor composes the guest's buffer, or scans it out.
The picture lands where the pixel path's `wl_shm` buffer does: one picture
pixel to one device pixel, through `wp_viewporter`, at the scale
`wp_fractional_scale_v1` gives. A layered window's alpha is applied by
the compositor (`wp_alpha_modifier_v1`). A window region (WPF's rounded
`WindowChrome`, `SetWindowRgn`) and a colour key (`LWA_COLORKEY`) need no
cutting: Windows' capture hands over what lies outside the region, and
the keyed texels, as clear (premultiplied 0), the ring's textures and the
dma-bufs are ARGB8888, so such a window stays on the GPU and its corners
show the desktop. A region of a window that is not layered is its
surface's opaque region, so the compositor blends only the corners.
Windows that leave a margin round themselves (menus, tooltips) still go
as pixels: what they draw is found by looking at their pixels.

A failure of the connection to the GPU device - no socket, another GPU,
the connection lost - sends the guest back to pixels for every window,
with one line saying why. One window the host cannot draw this way (an
import EGL refuses, a surface the device will not export) goes to pixels
alone (`WindowPixels`), and a reset GPU device has every ring made afresh.

## Both halves from one build

Each end's first message is a `Hello` (`proto/hello.hpp`) naming its build
and the features it speaks; a feature is active at the lower version when
both list it, and every message belongs to one. A peer that does not speak `windows` is refused: the host
logs what it lacks and exits with status 3, a guest half logs it and ends.

## Building

    cmake -S . -B build -G Ninja
    cmake --build build

## Tests

    ctest --test-dir build -LE integration     # the unit tests, instant
    ctest --test-dir build -L integration      # the integration tests

The integration tests (`tests/integration/`) resize, maximise and
minimise a mirror end to end with no VM: each case starts a headless KWin
of its own (`kwin_wayland --virtual`, 1600x1000 at scale 1.2, its own
config directory) inside a private `dbus-run-session`, runs the
`remotewin` it was built with as a client of it, and plays the guest half
- a wire 6 peer over the host's TCP port with one window that answers
moves and system commands as Windows does, after a delay, with the
window's new fields and a frame of its new size by the pixel path. The
pointer is KWin's fake input; KWin's view of the window comes from a
watching KWin script, the host's from its `Describe`. Cases ending in
`-rounded` also play the GPU device's export socket, so the guest's
pictures of its other windows go on the GPU as the real guest's do: in
dma-bufs KWin shows (udmabufs of the harness's memory, where
`/dev/udmabuf` opens), clear outside their region as Windows' capture
hands them over, and read back off KWin's screen (`ScreenShot2`) to see
the corners show what is under them. Each
case takes about a second; all of them about 25 s one after another (`-j`
runs them side by side). They are skipped (exit 77) where `kwin_wayland` or
`dbus-run-session` is not installed. One case alone, with its verdicts
and timings:

    build/tests/integration/rwin_resize_integration                 # the cases
    build/tests/integration/rwin_resize_integration drag-corner

`RWIN_INTEGRATION_HOST=<path>` runs another `remotewin` against them (an
older build, to see a case fail), `RWIN_INTEGRATION_HOST_LOG=trace` sets
its log level, and `RWIN_INTEGRATION_WAYLAND_DEBUG=1` adds its Wayland
traffic to its log. A failing case keeps its KWin and host logs and says
where.

The guest half cross-builds from Linux with mingw-w64, the same way the
host half builds natively.

The host half needs a C and a C++20 compiler, pkg-config, and
`wayland-client`, `wayland-scanner`, `wayland-protocols` (1.38 or newer),
`xkbcommon`, `libdrm` (2.4.116 or newer) and `libsystemd` (sd-bus). It
links no Qt.
