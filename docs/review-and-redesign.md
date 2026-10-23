# remotewin: review and redesign

Written 2026-10-19 from a read-only audit of remotewin and the parts of LSW
that drive it. Part 1 is where remotewin stands and what is wrong with it.
Part 2 is a redesign: what remotewin would look like if it were built on
what LSW has that a remote-desktop protocol does not. Part 3 orders the
work.

Marks: **[P]** the code shows it (paths below). **[I]** inferred about
runtime, Qt or compositor behaviour; not run.

## Part 1 — where it stands

### What works

- The window model is already role-aware. Menus, tooltips and combo
  dropdowns become `xdg_popup`s positioned relative to their owner; owned
  dialogs become toplevels with `set_parent`; taskbar windows are plain
  toplevels. [P] `include/proto/owned_window.hpp:26-33`,
  `source/host/chrome/win32_style.cpp:36-40`,
  `source/host/mirror/mirror_window.cpp:160-203`.
- KWin is an exact-placement backend: placement by script, moves reported
  back and applied to the guest. [P] `source/host/placement/`.
- GPU frames cross through the export socket as dma-bufs; there is a pixel
  fallback; latency is instrumented (FrameStamps, LatencyLog, DisplayLock).

### Bugs (cheap)

| Bug | Where |
|---|---|
| Back/Forward mouse buttons arrive as left clicks | `source/host/input/guest_button.cpp:5-9` |
| Close sends `WM_CLOSE`, not `WM_SYSCOMMAND SC_CLOSE`: tray apps quit instead of hiding | `source/guest/session/guest_session.cpp:251-257` |
| A restyled window is rebuilt without its transient parent | `mirror_window.cpp:252-271` vs `:187` |
| No pointer-leave: hover highlights and tooltips stick | `mirror_window.cpp` (no `leaveEvent`) |
| Every application mirror truncates the same guest log | LSW `src/agent/src/core/paths.cpp:37-39` |
| `.desktop` entries declare MIME types but `Exec` has no `%F`: an opened file never reaches the app | LSW `src/host/src/apps/desktop_launcher.cpp:275-292` |
| Cursor caches keyed by HCURSOR, never invalidated or bounded | `guest_session.cpp:441-458`, `source/host/cursor/emulated_cursor.cpp` |
| After a GPU reset the host clears its rings without asking for new ones: mirrors freeze until a resize | `source/host/export/shared_frame_feed.cpp:275-288` |
| Falling back to pixels is permanent and for every window | `shared_frame_feed.cpp:58-69` |
| A pixel frame over ~16.7 Mpx exceeds the 64 MiB message cap and ends the mirror | `include/proto/frame_decoder.hpp:24` |
| A window whose capture cannot open is reported gone and reopened on the next list: a loop [I] | `source/host/mirror/mirror_session.cpp:386-395` |

### Gaps off KDE

- Owned dialogs keep Windows' own move loop, so off KWin their mirror
  cannot be dragged: the guest window moves, the toplevel does not. [P]
  `source/guest/windows/window_drag_watcher.cpp:36-37`.
- The guest is never told where windows are. Pointer input is injected at
  absolute guest-screen coordinates, so where guest and host overlap
  differently, hover and clicks reach the wrong guest window. [P]
  `source/guest/input/pointer_injector.cpp:19-32`.
- Topmost and no-activate do nothing: QtWayland honours neither hint on
  xdg-shell. [I]
- No `XDG_ACTIVATION_TOKEN` anywhere: GNOME will refuse raises. [P][I]
- One guest screen, the host's primary output; a mirror on another output
  maps outside it and Windows clamps menus and dialogs. [P][I] (Fixed:
  `OutputLayout`, rail-model.md "Outputs".)
- Popups are placed with Windows' screen-edge flipping already applied,
  against a guest screen unrelated to where the host window is. [I]

### Missing

Clipboard and drag-and-drop (none in LSW), fullscreen, per-window icons,
tray icons, notifications, keyboard layout sync, IME, touch and pen,
keyboard-shortcut inhibit, host-side minimize reaching the guest.

### Robustness and structure

- No reconnect: a dropped link ends the mirror (exit 4); a manager restart
  kills every mirror while the apps keep running in the guest. [P]
  `source/host/link/guest_link.cpp:60-88`, `source/host/main.cpp:174-177`.
