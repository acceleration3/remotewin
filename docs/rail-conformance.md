# RAIL conformance

Written 2026-10-20. This is a spec-and-code review of remotewin's wire 6
against MS-RDPERP's normative rules and the rules reversed from Windows'
own RAIL server (rail-model.md §2). Every row was checked in the code, not
taken from rail-model.md §7. It is not the old conformance matrix
(conformance.md), which ran windows against the mirror and is gone.

**Sources:**

- [MS-RDPERP] protocol revision 34.0, 14 July 2026, read from
  learn.microsoft.com's open-specs pages. Section numbers are its own.
- The reversed server rules, marked R§ with rail-model.md's section.
- The code at branch `rail`. Line numbers are at the commit that adds
  this file.

**Roles.** remotewin's guest half is RAIL's server and the host is the
client. A rule for "the server" is checked in `source/guest/`, a rule for
"the client" in `source/host/`, and the wire in `source/proto/`.

**Verdicts:**

- **CONFORMS.**
- **DEVIATES** on purpose: a decision written down in rail-model.md §4, §6
  or §7, or in review-and-redesign.md, with its reason.
- **NONCONFORMANT:** a bug, or a deviation nobody wrote down. Each one says
  what became of it.
- **NOT IMPLEMENTED:** a known open item, with an estimate.

## Summary

| Verdict | As found | After this branch | Rechecked 2026-10-28 |
|---|---|---|---|
| CONFORMS | 47 | 55 | 52 |
| DEVIATES on purpose | 18 | 25 | 26 |
| NONCONFORMANT | 16 | 1 | 3 |
| NOT IMPLEMENTED | 13 | 13 | 13 |
| **Rows** | **94** | **94** | **94** |

On 2026-10-28, at 76c9c15, every row was checked again and the rest of the
spec was gone through field by field: see [The whole spec](#the-whole-spec-2026-10-28)
below. Rows 55, 56 and 82 changed verdict; line numbers in rows 1-94 are
still those of 025a59d.

The NONCONFORMANT rows went three ways:

- **Eight were fixed in code**, each with a test where the logic runs
  host-native (Fixes, below).
- **Seven were real decisions** that rail-model.md never stated. They are
  now in rail-model.md §7.9, and count as DEVIATES.
- **One is open:** the host's resize band lies inside the window rather
  than outside it (row 25).

## The rules

### Window enumeration (which windows are reported)

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 1 | R§2.1 | Admit a window that is top-level: GA_PARENT is the desktop, or it has WS_POPUP. Refuse child and message-only windows. | The same test. | CONFORMS | source/guest/windows/window_reader.cpp:182 | |
| 2 | R§2.1 | A window becomes known the first time a show, location, focus or foreground event finds it effectively visible: IsWindowVisible, or the SysSetRedraw property. | The same events and the same visibility test. | CONFORMS | window_reader.cpp:175, window_tracker.cpp:88 | |
| 3 | R§2.1 | Filter nothing else: tool windows, menus, tooltips and zero-size windows are all reported. | Also refused: Explorer's own windows (Progman, WorkerW, the taskbars, the shell's tool windows, shell-managed windows kept out of the switchers) and remotewin's own. | DEVIATES | window_reader.cpp:189 | §6.2: RAIL's session has no Explorer, and this guest does |
| 4 | 3.3.1.3, R§2.1 | The marker window SHOULD NOT be remoted. | Never admitted: it is in the guest half's own process and has the `rwin-guest-` class prefix. | CONFORMS | window_reader.cpp:189 | |
| 5 | R§2.1, R§2.3 | A cloaked window is tracked, with show state 0 until it uncloaks. | Tracked. SHOW is Hidden while cloaked, and the CLOAKED field says so. | CONFORMS | window_reader.cpp:215, :236 | |
| 6 | R§2.1 | Hiding does not delete; only destruction does. RichEdit's dropdowns (REComboBox20W, REListBox20W) are deleted on hide. | The same. | CONFORMS | window_tracker.cpp:100-104, :228 | |
| 7 | R§2.1 | A window that becomes a child is deleted. Class WBPOPUP is spared on the parent-change path. | **Was:** the hook dropped EVENT_OBJECT_PARENTCHANGE for a window no longer top-level before the tracker saw it, and the sweep checked only IsWindow, so the window stayed reported for good. **Now:** the event reaches the tracker, and the 1 s sweep forgets it too; WBPOPUP is spared on both. | NONCONFORMANT, fixed | window_watch.cpp:65-71, window_tracker.cpp:108-115, :162-176 | e906dbf |
| 8 | R§2.1 | Office's MSO_BORDEREFFECT windows are reported with EnforceServerZOrder. | ZGLUE is set for that class. The host makes the window a subsurface placed below its neighbour. | CONFORMS | window_reader.cpp:222, source/host/mirror/mirror_role.cpp:27 | |
| 9 | R§2.1, R§2.9 | A hung window's ghost is shown in its place; the App ID falls back from the ghost to the hung window. | **Was:** the ghost was admitted with DWM's process and image, so no application's scope claimed it, and a hung window simply vanished. **Now:** a `Ghost` window's APPID (AUMID, pid, image) is its hung window's (HungWindowFromGhostWindow). | NONCONFORMANT, fixed | window_reader.cpp:308-330 | e906dbf. rdpshell also skips min/max for ghosts; remotewin still asks the ghost, which answers |
| 10 | R§2.1 | At most 256 tracked windows, and 256 in a z-order list. | 256, with one warning; ZOrder holds 256 plus the marker. | CONFORMS | include/guest/windows/window_tracker.hpp:39, include/proto/z_order.hpp | |
| 11 | R§2.1 | A minimized window is ShowState 2, never hidden. With HIDE_MINIMIZED_APPS the server sets ARW_HIDE. | ShowState 2. ARW_HIDE is not set by remotewin: Explorer, which runs in this guest, sets it itself. | NONCONFORMANT → documented | window_reader.cpp:236 | §5.9 planned it and §7 never said it was dropped. Now in rail-model.md §7.9 |
| 12 | R§2.1 | The scope is the session's input desktop, not a process. | The guest reports the whole session. Each host process mirrors its own application's windows (MirrorScope). | DEVIATES | source/host/mirror/mirror_scope.cpp:60 | §6.3 |
| 13 | R§2.2 | rdpshell's eight WinEvent ranges, out of context, OBJID_WINDOW only, on one worker queue. 0x0B and 0x19 are heard only when local move/size is on. | The same ranges and the same filter, on one STA thread. 0x19 and 0x0B are used only with `movesize`. | CONFORMS | window_watch.cpp:38-47, :65, :268-273 | |

