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
  maps outside it and Windows clamps menus and dialogs. [P][I]
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

- **No overlap in the guest.** Each mirrored top-level window gets its own
  region of the guest's virtual desktop, laid out by the guest so windows
  never overlap. avgpu owns the guest's display modes and can offer a large
  or multi-monitor virtual layout that is never scanned out.
  - Absolute input injection then always reaches the intended window.
  - WGC never captures a window covered by another.
  - The guest needs no knowledge of host positions at all.
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

### R3. Take windows from the driver where it already has them

DWM hands avgpu things remotewin currently reconstructs:

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
- **Everything else** (GDI and DWM-composed windows) keeps WGC, with
  `DirtyRegionMode` for damage.

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
| C | R2: guest canvas and popups by intent | Makes every compositor correct. Deletes the sync heuristics. |
| D | R4: mirror service, AUMID identity, reconnect | Robustness, grouping, icons. Survives manager and guest restarts. |
| E | R5: transport over the GPU device | Security, latency, capabilities. Needs an accel-virt service class. |
| F | R3: driver cursor and planes | Lowest-latency path for games and video. Exact cursors. |
| G | R6: clipboard, drag-and-drop, tray, notifications, IME, layout | Integration on top of the agent and shares. |

Phases B and C are the heart of it: a remotewin that hands the compositor
the guest's own buffers and never needs to know where the compositor put
a window. Phases E and F are what only LSW can do, because it owns both
ends of the GPU.
