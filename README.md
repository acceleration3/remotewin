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
`$RWIN_EXPORT_SOCKET`) connects to the device's export socket
(accel-virt's `docs/external-export.md`); when it answers and its GPU is
the one Qt draws on here (the EGL display's DRM device, by PCI address),
the guest is told so at its hello (`FrameRoute`). Then, per window and
for the desktop:

  * the guest half keeps a ring of three shared B8G8R8A8 textures the
    picture's size (`SurfaceRing`), made again on a resize, and reads
    the id the GPU driver gave each from the texture's allocation data
    through D3DKMT (`SurfaceIdReader`) - announced as `SlotRing`;
  * each frame is one `CopySubresourceRegion` on the guest's GPU into a
    slot the host is not reading, flushed, and named by its copy count
    (`FrameInSlot`);
  * the host half exports each surface once, asks the device until its
    count of copies reaches the frame's, and draws the dma-buf it was
    given as an EGL image (`GpuPicture`, a `QOpenGLWidget` over the
    mirror) after waiting on the copy's sync_file on its own GPU queue;
  * once a later frame is drawn and a fence after that draw signals,
    the slot goes back to the guest (`SlotFree`).

Nothing of a frame is copied by either CPU. The picture lands exactly
where the pixel path's QPainter puts one: the same device pixels at a fit
of 1, resampled bilinearly when fitted, a colour key matched against
each texel before it is resampled, a region kept to its rectangles, a
layered window's alpha applied. Windows that leave a margin round
themselves (menus, tooltips) still go as pixels: what they draw is found
by looking at their pixels.

A failure of the connection to the GPU device - no socket, another GPU,
the connection lost - sends the guest back to pixels for every window,
with one line saying why. One window the host cannot draw this way (an
import EGL refuses, a surface the device will not export) goes to pixels
alone (`WindowPixels`), and a reset GPU device has every ring made afresh.
The conformance harness never opens the socket, and judges the pixel path.

## Both halves from one build

Each end's first message is a `Hello` (`proto/hello.hpp`) naming its build
and the features it speaks; a feature is active at the lower version when
both list it, and every message belongs to one (docs/rail-model.md §4,
§7). A peer that does not speak `windows` is refused: the host logs what
it lacks and exits with status 3, a guest half logs it and ends.

## Building

    cmake -S . -B build -G Ninja
    cmake --build build

The guest half cross-builds from Linux with mingw-w64, the same way the
host half builds natively.