### Window fields

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 14 | 2.2.1.3.1.2.1, R§2.3 | OwnerWindowId is GW_OWNER only. A child owner becomes its GA_ROOT and the desktop becomes none. An invisible owner, or one with an empty rectangle or an empty normal rectangle while minimized, is the hidden-owner sentinel 0xFFFFFFFE. | The same, as `WindowId::hidden_owner()`. An owner that is never admitted is named by its handle with serial 0. | CONFORMS | window_reader.cpp:108-119, :291-301, window_tracker.cpp:263 | |
| 15 | 2.2.1.3.1.2.1 (client) | Owned windows are mirrored as owned. | A hidden owner reads as none, so the window stands alone. A dialog is a toplevel with `set_parent`; an owned popup is a popup. | CONFORMS | source/host/link/guest_window.cpp:13, mirror_role.cpp:27 | |
| 16 | R§2.3 | Style is sent as `& 0xC80F0000`, ex-style as `& 0x080C3088`, with WS_EX_TOOLWINDOW forced on when the shell refuses a button. | Masked the same, plus WS_CAPTION, WS_EX_TRANSPARENT, WS_EX_CONTEXTHELP and the class style, which the host's chrome reads. Toolwindow is forced the same way. | DEVIATES | window_reader.cpp:212-213 | §7.1 |
| 17 | R§2.3 | Taskbar presence by ExpectTaskbarIconOnClient. | Explorer's rule wherever it and rdpshell disagree (APPWINDOW beats TOOLWINDOW; NOACTIVATE without APPWINDOW gets no button). | DEVIATES | window_reader.cpp:270-282 | §7.1 |
| 18 | R§2.3, 2.2.1.3.1.2.1 (client) | The client's taskbar follows the style it is sent: a window forced to WS_EX_TOOLWINDOW has no button and no Alt+Tab entry. | **Was:** the host never applied TASKBAR, so unowned tool windows and no-activate overlays sat on this desktop's taskbar and in Alt+Tab. **Now, on KWin:** the caption tag carries the skips, and the watching script sets skipTaskbar and skipSwitcher. Off KWin no protocol can say it. | NONCONFORMANT, fixed (KWin) | source/host/chrome/win32_style.hpp (off_taskbar), source/host/placement/kwin_placement.cpp:52, source/host/mirror/window_mirror.cpp:281 | 253175a. An owned window stays with the compositor's transient handling |
| 19 | 2.2.1.3.1.2.1 | ShowState is 0, 2, 3 or 5: 0 while hidden or cloaked, otherwise from WS_MINIMIZE or WS_MAXIMIZE. | The same. The reader refuses any other value. | CONFORMS | window_reader.cpp:236-242, source/proto/window_fields.cpp | |
| 20 | 2.2.1.3.1.2.1, R§2.3 | Title at most 520 bytes, read with WM_GETTEXT on a 20 s timeout. | InternalGetWindowText, which never enters the window's thread, up to 1023 UTF-16 units. | DEVIATES | window_reader.cpp:218 | §6, "Also settled": a hung window cannot stall the window thread |
| 21 | 2.2.1.3.1.2.1, R§2.4 | Window offset and size are DWMWA_EXTENDED_FRAME_BOUNDS, falling back to rcWindow. | The same. rdpshell's Enhanced-RemoteApp exception (full rcWindow for THICKFRAME) is not copied: margins lie outside the frame, as the spec has it. | CONFORMS | source/guest/windows/window_surface.cpp, window_reader.cpp:244 | §6.10 |
| 22 | R§2.4 | A maximized window is clipped to its monitor. | Clipped to rcMonitor. | CONFORMS | window_reader.cpp:144-156 | |
| 23 | 2.2.1.3.1.2.1 | CLIENTAREAOFFSET and CLIENTDELTA: the client rectangle relative to the window, clamped to ≥ 0. | The client rectangle relative to the frame, clamped to it. Its screen offset is the frame plus that. | CONFORMS | window_reader.cpp:158-167 | |
| 24 | 1.3.4, R§2.4 | Resize margins: rcWindow − frame for a WS_THICKFRAME window. | The same, from the window rectangle's insets. | CONFORMS | window_reader.cpp:252-257 | |
| 25 | 1.3.4, 3.2.5.1.6, 2.2.1.3.1.2.1 (client) | The client extends the window by the margins with a transparent hit-testable band *outside* the window bounds. Input in that band SHOULD go to the server. | The host's band (ResizeBorder, sized from the margins) lies *inside* the mirror's picture: the outer 6-8 px of the window's own content resize it, and press there starts the compositor's resize instead of reaching the guest. | NONCONFORMANT, open | source/host/chrome/resize_border.cpp:13, window_mirror.cpp:373, :811 | Not fixed: it needs an input-only subsurface outside the window geometry and pointer routing for it, and it changes the integration tests' edge drags. About 1 day. Starting the compositor's resize locally, rather than forwarding the press, would stay as a deviation |
| 26 | 2.2.1.3.1.2.1, R§2.4 | WNDRECTS is the window's region (else its frame). VISOFFSET and VISIBILITY repeat it. | The region relative to the frame, empty meaning the frame. The VISIBILITY fields are dropped. | DEVIATES | window_reader.cpp:85-106 | §4.2: identical since Windows 8 [<6>, <7>] |
| 27 | 2.2.1.3.1.2.1 | CLIENTAREASIZE, RPCONTENT and ROOTPARENT. | Windows never sets them [<3>-<5>]. remotewin's client rectangle carries the size. | CONFORMS | – | |
| 28 | 2.2.1.3.1.2.1 (client) | EnforceServerZOrder: the client keeps the glued windows consecutive in the z-order. | A subsurface of its neighbour, below it. | CONFORMS | mirror_role.cpp:27, window_mirror.cpp (make_role) | |
| 29 | 2.2.1.3.1.2.2-3, R§2.3 | Window Icon and Cached Icon: small and big, at most 32 px (96 with HIGH_DPI_ICONS), through an icon cache, for every window. | BGRA up to 256 px with no cache (§7.5). Sent only for windows that have a taskbar button. | NONCONFORMANT → documented | window_tracker.cpp:179-181, source/guest/windows/window_icons.cpp | The BGRA format and the cache were decided in §7.5. The "taskbar windows only" condition was not; it is now in §7.9 |
| 30 | 2.2.1.3.1.2.1-3 | Overlay icon, OverlayDescription, ICON_OVERLAY_NULL. | – | NOT IMPLEMENTED | – | Reading an overlay set through ITaskbarList3 needs Explorer's taskbar state (UI Automation or a shell hook): 3-5 days; host via xdg_toplevel_icon or a StatusNotifier badge |
| 31 | 2.2.1.3.1.2.1, 2.2.2.14.1 | TaskbarButton (tabs) and Taskbar Tab Info. | – | NOT IMPLEMENTED | – | Not planned (rail-model §3): only tabbed applications use it, and Wayland has no tab groups |
| 32 | 2.2.1.3.1.2.1 | AppBar state and edge. | – | NOT IMPLEMENTED | – | Not planned: there is no appbar on a Wayland desktop. About 2 days on layer-shell compositors |
| 33 | 2.2.2.6.5, 2.2.2.8.2, R§2.9 | App ID by Get App ID request and response (Ex adds pid and image), resolved with IApplicationResolver. | Pushed as the APPID field (aumid, pid, image), resolved once at admission with IApplicationResolver. | DEVIATES | window_reader.cpp:321-336 | §4.4 |
| 34 | R§2.9 | After answering App ID, send TaskbarButtonCreated and refresh min/max. | Not sent: Explorer runs in this guest and sends it for each button it makes. | NONCONFORMANT → documented | – | §5.9 planned it and §7 was silent; now §7.9 |

### Window lifetime, synchronization and desktop orders

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 35 | 2.2.1.3.1.1 | WindowId is unique for every window in the session. | `{hwnd, serial}`: a recycled HWND gets a new serial. | CONFORMS | window_tracker.cpp:195-213 | Stronger than RAIL's hold-back |
| 36 | 2.2.1.3.1.2.1, R§2.2 | New: every field. Update: only what differs; an empty delta is not sent; geometry goes all or nothing. | WindowNew sends every field the link allows; WindowChanged sends the mask of differences. GEOMETRY is one field. | CONFORMS | window_tracker.cpp:141-153, window_fields.cpp:89 | |
| 37 | 2.2.1.3.1.2.4 | Deleted Window on destruction. | WindowGone. A failed capture is SurfaceLost, not WindowGone. | CONFORMS | window_tracker.cpp:228-240 | |
| 38 | 3.2.5.1.6 | The client ignores a delta, icon or deletion for an unknown WindowId. | Ignored. | CONFORMS | source/host/link/guest_link.cpp:386-413, mirror_session.cpp (window_icon) | |
| 39 | 3.2.5.1.6 | The client SHOULD create a local window for a new window. | **Was:** a mirror, and a subscription, for every belonging window, hidden ones included, against §7.3's own text, and a surface for a window of no size. **Now:** a mirror for each belonging window that is not hidden, and a surface once it has a size. Windows outside the application are not mirrored (row 12). | NONCONFORMANT, fixed | mirror_session.cpp:174-183, :288-299, guest_window.hpp (showable) | 4405ebc: part of the Notepad menu regression |
| 40 | 2.2.1.3.1.2.1 | ShowState 0: do not show the window. | The mirror loses its surface. Its subscription stays, for a window WinUI shows again. | CONFORMS | mirror_session.cpp:288-299 | |
| 41 | 1.3.2.3, 3.3.5.1.8, R§2.2 | On connect: ARC_BEGAN with HOOKED, then every window, then ARC_COMPLETED, then the active window. | SyncBegin(Connect), every window, SyncEnd, then ZOrder. | CONFORMS | window_tracker.cpp:71-93, window_watch.cpp:175-176 | |
| 42 | 3.2.5.1.8, 2.2.1.3.3.2.1 | On ARC_BEGAN the client discards everything; at ARC_COMPLETED, anything not resent is gone. | Discarding waits for SyncEnd: each window not resent is gone then, and mirrors that were resent stay up. | CONFORMS | source/host/link/guest_windows.cpp:44-61 | Same outcome, without the flicker |
| 43 | 1.3.2.3, 3.3.5.1.8, 2.2.1.3.3.2.2 | On a desktop switch (secure desktop), send DESKTOP_NONE, then HOOKED and a resync. The client discards everything on NONE. | – | NOT IMPLEMENTED | – | DesktopLost is not defined (§7.1). Guest: watch the input desktop (OpenInputDesktop name, or WTS session notifications); host: hide every mirror. About 1 day |
| 44 | 1.3.2.2 | Auto-reconnect resyncs. | The host exits 4 when the guest disconnects; LSW reports "The window mirror stopped unexpectedly" and does not start it again. SyncBegin::Reason::Resync is defined but never sent. | NOT IMPLEMENTED | source/host/main.cpp:44, :248 | Half a day if the host kept running. 2026-10-28: the old note said LSW restarts the mirror; it does not |
| 45 | 3.3.5.1.8 | A ZORDER order whenever the number or order of top-level windows changes, and ACTIVEWND whenever the active window changes. | ZOrder after any window event, and after each 1 s sweep. | CONFORMS | window_watch.cpp (restack), z_order_report.cpp:78 | |
| 46 | R§2.6 | The z-order list holds tracked, effectively visible windows and the marker; IME windows sit with their owners. | Tracked, effectively visible, uncloaked windows, and the marker as `{0,0}`. No IME fix-up. | CONFORMS | z_order_report.cpp:48-74 | The IME fix-up is a sub-item of row 85 |
| 47 | R§2.6 | The active window is GetGUIThreadInfo's hwndActive at its root; none for null, the desktop or the marker. | The same. | CONFORMS | z_order_report.cpp:32-38, :68-72 | |
| 48 | R§2.6 | Send when ready: the target is first, or first below the topmost windows. Otherwise back off 10…320 ms, never report a transient foreground, and suppress repeats. | The same. | CONFORMS | z_order_report.cpp:78-106 | |
| 49 | 3.2.5.1.8 (client) | The client SHOULD apply the server's z-order to its windows. | Not applied: a Wayland client cannot stack its toplevels. The z-order serves activation (the marker rule) and the order the host walks windows in. Popups and parts stack above or below what they belong to. | DEVIATES | mirror_session.cpp:169 | review-and-redesign R2 ("the host places top-level windows"), now stated in §7.9 |
| 50 | 3.2.5.1.8, 3.2.5.2.9.2 (client) | Activate the reported ActiveWindowId, only if it is above the marker. | Above the marker, and only while this desktop's focus is in a mirror. | DEVIATES | source/host/mirror/activation_sync.cpp:37-48, source/proto/z_order.cpp:8 | §5.5: a guest activation while the user is in a host window does not steal focus |
| 51 | 3.2.5.1.8 (client) | The reported active window is activated. | **Was:** a window the guest made active before its mirror had a surface (a dialog Windows activates as it opens) was never activated here. **Now:** followed again when its mirror maps. | NONCONFORMANT, fixed | mirror_session.cpp:237-248 | 4405ebc; integration dialog-in-front |