- The handshake demands equal versions, with no capabilities. [P]
  `include/proto/wire_hello.hpp`.
- Fallbacks (pixels, no KWin, no pointer constraints) never reach the
  manager's StatusHub. [P]
- The managed localhost port takes the first connection unauthenticated:
  another local process could take the guest's keystrokes. [I]
  `guest_link.cpp:65-72`.
- One TCP stream: input messages share a send lock with multi-megabyte
  frames. [I] `source/guest/session/tcp_client.cpp:88-116`.
- The whole window list is resent on every change; the guest loops over
  all windows on one thread every 16 ms.
- KWin placement loads a script per placement and makes blocking D-Bus
  calls on the GUI thread; windows are identified by zero-width characters
  appended to their titles. [P] `source/host/placement/kwin_script.cpp`,
  `kwin_placement.cpp:24-29`.
- The KWin script loader is duplicated in LSW
  (`src/host/src/platform/kwin_activation.cpp`).
- Process scope ("which windows belong to this app") is a set of
  heuristics, the broadest being "same directory, started later". [P]
  `source/guest/windows/launch_scope.cpp:169-174`.
- Custom cursors reach into QtWaylandClient private classes, which break
  with every Qt minor version. [P] `source/host/cursor/pixel_cursor.cpp`.

### Frames

- Capture is Windows.Graphics.Capture per window, BGRA8 only; a size change
  tears down and rebuilds the session.
- The "zero-copy" path is two GPU copies and a draw: guest ring copy, host
  import, blit into a GL texture per window, draw to a GL subsurface; one
  `QOpenGLContext` per window, created on a popup's first frame. [P]
  `source/guest/capture/surface_ring.cpp`, `source/host/mirror/gpu_picture.cpp`.
- No damage on either side; no HDR or 10-bit.

## Part 2 — redesign

remotewin today is built like a remote-desktop client: it scrapes windows
off the guest's screen, guesses their roles, polls the cursor, and talks
over TCP. LSW is not a remote desktop. It owns the guest's display and
render driver (avgpu, avumd), the device on the other end (QEMU's
virtio-gpu-av and the executor), and the host GPU. Most of remotewin's
problems are consequences of not using that.

Six changes, each independently useful, together a different program.

### R1. A native Wayland client, not Qt widgets

Each mirror is a `QWidget` holding a GL subsurface that a per-window GL
context blits into. Replace the mirror windows with a thin client written
directly against `libwayland-client`:

- **Buffers, not blits.** Attach the guest frame's dma-buf to the surface
  as a `wl_buffer` (`zwp_linux_dmabuf_v1`), synchronised with
  `wp_linux_drm_syncobj_v1`, scaled by `wp_viewporter`, damaged with
  `damage_buffer`. That is true zero-copy: no host GL context, no copy,
  the compositor scans the guest's buffer out or composes it directly.
  Slots return to the guest on `wl_buffer.release`.
- **Every protocol, directly.** Qt's Wayland platform exposes none of these:
  - `xdg_positioner` with anchor, gravity and constraint adjustment chosen
    per popup;
  - subsurfaces with explicit sibling order;
  - `xdg_activation_v1` tokens;
  - `xdg_toplevel_icon_v1`, `xdg_toplevel_tag_v1`;
  - `wp_cursor_shape_v1` plus a plain cursor surface (the private-API cursor
    hack goes);
  - `zwp_keyboard_shortcuts_inhibit_v1`, `zwp_text_input_v3`, pointer
    constraints;
  - fullscreen, and `xx_zones` where a compositor has it.
- **Small and owned.** Each mirror becomes one object of a few hundred
  lines (surface, role, buffers, input) instead of a widget tree. Qt stays
  where it is good (the manager), not on the frame path.

This is the enabler for most of what follows, and it alone removes the
per-window GL contexts, the copies and the version-fragile cursor code.

### R2. Decouple guest geometry from host geometry

The deepest problem off KDE is that two window managers disagree about
where windows are, and remotewin injects input by guest-screen position.
Stop trying to keep them in step. The guest desktop becomes a private
canvas:

- **Input reaches its window, wherever it is.** Pointer input is injected
  at absolute guest-screen positions, so it lands on whatever guest window
  is topmost there. Map input per surface instead: a point in a host
  surface becomes the same point in its guest window. Before injecting,
  raise that window to the top of the guest's stacking order
  (`SetWindowPos(HWND_TOP, SWP_NOACTIVATE)`). Nobody sees the guest's
  screen, so its stacking is free to follow input.
  - Capture does not need this: Windows.Graphics.Capture takes each window
    from DWM's own surface for it, so a window covered by another in the
    guest still captures whole.
  - The guest needs no knowledge of host positions at all.
  - An optional guest layout in which mirrored windows never overlap (avgpu
    owns the display modes and can offer a large virtual desktop) would make
    the raise unnecessary. It is not needed.
- **Popups by intent, not by result.** Send the popup's anchor rectangle on
  its owner and its intended direction, not the coordinates Windows chose
  after flipping against a guest screen edge. Sources: the owner-relative
  rect, `TrackPopupMenu`/`TPM_*` alignment, combo and tooltip conventions.
  The host builds an `xdg_positioner` from that intent and the compositor
  flips and slides against the real output.
- **The host places top-level windows.** That is the compositor's job on
  Wayland. Exact placement backends (KWin today, and GNOME, Hyprland or
  Sway IPC if wanted) become a nicety for first placement and saved
  layouts, not something correctness depends on.

This makes non-KDE desktops correct by construction rather than by
backend, and deletes the position-sync heuristics (echo filtering,
recent-placement lists) that exist only because two sides fight over
coordinates.

### R3. Take what the driver already has (an optimisation)

Windows.Graphics.Capture per window works for every window and stays the
general mechanism; it runs in the guest's user session, so capture stays
in remotewin's guest half. DWM also hands avgpu two things that can make
particular cases cheaper:

- **The cursor.** The miniport implements `SetPointerShape` and
  `SetPointerPosition`: DWM tells the driver the exact cursor image and
  position. Forward them from the device instead of polling
  `GetCursorInfo` and caching by HCURSOR. That is exact, needs no cache,
  and works for every cursor kind, animated and per-monitor-DPI ones
  included.
- **Planes.** avgpu reports multiplane overlay support, and the host
  already composes planes. A flip-model swapchain — games, video players,
  browsers — that DWM promotes to a plane arrives at the host as the
  application's own buffer. For those windows there is nothing to capture:
  attach the plane's buffer to the mirror (R1), at the application's
  present rate. That gives the lowest latency LSW can offer, and the
  fullscreen and borderless-game path falls out of it.
- **Everything else** keeps WGC, with `DirtyRegionMode` for damage. Planes
  only save a copy and perhaps a frame of latency for games and video: do
  this only if WGC's latency is measured to matter there.

