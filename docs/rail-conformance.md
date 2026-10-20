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

| Verdict | As found | After this branch |
|---|---|---|
| CONFORMS | 47 | 55 |
| DEVIATES on purpose | 18 | 25 |
| NONCONFORMANT | 16 | 1 |
| NOT IMPLEMENTED | 13 | 13 |
| **Rows** | **94** | **94** |

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
| 44 | 1.3.2.2 | Auto-reconnect resyncs. | The host exits when the guest disconnects, and LSW's manager starts the mirror again. SyncBegin::Reason::Resync is defined but never sent. | NOT IMPLEMENTED | source/host/main.cpp (disconnected) | Half a day if the host kept running |
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
| 55 | 3.2.5.2.5.1 | Activate is sent when a window is activated by means other than clicking it. A click goes as input. | A focus with no recent press in the mirror sends Activate; a press does not. | CONFORMS | window_mirror.cpp:857-870, activation_sync.cpp:23-35 | |
| 56 | 3.2.5.2.5.1, R§2.6 (client) | The client does not activate a window merely because it appeared. A RAIL client creates its windows without activating them, and Windows decides which is active. | **Was:** the compositor focuses a mirror as it maps it, and the host sent Activate for it. Every window an application showed without activating it (a panel, a palette) took the guest's foreground from the window Windows had made active, and Affinity's New Project dialog fell behind. **Now:** a surface's first focus within 500 ms of its mapping (FocusCause::Mapping) is not sent while the guest has another window active, and that window is followed instead. | NONCONFORMANT, fixed | source/host/mirror/focus_cause.hpp, window_mirror.cpp:864-870, activation_sync.cpp:23-35, mirror_session.cpp:220-224 | 4405ebc; integration map-keeps-guest-active, which fails before |
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
| 82 | 2.2.2.5.1, 3.3.5.2.4.1 | The server sends its system parameters. | WorkArea and Monitor (0xF001) at connect and on change. | CONFORMS | window_watch.cpp:352-360 | |
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