### Handshake and capabilities

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 52 | 3.1.5.1-2, 3.2.5.2.1.2 | The Handshake comes before any other PDU, nothing is processed before it, and build numbers are logged, never enforced. | Hello first, nothing processed before it, the build logged. | CONFORMS | guest_link.cpp:203-226, source/guest/session/guest_session.cpp (greet) | |
| 53 | 2.2.1.1, 3.2.5.1.5, 3.3.5.1.5 | Capability sets; drop the connection if RAIL is unsupported. | Named features, each active at the lower of the two versions; `windows` required, and a peer without it refused with one line. | DEVIATES | source/proto/feature_set.cpp:22 | §4.1 |
| 54 | 3.1.2, 3.1.6 | A handshake timer MAY be used; on expiry the receiver SHOULD drop the connection. | No timer. | CONFORMS | – | Optional. But a guest that connects and never says hello holds the host's one guest slot for good. A 30 s timer (Windows' <28>) would cost an hour |

### Client to server

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 55 | 3.2.5.2.5.1 | Activate is sent when a window is activated by means other than clicking it. A click goes as input. | A focus within 500 ms of a press on that surface is taken as a click and not sent. But a press on the resize band (it starts the compositor's resize) or on the letterbox backdrop (no picture point) never reaches the guest, and still counts as the click. | NONCONFORMANT, open | window_mirror.cpp:803-817, :847-850, :861-865, activation_sync.cpp:26 | 2026-10-28: was CONFORMS. Finding F2 |
| 56 | 3.2.5.2.5.1, R§2.6 (client) | The client does not activate a window merely because it appeared. A RAIL client creates its windows without activating them, and Windows decides which is active. | 4405ebc's fix holds for this application's windows: a Mapping focus is not sent while the guest has another window active, and that window is followed. **But** `guest_active_` is often another application's window, which this host has no mirror for, so nothing is followed; and the launcher-token focus in `mapped_now` is classed as Mapping and sends nothing either. | NONCONFORMANT, open (regression) | activation_sync.cpp:27-31, mirror_session.cpp:220-223, :241-249 | 2026-10-28: was "fixed". Finding F1 |
| 57 | 3.2.5.2.9.2 | When a local, non-RAIL window is activated, send Activate with Enabled = 0. | Sent when focus has left every mirror for 100 ms. It names the window left, and the guest acts on it only while that window is still its foreground. | DEVIATES | activation_sync.cpp:50-61, window_watch.cpp:330-350 | §6.6: several guest halves share one guest |
| 58 | 3.3.5.2.5.1, R§2.6 | Activate the window if it is tracked; Enabled = 0 activates the marker; ignore an unknown window. | The same, through the marker's thread. | CONFORMS | window_watch.cpp:330-350 | |
| 59 | R§2.6 | Foreground with SetForegroundWindow, falling back to the hotkey trick (VK 0xB9 and lower, 20 tries, 1 s). | The same. | CONFORMS | source/guest/windows/marker_window.cpp:133-179 | |
| 60 | R§2.10, 3.3.1.3 | The marker: WS_POPUP\|WS_VISIBLE, 0×0, UIA_WindowVisibilityOverridden = 2. | The same, plus WS_EX_TOOLWINDOW. | DEVIATES | marker_window.cpp:87-98 | §6.6: Explorer's Alt+Tab would list it |
| 61 | 2.2.2.11.1, 3.2.5.2.9.2 | The marker's id goes in Z-Order Sync Information. | Implicit: the marker is `{0,0}` in every ZOrder. | DEVIATES | include/proto/z_order.hpp | §7.1 |
| 62 | 2.2.2.6.3 | SysCommand is one of SC_SIZE, MOVE, MINIMIZE, MAXIMIZE, CLOSE, KEYMENU, RESTORE or DEFAULT. | Exactly those; any other is refused. | CONFORMS | source/proto/sys_command.cpp:24-33 | |
| 63 | 3.3.5.2.5.3, R§2.7 | Post the command (never send it). SC_KEYMENU is preceded by WM_SYSCHAR; SC_RESTORE of a minimized, disabled window is ShowWindowAsync. | The same. | CONFORMS | source/guest/windows/window_commands.cpp:29-47 | rail-model §2.7 printed the WM_SYSCHAR lParam as 0x2039001; it is 0x20390001, as the code has it (corrected) |
| 64 | 3.2.5.2.5.3 | SysCommand is sent when a window receives a system command by means other than clicking (keyboard, the taskbar). | Close, maximize, restore and minimize asked by the compositor. Minimize is reported on KWin only; restore is also sent before activating a minimized window. | DEVIATES | window_mirror.cpp:60-78, :583-593, mirror_session.cpp:339-350 | Off KWin xdg-shell does not report minimize (review Part 5, Left). Nothing on Wayland asks for SC_MOVE, SIZE, KEYMENU or DEFAULT |
| 65 | 3.2.5.2.5.2, 3.3.5.2.5.2 | System Menu is sent when the system menu is asked for by means other than clicking; the server posts WM_POPUPSYSTEMMENU at screen coordinates. | Guest: posts it at frame-relative coordinates made screen coordinates. Host: never sends it. §7.3 said the Qt host sent it from a right press on its bars; the native host has no bars, and the compositor's window menu tells the client nothing. | NONCONFORMANT → documented | window_commands.cpp:49-51, window_watch.cpp:311-317 | Now §7.9 |
| 66 | 1.3.2.5, 3.3.5.2.6.1-2, R§2.5 | On event 0x19: Min Max Info, then Move/Size Start with the kind (1-11). | The same; kinds by rdpshell's mapping. | CONFORMS | source/guest/windows/move_size_report.cpp:46-102 | |
| 67 | 2.2.2.7.2, R§2.5 | PosX/Y: for MOVE, the pointer's offset from the frame; otherwise, the last button-down point. | MOVE: the offset. Other kinds: where the cursor is when the event is heard (the guest half has no RDP stack to record a button-down). | NONCONFORMANT → documented | move_size_report.cpp:90-97 | The host reads neither: the compositor gets the host's own press serial. Now in move_size_start.hpp and §7.9 |
| 68 | R§2.5 | Processes in LocalMoveSizeIgnoreList keep Windows' loop. | The same: value names, file names, compared as they are. | CONFORMS | move_size_report.cpp:30-44, :66-80 | |
| 69 | 3.2.5.2.7.2 | The client starts a local move or size of that kind and stops forwarding input. | xdg_toplevel.move or .resize with the press's serial; the compositor holds the pointer. Keyboard kinds are left to Windows' loop. | CONFORMS | window_mirror.cpp:682-704 | |
| 70 | 1.3.2.5, 3.2.5.2.7.3 | At the end of the move the client forwards the button-up, and sends Window Move for keyboard moves and every resize. | The button-up goes at once, which ends Windows' loop before it moved anything. A resize goes as WindowMove while it runs (one ask at a time); a move goes as WindowMove on KWin, from the frame KWin reports. | DEVIATES | window_mirror.cpp:682-704, :618-622, source/host/mirror/move_requests.cpp | §6.5 (V2's client-driven loop is the shell's privilege) and §7.8 |
| 71 | 3.3.5.2.6.4, 2.2.2.7.3 | Move/Size End carries the final position. | MoveSizeEnd carries the whole frame. | DEVIATES | move_size_report.cpp:104-116 | §4.3 |
| 72 | 3.2.5.2.7.4 | On Move/Size End the client moves its window to TopLeft. | Logged and ignored. The End arrives as the compositor's drag *begins* (the button-up was forwarded at once), so its corner is where the drag started. Positions follow the GEOMETRY deltas. | NONCONFORMANT → documented | mirror_session.cpp:86-89 | Now §7.9 |
| 73 | 3.3.5.2.6.3, 2.2.2.7.4 | Move the window to the rectangle, margins included. | The frame plus the window's insets is the window rectangle, applied asynchronously and without activating. Also: nothing is done to a minimized or maximized window, and a new size is held to the work area. | NONCONFORMANT → documented | window_commands.cpp:85-108 | The margins and the asynchrony were decided (§4.4, §6); the two refusals were not. Now §7.9 |
| 74 | 3.2.5.1.6 | The client SHOULD NOT expect the reported rectangle to equal the one it sent. | An answer of another size is the window's own. | CONFORMS | move_requests.cpp, window_mirror.cpp (answered) | |
| 75 | 3.2.5.2.7.1 | The client applies Min Max Info: MinTrack and MaxTrack sizes, maximized size and position. | **Was:** the minimum applied only when both its sides came to more than nothing, the maximum needed both track sides, and a minimum above the work area made a maximum under it, which xdg_toplevel refuses (invalid_size). **Now:** each side on its own, the largest never under the smallest. The maximized size and position are the compositor's. | NONCONFORMANT, fixed | window_mirror.cpp:397-414, source/host/mirror/picture_limits.cpp:18 | b4554b7; tests/picture_limits_test.cpp. The maximized geometry is a deviation (review Part 5, Left) |
| 76 | 3.3.5.2.6.1, R§2.7 | Min Max Info is sent at window setup and at each move/size start, from WM_GETMINMAXINFO with a 1 s SendMessageTimeout (ABORTIFHUNG). | The same. | CONFORMS | window_tracker.cpp:216-226, move_size_report.cpp:87-89, source/guest/windows/min_max_reader.cpp:15 | |
| 77 | 2.2.2.7.5, 3.3.5.2.6.5 | Window Snap, with an arrangement that restores the size on unsnap. | – | NOT IMPLEMENTED | – | The host can see xdg_toplevel's tiled states. The guest cannot reach the shell's privileged arrangement (§2.5), but could use Aero Snap through SendInput (Win+arrows) or arrange the window itself. 2-3 days |
| 78 | 2.2.2.6.4, 2.2.1.3.2, 3.3.5.2.5.4 | Notification icons and Notify Event. | – | NOT IMPLEMENTED | – | §6.8: Explorer owns Shell_TrayWnd in this guest. Read icons through UI Automation of the notification area or a Shell_NotifyIcon shim; host through StatusNotifierItem. 1-2 weeks |
| 79 | 2.2.2.12.1, 3.2.5.2.10, 3.3.5.2.9 | Cloak both ways. | Server to client: show state 0 and the CLOAKED field, as Windows does for a client without BIDIRECTIONAL_CLOAK (R§2.10). Client to server: nothing; Wayland has no per-window cloak (a workspace switch is the nearest). | NOT IMPLEMENTED | window_reader.cpp:215 | Bidirectional: 2 days with ext-workspace |
| 80 | 2.2.2.9-2.2.2.10, 3.3.7 | Language bar, language profile, IME compartments. | – | NOT IMPLEMENTED | – | Needs zwp_text_input_v3 on the host and TSF on the guest. 1-2 weeks |
| 81 | 2.2.2.4.1, 3.2.5.2.3.1 | The client sends its system parameters at connect and on change (work area, display, high contrast, dark mode, accent and so on). | Only SPI_SETDRAGFULLWINDOWS, once. | NOT IMPLEMENTED | mirror_session.cpp:54 | §7.1. Dark mode, accent and high contrast from xdg-desktop-portal's Settings: 2 days. The work area waits for R4's one mirror service (review Part 5, Left) |
| 82 | 2.2.2.5.1, 3.3.5.2.4.1 | The server sends its system parameters (SPI_SETSCREENSAVEACTIVE, SPI_SETSCREENSAVESECURE). | Neither. Instead WorkArea (0x2F) and Monitor (0xF001, the primary monitor), at connect and on change, on `windows` rather than §4.1's `sysparam`: RAIL's client-to-server ids sent the other way, kept by the host as its desktop model. | DEVIATES | window_watch.cpp:176, :209-212, :354-362, include/proto/sys_param.hpp | §7.1 SysParam. 2026-10-28: was CONFORMS. Screen-saver parameters not needed: the guest's lock is off |
| 83 | 2.2.2.3 | Client Execute and Execute Result. | LSW's agent launches programs. | NOT IMPLEMENTED | – | §7.1. Not wanted while the agent launches |
| 84 | 2.2.2.13 | Power Display Request. | – | NOT IMPLEMENTED | – | Guest: poll display-required power requests; host: idle-inhibit. 1 day |
| 85 | 2.2.2.15, R§2.6 | Text Scale and Caret Blink; the IME z-order fix-up. | – | NOT IMPLEMENTED | – | Half a day each |

### Popups and menus

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 86 | 2.2.1.3.1.2.1, R§2.1 | Menus, tooltips and combo lists are ordinary windows with their server coordinates and owner, and the client places them there. | Placed by intent: an xdg_popup of what they open from, with the anchor rectangle, edge and gravity Windows meant before it flipped them, which the compositor applies against the real output. | DEVIATES | source/host/mirror/popup_layout.cpp:58, mirror_role.cpp:27 | R2, §7.5: Wayland gives a client no positions |
| 87 | 2.2.1.3.1.2.1 (client) | A popup shows above what it belongs to. | An xdg_popup of its opener, a subsurface of its owner, or a transient toplevel. | CONFORMS | mirror_role.cpp:27-39 | Integration menus-in-front |
| 88 | 2.2.1.3.1.2.1 | A window shown at 0×0 and sized a frame later (WinUI's popups) is mirrored once it has a size, and every time it is shown again. | **Was:** the host subscribed it at 0×0. The guest's first capture found no size and went into a 1 s back-off, and every retry while the menu was closed failed with E_INVALIDARG (Windows' capture refuses a hidden window), so Notepad's menus never showed. **Now:** the guest does not capture a hidden or unsized window and captures it afresh, at once, when it is shown with a size; the host gives it a surface only then. | NONCONFORMANT, fixed | source/guest/session/guest_session.cpp:590-600, include/guest/windows/window_surface.hpp (capturable), mirror_session.cpp:288-299 | 7ddce35, 4405ebc. Checked live on Notepad (build 26200); integration winui-popup |

### Minimize, maximize and restore

| # | Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|---|
| 89 | 2.2.1.3.1.2.1 (client) | The client shows the window minimized (2), maximized (3) or normally (5). | StateSync carries the guest's state to xdg_toplevel's maximize and minimize, and its own changes are not echoed back. | CONFORMS | window_mirror.cpp:424-437, source/host/mirror/state_sync.cpp | |
| 90 | 3.2.5.2.5.3 | The client asks for state changes with SysCommand, not by moving or sizing the window. | SC_MAXIMIZE, SC_RESTORE and SC_MINIMIZE. No WindowMove is sent while either side is in a state or heading into one. | CONFORMS | window_mirror.cpp:60-78, :506-516, :628-636 | |
| 91 | R§2.6, R§2.7 | Activating a minimized window does not restore it; the client restores it first. | A mirror focused or raised while its window is minimized sends SC_RESTORE, then Activate. | CONFORMS | window_mirror.cpp:857-862, mirror_session.cpp:345 | |
| 92 | R§2.1 | A minimized window keeps its record. | Kept. Its size stays the last normal size, and the stub frame Windows' capture hands over after a restore is dropped. | CONFORMS | window_mirror.cpp (follow), source/guest/capture/window_capture.cpp:29-47 | |
| 93 | – | A captionless window covering the monitor is shown fullscreen. | xdg_toplevel.set_fullscreen. | DEVIATES | window_mirror.cpp:472-477 | Not a RAIL rule: remotewin's, for games (Part 4) |
| 94 | 2.2.2.6.3 (SC_CLOSE), R§2.7 | Close is SC_CLOSE, so the application's own handler decides. | SC_CLOSE, and nothing for a CS_NOCLOSE class, which Windows' own Close would not act on. | CONFORMS | window_mirror.cpp:583-593 | |

## Fixes

| Commit | Rows | What |
|---|---|---|
| 7ddce35 | 88 | guest: a hidden or unsized window is not a lost capture; it is captured afresh when shown |
| 4405ebc | 39, 51, 56, 88 | host: a mirror mapped is not a window activated; the guest's active window is followed once mapped; mirrors only for shown windows, surfaces once sized. Integration cases dialog-in-front, menus-in-front, winui-popup, activation-both-ways and map-keeps-guest-active |
| b4554b7 | 75 | host: MinMax track sizes applied side by side, the largest never under the smallest (unit test picture_limits) |
| e906dbf | 7, 9 | guest: windows made children are forgotten (WBPOPUP spared); a ghost carries its hung window's App ID |
| 253175a | 18 | host: Windows' taskbar rule on KWin's taskbar and switcher (unit tests win32_style, kwin_tag; integration taskbar-presence) |
| this commit | 11, 29, 34, 65, 67, 72, 73 | rail-model.md §7.9 writes down the decisions that were real but undocumented; move_size_start.hpp says what PosX/Y is; the §2.7 lParam typo |

Against the host from before this branch, these integration cases fail:

- map-keeps-guest-active: the dialog loses the active window to the
  panel, and the guest is asked to activate the panel;
- winui-popup: something is shown at 0×0;
- activation-both-ways;
- taskbar-presence.

The guest-side fixes (rows 7, 9 and 88) cannot run host-native. Row 88
was checked live: this branch's guest and host, the host on a private
headless KWin, Notepad's File menu opened twice. The menu's window was
captured both times, and the host received its frame at 338×573.

## The whole spec (2026-10-28)

Checked at 76c9c15 against every page of [MS-RDPERP] on learn.microsoft.com
(278 pages, fetched 2026-10-28), field by field and flag by flag, §1 to §6.
Rows 1-94 above were all checked again. Old verdicts that changed: 55
(CONFORMS → NONCONFORMANT), 56 (fixed → NONCONFORMANT, a regression for a
second application), 82 (CONFORMS → DEVIATES). Row 44's note was wrong. The
rows below are the rules the table above did not have. Line numbers are at
76c9c15. `LSW:` paths are relative to linux-subsystem-windows/src, for the
rules LSW's manager and agent carry rather than remotewin.

N/A means the rule is about the RDP wire or connection sequence only (byte
layout, orderType values, MCS, INFO_RAIL, length limits on 16-bit counts),
and wire 6 has no counterpart. Those rules are listed by section, not one
row each.

### Findings

| # | Severity | Rule | What goes wrong | Where | Fix |
|---|---|---|---|---|---|
| F1 | medium-high | 3.2.5.2.5.1, R§2.6 (row 56) | App #1 holds the guest's foreground. App #2 is launched through LSW; Windows' foreground lock leaves its window inactive. KWin focuses #2's mirror with the launcher token, which is classed as Mapping, and `guest_active_` is A1, so host #2 sends nothing and follows nothing (it has no mirror for A1). Host #1 loses focus and deactivates A1 to its marker. Keys typed into #2's mirror land on marker #1 until the user clicks | source/host/mirror/activation_sync.cpp:27-31, mirror_session.cpp:220-223, :241-249, window_mirror.cpp:864 | Suppress a Mapping focus only when `guest_active_` is this session's mirror above the marker; send Activate in `mapped_now`'s token branch, as `raise()` does |
| F2 | medium | 3.2.5.2.5.1 (row 55) | Mirror A active; the user resizes mirror B by its band. KWin focuses B; the press is counted as a click, so no Activate goes, but the guest never saw a press. Typing in B's mirror goes into A | window_mirror.cpp:803-817, :847-850, :861-865 | Record that the last press was sent (next to `send_button`) and have `pressed_into()` require it |
| F3 | medium | 1.3.4 (row 25) | Still open: the band lies inside the picture | resize_border.cpp:5-20, window_mirror.cpp:808-814 | As row 25. It also causes F2 and F5 |
| F4 | medium | 2.2.2.3.2 (Exec result), LSW | Every CreateProcess failure is handed to `explorer.exe <path>` and answered ok with pid 0; `--pid 0` reaches remotewin, so no scope claims anything. An uninstalled exe shows Explorer's "cannot find" dialog unmirrored and a 60 s "No window appeared"; an .msc or document opens in an unmirrored handler. RAIL answers RAIL_EXEC_E_FILE_NOT_FOUND at once | LSW: agent/src/apps/launcher.cpp:240, :276-281, agent/src/rpc/handlers/apps/launch.cpp:74, host/src/mirror/window_mirror.cpp:32 | Fail ERROR_FILE/PATH_NOT_FOUND with the code; shell only non-PE targets, via ShellExecuteExW with SEE_MASK_NOCLOSEPROCESS; else pass the AssocQueryString handler as `--image`; never `--pid 0` |
| F5 | medium | 5.1, 3.1.2 | The host's port takes the first connection, with no token and no Hello deadline. A local process (standalone: anything on the LAN, 0.0.0.0) that connects first with a wire-7 Hello can show any window on the user's desktop and receives every key typed into it; a silent one holds the slot until LSW's 60 s deadline | source/host/link/guest_link.cpp:64-76, :91-105 | Per-launch random token from the manager to both halves, in the Hello, compared in constant time; a 30 s Hello deadline (<28>) that drops and keeps listening. R5 removes the socket |
| F6 | medium-low | ShowState 3 → 2 → 3 (client) | Minimising a maximised window clears WS_MAXIMIZE, so the maximise StateSync hears "not maximised" and unmaximises the minimised mirror. Unminimised, it comes back at normal size with the maximised picture cropped, then maximises again a round trip later | window_mirror.cpp:419-424, state_sync.cpp:33-38 | While `window_.minimized()`, do not feed `maximize_.guest_says` |
| F7 | medium-low | 2.2.1.3.1.2.2 | `asked_icon` returns null both on a timeout and on "no icon", so a busy or hung window's icon flips to its class icon each sweep and back. Each busy taskbar window also blocks the window thread up to 2 × 50 ms per 1 s sweep, the thread that serves Activate, SysCommand, WindowMove and the z-order | source/guest/windows/window_icons.cpp:20-44, window_tracker.cpp:175 | On a timeout, leave the icon; no class fallback for a window that answered before; skip `IsHungAppWindow` |
| F8 | medium-low | 2.2.2.3.1 TRANSLATE_FILES, FILE (LSW) | Associations write `MimeType=` but `Exec=` has no `%F` and `--launch` takes only an id: double-clicking foo.psd opens Photoshop with no document | LSW: host/src/apps/desktop_launcher.cpp:275, :285-291, host/src/app/cli_parser.cpp:41-43 | `%F`, forward paths, translate through the virtiofs share map, refuse paths outside every share. Already in review-and-redesign.md:36 |
| F9 | low | 3.2.5.2.9.2 | When the focused mirror's window is destroyed, `KeyboardEvents::forget` clears focus without `moved_`, so no deactivation is sent; or Activate{gone,0} is sent and the guest drops the unknown id before the root-owner check. The dialog's owner stays active in the guest while the user is in a host app | source/host/wayland/keyboard_events.cpp:51-55, source/guest/windows/window_watch.cpp:333-337 | Emit `moved_` from `forget`; in `focus_left` name `guest_active_` when the left window is gone |
| F10 | low | 3.2.5.2.7.1 | MinMax is read at admission and when Windows' loop starts. A resize from the host's band never enters that loop, so after a DPI or style change the compositor uses stale limits | window_tracker.cpp:222-225, move_size_report.cpp:87-89 | Re-read MinMax in `refresh` on STYLE or DPI change, send when it differs |
| F11 | low | FIELD_WNDRECTS | A region of more than 256 rectangles (an ellipse of 300 px) is sent as empty, which means the frame: clicks in the transparent corners go to the guest window. The cap is only a code comment | window_reader.cpp:92, include/proto/window_fields.hpp (kMaxRegionRects), window_fields.cpp:51 | Raise to 4096 (u16 count, 16 B each), or write it down in §7 |
| F12 | low | 2.2.1.3.1.2.2 ICON_BIG | One icon is sent, always labelled Big. A window with only WM_SETICON(ICON_SMALL) and a large class icon gets a 16 px "Big" icon, scaled up blurry | window_icons.cpp:29-34, :52 | Big: ICON_BIG then GCLP_HICON; Small: ICON_SMALL/SMALL2 then GCLP_HICONSM; send both with true labels. Or write the one-icon decision into §7.9 |

Not written down, harmless: MinMax is asked of a ghost window (rdpshell
skips it; the ghost answers); the 1023-unit title length (§6 covers only
how it is read). Latent: the guest's main loop can send CursorShape,
CursorChanged and PointerGrab before the host's Hello arrives, without
checking `cursor` or `pointer.grab` is active (guest_session.cpp:113-116,
:463-490), against §4.1; every shipped host has both features. A
z-order-only change with no activation (SetWindowPos HWND_TOP with
SWP_NOACTIVATE) is reported up to 1 s late, by the sweep: no
EVENT_OBJECT_REORDER is hooked, as in RAIL. If Resync is ever sent,
`GuestWindows::begin_sync` must clear `order_` (guest_windows.cpp:44-48).

The most important unimplemented areas, by what a user notices:

1. Notification icons (row 78): a tray app that hides its window on close
   (Discord, Steam, Teams) has no way back. 1-2 weeks: read icons through
   Explorer's ITrayNotify or UIA, StatusNotifierItem and
   org.freedesktop.Notifications on the host.
2. Keyboard layout (2.2.2.10.1): scancodes go through the guest's layout,
   set only at install. A German host layout types "y" for Z. 1-2 days.
3. Light/dark theme (RAIL_SPI_APPS_USE_LIGHT_THEME, SYSTEM_USES_LIGHT_THEME):
   light guest apps on a dark host. Half a day from the portal's
   color-scheme.
4. Power display request (row 84): a video in a guest browser lets the host
   blank. 1-2 days.
5. Sticky keys (TS_STICKYKEYS): the host applies a latched modifier to its
   own xkb state only, so Shift tapped then A should type "a" (read from
   the code, not tried). 1 day.
6. High contrast, accent colour, text scale, caret blink: half a day to two
   days each.

### Overview, capabilities and common structures

| Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|
| 1, 1.3 | One local window per server window; input to it goes to the server | One mirror per belonging, shown window; per-window pointer and keys | DEVIATES (scope) | mirror_session.cpp:177-196, window_mirror.cpp:750-830 | §6.3 |
| 1.3.2.1 | INFO_HIDEF asks for Enhanced RemoteApp, with fallback | Always per-window content | N/A | – | No non-Enhanced mode |
| 1.3.2.1 | Server Handshake first; HandshakeEx when both support it | Each end's Hello first on its stream; nothing handled before it | CONFORMS | guest_session.cpp:103-106, guest_link.cpp:120-124, :503-511 | |
| 1.3.2.4 | Client sends Client Info and SysParams after the handshake | DragFullWindows only, at connect | CONFORMS | mirror_session.cpp:57 | Contents: row 81 |
| 1.3.2.4 | Server sends Server SysParams after the handshake, and on change | WorkArea and Monitor before the first sync, again on WM_SETTINGCHANGE/WM_DISPLAYCHANGE | CONFORMS | window_watch.cpp:176, :209-212, desktop_settings.cpp:47-53 | What they are: row 82 |
| 1.3.2.4 | Resize margins only with WINDOW_RESIZE_MARGIN_SUPPORTED | Always in GEOMETRY | CONFORMS | window_reader.cpp:253-258 | One build on both sides |
| 1.3.3 | Enhanced RemoteApp: whole window contents even when covered; no desktop | WGC per-window; Progman and WorkerW refused | CONFORMS | window_reader.cpp:29-30, :198, source/guest/capture/window_capture.cpp:60-73 | |
| 1.6, 1.7, 3.2.1.1, 3.3.1.2 | Capabilities in Demand/Confirm Active; support level per side | Named features in Hello; FeatureSet | DEVIATES | hello.cpp:24-37, feature_set.cpp:7-31 | §4.1 |
| 2.2.1.1.1 | TS_RAIL_LEVEL_SUPPORTED; other bits 0 without it | `windows`, required; a peer without it is refused whatever it lists | CONFORMS | feature_set.cpp:22-31 | |
| 2.2.1.1.1 | DOCKED_LANGBAR, LANGUAGE_IME_SYNC, SERVER_TO_CLIENT_IME_SYNC | – | NOT IMPLEMENTED | – | row 80 |
| 2.2.1.1.1 | SHELL_INTEGRATION (tabs, overlay icons) | – | NOT IMPLEMENTED | – | rows 30, 31 |
| 2.2.1.1.1 | HIDE_MINIMIZED_APPS | Explorer in the guest sets ARW_HIDE | DEVIATES | – | §7.9, row 11 |
| 2.2.1.1.1 | WINDOW_CLOAKING | CLOAKED field and SHOW 0, one way | NOT IMPLEMENTED | – | row 79 |
| 2.2.1.1.2 | WndSupportLevel 0 means drop; SUPPORTED_EX fields | No `windows` means refuse; the client size always travels | CONFORMS / N/A | feature_set.cpp:29 | Windows never sets the EX fields [<3>-<5>] |
| 2.2.1.1.2, 2.2.1.2.3-4, 2.2.1.3.1.2.3, 3.1.1.2, 3.2.5.1.4, 3.3.5.1.5 | Icon caches: counts, CacheEntry/CacheId, Cached Icon orders | No cache; icons deduplicated per window by handle | DEVIATES | window_icons.cpp:40-44 | §7.5 |
| 2.2.1.2.1 | UNICODE_STRING: UTF-16, u16 length | UTF-8, u32 length | DEVIATES | message_reader.cpp:42-48 | §4.1 |
| 2.2.1.2.2 | TS_RECTANGLE_16 | Rect, i32 x/y and u32 w/h | CONFORMS | include/proto/rect.hpp | Wider |
| 2.2.1.2.3 | Bpp 1-32 with colour table; AND mask; colour bits; Width/Height u16 | Always 32-bit BGRA, palette resolved by GDI, mask folded into alpha, top-down; ≤ 256 px | DEVIATES (format) / CONFORMS (content) | window_icon.cpp:13-31, cursor_image.cpp:48-91 | §4.3 |
| 3.1.1.1 | Server states WaitCaps → Initializing → SyncDesktop → WaitForData | Hello checked; work area, SyncBegin…SyncEnd, ZOrder; then serve; requests during the sync are queued | CONFORMS | guest_session.cpp:149-167, window_watch.cpp:167-224 | Desktop switch: row 43 |
| 3.2.5.1.4-5, 3.3.5.1.4-5 | Both sets present; drop if missing or level 0 | Both ends refuse and close | CONFORMS | guest_link.cpp:211-233, guest_session.cpp:158-162 | |
| 3.3.5.1.6 | Window icon orders not sent under SUPPRESS_ICON_ORDERS; clients MUST still process icons | Sent only with `icons`; the host reads WindowIcon whenever it comes | CONFORMS | window_tracker.cpp:68, :180, guest_link.cpp:236-239 | |
| 1.3.1, 1.5, 1.8, 1.9, 2.1, 2.2.2.1 (layout), 3.2.5.1.1-3, 3.3.5.1.1-3, §4 | RDP channels, MCS, INFO_RAIL/HIDEF, CHANNEL_FLAG_SHOW_PROTOCOL, byte layout, examples | Own TCP stream, `[len:u32be][id:u8]` | N/A | frame_decoder.cpp | |

### Window information orders

| Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|
| 2.2.1.3.1.1 | FieldsPresentFlags; unknown bits | u32 mask; unknown bits rejected | CONFORMS | window_fields.cpp:145-147 | |
| 2.2.1.3.1.2.1 | STATE_NEW vs existing; each field present iff its flag | WindowNew vs WindowChanged (a WindowNew for a known id restates it); mask order | CONFORMS | guest_link.cpp:362-382, window_fields.cpp:109-189 | |
| FIELD_CLIENTAREASIZE | Client width and height | GEOMETRY.client w,h | CONFORMS | window_reader.cpp:166-167 | row 27 |
| FIELD_WNDOFFSET, WNDSIZE, CLIENTDELTA | Window corner and size; window-to-client delta | GEOMETRY.frame; GEOMETRY.client x,y | CONFORMS | window_surface.cpp:15-25, window_reader.cpp:134-168 | rows 21-23 |
| FIELD_RESIZE_MARGIN_X/Y | Margins outside the bounds | THICKFRAME windows only, from the insets | CONFORMS | window_reader.cpp:253-258 | [<31>]; client side row 25 |
| FIELD_WNDRECTS | Shape rectangles | Empty means the frame (DEVIATES, row 26); more than 256 also empty | NONCONFORMANT | window_reader.cpp:86-107 | F11 |
| FIELD_VISOFFSET, VISIBILITY | Visible region | Dropped | DEVIATES | – | §4.2; equals WNDOFFSET since Win8 [<6>, <7>] |
| FIELD_RPCONTENT, ROOTPARENT | Render plug-in; top-level parent | – | N/A | – | Never set by Windows [<4>, <5>]; only top-level windows are admitted |
| FIELD_OVERLAY_DESCRIPTION, ICON_OVERLAY_NULL, TASKBAR_BUTTON | Overlay text, overlay removed, tab groups | – | NOT IMPLEMENTED | – | rows 30, 31 |
| FIELD_APPBAR_STATE, APPBAR_EDGE (0-3) | Appbars | – | NOT IMPLEMENTED | – | row 32 |
| 2.2.1.3.1.2.2 | Window Icon on set or update | At admission and when the 1 s sweep sees a new HICON, taskbar windows only | DEVIATES | window_tracker.cpp:175-181, :221 | §7.5, §7.9, row 29. Busy windows: F7 |
| 2.2.1.3.1.2.2 ICON_BIG | Big and small, each its own order [<8>] | One icon, always Big | NONCONFORMANT | window_icons.cpp:29-34, :52 | F12 |
| 2.2.1.3.1.2.2 ICON_OVERLAY | Overlay icon | – | NOT IMPLEMENTED | – | row 30 |
| 3.2.5.1.6 | Icon for an unknown id is ignored | Not applied, but kept in `icons_` | CONFORMS | mirror_session.cpp:105-108, :251 | Only matters for a peer that breaks ordering |

### Notification icons and desktop orders

Notification icons are not built at all (row 78): no `tray` feature, no
TrayIcon, TrayIconGone or NotifyEvent message. Every rule of 2.2.1.3.2,
2.2.2.6.4, 3.2.5.1.7, 3.2.5.2.5.4, 3.3.5.1.7 and 3.3.5.2.5.4 is NOT
IMPLEMENTED: WindowId and NotifyIconId; STATE_NEW (a new icon MUST carry
an icon); NOTIFY_VERSION (0, 3, 4: it decides how the callback is encoded,
R§2.8, and which Notify Event messages are allowed); NOTIFY_TIP;
NOTIFY_INFO_TIP and NOTIFY_STATE (SHOULD NOT for version 0); ICON (MUST NOT
with CACHED_ICON; CACHED_ICON itself is N/A, never set by Windows 7 to 2012
[<10>]); the deleted-icon order (example 4.1.1.4 breaks its own MUST, so a
client should tolerate extra flags); the balloon's Timeout (clamped to
10-30 s [<11>]), InfoFlags (NIIF_NONE…ERROR, NOSOUND, LARGE_ICON),
InfoTipText ≤ 510 B and Title ≤ 126 B (UTF-16 units, so the guest should
enforce them before UTF-8); the client's create, update and delete with
unknown icons ignored; the Notify Event messages (L/R button down, up and
double-click, WM_CONTEXTMENU, NIN_SELECT, NIN_KEYSELECT, NIN_BALLOON*); the
server posting them and ignoring unknown icons. Host mapping when built:
StatusNotifierItem (ToolTip, Status=Passive for hidden, Activate,
ContextMenu) and org.freedesktop.Notifications (expire_timeout, app_icon
by InfoFlags, ActionInvoked/NotificationClosed → BALLOONUSERCLICK,
BALLOONTIMEOUT).

| Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|
| 2.2.1.3.3.2.1 | DESKTOP_HOOKED | Implied by SyncBegin; no HOOKED-only message | DEVIATES | include/proto/sync_begin.hpp | §4.3 id 65; a standalone HOOKED only follows a NONE, not built |
| 2.2.1.3.3.2.1 | ARC_BEGAN with HOOKED; ARC_COMPLETED alone | SyncBegin{reason}; SyncEnd with no fields | CONFORMS | window_tracker.cpp:72-90, sync_begin.cpp:8-13 | rows 41, 42 |
| 2.2.1.3.3.2.1 | ACTIVEWND; ActiveWindowId (0xFFFFFFFF none) | `ZOrder.active` always present, `{0,0}` none | CONFORMS | include/proto/z_order.hpp:20-35, z_order.cpp:17-21 | As example 4.1.1.5 |
| 2.2.1.3.3.2.1 | ZORDER; NumWindowIds; WindowIds in z-order | u16 count ≤ 257 (256 + marker), short lists rejected | CONFORMS | z_order.cpp:17-38, z_order_report.cpp:48-67 | The 1-byte count could not hold 257 |
| 3.2.5.1.8 | On HOOKED prepare for orders | SyncBegin sets `syncing_`; reconcile waits | CONFORMS | mirror_session.cpp:58-60, :178 | |
| 3.3.5.1.8 | A z-order order when the number or order of windows changes | Show, hide, destroy, focus, foreground, state, name, cloak, parent and the 1 s sweep | CONFORMS | window_watch.cpp:241-279, z_order_report.cpp:78-97 | A reorder with no activation is up to 1 s late (see above) |
| 2.2.1.3.3.2.2, 3.2.5.1.8, 3.3.5.1.8 | DESKTOP_NONE; the client discards everything; NONE then HOOKED on a switch | – | NOT IMPLEMENTED | – | row 43 |

### Channel, launching and system parameters

| Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|
| 2.2.2.1 | orderType MUST be a listed value | Unknown ids logged and ignored at both ends | CONFORMS | guest_session.cpp:266-268, guest_link.cpp:241 | |
| 2.2.2.1 | HANDSHAKE, CLIENTSTATUS, HANDSHAKE_EX | One Hello each way: build string and features | DEVIATES | hello.cpp, message_id.hpp:21, :47 | §4.1, §4.4 |
| 2.2.2.1 | EXEC, EXEC_RESULT, NOTIFY_EVENT, LANGBARINFO, TASKBARINFO, LANGUAGEIMEINFO, COMPARTMENTINFO, CLOAK, POWER_DISPLAY_REQUEST, SNAP_ARRANGE, TEXTSCALEINFO, CARETBLINKINFO | No message ids | NOT IMPLEMENTED | message_id.hpp | §7.1 |
| 2.2.2.2.1, 3.1.5.1 | buildNumber SHOULD be the build | `"remotewin wire 7"` | CONFORMS | hello.cpp:13 | It names the wire; the refusal text's "another build" cannot tell two wire-7 builds apart |
| 2.2.2.2.2 | ALLOWLOCALMOVESIZE recorded by the server | `movesize`; the guest gates MinMax and MoveSize on it | CONFORMS | window_watch.cpp:271, :274, window_tracker.cpp:67, :222 | |
| 2.2.2.2.2 | ZORDER_SYNC: marker made, or destroyed without it | Always made (part of `windows`, required) | CONFORMS | window_watch.hpp:90 | |
| 2.2.2.2.2 | HIGH_DPI_ICONS (96 px, else 32) | `icons`, up to 256 px | DEVIATES | window_tracker.cpp:68 | §7.5 |
| 2.2.2.2.2 | APPBAR_REMOTING, POWER_DISPLAY_REQUEST, BIDIRECTIONAL_CLOAK | – | NOT IMPLEMENTED | – | rows 32, 84, 79 |
| 2.2.2.2.3 | HIDEF iff Enhanced RemoteApp; the client handles surface-to-window | `frames.shared` and FrameRoute{shared}; pixels without an export socket | CONFORMS | guest_link.cpp:598-601, main.cpp:223-230 | |
| 2.2.2.2.3 | EXTENDED_SPI 1-3 [<12>], SNAP_ARRANGE, TEXT_SCALE, CARET_BLINK | Not advertised | NOT IMPLEMENTED | – | |
| 2.2.2.3.1 | ExeOrFile executable; a .lnk | .lnk resolved to target, arguments and working directory | CONFORMS | LSW: agent/src/apps/launcher.cpp:118-137 | |
| 2.2.2.3.1 | FILE: a document | Handed to the shell, answered ok with pid 0, never mirrored | NONCONFORMANT | LSW: launcher.cpp:240-281 | F4 |
| 2.2.2.3.1 | TRANSLATE_FILES | No host path reaches a launch | NONCONFORMANT | LSW: desktop_launcher.cpp:275, :285-291 | F8 |
| 2.2.2.3.1 | APP_USER_MODEL_ID | `launch_store_app` → IApplicationActivationManager | CONFORMS | LSW: agent/src/apps/store_apps.cpp:154-182 | |
| 2.2.2.3.1 | EXPAND_WORKINGDIRECTORY, EXPAND_ARGUMENTS | Always expanded, in the target user's environment | CONFORMS | LSW: launcher.cpp:140-142, :190 | No literal `%NAME%` possible |
| 2.2.2.3.1 | WorkingDir chosen by the client | The .lnk's or the exe's folder only | NOT IMPLEMENTED | LSW: launcher.cpp:173-178 | Half a day |
| 2.2.2.3.2, 3.2.5.2.2.1-2 | Result matched to its request and reported to the user | One Launcher per launch; Launcher::failed reaches the UI | CONFORMS | LSW: host/src/apps/guest_app_launcher.cpp:45-49, :102-109 | Except F4's false successes |
| 2.2.2.3.2 | S_OK, E_FILE_NOT_FOUND | Missing paths answered ok | NONCONFORMANT | LSW: launcher.cpp:240, :276-281 | F4 |
| 2.2.2.3.2 | E_DECODE_FAILED, E_FAIL with RawResult [<14>] | RPC error; `ok:false` with both Win32 errors as text | CONFORMS | LSW: launcher.cpp:265-271 | |
| 2.2.2.3.2 | E_HOOK_NOT_LOADED, E_NOT_IN_ALLOWLIST, E_SESSION_LOCKED | Shows only as the 60 s deadline; no policy; lock disabled | NOT IMPLEMENTED / N/A | LSW: host/embedded/scripts/tune-windows.cmd:46, :49 | |
| 2.2.2.4.1, 3.3.5.2.3.1 | SPI_SETDRAGFULLWINDOWS applied by the server | The host always sends "on", not its user's setting; the guest applies only "on" | DEVIATES | mirror_session.cpp:57, guest_session.cpp:54-62, :227-232 | §7.1 SysParam |
| 2.2.2.4.1 | SPI_SETKEYBOARDCUES, KEYBOARDPREF, CARETWIDTH | – | NOT IMPLEMENTED | – | Low |
| 2.2.2.4.1 | SPI_SETWORKAREA, RAIL_SPI_DISPLAYCHANGE, TASKBARPOS (client to server) | The guest sends the first two the other way | NOT IMPLEMENTED | – | Waits for R4; TASKBARPOS not needed |
| 2.2.2.4.1 | SPI_SETMOUSEBUTTONSWAP | – | N/A | – | The compositor swaps before buttons reach a client |
| 2.2.2.4.1-2, 4.4.1 | SPI_SETHIGHCONTRAST, TS_HIGHCONTRAST (HCF_*, ColorScheme) | – | NOT IMPLEMENTED | – | Portal `contrast`. 1 day |
| 2.2.2.4.5 | SPI_SETSTICKYKEYS, TS_STICKYKEYS (SKF_*, the 16 lock and latch bits) | – | NOT IMPLEMENTED | keyboard_events.cpp:122-127 | Latched host modifiers are not sent. 1 day |
| 2.2.2.4.3-4 | Filter keys, toggle keys | – | N/A | keyboard_events.cpp:129-139 | The compositor filters, the host repeats, the sound plays on the host |
| 2.2.2.4.1 | RAIL_SPI_DISPLAY_ANIMATIONS, ADVANCED_EFFECTS, AUTO_HIDE_SCROLLBARS, MESSAGE_DURATION | – | NOT IMPLEMENTED | – | Low |
| 2.2.2.4.1 | RAIL_SPI_CLOSED_CAPTION_* | – | N/A | – | Nothing to read them from on the host |
| 2.2.2.4.6, <15> | RAIL_SPI_ACCENT_COLOR, TS_ACCENTCOLOR | – | NOT IMPLEMENTED | – | Portal `accent-color`. 1-2 days |
| 2.2.2.4.1, <16>, <17> | SYSTEM_USES_LIGHT_THEME, APPS_USE_LIGHT_THEME | – | NOT IMPLEMENTED | – | Portal `color-scheme` → Personalize + ImmersiveColorSet. Half a day |
| 2.2.2.4.1 | Extended parameters only if advertised | None sent | CONFORMS | – | Trivially |
| 2.2.2.9.1, 3.2.5.2.6, 3.3.5.2.5.5, 3.3.7.1 | Language bar (TF_SFT_*) | Not advertised, not sent | NOT IMPLEMENTED (the MUST NOTs hold) | – | row 80; no language bar on Wayland |
| 2.2.2.10.1, 3.3.7.2 | Language profile (type, LanguageID, CLSID, ProfileGUID, HKL), sent on change, applied | – | NOT IMPLEMENTED | include/proto/key_stroke.hpp:9-15 | row 80. Host xkb layout → KLID → LoadKeyboardLayout + WM_INPUTLANGCHANGEREQUEST. 1-2 days |
| 2.2.2.10.2, 3.3.7.3 | Compartment status (ImeState, conversion and sentence modes, KANA) | – | NOT IMPLEMENTED | key_stroke.hpp:13-14 | Host IME text arrives as characters |
| 2.2.2.13.1, 3.2.5.2.11.1, 3.3.5.2.10.1 | Power display request | – | NOT IMPLEMENTED | – | row 84. Host: ScreenSaver.Inhibit or idle-inhibit; guest: no public API for display-required requests |
| 2.2.2.14.1, 3.2.5.2.12.1, 3.3.5.2.11.1 | Taskbar tab info (TAB_REGISTER…PROPERTIES) | Never sent | NOT IMPLEMENTED (the SHOULD NOTs hold) | – | row 31 |
| 2.2.2.15, 3.2.5.2.13, 3.3.5.2.12 | Text scale 100-225, caret blink; not advertised without the feature | Not advertised | NOT IMPLEMENTED | – | row 85. Half a day each |
| 5.1 | Security comes from RDP | Plain unauthenticated TCP; first connection wins; no Hello deadline | NONCONFORMANT | guest_link.cpp:64-105 | F5; review-and-redesign.md:76 |

### Activation, App ID, z-order, cloak

| Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|
| 2.2.2.6.1 | Activate Enabled nonzero/0 | u8 0/1, >1 refused | CONFORMS | source/proto/activate.cpp:13-18 | |
| 2.2.2.6.2 | SysMenu Left/Top, i16 screen | i32 relative to the frame, made screen and MAKELPARAM'd by the guest | DEVIATES | sys_menu.hpp:15, window_watch.cpp:313-318, window_commands.cpp:49-51 | §4.4 |
| 2.2.2.6.3 | The eight SC_* values only | Exactly those; others refused | CONFORMS | sys_command.hpp:19-28, sys_command.cpp:12-32 | row 62 |
| 2.2.2.8.1-2, 3.2.5.2.8.2-3, <22>, <23>, <33>, <34> | App ID response (≤ 520 B) and extended (pid, image); the client updates and caches them | APPID: AUMID UTF-8, pid, full image path, process tree; stored per window, drives MirrorScope and the manager's grouping | DEVIATES (format) / CONFORMS (use) | window_reader.cpp:322-339, mirror_scope.cpp:60-89 | The xdg app_id is the session's `--app-id`, not the AUMID (R4 not built) |
| 3.2.5.2.9.2 | Enabled=0 reaches the server when the window left has gone | Lost, or dropped as unknown | NONCONFORMANT | keyboard_events.cpp:51-55, window_watch.cpp:333-337 | F9 |
| 3.2.5.2.10.1-2, 3.3.5.2.9.1-2 | Cloak PDU both ways | CLOAKED field and SHOW=Hidden, server to client | NOT IMPLEMENTED / DEVIATES | window_reader.cpp:216-217, :237-238 | row 79, R§2.10 |
| 3.3.5.2.8.1 | Marker made; destroyed without z-order sync | Always made | CONFORMS | marker_window.cpp:80-103 | Vacuously |
| R§2.6 | IME windows sit with their owners in the z-order | No | NOT IMPLEMENTED | – | Part of row 85; harmless while the host does not stack by z-order |
| R§7.5 | Compositor dismissal closes the guest menu the way its kind closes | WM_CANCELMODE, CB_SHOWDROPDOWN FALSE, else Escape; skipped when the dismissing press was this application's | CONFORMS | window_commands.cpp:53-85, window_mirror.cpp:596-612 | |

### Move, resize, min/max, snap

| Section | Rule | remotewin | Verdict | Where | Notes |
|---|---|---|---|---|---|
| R§2.5 | idChild → kind; MinMax via WM_GETMINMAXINFO, 1 s, abort if hung, prefilled | Same table; SMTO_BLOCK added; a hung window gets the prefill | CONFORMS | move_size_report.cpp:46-52, min_max_reader.cpp:15-32 | |
| R§2.7 | MinMax at setup, after App ID, at each start; not for ghosts | Admission and each start; ghosts asked | DEVIATES (App ID) / undocumented (ghosts) | window_tracker.cpp:216-226 | See above |
| 3.2.5.2.7.2 | Unknown window, no press held, or press on another surface | Ignored, or left to Windows with input forwarded | CONFORMS | mirror_session.cpp:81, window_mirror.cpp:678-684 | |
| 3.2.5.2.7.2-3 | Stop forwarding during the local move; MUST resume | The compositor's grab holds the pointer; the button-up goes at once (DEVIATES, §6.5); no suppression survives | DEVIATES / CONFORMS | window_mirror.cpp:692-700, :806 | |
| 2.2.2.7.1 | MaxWidth/Height, MaxPosX/Y | Carried, not applied: the compositor decides the maximised geometry | DEVIATES | min_max_reader.cpp:23-31 | review Part 5 "Left" |
| 2.2.2.7.1 | Min and max track sizes | Applied each side, less margins; max held to the work area, never under the min | CONFORMS | picture_limits.cpp:25-36 | row 75 |
| 3.2.5.2.7.1 | The extents are those read as the resize starts | Admission-time limits for host-started resizes | NONCONFORMANT | window_tracker.cpp:222-225 | F10 |
| 2.2.2.7.2 | MoveSizeType 1-11, refused outside; PosX/Y for MOVE is the pointer offset | Same values, mapped to xdg edges; cursor minus frame corner | CONFORMS | move_size_kind.hpp:10-26, move_size_start.cpp:21, move_size_report.cpp:90-97 | Keyboard kinds: §6.5 |
| 2.2.2.7.4 | Window Move rectangle | WindowId and frame; a frame of no size refused | CONFORMS | source/proto/window_move.cpp:8-18 | Margins: row 73 |
| R§2.5 | A Window Move during KEYMOVE/KEYSIZE ends the loop and sends End | Not done; the host never moves keyboard kinds | DEVIATES | – | §6.5 |
| 3.3.5.2.6.5, 3.2.5.2.7.5 | Without snap: not advertised; Window Move instead, never both | Not advertised; a tiled configure is a size, sent as WindowMove | CONFORMS | hello.cpp:20-23, window_mirror.cpp:494-502 | Snap itself: row 77 |
| ShowState 3 → 2 → 3 (client) | Minimise a maximised window and back | The mirror is unmaximised while minimised | NONCONFORMANT | window_mirror.cpp:419-424, state_sync.cpp:33-38 | F6 |
| §7.8 | MoveRequests: one ask out, newest size and place together, unmoved edges kept, 250 ms overdue, 8 kept | As written | CONFORMS | move_requests.cpp:18-79 | |