To find out: how many planes DWM will promote for windowed swapchains
under our caps, and how a plane is matched to its HWND (the present's
source and the swapchain's window).

### R4. One mirror service per VM, identity by AppUserModelID

Today each application gets its own remotewin process, started by the
manager over stdin, and a set of heuristics decides which windows belong to
it. Instead:

- **One long-lived mirror service** (a systemd user service) owns the guest
  link and the Wayland connection for every application.
  - The manager is a client of it.
  - It survives manager restarts and reconnects when the guest or agent
    restarts, re-adopting the windows still open.
  - Cross-process ownership (a dialog from a helper process owned by the
    app's window) is no longer a special case: the service sees every window.
- **Identity by AppUserModelID**, the key Windows' own taskbar groups by
  (`SHGetPropertyStoreForWindow`, `PKEY_AppUserModel_ID`). It becomes the
  Wayland `app_id`, with a generated `.desktop` file carrying the app's icon.
  - Grouping, pinning and icons then match Windows.
  - The pid, job and same-directory scope heuristics go.
- **State per window**, as a small state machine (created, shown,
  minimised, fullscreen, gone), fed by events rather than whole-list diffs.

### R5. The GPU device as the transport

The guest reaches remotewin over TCP through QEMU user networking, because
the hosts have no working `vhost_vsock`. But LSW already has a paravirtual
guest↔host channel: virtio-gpu-av, with rings, fences and shared memory,
and the executor on the host end. Carry remotewin over it:

- **A window service on the device:** control and input as small ring
  messages, frames by reference (allocation and fence), never by bytes.
  - No socket, so no unauthenticated port and no input queued behind a frame.
  - One fewer hop than SLIRP for every frame notification.
- **Capabilities, not versions.** The service negotiates features, so old
  and new halves interoperate and a missing feature degrades a window, not
  the link.
- **The pixel path** remains only for hosts without GPU export, and sends
  damage rectangles in chunks rather than whole frames under a 64 MiB cap.

### R6. Integration through the agent and the shares

Now that host folders mount in the guest:

- **Clipboard and drag-and-drop** go through the agent.
  - Text and images by value.
  - Files by path through the shared folder: a host file dropped on a
    mirror arrives in the guest as its `Z:\...` path, with no copy.
  - Guest file drags to the host the same way, through the guest's SMB share.
- **Notifications:** Windows toasts become `org.freedesktop.Notifications`.
- **Tray icons:** `Shell_NotifyIcon` becomes StatusNotifierItem with a
  DBusMenu, so a tray app's icon and menu live in the host's panel.
- **Keyboard:** the host's XKB layout is mirrored to the guest
  (`LoadKeyboardLayout`). Text input goes through `text_input_v3` as
  Unicode, so IMEs work.
- **Fullscreen:** a captionless window covering its monitor, or a promoted
  plane (R3), becomes `set_fullscreen` with shortcuts inhibited.

## Part 3 — order of work

| Phase | What | Why first |
|---|---|---|
| A | The bug list in Part 1 | Days of work. Each fix is user-visible now. |
| B | R1: native Wayland client with direct buffers | Removes copies and GL contexts. Unlocks positioner, subsurfaces, activation, icons, shortcut inhibit and fullscreen. |
| C | R2: per-surface input mapping, the target raised in the guest, and popups by intent | Makes every compositor correct. Deletes the sync heuristics. |
| D | R4: mirror service, AUMID identity, reconnect | Robustness, grouping, icons. Survives manager and guest restarts. |
| E | R5: transport over the GPU device | Security, latency, capabilities. Needs an accel-virt service class. |
| F | R3: driver cursor, then planes if WGC's latency matters | Exact cursors. Planes only if measured to matter for games and video. |
| G | R6: clipboard, drag-and-drop, tray, notifications, IME, layout | Integration on top of the agent and shares. |

Phases B and C are the heart of it: a remotewin that hands the compositor
the buffers it captured and never needs to know where the compositor put
a window. Capture stays in the guest; the device carries buffers and
messages (phase E); planes are an optimisation, last.

## Part 4 — R1 and R2 as built

Built on branch `native`, R1 and the popup and input halves of R2
together, since each needs the other: a positioner needs a native
surface, and a native surface has nothing Qt guessed for it to place.

### No Qt in remotewin

The host half links no Qt at all. What Qt had been doing for it, and what
does it now:

| Qt's part | Now |
|---|---|
| the event loop (`QCoreApplication`, `QSocketNotifier`, `QTimer`) | `PollLoop`: poll(2) over the Wayland fd (`wl_display_prepare_read`/`read_events`, flushed before each wait), sockets, timers. The link code takes an `EventLoop` (watches and timers), so LSW's manager gives it its Qt loop (`QtEventLoop`, in LSW) for the preview |
| signals and slots | `Signal<Args...>`, a list of `std::function`s, and `Deferred` for retries |
| `QTcpServer`/`QTcpSocket`, `QLocalSocket` | POSIX sockets in `GuestLink` and `SharedFrameFeed` |
| QtDBus (KWin's scripting, the placement callbacks, `Describe`) | sd-bus (libsystemd), driven from the loop by `BusConnection`. QtDBus was the last reason to keep QtCore; sd-bus is already on every systemd desktop |
| `QJsonDocument` (`Describe`) | `JsonText`, a writer of the few shapes `Describe` has |
| QtWaylandClient, the widget tree, QOpenGLWidget, the private cursor surface | libwayland-client directly, and the generated code of each protocol below |
| `QKeyEvent` text and keysyms | xkbcommon on the compositor's keymap; key repeat timed here from `repeat_info` |

Qt stays in the manager, where it draws UI. The code split in two
libraries: `rwin_link` (the guest link, the export feed, latency, the
cursor model, geometry; no Wayland, no Qt), which LSW's manager links for
its desktop preview, and `rwin_host` (Wayland, sd-bus) for the executable.

### Protocols

Required — remotewin refuses to start without them: `wl_compositor` 4+,
`wl_subcompositor`, `wl_shm`, `xdg_wm_base`, `wl_seat`, `wp_viewporter`.

Optional — each used where offered, the line in the log at start says
which the compositor has:

| Protocol | For | Without it |
|---|---|---|
| `zwp_linux_dmabuf_v1` (v4 feedback) | guest frames as dma-bufs; the main device tells which GPU the compositor composes on | frames come as pixels in `wl_shm` buffers |
| `wp_linux_drm_syncobj_manager_v1` | explicit sync: the guest's fence imported into an acquire point, the release point's eventfd returns the slot | the fence fd is waited on (POLLIN) before the attach; `wl_buffer.release` returns the slot |
| `wp_fractional_scale_v1` | the surface's exact scale | the outputs' integer scale |
| `wp_cursor_shape_v1` | guest cursors that are system shapes | every cursor a picture on a cursor surface, scaled with a viewport |
| `zwp_pointer_constraints_v1`, `zwp_relative_pointer_v1` | guest ClipCursor and pointer lock | no confinement; the guest's cursor follows the absolute pointer |
| `zwp_keyboard_shortcuts_inhibit_v1` | the compositor's shortcuts go to the guest while a window is fullscreen or the pointer locked | they stay the compositor's |
| `xdg_activation_v1` | `XDG_ACTIVATION_TOKEN` and `RAISE <token>` | the manager raises through KWin's scripting |
| `xdg_toplevel_icon_v1` | the guest window's own icon | the app id's .desktop entry's icon |
| `zxdg_decoration_manager_v1` | client-side decoration asked for explicitly (the guest draws its own frame); the desktop mirror asks for server-side | the compositor's default |
| `xdg_wm_dialog_v1` | modal dialogs said as modal | transient only |
| `wp_presentation` (monotonic) | the `presented` latency stage | the stage is not measured |
| `wp_alpha_modifier_v1` | a layered window's constant alpha | opaque |

### Frames

One `wl_surface` per guest window. A shared frame's slot becomes a
`wl_buffer` through `zwp_linux_buffer_params_v1` (made asynchronously,
kept per slot, evicted when the ring is remade); with explicit sync the
guest's fence fd goes into a timeline syncobj on the compositor's render
node and its point is the acquire point. A picture is shown one picture
pixel to one device pixel: the logical size is the picture over the scale,
rounded down, and the viewport's source crops to exactly that many device
pixels; `damage_buffer` names what changed. A window the compositor makes
larger than that (maximised, tiled, fullscreen) gets the picture centred
over a black one-pixel subsurface scaled behind it (`Backdrop`). No host
GL context exists anywhere. A layered window's constant alpha is the
compositor's (`wp_alpha_modifier_v1`); a colour key and a window region
come cut already - Windows' capture hands their outside over clear - so a
window with either stays on the GPU (rail-model.md §7.10).

### Roles, popups and input

`MirrorRole` decides, from the guest's fields, whether a window is an
`xdg_toplevel` (with `set_parent` to its owner's mirror), an `xdg_popup`
of the window it opens from, or a `wl_subsurface` of the one it is glued
to (z_glue: Office's shadow windows). The guest sends a popup's intent
(the POPUP field, rail-model.md §7.5): what kind it is and the rectangle
on its anchor window it opens from. `PopupLayout` builds the positioner:

| Kind | Anchor rectangle | Anchor edge, gravity | Adjust |
|---|---|---|---|
| menu bar menu | the menu bar item | bottom-left, bottom-right | slide x, slide y, flip y |
| submenu | the parent item | top-right, bottom-right | slide x, slide y, flip x |
| combo list | the combo box | bottom-left, bottom-right | slide x, slide y, flip y |
| context menu | the cursor at the press | top-left, bottom-right | slide and flip both ways |
| tooltip | the cursor, offset below it | top-left, bottom-right | slide x, slide y, flip y |
| exact | the position Windows chose | top-left, bottom-right | slide x, slide y, flip y |

Points are 1x1 rectangles (KWin refuses an empty anchor rectangle). No
`resize` adjustment: a popup's size is the guest's, and a smaller surface
would only crop it. Menus, combo lists and context menus grab with the
serial of the press that opened them; when the compositor dismisses a
grabbing popup the guest is told (PopupDismiss) and closes it the way that
kind closes (WM_CANCELMODE to a menu's owner, CB_SHOWDROPDOWN to a combo,
Escape otherwise) — 80 ms later, and not at all when the press that
dismissed it landed on another of the application's surfaces, so that
moving along a menu bar does not close the menu Windows is opening.

Input is per surface: a point on a surface is the same point in its guest
window, sent with the window's id, and the guest raises that window to the
top of its own stacking order (`SetWindowPos(HWND_TOP, SWP_NOACTIVATE)`,
`InputTarget`) when another window is at the point, before a press, a
wheel turn or a move (not while a button is held), so the
injected input reaches it however the guest's windows overlap. Wheel
input groups by `wl_pointer.frame` and takes `axis_value120`; Back and
Forward are buttons 3 and 4; leaving a surface sends PointerLeave. Keys
are evdev codes made set-1 scancodes (`stroke_for`), with the
compositor's modifiers and repeat.

### Windows

Close, minimise, maximise and restore are SysCommands; the compositor's
minimise and maximise requests become them. A captionless window covering
its monitor becomes `set_fullscreen`, with shortcuts inhibited. A guest
move/size loop (MoveSizeStart) becomes `xdg_toplevel.move` or `.resize`
with the press's serial. The guest sends each window's icon (WindowIcon:
WM_GETICON, then the class icon, as BGRA, on every change of handle), and
the toplevel wears it through `xdg_toplevel_icon_v1`. The KWin placement
backend, the display lock, latency stamps, logging and the `--managed`
stdin/stdout lines are as they were; KWin scripts are now started with
`Scripting.start` (a script's id can be a live script's after an unload,
so its object path is not reliably its own), and SIGTERM, SIGINT and
SIGHUP end the loop so the scripts are unloaded and their files removed.

### What went

`MirrorWindow`, `GpuPicture`, `DrawPicture`, `MonitorWindow`,
`PictureLayout`, `PixelMapping`, `PointerGlobals`,
`CursorSurfaceViewport`, `PixelCursor`, `WornCursor`, `RenderingGpu`, the
conformance harness (`remotewin-conform`, its scenarios and its tests: it
drove Qt widgets in-process, and is to be written again against
`Describe` and KWin's view when it is wanted), and LSW's
`scripts/remotewin-conform.sh`. LSW's desktop preview draws the monitor's
pixels itself, read-only, through `rwin_link`.

### Left

- The conformance harness, against the new host (Part 5's integration
  tests cover resizing, maximising and minimising).
- `xx_zones`, `xdg_toplevel_tag_v1`, `zwp_text_input_v3`, topmost
  (no protocol for keep-above).
- The desktop preview's frames on the GPU (it takes pixels).
- Placement on desktops other than KDE needs nothing more for
  correctness; a backend per desktop for first placement is optional.

## Part 5 — resizing, maximising and minimising

Written 2026-10-20 after "resizing is extremely laggy and buggy, even
maximizing and minimizing". Measured first, with integration tests built
for it (README, Tests) and a live run on KDE Plasma 6.8 at 1.2 against
Notepad with frames on the GPU; then fixed, each fix with the case that
catches it.

### What was wrong

| Cause | Evidence | Fix |
|---|---|---|
| A configure was acknowledged and answered by a commit of the old size: the window stayed at the guest's last picture until the guest answered | the mirror's frame followed a drag 55 ms behind (220 ms with a guest answering in 120 ms) | the compositor's size is the window's at once; the picture is shown from its corner, cropped or over the backdrop, until the guest's catches up (0.1 ms behind) |
| `MirrorSurface::fit_*` committed by themselves, between the acknowledgement and the window geometry | KWin took the configure as answered with the old size and moved the window: the frame jittered 4 units at every step | they only set the view; the caller's commit carries it with the geometry |
| KWin's placer set the frame's size with its place, from the mirror's last picture | every drag snapped back after the release (KWin's frame 1040x780 back to 800x600); a restore went to the maximised size, then to 400,180 instead of 200,150 | the placer moves only |
| The "KWin's own placement, put it back" rule fired for maximises and drags within a second of a window appearing | "put at 0,0 by KWin as it was placed; put back" on a maximise | only for a move alone, never a resize, a state or a user's drag |
| KWin reports a maximised frame before its maximise mode; reports had no state at all | the guest was moved to 0,0 at its old size as it maximised ("frame to 0,0 1505x1040"), and Windows then had it at 1,0 2559x1390 | reports carry maximised, fullscreen and minimised; no place is asked of the guest while either side is in a state or on its way |
| Sizes and places went to the guest from two senders, each with a stale half (the size request kept the old corner, the placement the old size) | the moves undid each other: a left or top drag ended with the guest window back at its old size or corner | one sender per window (`MoveRequests`): newest size and place asked together once the last ask lands; a resize keeps the edges it does not move |
| A size request's answer was any report; a timeout let the next out, and a late answer was taken for a refusal | with a guest slower than 250 ms the mirror took an old size as the guest's and snapped back | asks are kept until seen to land; only the newest, or a frame nothing asked for, answers |
| Maximise and minimise followed by level: a guest report written before the maximise landed undid it, and answers were sent back | ping-pong; the restore cascade above | `StateSync`: changes only, an answer is not a change, nothing believed while an ask is on its way |
| The compositor's minimise never reached the guest | the guest window stayed shown, captured, behind a minimised mirror | KWin's watcher reports `minimizedChanged`; the guest is sent SC_MINIMIZE and SC_RESTORE |
| A size the guest took by itself was asked back to the last configure's | an installer page or a dialog's "more" snapped back | the mirror takes it, and goes back to the guest's corner when KWin re-centres it |
| The mirror took its size from frames | after an unminimise, Windows' capture hands over the 185x27 frame it took as the window went: the mirror shrank to 154x22 and back for 70 ms | the window is the size the guest reports; frames are shown in it |
| The guest made a new capture session and a new ring at every size | 30-70 ms to the new session, 20-30 ms to the ring, then exports and imports on the host, per step: 87 sessions and 16 rings in one live run | buffers with room (picture rounded up to 128, and 128 more); the pool made again in its session to shrink, a new session to grow: 4 and 4 |
| A frame captured before the pool grew was cropped and sent | a 1408x1024 picture of a 1491x1030 window | dropped, and the pool made for the size it needed |
| The guest read a moved window's frame again only 60 ms after the move | each step of a drag waited for it | read again at 20 ms and at 60 ms |

### What the tests do

`tests/integration/`: a headless KWin of the case's own in a private
D-Bus session, the host under test as its client, KWin's fake input for
the pointer, a KWin script watching the window, and a played guest
(`FakeGuest`, `ModelWindow`) answering as Windows does after a set delay.
Twelve cases: maximise and restore, and minimise and unminimise, each
from the compositor and from the guest; corner drags out and in, and with
guests answering in 120 and 400 ms; a top-edge drag that Windows begins
(MoveSizeStart); a left-edge drag; a resize by KWin's scripting; and an
application resizing its own window. Each judges where both sides settle
and how soon, how many times a state changed, whether anything went back
against a drag, how long the mirror's frame took to follow each pointer
step, and what the guest was asked. Against the host before these
fixes, all twelve fail.

### Left

- Frames on the GPU are not in the integration tests (the export socket
  needs the device); the guest's capture and rings were checked live.
- The guest's picture still lags a drag by about two of its answer
  times: one ask is out at a time, so a slow application is not flooded.
- A maximised guest window is its own work area's size, which is not the
  compositor's maximised size: it is centred over black bars. Telling
  the guest the host's work area (RAIL's Client System Parameters)
  would make them one; every application of the guest shares that
  setting, so it waits for the one mirror service (R4).
- Off KWin, minimising a mirror does not reach the guest (xdg-shell says
  nothing of it), and nothing places a window that resized itself.
- A `wl_shm` buffer is still made per size on the pixel path.
