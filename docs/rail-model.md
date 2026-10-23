# remotewin and RAIL: the model to copy

Written 2026-10-19 as research and a design; wire 6 has since been built
from it (§7 says what was built and where it departs from §4 and §5). It
redesigns remotewin's guest-host protocol and the guest half's window
decisions after Microsoft's RemoteApp protocol, RAIL (MS-RDPERP).

- §1 is RAIL's published message model.
- §2 is the decision rules of Windows' own RAIL server, reverse-engineered
  from the guest's binaries.
- §3 maps both onto remotewin's current wire.
- §4 proposes wire 6, a clean break.
- §5 lists the guest-half changes that produce it.
- §6 is the open questions, and what became of each.
- §7 is wire 6 as built.

docs/rail-conformance.md checks the built code against every rule here and
in MS-RDPERP, row by row.

Marks used throughout:

- **[S]** the published spec says it, with its section number.
- **[R]** reversed from a binary. The function and its address are given.
- **[I]** inferred. Not confirmed by code that was read.

**Sources.**

- [MS-RDPERP] v20260714 (14 July 2026). [MS-RDPBCGR], [MS-RDPEGDI] and
  [MS-RDPEGFX], current editions.
- The guest's own RAIL binaries, Windows 11 24H2 build 26100.7920 (March
  2026 servicing), with Microsoft's public PDBs. Each one is listed with its
  PDB id in the appendix.
- Decompilation: Ghidra 12.1, headless. Addresses are virtual addresses at
  the image's preferred base, 0x140000000.

The reversing was done for interoperability. This document paraphrases what
the code does. It reproduces none of it.

## §1 The RAIL message model (from the spec)

### 1.1 Transport and negotiation

RAIL rides an established RDP connection over two paths [S 1.3.1]:

- **Window, notification-icon and desktop state.** Server to client, as
  *Windowing Alternate Secondary Drawing Orders* inside the core
  drawing-order stream. Each order starts with the one-byte
  `ALTSEC_DRAWING_ORDER_HEADER`: class `TS_SECONDARY` (2 bits) and orderType
  `TS_ALTSEC_WINDOW` = 0x0B (6 bits) [MS-RDPEGDI 2.2.2.2.1.3.1.1]. Orders
  travel in fast-path or slow-path order updates [MS-RDPBCGR 2.2.9.1.2.1.2].
- **Everything else.** A static virtual channel named `RAIL`, both ways
  [S 2.2.2]. The channel PDU header must carry `CHANNEL_FLAG_SHOW_PROTOCOL`
  [S 1.5].

How a session becomes RAIL:

- The client asks for it with `INFO_RAIL` (0x00008000) in the Client Info
  PDU. It adds `INFO_HIDEF_RAIL_SUPPORTED` (0x02000000) to ask for
  *Enhanced RemoteApp* [S 3.2.5.1.3; MS-RDPBCGR 2.2.1.11.1.1].
- In Enhanced RemoteApp each window's pixels come as an RDPEGFX surface
  mapped to the window:
  - `RDPGFX_MAP_SURFACE_TO_WINDOW_PDU` [MS-RDPEGFX 2.2.2.20]: surfaceId
    u16, windowId u64, mappedWidth u32, mappedHeight u32.
  - `..._TO_SCALED_WINDOW_PDU` [2.2.2.23] adds targetWidth and
    targetHeight.
  - The client therefore always has a window's whole content, even where
    it is covered on the server [S 1.3.3]. This is the mode remotewin
    resembles.

Two capability sets go in Demand Active and Confirm Active [S 2.2.1.1]:

| Set | Type | Contents |
|---|---|---|
| Remote Programs | `CAPSTYPE_RAIL` 0x0017 | `RailSupportLevel` bits: SUPPORTED 0x1, DOCKED_LANGBAR 0x2, SHELL_INTEGRATION 0x4 (tabs, overlay icons), LANGUAGE_IME_SYNC 0x8, SERVER_TO_CLIENT_IME_SYNC 0x10, HIDE_MINIMIZED_APPS 0x20, WINDOW_CLOAKING 0x40, HANDSHAKE_EX 0x80 |
| Window List | `CAPSTYPE_WINDOW` 0x0018 | `WndSupportLevel` 0 none, 1 supported, 2 SUPPORTED_EX (adds the CLIENTAREASIZE, RPCONTENT and ROOTPARENT fields); `NumIconCaches` u8; `NumIconCacheEntries` u16 |

Rules for the sets:

- The client reports the minimum of its own icon-cache values and the
  server's [S 3.2.5.1.4].
- Either side drops the connection if the other's sets are missing or
  report level 0 [S 3.2.5.1.5, 3.3.5.1.5].

### 1.2 Window Information orders [S 2.2.1.3.1]

Every window order begins with `TS_WINDOW_ORDER_HEADER` (11 bytes): the
altsec header, OrderSize u16, FieldsPresentFlags u32 and WindowId u32. The
server generates the id, unique per window per session. Windows uses the
HWND [R, §2.1].

**New or existing window** [2.2.1.3.1.2.1]. Type flags:

- `WINDOW_ORDER_TYPE_WINDOW` 0x01000000 is always set.
- `WINDOW_ORDER_STATE_NEW` 0x10000000 marks a new window. Without it, the
  order is a delta for an existing one.

Fields, in wire order, each present only if its flag is set:

| Flag | Value | Fields |
|---|---|---|
| OWNER | 0x00000002 | OwnerWindowId u32 |
| STYLE | 0x00000008 | Style u32, ExtendedStyle u32 |
| SHOW | 0x00000010 | ShowState u8: 0 hidden, 2 minimized, 3 maximized, 5 shown |
| TITLE | 0x00000004 | TitleInfo, UNICODE_STRING ≤ 520 bytes |
| CLIENTAREAOFFSET | 0x00004000 | ClientOffsetX/Y i32 (screen) |
| CLIENTAREASIZE | 0x00010000 | ClientAreaWidth/Height u32 (EX only; Windows never sets it [S <3>]) |
| RESIZE_MARGIN_X | 0x00000080 | Left/Right resize margin u32 |
| RESIZE_MARGIN_Y | 0x08000000 | Top/Bottom resize margin u32 |
| RPCONTENT | 0x00020000 | u8 (never set by Windows [S <4>]) |
| ROOTPARENT | 0x00040000 | u32 (never set by Windows [S <5>]) |
| WNDOFFSET | 0x00000800 | WindowOffsetX/Y i32 (screen) |
| CLIENTDELTA | 0x00008000 | WindowClientDeltaX/Y i32 (window to client) |
| WNDSIZE | 0x00000400 | WindowWidth/Height u32 |
| WNDRECTS | 0x00000100 | NumWindowRects u16, TS_RECTANGLE_16[] (window-relative shape) |
| VISOFFSET | 0x00001000 | VisibleOffsetX/Y i32 (equal to WNDOFFSET since Windows 8 [S <6>, <7>]) |
| VISIBILITY | 0x00000200 | NumVisibilityRects u16, TS_RECTANGLE_16[] |
| OVERLAY_DESCRIPTION | 0x00400000 | UNICODE_STRING |
| ICON_OVERLAY_NULL | 0x00200000 | the overlay icon was removed |
| TASKBAR_BUTTON | 0x00800000 | u8: 0 add a tab to the group, 1 remove it |
| ENFORCE_SERVER_ZORDER | 0x00080000 | u8: keep this window glued in the server's order |
| APPBAR_STATE | 0x00000040 | u8 registered or not |
| APPBAR_EDGE | 0x00000001 | u8: 0 left, 1 top, 2 right, 3 bottom |

Rules for these fields:

- The window bounds (offset and size) cover only the visible window. The
  resize margins lie outside them [S 1.3.4, 3.2.5.1.6].
- Client Window Move and Snap PDUs *include* the margins. The client
  therefore must not expect a reported rect to equal the one it sent
  [S 3.2.5.1.6].
- A delta for an unknown WindowId is ignored [S 3.2.5.1.6].

**Window Icon** [2.2.1.3.1.2.2]:

- Flag `WINDOW_ORDER_ICON` 0x40000000, plus `ICON_BIG` 0x2000 or
  `ICON_OVERLAY` 0x00100000.
- The body is `TS_ICON_INFO`:
  - CacheEntry u16, CacheId u8 (0xFF means do not cache), Bpp u8 (1, 4, 8,
    16, 24 or 32), Width u16, Height u16;
  - CbColorTable (only for 1, 4 and 8 bpp), CbBitsMask, CbBitsColor;
  - then the mask, the color table and the color bits.
- Icons are limited to 32×32 unless the client set HIGH_DPI_ICONS (96×96).

**Cached Icon** [2.2.1.3.1.2.3]: flag `WINDOW_ORDER_CACHEDICON` 0x80000000,
body CacheEntry u16 and CacheId u8. The client's SUPPRESS_ICON_ORDERS flag
stops both icon orders.

**Deleted Window** [2.2.1.3.1.2.4]: flag `WINDOW_ORDER_STATE_DELETED`
0x20000000 and nothing else.

### 1.3 Notification icon orders [S 2.2.1.3.2]

The header is `TS_NOTIFYICON_ORDER_HEADER` (15 bytes): altsec header,
OrderSize, FieldsPresentFlags, WindowId (the owning window) and NotifyIconId
(the application's uID).

New or existing icon, with `WINDOW_ORDER_TYPE_NOTIFY` 0x02000000 and
optionally STATE_NEW:

| Flag | Value | Field |
|---|---|---|
| NOTIFY_VERSION | 0x8 | Version u32 (0, 3 or 4) |
| NOTIFY_TIP | 0x1 | ToolTip UNICODE_STRING |
| NOTIFY_INFO_TIP | 0x2 | `TS_NOTIFY_ICON_INFOTIP`: Timeout u32 ms, InfoFlags u32 (NIIF_*), InfoTipText ≤ 510 bytes, Title ≤ 126 bytes |
| NOTIFY_STATE | 0x4 | State u32 (1 hidden) |
| WINDOW_ORDER_ICON | 0x40000000 | TS_ICON_INFO |
| WINDOW_ORDER_CACHED_ICON | 0x80000000 | TS_CACHED_ICON_INFO |

A new icon carries exactly one of Icon or CachedIcon. A deleted icon is
TYPE_NOTIFY plus STATE_DELETED [2.2.1.3.2.2.2]. Windows clamps the balloon
timeout to 10–30 s [S <11>].

### 1.4 Desktop orders [S 2.2.1.3.3]

The header is `TS_DESKTOP_ORDER_HEADER` (7 bytes), with
`WINDOW_ORDER_TYPE_DESKTOP` 0x04000000.

| Flag | Value | Meaning / field |
|---|---|---|
| DESKTOP_NONE | 0x01 | Non-Monitored Desktop: the server stops reporting (secure desktop); the client drops everything |
| DESKTOP_HOOKED | 0x02 | the server reports the current input desktop |
| DESKTOP_ARC_BEGAN | 0x08 | start of a full synchronization (needs HOOKED); the client drops everything |
| DESKTOP_ARC_COMPLETED | 0x04 | end of synchronization; anything not resent is gone |
| DESKTOP_ACTIVEWND | 0x20 | ActiveWindowId u32: the window the client should activate |
| DESKTOP_ZORDER | 0x10 | NumWindowIds u8, WindowIds u32[] top to bottom |

The synchronization sequence [S 1.3.2.3, 3.3.5.1.8]:

1. On connect or reconnect: ARC_BEGAN|HOOKED, then every window and icon
   order, then ARC_COMPLETED.
2. On a desktop switch: NONE, then HOOKED once the new desktop can be
   monitored.
3. Afterwards, an order with ZORDER whenever the number or order of
   top-level windows changes, and one with ACTIVEWND whenever the active
   window changes.

### 1.5 RAIL virtual channel PDUs [S 2.2.2]

Every PDU starts with `TS_RAIL_PDU_HEADER`: orderType u16 and orderLength
u16 [2.2.2.1]. C→S is client to server, S→C server to client.

| orderType | PDU | Dir | Section | Fields |
|---|---|---|---|---|
| 0x0005 | Handshake | both | 2.2.2.2.1 | buildNumber u32 |
| 0x0013 | HandshakeEx | S→C | 2.2.2.2.3 | buildNumber u32, railHandshakeFlags u32: HIDEF 0x1, EXTENDED_SPI 0x2, SNAP_ARRANGE 0x4, TEXT_SCALE 0x8, CARET_BLINK 0x10, EXTENDED_SPI_2 0x20, EXTENDED_SPI_3 0x40 |
| 0x000B | Client Information | C→S | 2.2.2.2.2 | Flags u32: ALLOWLOCALMOVESIZE 0x1, AUTORECONNECT 0x2, ZORDER_SYNC 0x4, WINDOW_RESIZE_MARGIN 0x10, HIGH_DPI_ICONS 0x20, APPBAR_REMOTING 0x40, POWER_DISPLAY_REQUEST 0x80, BIDIRECTIONAL_CLOAK 0x200, SUPPRESS_ICON_ORDERS 0x400 |
| 0x0001 | Client Execute | C→S | 2.2.2.3.1 | Flags u16 (EXPAND_WORKINGDIRECTORY 0x1, TRANSLATE_FILES 0x2, FILE 0x4, EXPAND_ARGUMENTS 0x8, APP_USER_MODEL_ID 0x10), lengths, ExeOrFile ≤ 520 bytes, WorkingDir ≤ 520 bytes, Arguments ≤ 16000 bytes |
| 0x0080 | Server Execute Result | S→C | 2.2.2.3.2 | Flags u16 (echoed), ExecResult u16 (0 OK, 1 HOOK_NOT_LOADED, 2 DECODE_FAILED, 3 NOT_IN_ALLOWLIST, 5 FILE_NOT_FOUND, 6 FAIL, 7 SESSION_LOCKED), RawResult u32 (Win32 error), ExeOrFile echoed |
| 0x0003 | Client System Parameters | C→S | 2.2.2.4.1 | SystemParam u32 + body: SPI_SETDRAGFULLWINDOWS, KEYBOARDCUES, KEYBOARDPREF, MOUSEBUTTONSWAP (1 byte each); SPI_SETWORKAREA, RAIL_SPI_DISPLAYCHANGE 0xF001, RAIL_SPI_TASKBARPOS 0xF000 (TS_RECTANGLE_16); SETHIGHCONTRAST (TS_HIGHCONTRAST); with EXTENDED_SPI: CARETWIDTH, STICKY, TOGGLE and FILTER keys; with _2 and _3: animations, transparency, auto-hide scrollbars, message duration, closed-caption settings, RAIL_SPI_ACCENT_COLOR 0xF00F, SYSTEM_USES_LIGHT_THEME 0xF010, APPS_USE_LIGHT_THEME 0xF011 |
| 0x0003 | Server System Parameters | S→C | 2.2.2.5.1 | SPI_SETSCREENSAVEACTIVE, SPI_SETSCREENSAVESECURE (1 byte) |
| 0x0002 | Client Activate | C→S | 2.2.2.6.1 | WindowId u32, Enabled u8 |
| 0x000C | Client System Menu | C→S | 2.2.2.6.2 | WindowId u32, Left i16, Top i16 (screen) |
| 0x0004 | Client System Command | C→S | 2.2.2.6.3 | WindowId u32, Command u16: SC_SIZE, MOVE, MINIMIZE, MAXIMIZE, CLOSE, KEYMENU, RESTORE, DEFAULT |
| 0x0006 | Client Notify Event | C→S | 2.2.2.6.4 | WindowId u32, NotifyIconId u32, Message u32 (WM_L/RBUTTONDOWN/UP/DBLCLK, WM_CONTEXTMENU, NIN_SELECT, KEYSELECT, BALLOONSHOW, HIDE, TIMEOUT, USERCLICK) |
| 0x000E | Client Get Application ID | C→S | 2.2.2.6.5 | WindowId u32 |
| 0x000F | Server Get AppID Response | S→C | 2.2.2.8.1 | WindowId u32, ApplicationId 520 bytes |
| 0x0018 | Server Get AppID Response Ex | S→C | 2.2.2.8.2 | adds ProcessId u32 and ProcessImageName 520 bytes |
| 0x000A | Server Min Max Info | S→C | 2.2.2.7.1 | WindowId, MaxWidth/Height, MaxPosX/Y, MinTrack W/H, MaxTrack W/H (all i16) |
| 0x0009 | Server Move/Size Start | S→C | 2.2.2.7.2 | WindowId, IsMoveSizeStart u16 ≠ 0, MoveSizeType u16 (RAIL_WMSZ_LEFT 1 … BOTTOMRIGHT 8, MOVE 9, KEYMOVE 10, KEYSIZE 11), PosX/Y i16 (pointer offset inside the window for MOVE; last button-down point otherwise) |
| 0x0009 | Server Move/Size End | S→C | 2.2.2.7.3 | WindowId, IsMoveSizeStart 0, MoveSizeType, TopLeftX/Y i16 |
| 0x0008 | Client Window Move | C→S | 2.2.2.7.4 | WindowId, Left/Top/Right/Bottom i16 (margins included) |
| 0x0017 | Client Window Snap | C→S | 2.2.2.7.5 | same as Window Move; the server must restore the size on unsnap |
| 0x000D | Language Bar Information | both | 2.2.2.9.1 | LanguageBarStatus u32 (TF_SFT_*) |
| 0x0011 | Language Profile Information | C→S | 2.2.2.10.1 | ProfileType u32, LanguageID u16, LanguageProfileCLSID GUID, ProfileGUID GUID, KeyboardLayout u32 |
| 0x0012 | Compartment Status | both | 2.2.2.10.2 | ImeState, ImeConvMode, ImeSentenceMode, KANAMode (u32 each) |
| 0x0014 | Z-Order Sync Information | S→C | 2.2.2.11.1 | WindowIdMarker u32 |
| 0x0015 | Window Cloak State Change | both | 2.2.2.12.1 | WindowId u32, Cloaked u8 |
| 0x0016 | Power Display Request | S→C | 2.2.2.13.1 | Active u32 |
| 0x0010 | Taskbar Tab Info | S→C | 2.2.2.14.1 | TaskbarMessage u32 (REGISTER, UNREGISTER, ORDER, ACTIVE, PROPERTIES), WindowIdTab u32, Body u32 |
| 0x0019 | Text Scale Information | C→S | 2.2.2.15.1 | TextScaleFactor u32, 100–225 |
| 0x001A | Caret Blink Information | C→S | 2.2.2.15.2 | CaretBlinkRate u32 |

There are newer messages than the spec has. RemoteApp V2 is a separate
protocol that this build already contains but which is not published. It
runs over a dynamic channel, and its client and server negotiate **named
features**, a string-to-bool map. The names seen:

- RemoteAppV2, LocalMoveSize, ZOrderSync, WindowResizeMargin, HighDpiIcons;
- AppBars, PowerDisplayRequest, AppIdResponse, BidirectionalCloak,
  SuppressIconOrders;
- settings sync such as AccentColor and Colorization*.

[R `RailProtocolHandler::V2_HandlePacket`,
`CRailCapsFromClient::CheckFeature` @1400255ac.] Its window messages
(`V2_OnWindowActionPdu`, `V2_SendShowInTaskSwitchersPdu`,
`SendRequestResendRecalc`, `SendWindowSyncBegin/End`) were not decoded byte
for byte (§2.11).

### 1.6 What the server must send, and when

- **Handshake.** The Handshake or HandshakeEx comes before any other PDU.
  Neither side processes PDUs until it has the other's. A missing handshake
  drops the connection after a timeout, 30 s in Windows [S 3.1.5, <28>].
  Build numbers are logged, never enforced [S <29>, <32>].
- **After the handshake.** The client sends Client Information, then System
  Parameters (at every connect, and on every change), then Language Bar
  if supported. The server sends its System Parameters [S 1.3.2.4,
  3.2.5.2.3.1, 3.3.5.2.4.1].
- **Z-order sync.** If the client sets ZORDER_SYNC, the server creates a
  *marker window* that is never remoted, and sends its id in Z-Order Sync
  Information. If the client does not, the server destroys it [S 3.3.1.3,
  3.3.5.2.8].
- **Window and desktop orders.** These follow the sequences in §1.4.
- **Local move/size** [S 1.3.2.5, 3.3.5.2.6]. When the forwarded input
  starts a move or resize on the server:
  1. The server sends Min Max Info, then Move/Size Start.
  2. The client runs the move locally and stops forwarding input.
  3. At the end the client forwards the button-up. For keyboard moves and
     all resizes it also sends a Window Move.
  4. The server answers with Move/Size End, carrying the final position.
- **Cloak** [S 3.3.5.2.9]:
  - The server sends a Cloak PDU when a window cloaks, unless the cloak was
    caused by the client's own Cloak PDU. It never echoes.
  - Without BIDIRECTIONAL_CLOAK the spec says to send Deleted Window and New
    Window instead. Windows actually sends a show-state change (§2.10).
- **Exec, App ID, language and accessibility.** Exec, App ID and Language
  Bar messages are answered when received. Power Display Request, Taskbar
  Tab Info, Text Scale and Caret Blink are sent only if negotiated.

## §2 Windows' RAIL server, reversed

### 2.0 Who does what

| Binary | Role | Main classes |
|---|---|---|
| `rdpinit.exe` | The session's shell in a RemoteApp session (Explorer does not run). Owns the tray (a cloaked `Shell_TrayWnd`), the appbar manager, the marker window, the cloak sync, and the RPC server rdpshell calls into | `CRdpTray`, `CShellNotifyManager`, `CAppBarManager`, `CloakSyncComponent`, `CMarkerWindow`, `CPrivilegedPresentationOperations` |
| `rdpshell.exe` | Window tracker and RAIL encoder. Per-monitor DPI aware and **UIAccess** by manifest | `CHook` (hook installation), `CHookEventProcessor` (event logic, V1), `CHookWindow` (one tracked window), `HookZOrderSync` (z-order and activation), `RailWindowManager` (V2), `RailOrderEncoder`, `RailProtocolHandler` |
| `RdpShellExt.dll`, `RdpShellLoader.dll` | The same tray, cloak and window components, hosted inside Explorer for RemoteApp V2 | as rdpinit and rdpshell |
| `rdpcorets.dll`, `termsrv.dll`, `rdpserverbase.dll`, `rdpbase.dll` | The RDP stack and channels. They carry RAIL but make none of its window decisions | – |
| `win32kfull.sys` | Raises the WinEvents everything above reacts to | `xxxMoveSize` |

The window logic is in user mode. A legacy kernel path exists (the
`KM_*` encoders, `SendToDD`), but it applies only when user-mode
encoding is off [R `RdpShellInstance::IsUserModeEncodingEnabled`].

All window events run on **one worker queue**, `AsyncTaskQueue`, in arrival
order. Every call into another process is bounded by a timeout
(`SendMessageTimeout` with SMTO_ABORTIFHUNG|SMTO_BLOCK and 1 s for
`WM_GETMINMAXINFO`; 20 s for `WM_GETTEXT`). A hung application therefore
never stalls the tracker. [R `CHookEventProcessor::OnHookEventAsync`
@140071374, `SendWindowMinMaxInfo` @1400742d0,
`CHookWindow::UpdateWindowTitle` @14007e9b0.]

### 2.1 Which windows it reports, and which it hides (a)

**Admission.** `IHookWindow::CreateInstance` @14007b590 accepts a window
when:

- `IsWindow` is true;
- its style can be read;
- it is top-level: `GetAncestor(GA_PARENT)` is the desktop window, or the
  window has `WS_POPUP`.

Child windows and message-only windows are refused. **Nothing else is
filtered:**

- no process or process-tree check;
- no tool-window, menu, tooltip, layered or transparency check;
- no zero-size check.

Menus (`#32768`), tooltips, the combo box's desktop-child list, IME windows
and owned popups all reach the client as RAIL windows. The client decides
how to present each from its style, ex-style and owner.

**When a window becomes known.** `SendWindowSetupMessages` @140074820 runs
the first time any of these events finds an unknown window that
`GetEffectiveWindowVisibility` @14006eea0 calls visible: show, location
change, focus or foreground. The window counts as visible if either holds:

- `IsWindowVisible`;
- the window carries the `SysSetRedraw` property, which user32 sets while
  WM_SETREDRAW(FALSE) has cleared WS_VISIBLE. A redraw-suspended window
  is still a shown window.

**Session scope instead of process scope.** The scope is the session's
input desktop. rdpshell opens it by name and hooks it [R
`CHook::Initialize` @14004c048]. A RemoteApp session contains only the
remote applications and rdpinit, so "every top-level window" means "the
application's windows". RAIL has no notion of which program a window
belongs to beyond the App ID (§2.9).

**Exclusions and special cases:**

- **The marker window.** Compared by handle and never admitted
  [R `SendWindowSetupMessages`].
- **Cloaked windows.** Admitted, but marked *hidden from client*
  (`SetHideFromClient`). Their show state goes out as 0 until they
  uncloak [R `CHookWindow::CHookWindow` @14007aa04,
  `UpdateShowCmdInternal` @14007d434].
- **Hidden windows.** `EVENT_OBJECT_HIDE` does **not** delete. The window
  stays tracked with ShowState 0, and only `EVENT_OBJECT_DESTROY` deletes
  it. The one exception: RichEdit dropdowns (`REComboBox20W`,
  `REListBox20W`) are deleted on hide [R `OnHookEventHide` @140071d58,
  `IsRichEditControl`].
- **Windows that become children.** A window that gets WS_CHILD and a
  non-desktop parent is deleted. The parent-change path spares class
  `WBPOPUP` [R `OnHookEventLocationChange` @140072024,
  `OnHookEventParentChange` @1400729f8].
- **Office border windows.** `MSO_BORDEREFFECT_WINDOW_CLASS` windows (the
  frame-shadow windows Office keeps around its main window):
  - outside Enhanced RemoteApp they are disabled;
  - in Enhanced RemoteApp they are reported with
    `EnforceServerZOrder = 1`, so the client keeps them glued to their
    neighbours in z-order.
  [R `InternalHookEvent` @14006f790, `DisableBorderWindow`,
  `UM_SendWindowOrder` @1400981f0.]
- **Hung windows.** For a hung window, Windows' ghost replacement is shown
  in place of the hung window and back again. App ID and min/max lookups
  fall back from the ghost to the hung window [R `OnHookEventHide`,
  `WindowGetAppIDOrder` @140076b60].
- **Limit.** At most 256 tracked windows, and at most 256 entries in a
  z-order list [R `SendWindowSetupMessages`,
  `EnumWindowsFullZOrderProc` @14007fdec].
- **Minimized windows.** Reported with ShowState 2, never hidden. When the
  client supports HIDE_MINIMIZED_APPS, the server sets
  `SPI_SETMINIMIZEDMETRICS` with `ARW_HIDE`. A minimized window then parks
  off-screen instead of as a stub at the bottom-left [R
  `CShellSettings::SetHideMinimizedApps` @14004b5d4].
- **Shell-managed (immersive) windows.** Admitted. Only their taskbar
  presence changes (§2.3).

**Session emptiness** (when RAIL may log the session off) is judged
separately by `DoesAnyVisibleWindowExist` @14006e83c. A tracked window
counts as long as it is all of these:

- effectively visible;
- of non-empty rectangle;
- not cloaked, unless it expects a taskbar icon;
- not the language bar (`CiceroUIWndFrame`).

### 2.2 How it finds windows and their changes, and delta versus full (b)

**Hooks.** `CHook::CommandStart` @14004baf0 installs eight out-of-context
`SetWinEventHook` ranges over all processes and threads. The table is at
0x1400cd1d0. The events, and what each one does:

| Event | Effect |
|---|---|
| 0x0003 EVENT_SYSTEM_FOREGROUND | foreground change: z-order and active window |
| 0x8001–0x8003 OBJECT_DESTROY/SHOW/HIDE | delete / admit or show / hide |
| 0x8005 OBJECT_FOCUS | admit if unknown, refresh geometry |
| 0x800A–0x800C STATECHANGE/LOCATIONCHANGE/NAMECHANGE | refresh style, show state, geometry, owner, title |
| 0x000B SYSTEM_MOVESIZEEND | local move/size end |
| 0x0019 *undocumented* | local move/size start, idChild = kind (§2.5) |
| 0x800F OBJECT_PARENTCHANGE | owner change or becoming a child |
| 0x8017–0x8018 OBJECT_CLOAKED/UNCLOAKED | cloak sync |

The hook rules:

- Only events with `idObject == OBJID_WINDOW` and a non-null HWND are
  processed [R `InternalHookEvent`].
- 0x0B and 0x19 are dropped unless both sides support local move/size
  [R `CHook::HookProc` @14004bf6c].
- rdpshell also registers a `SHELLHOOK` window (`RegisterShellHookWindow`)
  [R `CHook::Initialize`].

**The initial sync** is an `EnumWindows` pass that runs the same admission
logic, bracketed by the desktop ARC_BEGAN and ARC_COMPLETED orders. The
active window is then sent [R `CompleteInitialWindowSync` @14006e504,
`ShowActiveWindow`].

**Every event recomputes the whole record.** Each handler calls:

- `UpdateWindowStyle`;
- `UpdateShowCmd`;
- `UpdateWindowGeometry`;
- `UpdateWindowOwner` (when GW_OWNER changed);
- then `SendWindowChangesToClient`.

**Delta versus full.** `CHookWindow::PopulateWindowProperties` @14007c514
compares the current property set with a snapshot of what was last sent:

- **New window.** `SendNewWindowToClient` @14007cae0 fills every field.
- **Update.** Only the fields that differ are filled, and an empty delta
  is not sent [R `SendWindowChangesToClient` @14007cd30,
  `RailWindowOrderProperties::IsEmpty`].
- **Geometry is all or nothing.** If any geometry field changed (window
  offset, size, client offset, client delta or region), all of them are
  sent together. The exception is a client that negotiated partial
  geometry (V2 only) [R `IsPartialGeometrySupported` @1400916d0].
- **Icons** are compared and sent separately, through an icon cache.

### 2.3 Owner, style, ex-style and taskbar presence (c)

**Owner** [R `CHookWindow::GetWindowOwnerToTransmit` @14007babc]:

- The owner comes from `GW_OWNER` alone. RAIL does **not** infer owners
  from `GUITHREADINFO`, capture or menu state.
- An owner that is a child window is replaced by its `GA_ROOT`.
- An owner that is the desktop is sent as none (0xFFFFFFFF).
- Unless the policy `fTransmitHiddenOwnerWindows` = 1, an owner that is
  invisible, minimized with an empty normal rectangle, or has a (0,0,0,0)
  window rectangle is sent as the sentinel **0xFFFFFFFE, "hidden owner"**.

The sentinel lets the client treat a window whose owner is the usual
hidden top-level helper as a window of its own, with a taskbar button,
instead of as a child of something it never sees.

**Style and ex-style are masked** [R `CHookWindow::UpdateWindowStyle`
@14007e7c0]:

- **Style.** Sent as `style & 0xC80F0000`: WS_POPUP, WS_CHILD,
  WS_DISABLED, WS_SYSMENU, WS_THICKFRAME, WS_MINIMIZEBOX and
  WS_MAXIMIZEBOX. Caption, border, visible, minimize and maximize bits are
  not sent. Show state carries the last three.
- **Ex-style.** Sent as `exstyle & 0x080C3088`: WS_EX_NOACTIVATE,
  WS_EX_LAYERED, WS_EX_APPWINDOW, WS_EX_RTLREADING, WS_EX_RIGHT,
  WS_EX_TOOLWINDOW and WS_EX_TOPMOST.
- **Layered.** WS_EX_LAYERED is cleared outside Enhanced RemoteApp,
  except for Windows Media Player's skin host ("WMP Skin Host").
- **Toolwindow forced on.** WS_EX_TOOLWINDOW is *forced on* when the window
  may not have a taskbar icon (below). The client's ordinary rule then
  keeps it off the taskbar.

**Taskbar presence.** Windows' own taskbar rule is evaluated server-side
[R `ExpectTaskbarIconOnClient` @14008f194,
`CHookWindow::ExpectTaskbarIconOnClient` @14007b7b0]. A window expects a
taskbar icon when all of these hold:

- it is `IsWindowVisible`;
- it has no `GW_OWNER`, or has `WS_EX_APPWINDOW`;
- it does not have `WS_EX_TOOLWINDOW`;
- *taskbar allowed*.

*Taskbar allowed* [R `CHookWindow::UpdateTaskbarIconAllowed` @14007d500]
is true unless both of these hold:

- user32's `IsShellManagedWindow` (ordinal 2574) says the window is
  shell-managed, and the window lacks the
  `Microsoft.Windows.ShellManagedWindowAsNormalWindow` property;
- its `IApplicationView` (from `CLSID_ImmersiveShell` →
  `SID_ApplicationViewManager` → `GetViewForHwnd`) says
  `GetShowInSwitchers` = false, or it has no view. The service id asked
  for is `IApplicationViewCollection`'s IID,
  1841c6d7-4f9d-42c0-af41-8747538f10e5; `GetViewForHwnd` is its vtable
  slot 6 (offset 0x30), and `GetShowInSwitchers` the view's slot 27
  (offset 0xd8). IsShellManagedWindow is user32 ordinal 2574,
  `int (HWND)`, looked up once.

The result reaches the client in two ways:

- through the ex-style, as the forced WS_EX_TOOLWINDOW above;
- in V2, through the separate ShowInTaskSwitchers and AllowInTaskSwitchers
  PDUs [R `RailWindowManager::ResyncShowOnTaskbarIfNecessary`].

`TaskbarButton` and the Taskbar Tab PDUs cover only tabbed applications.

**Show state** [R `UpdateShowCmdInternal` @14007d434]:

- 0 while hidden from the client (cloaked, or hidden on the server);
- otherwise, from the style when WS_VISIBLE: WS_MAXIMIZE → 3,
  WS_MINIMIZE → 2, anything else → 5;
- a hide event forces 0.

**Title** [R `CHookWindow::UpdateWindowTitle` @14007e9b0]:

- read with `SendMessageTimeout(WM_GETTEXT, 20 s)` on the worker;
- the language bar gets `TF_FloatingLangBar_WndTitle`.

**Icons** [R `CHookWindow::LoadWindowIcon` @1400847f8]:

- small and big icons, at most 32 px, or 96 px with HIGH_DPI_ICONS;
- sent through the icon cache.

### 2.4 Window rect, client rect, visibility region, DWM frames and DPI (d)

[R `CHookWindow::UpdateWindowGeometry` @14007da20, `GetWindowRgn`
@14007bdc4, `GetMonitorClippingRect` @14007b8d0]

- **Units.** rdpshell is per-monitor DPI aware (manifest
  `dpiAware=true/PM`), so every coordinate is in physical pixels of the
  session's single virtual screen. That screen *is* the client's monitor
  layout: the core display-control channel sizes it. No DPI scaling
  happens in RAIL itself. `Win32DpiApi::GetScaleFactorForMonitor` is used
  only to describe monitor topology in V2.
- **Window rect.** `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`,
  the visible frame without the invisible resize borders and shadow. It
  falls back to `GetWindowInfo().rcWindow`. There is one exception. In an
  Enhanced RemoteApp session where `AreWindowOrdersSupported`, a
  `WS_THICKFRAME` window is reported with the full `rcWindow`, invisible
  borders included.
- **Client rect.** `rcClient`, shifted by the difference between rcWindow
  and the frame bounds. It is sent as a delta from the window's corner,
  clamped to ≥ 0, plus a screen offset.
- **Window shape (WindowRects).** The window's own region when
  `GetWindowRgnBox` reports one (SetWindowRgn). Otherwise the single
  rectangle (0,0)–(frame width, frame height).
- **Visibility region.** *Identical to the window shape*, and
  VisibleOffset is identical to WindowOffset. The encoder writes the same
  rectangles under both flags [R `UM_SendWindowOrder`]. No occlusion
  clipping is done, which matches [S <6>].
- **Maximized windows** (WS_MAXIMIZE, in Enhanced RemoteApp) are clipped
  to their monitor's `rcMonitor`. The overhanging borders of a maximized
  window are cut away, and the client offset and region are shifted to
  match.
- **Resize margins** (client supports WINDOW_RESIZE_MARGIN). For a
  WS_THICKFRAME window whose DWM frame lies strictly inside rcWindow, the
  four margins are `frame − rcWindow` per side, that is, the invisible
  resize borders. They are reported separately so the client can make a
  transparent hit-testable band and send resize input back.
- **A conflict with the spec.** In the Enhanced RemoteApp case above, the
  margins therefore describe a band *inside* the reported rcWindow. The
  spec says margins lie outside the bounds [S 1.3.4]. The code was read
  correctly, but which reading the client applies was not pinned down
  (§6).

### 2.5 Local move/size (e)

**Detection.** rdpshell does not use `EVENT_SYSTEM_MOVESIZESTART`. It
uses event **0x19**, which `win32kfull!xxxMoveSize` (0x14024af80) raises
immediately after 0x0A MOVESIZESTART in every move/size loop, with idChild
set to the loop's kind. `CHookEventProcessor::GetRailMovesizeTypeFromChildId`
@1400831b8 maps idChild to the kind:

| idChild | Kind |
|---|---|
| 0 | KEYSIZE |
| 1–8 | LEFT, RIGHT, TOP, TOPLEFT, TOPRIGHT, BOTTOM, BOTTOMLEFT, BOTTOMRIGHT |
| 9 | MOVE |
| 10 | KEYMOVE |

The end is 0x0B MOVESIZEEND. `xxxMoveSize` also raises the undocumented
0x80000004 before the start pair and 0x80000005 before the end.

**Start** [R `OnHookEventMoveSizeStartEx` @140072614, `SendMoveSizePdu`
@140083a84]:

1. Record the kind for the window.
2. Send Min Max Info, from WM_GETMINMAXINFO through SendMessageTimeout with
   1 s and SMTO_ABORTIFHUNG, prefilled with the SM_CXMAXTRACK and
   SM_CXMINTRACK system metrics.
3. Send Move/Size Start. Its position depends on the kind:
   - for MOVE: the cursor's offset from the DWM frame's top-left;
   - for every other kind: the last mouse button-down point the RDP stack
     recorded (`WinStationGetConnectionProperty`,
     `PROPERTY_TYPE_MBUTTON_DOWN_POSITION`), with the left and right
     buttons swapped under SM_SWAPBUTTON.

**During the move.** rdpshell does nothing. The server's own modal loop is
**left running**. It does not fight the client because the client stops
forwarding input [S 3.2.5.2.7.2]: the loop sees no mouse motion and moves
nothing.

**End.**

- *Mouse kinds.* The client's forwarded button-up ends the server loop,
  whose 0x0B event makes rdpshell send Move/Size End with the DWM frame's
  top-left. It then refreshes geometry and sends the window delta [R
  `OnHookEventMoveSizeEnd` @1400724b8, `OnMoveSizeEnd` @140073488].
- *Client Window Move* [R `WindowMoveSizeOrder` @140083e80,
  `ApplyWindowMoveSize` @140082fb0]:
  1. `MoveWindow(hwnd, l, t, r−l, b−t, repaint)`. The rect is the
     client's, margins included, so it lands on the full rcWindow.
  2. If the recorded kind is KEYMOVE or KEYSIZE, which no button-up ends,
     the server ends the move itself and sends Move/Size End.

**Opt-out.** Processes named under
`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Terminal Server\Compatibility\LocalMoveSizeIgnoreList`
never get local move/size [R `RailWindowManager::CacheIgnoreList`
@14005feec, `IsInMoveSizeIgnoreList` @1400607b0]. Only the value names
count, not their data; each is compared, case and all, with the base file
name of the window's process image.

**V2: the client drives the move.** In RemoteApp V2 the client announces
a move/size it is driving. The server then:

1. *posts* `WM_ENTERSIZEMOVE` (0x231), or later `WM_EXITSIZEMOVE` (0x232),
   to the window;
2. sets the cursor to the client's point;
3. applies the client's intermediate positions as buffered *window
   actions* through the shell's privileged broker.

[R `RailWindowManager::OnClientWindowEnterExitMoveSize` @140061980,
`OnClientWindowAction`, `CPrivilegedPresentationOperations::ApplyWindowAction`
@140034fc0 in rdpinit.] The application thus sees the brackets of a
move/size loop without Windows running one.

**How V2 keeps Windows' loop from running** [R win32kfull 10.0.26100.8036]:

- The decision is not in `xxxMoveSize` but one level up, in
  `xxxSysCommand` @1402b5464. For SC_SIZE and SC_MOVE it first asks
  `DelegateMoveSizeToShell` @1402b4e84; when that answers yes,
  `xxxMoveSize` is never called. No WM_ENTERSIZEMOVE, WM_MOVING,
  WM_SIZING, WM_EXITSIZEMOVE, event 0x19 or 0x0A/0x0B is raised by the
  kernel in that path; rdpshell posts the two brackets itself, and nothing
  synthesizes WM_MOVING or WM_SIZING.
- `DelegateMoveSizeToShell` delegates kinds 1–9 when the window's
  shell-window-management bits allow it (tagWND+0x184: bit 2 move, bit 4
  size). It then sends DWM an async *move/size intercepted* notification
  with the kind and cursor point, posts the window a WM_LBUTTONUP and
  forces the capture away.
- The bits are set by `NtUserEnableWindowShellWindowManagementBehavior`
  @140294700, which refuses (ERROR_ACCESS_DENIED) any caller that is
  neither the registered ShellWindowManagement thread nor granted IAM
  access (`IAMThreadAccessGranted`) - the session shell's key.
  `NtUserApplyWindowAction` @140292770, which applies the client's
  intermediate rectangles, has the same gate and wants a per-monitor-v2
  caller.
- rdpinit reaches both through `IRemoteAppPrivilegedOperationBroker2`
  (82ec76cd-…), made by the immersive shell's `RemoteAppBrokerFactory`
  (SID 8982be9d-1779-4400-a6ab-c69244842534, broker IID
  f9dbd15e-c7b9-4992-9ebe-b4fb20bc4235), which lives in **twinui.dll**,
  not twinui.pcshell.dll. `RemoteAppBrokerFactory::CreateInstance`
  @1802c5de0 hands the broker only to a caller whose process UI context is
  `PROCESS_UICONTEXT_IMMERSIVE_BROKER`; anyone else gets E_ACCESSDENIED.
  The broker's `ApplyWindowAction` (slot 0x30) and
  `EnableWindowShellWindowManagementBehavior` (slot 0x28) forward to the
  shell's privileged presentation and windowing operations.

So V2's client-driven move/size is the session shell's privilege. An
ordinary process - remotewin's guest half, medium or high integrity, with
or without UIAccess - can neither obtain the broker nor set the bits
itself (§6.5).

### 2.6 Activation, foreground, and the races (f)

**Client Activate.** On a Client Activate PDU [R
`HookZOrderSync::WindowActivateOrder` @1400819e8, `ApplyWindowActivate`
@14007f7f0]:

- **Target.** `Enabled` ≠ 0 targets that window, which must be tracked.
  `Enabled` = 0 targets the **marker window**, or the desktop when there
  is no marker.
- **Cancel the pending timer.** Any running activation timer is cancelled,
  and the target is remembered as `m_hwndClientActive`.
- **Set the foreground** [R `SetForegroundWindowHelper` @1400810ec]:
  - With UIAccess, which rdpshell has by manifest, it calls
    `SetForegroundWindow` directly, past the foreground lock.
  - Without it, a helper thread with a 1 s limit runs the hotkey trick
    [R `SetForegroundHelperImpl` @14007a370]: create a hidden window;
    `RegisterHotKey`; release any held Shift, Alt or Ctrl with SendInput;
    send the hotkey; press the modifiers again; wait for WM_HOTKEY. That
    input makes the thread the last one with input, which grants
    foreground rights, and it then calls `SetForegroundWindow`. The window
    is "ForegroundHotKeyWindow"; the hotkey has no modifiers and VK 0xB9,
    a key no keyboard has, the next lower one tried when it is taken, 20
    tries (`RegisterHotKey` @14007a018); the keys go in one `SendInput`
    each; the wait is a 1 s `SetTimer` in a message loop.
- **The marker case.** For `Enabled` = 0 the server also calls
  `AllowSetForegroundWindow` for the marker's process [R
  `ProtectMarkerWindowAgainstSetForegroundWindow` @140079f48]. It then
  publishes the new foreground to a WNF state (0x0f840539a3bc3835).
- **Confirm before reporting.** Activation is asynchronous, so the server
  checks before it reports:
  1. It recomputes the z-order list.
  2. When the list is *ready*, meaning the target is first in it or first
     below the topmost windows, it sends the desktop order with ZORDER and
     ACTIVEWND at once [R `IsZOrderReadyToSend` @140080680].
  3. Otherwise it starts `CWindowActivationHelper`, a waitable-timer
     thread [R `WindowActivationHelperThreadMain` @140085cb4]. Each tick
     recomputes. The period starts at 10 ms and doubles each tick (10, 20,
     40, 80, 160, 320 ms, about 630 ms in all); past 320 ms it sends the
     current order anyway and logs "the correct z-order was not sent"
     [R `CWindowActivationHelper::SetTimer` @140085a28,
     `OnWindowActivationTimerElapsed` @140080990]. "Ready" is the target
     first in the list, or the first entry without WS_EX_TOPMOST.

**Foreground changes on the server** [R `OnHookEventForeground`
@140071ac0, `HookZOrderSync::OnForegroundWindowChanged` @140080730]:

- The window's show state and geometry are refreshed.
- If the new foreground is the one the client asked for, and the list is
  ready, the order is sent and the timer stopped.
- If the server changed foreground *by itself* and the list is ready, the
  order is sent at once.
- Otherwise sending is *delayed* through the same timer ("Delay sending
  incorrect z-order"). A transient foreground is never reported.
- Repeated sends of an unchanged list and active window are suppressed
  [R `SendZOrderToClient` @140080ef4].

**The active window reported** [R `GetHookActiveWindow` @14008041c] is
`GetGUIThreadInfo(0).hwndActive`, taken to its root when it is a child. It
is sent as "none" (0xFFFFFFFF) when it is null, the desktop or the marker.

**The z-order list** [R `ComputeZOrderList` @14007fc88,
`EnumWindowsFullZOrderProc`]:

- an `EnumWindows` walk that keeps tracked, effectively visible windows
  and always keeps the marker;
- windows of an IME class (CS_IME) are moved to sit with their owners
  (`FixupImeWindowsZorder`).

**Races, resolved by causality through the marker** [S 3.2.5.2.9.2]:

- A click in a RAIL window goes as input and activates on the server by
  itself. The client sends Activate only for activation by other means
  (Alt+Tab, the taskbar) [S 3.2.5.2.5.1].
- When a local non-RAIL window is activated, the client sends Activate with
  Enabled = 0. The server makes the marker the foreground, so every remote
  window is inactive and none can be reported active.
- When the server reports an ActiveWindowId, the client activates it only
  if it sits *above the marker* in the reported z-order. A server
  activation that happened while the user was in a local window cannot
  steal focus.

### 2.7 System commands, system menu, min/max info (g)

**System Command** [R `WindowSysCommandOrder` @1400770b0,
`ApplyWindowSysCommand` @14006dfe0]. Only tracked windows are acted on, and
every action is a `PostMessage`, never a send:

| Command | Action |
|---|---|
| SC_SIZE, SC_MOVE, SC_MINIMIZE, SC_MAXIMIZE, **SC_CLOSE**, SC_DEFAULT | `WM_SYSCOMMAND(cmd, 0)` |
| SC_KEYMENU | first `WM_SYSCHAR(' ', 0x20390001)` (an Alt+Space keystroke), then `WM_SYSCOMMAND(SC_KEYMENU, ' ')` |
| SC_RESTORE | if the window is minimized **and disabled** (it owns a modal dialog, so it ignores WM_SYSCOMMAND), `ShowWindowAsync(SW_RESTORE)`; otherwise `WM_SYSCOMMAND(SC_RESTORE)` |
| anything else | ignored |

Close is therefore `SC_CLOSE`, so a tray application's own handler decides
whether to hide or quit.

**System Menu** [R `WindowSystemMenuOrder` @1400772f0]: posts the
undocumented `WM_POPUPSYSTEMMENU` (0x313) with lParam = MAKELPARAM(x, y).

**Min/max info** [R `SendWindowMinMaxInfo`] goes out at window setup,
after an App ID request, and at every move/size start. It is skipped for
ghost windows.

### 2.8 Notification icons (h)

[R rdpinit: `CRdpTray::Initialize` @140037030, `CRdpTray::WndProc`
@140038170, `CShellNotifyManager::HandleValidatedMessage` @14003ba60,
`OnProperties` @14003d670, `SyncItem` @14003e900, `ShellNotifyEvent`
@14003dfb4]

**Collection.** rdpinit *is* the tray:

- It creates a window of class `Shell_TrayWnd` and cloaks it
  (`DWMWA_CLOAK`).
- It broadcasts `TaskbarCreated`, so every application re-adds its icons.
- It receives `Shell_NotifyIcon`'s `WM_COPYDATA`:
  - dwData 1, `TRAYNOTIFYDATAW`, carries NIM_ADD, MODIFY, DELETE,
    SETFOCUS and SETVERSION;
  - dwData 0 is AppBar.
- Every message is validated for signature and size.
- Icons are tracked by (hWnd, uID).

**Updates:**

- A MODIFY that changes nothing is dropped, and an unchanged icon is not
  resent.
- An icon is re-added when it was never sent.
- Balloons (NIF_INFO) become InfoTip fields.
- `ShouldSendToDriver` filters hidden icons. The policies
  `RailShowAllNotifyIcons` and `RailShowLocationNotifyIcon` widen it.

**Client events:**

1. If the owner window is gone, the icon is deleted.
2. Otherwise `AllowSetForegroundWindow` is called for the owner's process,
   so the menu it opens can come to the front.
3. Then the icon's `uCallbackMessage` is *posted*:
   - for NOTIFYICON_VERSION_4: wParam = the cursor position (x, y),
     lParam = MAKELONG(msg, uID);
   - for older versions: wParam = uID, lParam = msg.

### 2.9 App ID (i)

[R `WindowGetAppIDOrder` @140076b60, `GetWindowAppID` @14006f110]

- The App ID is the taskbar's own answer:
  `CoCreateInstance(CLSID_StartMenuCacheAndAppResolver)` returns
  `IApplicationResolver` (IID de25675a-72de-44b4-9373-05170450c140), and
  its `GetAppIDForWindow(hwnd, …)` (vtable slot 5) gives the ID. That
  covers an explicit AUMID on the window, one on the process, and IDs
  derived from the executable or its shortcut.
- For a ghost window it asks again for the hung window.
- The Ex response adds the process id and image name from
  `GetProcessInfo`.
- After answering, the server *sends* `TaskbarButtonCreated` to the window
  (`SendNotifyMessage`, `RegisterWindowMessage("TaskbarButtonCreated")`) and
  refreshes min/max info. Applications that wait for their taskbar button
  before setting overlay icons or progress therefore proceed [R
  `NotifyTaskbarButtonCreated` @14006fdbc].

### 2.10 Cloak, z-order sync, snap (j)

**Cloak** [R rdpshell `OnHookEventCloaked` @14007158c,
`OnWindowCloakStateChanged` @140073f64, `SendWindowCloakState`
@140074040; rdpinit `CloakSyncComponent::OnWindowCloakedLocally`
@140041cb0, `GetWindowNonRdpCloakState` @140041a78, `SetRdpCloak`
@140043104]:

- **Where the state lives.** rdpinit keeps, per window, two flags:
  - *rdp-cloaked*: cloaked because the client asked;
  - *non-rdp-cloaked*: cloaked for any other reason.
- **Local cloak events.** These re-read `DWMWA_CLOAKED`. While
  rdp-cloaked, the shell bit is ignored. Only a change in the non-rdp
  state is reported to the client, so a cloak the client caused is never
  echoed.
- **Client Cloak PDUs** set rdp-cloaked and call the local cloak manager
  (`DWMWA_CLOAK`).
- **Clients without BIDIRECTIONAL_CLOAK.** The server marks the window
  hidden-from-client and synthesizes a SHOW or HIDE event. The client sees
  ShowState 0 or 5, *not* the Deleted and New Window orders the spec
  suggests.
- **Clients with it.** The server sends the Cloak PDU (0x15).

**Z-order sync.** The marker window lives in rdpinit (`CMarkerWindow`,
`Initialize` @14002ef50): a window titled "RemoteApp Marker Window",
WS_POPUP | WS_VISIBLE, no extended style, 0×0, with the property
`UIA_WindowVisibilityOverridden` = 2 to keep it out of UI Automation.
§2.6 has the rules.

**Snap** [R `WindowSnapArrangeOrder` @1400840a0 → shell helper
`SetWindowSnapArrangement` → rdpinit
`CPrivilegedPresentationOperations::SetWindowSnapArrangement`]. The rect
goes to the shell's privileged broker, which arranges the window so that
unsnapping restores its size.

### 2.11 Not resolved in the first pass, and how it was pinned down

The items below were open when this document was first written; the
reversing done while building wire 6 settled them (§2.5 for the V2
mechanism, §6 for each answer).


- **The V2 wire format and window actions.** The decompilation shows the
  dispatch and the named-feature negotiation, not every field. The window
  actions go through the COM interface `IRemoteAppPrivilegedOperationBroker`
  (IID 82ec76cd-c8b6-41e9-b752-46c9276f9bff), which is implemented in the
  shell, not in these binaries. To pin down:
  - the implementing DLL, by the CLSID in
    `CPrivilegedPresentationOperations::Initialize` and the
    `HKCR\CLSID\{…}\InprocServer32` value, read offline from the guest's
    SOFTWARE hive copy;
  - then Ghidra on that DLL, most likely `twinui.pcshell.dll`.
- **How V2 stops Windows' own move loop.** win32kfull has
  `DelegateMoveSizeToShell` and `EnableWindowShellWindowManagementBehavior`
  is called per window, but the kernel side was not followed. To pin down:
  Ghidra on win32kfull.sys (PDB available, 4 MB) from `xxxMoveSize` into
  `DelegateMoveSizeToShell`.
- **Event 0x19.** Raised unconditionally in `xxxMoveSize`. The exact
  source of idChild, a field of MOVESIZEDATA, is inferred from rdpshell's
  mapping, not read from the kernel. To pin down, empirically: hook
  0x0A..0x19 in the guest and drag each edge.
- **Whether `Feature_EnableRailV2` is on in this guest.** It is a staging
  flag. Reading it needs the registry, which this task did not touch.
- **win32k's own sizing and DWM frame decisions.** Not needed here; they
  sit behind `DwmGetWindowAttribute`.

## §3 RAIL against remotewin today (wire 5)

Status: **present**, **partial**, **missing**, or **ours-only** (needed by
remotewin, not in RAIL).

| RAIL message or field | remotewin wire 5 | Status | Note |
|---|---|---|---|
| Capability sets, Handshake/HandshakeEx, Client Information flags | HostHello/GuestHello `{version}`; versions must be equal | partial | No capabilities. A mismatch kills the link |
| RemoteApp V2 named features | – | missing | The model for §4's negotiation |
| Window order, NEW (full record) | WindowList: every window, every field, every change | partial | Whole list, no NEW/delta distinction, resent on any change; 16 ms polling |
| Window order, delta (FieldsPresentFlags) | – | missing | |
| Deleted Window | WindowGone (also sent when capture fails) | partial | Conflates "gone" with "cannot capture", which makes a reopen loop |
| WindowId (server HWND) | handle u64 | present | No reuse guard |
| OwnerWindowId (GW_OWNER, normalized, hidden-owner sentinel) | owner (GW_OWNER, else GUITHREADINFO menu owner or active root) | partial | Different rule (§5) |
| Style, ExtendedStyle | style, extended_style, class_style (unmasked) | present | remotewin sends more, which is fine: its host decides chrome |
| ShowState 0/2/3/5 | flags minimized, maximized; hidden never sent (filtered out) | partial | Hidden windows vanish instead of persisting |
| TitleInfo | title | present | |
| WindowOffset, WindowSize (DWM frame bounds) | x, y, width, height of the "drawn area" (capture-derived) | partial | Derived from the captured picture, not from DWM frame bounds |
| ClientOffset, ClientDelta | – | missing | |
| WindowRects (window region) | region (≤ 256 rects, picture-relative) | present | |
| VisibleOffset, VisibilityRects | – | n/a | RAIL sends the window rects again; nothing to add |
| Resize margins | – (host-side resize band) | missing | |
| EnforceServerZOrder | – | missing | |
| TaskbarButton / taskbar rule | – (host computes `in_taskbar` from style + owner) | partial | Immersive and ShellManaged handling missing |
| Overlay icon and description, AppBar state and edge, Taskbar Tab PDU | – | missing | Not planned |
| Window Icon, Cached Icon | – | missing | R6 / xdg_toplevel_icon |
| Notification icon orders, Notify Event | – | missing | R6 |
| Desktop order: ZORDER | – | missing | |
| Desktop order: ACTIVEWND | per-window `foreground` flag in the list | partial | |
| Desktop ARC_BEGAN/COMPLETED, NONE | – (every list is a full sync) | missing | Needed once deltas exist |
| Marker window, Z-Order Sync Info | – | missing | |
| Client Activate | FocusWindow | partial | Guest applies its own filters; the host has a 750 ms heuristic |
| Client System Command | CloseWindow (WM_CLOSE) only | partial | Wrong message for close; no min/max/restore |
| Client System Menu | – | missing | |
| Client Execute / Exec Result | – (LSW agent launches) | missing | |
| Client/Server System Parameters | GuestDesktop {room, drop_shadows} at the head of each list | partial | Only work area and shadows, guest to host |
| Min Max Info | – (host learns the room only) | missing | |
| Move/Size Start (with kind) | DragStarted {window} (unowned windows only) | partial | No kind, no position, owned windows excluded |
| Move/Size End | – | missing | |
| Client Window Move | MoveWindow {x,y} + ResizeWindow {w,h} | partial | Two messages, no atomic rect |
| Client Window Snap | – | missing | |
| Get App ID (+Ex) | – (scope by job, pid, directory) | missing | |
| Cloak (both directions) | – (cloaked windows filtered out) | missing | |
| Language bar, profile, compartment | – | missing | R6 IME/layout |
| Power Display Request | – | missing | |
| Text scale, caret blink | – | missing | |
| Input (core RDP input PDUs in RAIL) | PointerMove, PointerButton, PointerWheel, PointerDelta, PointerAt, Key | ours-only | Per-window coordinates; RAIL uses screen coordinates |
| Enhanced RemoteApp surface-to-window | SlotRing, FrameInSlot, SlotFree, FrameRoute | ours-only | Analogous role; GPU shared surfaces |
| Pixel frames | Frame, FramePatch | ours-only | |
| Cursor | CursorShape, CursorChanged | ours-only | RAIL uses the core pointer PDUs |
| Pointer grab | PointerGrabbed | ours-only | |
| Latency | ClockProbe, ClockEcho, frame stamps | ours-only | |
| App lifetime | `alive` byte in WindowList | ours-only | RAIL: session-emptiness logoff (§2.1) |
| Scope | WatchScope {pid, job} | ours-only | Has no RAIL counterpart; §5 removes it |
| Whole monitor | WatchMonitor | ours-only | |
| Subscribe/Unsubscribe frames | Subscribe, Unsubscribe | ours-only | |
| Layering (flags, alpha, color key), band, dwm_frame, class name | in WindowList | ours-only | Host chrome needs them |

## §4 Wire 6: proposed

A clean break. Nothing from wire 5 needs to keep working. §7 says what
was built and where it departs from this.

### 4.1 Principles

**Framing.** Framing stays `[len:u32be][id:u8][fields]`, big-endian.

**Id ranges.** Host-to-guest ids are 1–63, guest-to-host ids 64–127. The
probe keeps its own ranges.

**Strings.** Strings are UTF-8 with a u32 length.

**Window identity.** `WindowId` = `{hwnd:u32, serial:u32}`. The guest
assigns `serial` when it admits the HWND. A recycled HWND then cannot be
mistaken for the window it replaced. RAIL does this with a short
"recently destroyed" hold in V2. This makes it explicit.

**Deltas, not lists.** These follow RAIL §2.2:

- `WindowNew` carries every field.
- `WindowChanged` carries a FieldsPresent mask and only the changed fields.
- The geometry fields form one group that is always sent whole.
- Desktop-level facts (z-order, active window) have messages of their own.
- `SyncBegin`/`SyncEnd` bracket a full resync, after connect, reconnect or
  a desktop switch. Anything not resent inside the bracket is gone.

**Capabilities, not versions.** These follow RemoteApp V2's named features:

- `Hello` carries a build string and a list of `{feature:string,
  version:u16}`.
- A feature is active at the lower of the two versions, when both list it.
- A message belongs to one feature and is sent only when that feature is
  active.
- `windows` is required. A peer without it is refused with one line naming
  the missing feature. Every other feature degrades a window, not the link.

The feature names:

| Feature | Covers |
|---|---|
| `windows` | Sync*, Window*, ZOrder, Activate, SysCommand, SysMenu, WindowMove |
| `input` | pointer and key messages |
| `frames.pixels` | Frame, FramePatch |
| `frames.shared` | SurfaceRing, FrameInSlot, SlotFree |
| `cursor` | CursorShape, CursorChanged |
| `pointer.grab` | PointerGrab |
| `latency` | ClockProbe, ClockEcho, and the frame timestamps |
| `movesize` | MinMax, MoveSizeStart, MoveSizeEnd, MoveSizeEnterExit |
| `icons` | WindowIcon, WindowIconCached |
| `appid` | the APPID field |
| `cloak` | Cloak both ways |
| `snap` | WindowMove arrange kinds |
| `tray` | TrayIcon*, NotifyEvent |
| `sysparam` | SysParam both ways |
| `exec` | Exec, ExecResult |
| `ime` | LanguageProfile, ImeState |
| `power` | PowerDisplay |
| `monitor` | WatchMonitor |
| `popup.intent` | the POPUP field |

**Coordinates.**

- Window geometry is in guest-screen physical pixels, as in RAIL. It is
  the guest's truth.
- Input stays per window, in the window's picture coordinates (R2).
- The host never has to reproduce the guest's layout.

### 4.2 Window fields (WindowNew / WindowChanged)

`mask:u32`, then the present fields in this order:

| Bit | Field | Type | RAIL source |
|---|---|---|---|
| 0 OWNER | owner WindowId, or `{0,0}` none, or `{0xFFFFFFFE,0}` hidden owner | 8 B | OwnerWindowId 2.2.1.3.1.2.1 + §2.3 sentinel |
| 1 STYLE | style u32, exstyle u32, class_style u32 (unmasked) | 12 B | Style/ExtendedStyle |
| 2 SHOW | u8: 0 hidden, 2 minimized, 3 maximized, 5 shown | 1 B | ShowState |
| 3 TITLE | string | | TitleInfo |
| 4 GEOMETRY | frame {x,y,w,h} i32 (DWM extended frame bounds); client {dx,dy,w,h} relative to frame; margins {l,t,r,b} u16; region count u16 + rects (frame-relative); picture inset {x,y} i32 (where the frame sits in the captured picture: ours) | | WNDOFFSET, WNDSIZE, CLIENTAREAOFFSET, CLIENTDELTA, RESIZE_MARGIN_X/Y, WNDRECTS (VISOFFSET and VISIBILITY dropped: identical, §2.4) |
| 5 TASKBAR | u8 bits: 1 taskbar button expected (§2.3 rule, computed in the guest), 2 shown in switchers | 1 B | ExStyle forcing + V2 ShowInTaskSwitchers |
| 6 ZGLUE | u8 keep with its z-neighbours | 1 B | EnforceServerZOrder |
| 7 APPID | aumid string, pid u32, image string | | Get App ID Response Ex 2.2.2.8.2, *pushed* instead of requested |
| 8 CLASS | class name string | | ours |
| 9 LAYERING | flags u32, alpha u8, color key u32 | | ours (host chrome) |
| 10 BAND | u32 (GetWindowBand) | | ours |
| 11 FRAME | u8 bits: 1 DWM draws the non-client frame, 2 drop shadow | | ours |
| 12 POPUP | anchor WindowId, anchor rect {x,y,w,h} on the anchor, gravity u8, constraint flags u8 | | ours (R2, popups by intent) |
| 13 DPI | u16 window DPI | | ours (RAIL needs none: one physical screen) |
| 14 CLOAKED | u8 | | carries cloak state in the record for peers without the `cloak` feature, as Windows does (§2.10) |

### 4.3 Guest to host (ids 64–127)

| Id | Message | Fields | Mirrors |
|---|---|---|---|
| 64 | Hello | build string, features[] | Handshake/HandshakeEx 2.2.2.2.1/3, capability sets 2.2.1.1 |
| 65 | SyncBegin | reason u8 (connect, reconnect, desktop switch) | Desktop ARC_BEGAN\|HOOKED 2.2.1.3.3.2.1 |
| 66 | SyncEnd | – | Desktop ARC_COMPLETED |
| 67 | DesktopLost | reason u8 (secure desktop, lock) | Non-Monitored Desktop 2.2.1.3.3.2.2 |
| 68 | WindowNew | WindowId, mask, all fields | Window order with STATE_NEW |
| 69 | WindowChanged | WindowId, mask, changed fields | Window order without STATE_NEW |
| 70 | WindowGone | WindowId | Deleted Window 2.2.1.3.1.2.4 |
| 71 | WindowIcon | WindowId, kind u8 (small, big, overlay), cache u16 (0xFFFF none), w u16, h u16, BGRA | Window Icon 2.2.1.3.1.2.2 (BGRA ≤ 256 px instead of bpp/mask) |
| 72 | WindowIconCached | WindowId, kind, cache | Cached Icon 2.2.1.3.1.2.3 |
| 73 | ZOrder | active WindowId (`{0,0}` = none or marker), count u16, ids[] top-first | Desktop ZORDER + ACTIVEWND |
| 74 | MinMax | WindowId, max {w,h}, max pos {x,y}, min track {w,h}, max track {w,h} (i32) | Min Max Info 2.2.2.7.1 |
| 75 | MoveSizeStart | WindowId, kind u8 (RAIL_WMSZ 1–11), x i32, y i32 | Move/Size Start 2.2.2.7.2 |
| 76 | MoveSizeEnd | WindowId, kind u8, frame {x,y,w,h} | Move/Size End 2.2.2.7.3 (whole rect, not just the corner) |
| 77 | Cloak | WindowId, cloaked u8 | Cloak 2.2.2.12.1 |
| 78 | TrayIcon | owner WindowId, uid u32, mask u8, version u32, tip string, balloon {timeout u32, flags u32, text, title}, state u32, icon (as WindowIcon) | Notification icon order 2.2.1.3.2.2.1 |
| 79 | TrayIconGone | owner WindowId, uid u32 | Deleted notification icon 2.2.1.3.2.2.2 |
| 80 | ExecResult | request u32, result u16, raw u32 | Exec Result 2.2.2.3.2 |
| 81 | SysParam | param u32, body | Server System Parameters 2.2.2.5.1. Also carries what wire 5's GuestDesktop did: work area, drop shadows |
| 82 | PowerDisplay | active u8 | Power Display Request 2.2.2.13.1 |
| 83 | ImeState | open u8, conversion u32, sentence u32, kana u8 | Compartment Status 2.2.2.10.2 |
| 90 | SurfaceRing | WindowId, ring u32, w, h, surfaces u64[3] | ours (Enhanced RemoteApp's surface-to-window, 2.2.2.20) |
| 91 | FrameInSlot | WindowId, ring, slot, copies u64, composed/taken/copied i64 | ours |
| 92 | Frame | WindowId, w, h, BGRA | ours |
| 93 | FramePatch | WindowId, w, h, rects + pixels, chunked below the cap | ours |
| 94 | SurfaceLost | WindowId, reason u8 | ours: capture lost ≠ window gone |
| 95 | CursorShape | id u64, system u8, inverts u8, frames[] | ours |
| 96 | CursorChanged | id u64, shown u8 | ours |
| 97 | PointerGrab | kind u8, WindowId, area, x, y | ours |
| 98 | ClockEcho | probed u64, guest i64 | ours |

### 4.4 Host to guest (ids 1–63)

| Id | Message | Fields | Mirrors |
|---|---|---|---|
| 1 | Hello | build string, features[] | Handshake, Client Information 2.2.2.2.2 |
| 2 | Exec | request u32, flags u16 (FILE, AUMID, EXPAND_*), target, args, workdir strings | Client Execute 2.2.2.3.1 |
| 3 | SysParam | param u32, body: work area, display change, drag-full-windows, mouse swap, keyboard cues, high contrast, dark mode, accent colour, text scale, caret blink | Client System Parameters 2.2.2.4.1, Text Scale 2.2.2.15.1, Caret Blink 2.2.2.15.2 |
| 4 | Activate | WindowId, enabled u8 (0 = activate the marker) | Client Activate 2.2.2.6.1 |
| 5 | SysCommand | WindowId, command u16 (SC_*) | Client System Command 2.2.2.6.3 (replaces CloseWindow) |
| 6 | SysMenu | WindowId, x i32, y i32 (frame-relative) | Client System Menu 2.2.2.6.2 |
| 7 | WindowMove | WindowId, frame {x,y,w,h}, arrange u8 (none, maximize, tile left/right/top/bottom, fullscreen) | Client Window Move 2.2.2.7.4 + Snap 2.2.2.7.5 (replaces MoveWindow and ResizeWindow) |
| 8 | MoveSizeEnterExit | WindowId, entering u8 | RemoteApp V2 enter/exit move-size (§2.5) |
| 9 | Cloak | WindowId, cloaked u8 | Cloak 2.2.2.12.1 |
| 10 | NotifyEvent | owner WindowId, uid u32, message u32, x i32, y i32 | Client Notify Event 2.2.2.6.4 |
| 11 | LanguageProfile | type u32, langid u16, clsid, profile GUID, hkl u32 | Language Profile 2.2.2.10.1 |
| 20 | FrameRoute | shared u8 | ours |
| 21 | SlotFree | WindowId, ring, slot | ours |
| 22 | Subscribe / 23 Unsubscribe | WindowId | ours |
| 24 | ClockProbe | host_ns u64 | ours |
| 25 | WatchMonitor | – | ours |
| 26 | DisplayLayout | monitors[] {x,y,w,h,scale} | ours (RAIL uses the core display-control channel) |
| 30–36 | PointerMove, PointerButton, PointerWheel, PointerDelta, PointerAt, Key, **PointerLeave** | as wire 5, with WindowId; PointerLeave is new (fixes stuck hover) | ours (RAIL uses core input) |

**Dropped from wire 5:**

- WatchScope (§5.2);
- CloseWindow, MoveWindow, ResizeWindow and FocusWindow (now SysCommand,
  WindowMove and Activate);
- WindowList, DragStarted and the GuestDesktop header (now the window
  deltas, MoveSizeStart and SysParam);
- the per-window `alive` byte (the host derives lifetime from
  `WindowGone` and the APPID's pid, as RAIL derives session emptiness).

**Get App ID.** RAIL's request/response is replaced by the pushed APPID
field. The guest resolves the ID once, at admission, on its worker. This
removes a round trip per window. RAIL makes it a request only because old
clients ignored it [S <21>].

## §5 Guest-half changes

These replace the current heuristics with Windows' rules. They are listed
in the order the code would change.

### 5.1 Finding windows: events and deltas instead of polling and whole lists

**Today.**

- `WindowListWatcher::changed` (window_list_watcher.cpp) calls
  `EnumWindows` every 16 ms.
- `same()` compares every field of every window.
- On any difference, `send_window_list` resends the lot.

**RAIL** (§2.2):

- one thread with the eight out-of-context WinEvent ranges, including
  0x0A/0x19;
- events queued in order to one worker;
- only `OBJID_WINDOW` events;
- each event recomputes that one window's record, which is diffed against
  the last record sent;
- `EnumWindows` only for the initial sync, bracketed by SyncBegin/SyncEnd.

**New classes:**

- `WindowRecord`: one window's fields and its last-sent snapshot, with the
  delta mask (`CHookWindow` and `PopulateWindowProperties`);
- `WindowTracker`: the HWND → `WindowRecord` map, admission, the 256-window
  cap and serial assignment (`CHookEventProcessor`);
- `WinEventSource`: the hook thread and its queue (`CHook`).

**Effect.** This removes the 16 ms loop and the main loop's
"enumeration delays input" note.

### 5.2 Admission: RAIL's rule plus one LSW exclusion; no process scope

**Today.**

- `worth_showing` filters: visible, not cloaked, non-empty rectangle.
- `collect` filters by `LaunchScope::contains` and `owned_in_scope`.
- `LaunchScope` (launch_scope.cpp) layers several guesses to decide which
  processes are the application's:
  - membership of a job;
  - the launched pid;
  - every process from the same directory that started later (the "same
    directory" rule);
  - a parent-chain walk with start-time checks;
  - "adoption" by `ForegroundHandOff` (5 s and 100 ms input windows).

**RAIL rule** (§2.1). Admit every top-level window of the input desktop
that is IsWindowVisible or under `SysSetRedraw`:

- keep hidden windows tracked with show state 0 until destroyed;
- keep cloaked windows tracked, sent hidden or with Cloak;
- do not filter zero-size windows, tool windows, menus or tooltips;
- skip the marker;
- delete a window that becomes a child.

**The difference from RAIL.** remotewin's guest runs a full Explorer shell
and the user's other programs, which RAIL's session never has. Two things
follow:

1. **Shell windows.** Exclude the shell's own windows: `Progman`,
   `WorkerW`, `Shell_TrayWnd`, `Shell_SecondaryTrayWnd`, and anything
   owned by `explorer.exe`'s shell-managed set (user32 `IsShellManagedWindow`
   without the `ShellManagedWindowAsNormalWindow` property and without
   ShowInSwitchers). A class and shell-managed test, not a process tree.
2. **Belonging.** Replace "which windows are this application's" with RAIL's
   answer: every window carries its **App ID** (IApplicationResolver, §2.9)
   and pid/image, and the host groups windows by App ID. This is R4's
   identity, arrived at from RAIL.

`LaunchScope`, `ApplicationProcess`, `owned_in_scope`, `ForegroundHandOff`
and `WatchScope` are deleted.

Until R4's single mirror service exists, a per-application host process
can filter the delta stream by App ID or pid on its side. That is cheaper
than the guest deciding, because the stream is already per window.

### 5.3 Owner: GW_OWNER, normalized, with the hidden-owner sentinel

**Today.** `owner_of` (window_enumerator.cpp) adds two inferences to
`GW_OWNER`:

- `GUITHREADINFO.hwndMenuOwner` for `#32768` menus;
- the active root for desktop-child or capturing windows (combo lists).

**RAIL** (§2.3): GW_OWNER only.

- An owner that is a child becomes its GA_ROOT.
- An owner that is the desktop becomes none.
- An owner that is invisible, has an empty rectangle, or is minimized with
  an empty normal rectangle becomes `hidden owner`.

**What happens to the inferred cases.** A menu or combo list's
relationship to a window is not ownership. It moves to the POPUP field:
the anchor window and the anchor rectangle on it, which is R2's "popups by
intent". The owner field then means what Windows means by it, and the
host's transient-parent logic stops depending on guesses.

### 5.4 Role and taskbar: owned_window_stands_alone becomes the host's, fed by RAIL's taskbar rule

**Today.** `owned_window_stands_alone` (proto/owned_window.hpp) is shared
by both ends. The guest uses it to decide whether focusing a window may
move the foreground.

**RAIL** leaves presentation to the client, from style, ex-style and owner.
The server computes only the taskbar rule, including the immersive and
ShowInSwitchers check.

**The change:**

- The guest computes the TASKBAR field by §2.3.
- `owned_window_stands_alone` moves into the host (`host/chrome`) as a pure
  presentation rule. It leaves `proto/`, because the guest no longer needs
  it (§5.5).
- The `hidden owner` sentinel makes such a window a toplevel with a taskbar
  entry, as Windows' taskbar does.

### 5.5 Activation: RAIL's causality instead of the 750 ms filter and the guest's guards

**Today:**

- **The host** ignores a focus-in within `kMapFocusMs` = 750 ms of a show
  or minimize (mirror_window.cpp).
- **The guest's `WindowFocus::give_to`** refuses in several cases:
  - while something is tracking (menu mode or capture);
  - for owned popups;
  - when the root owner is already foreground;
  - for NOACTIVATE windows.

  It also restores a minimized window, then calls
  `AllowSetForegroundWindow(ASFW_ANY)` and `SetForegroundWindow`, with no
  confirmation.

**RAIL** (§2.6):

- **Clicks** activate by themselves through the injected input. The host
  sends `Activate` only when a mirror gains focus *without* a pointer
  press in it: Alt+Tab, a compositor or taskbar switch, a new map focused
  by the compositor.
- **Focus leaves all mirrors.** The host sends `Activate{enabled=0}`. The
  guest moves the foreground to its **marker window**: an invisible,
  never-reported window of the guest half. Every guest window then
  deactivates, so carets stop and menus close, as on a real desktop.
- **The guest reports, rather than trusts.** It reports the resulting
  active window through `ZOrder`, after confirming the activation took. It
  re-sends after a debounce timer, never a transient state.
- **The host applies a reported active window** only if it is above the
  marker in the reported z-order, and only if the host's focus is in a
  mirror. That is the client rule in [S 3.2.5.2.9.2]. A guest activation
  while the user is in a host window does not steal focus. A host focus
  that the guest reports back is not echoed, because it matches.
- **Foreground rights without UIAccess.** RAIL's hotkey trick (§2.6):
  `RegisterHotKey`, a SendInput of that hotkey with the modifiers
  released, wait for WM_HOTKEY, then `SetForegroundWindow`. Not
  `AllowSetForegroundWindow(ASFW_ANY)`, which only lends rights the
  caller already has.
- **Restoring** is no longer a side effect of focus. It is
  `SysCommand(SC_RESTORE)`, using `ShowWindowAsync` for a minimized
  disabled window (§2.7).

This deletes the 750 ms constant and `WindowFocus::something_is_tracking`.
The "owned popup" and "root owner already foreground" guards also go: a
popup is never sent Activate, because it has no host focus, and the
root-owner case is the ZOrder echo.

### 5.6 Move and resize: RAIL's detection, V2's client-driven loop, all windows

**Today.** `WindowDragWatcher` hooks `EVENT_SYSTEM_MOVESIZESTART`.

- For a mirrored window *without a visible owner* it posts `WM_CANCELMODE`
  and reports `DragStarted{window}`.
- Owned windows keep Windows' loop, so off KWin they cannot be dragged
  (review Part 1).
- No kind and no position are reported.

**What RAIL adds:**

- **Event 0x19 gives the kind.** Move, each of the eight edges, keyboard
  move or keyboard size. The host can therefore call
  `xdg_toplevel.resize(edge)`, not only `move`.
- **The sequence.** On start, send `MinMax`, then
  `MoveSizeStart{kind, pos}`. Make no exception for owned windows. Make
  only a compatibility exception: a process list like
  `LocalMoveSizeIgnoreList`.
- **Ending the move.** With a mouse kind, Windows' loop ends on the
  button-up. With a keyboard kind, the first `WindowMove` ends it (§2.5).

**Which loop to copy.** RAIL V1 leaves Windows' modal loop running and
relies on the client not forwarding motion. That cannot carry live
resizing, because the loop would recommit its own rectangle at the end.
remotewin should copy **V2**:

1. Cancel Windows' loop, as today with `WM_CANCELMODE`, for every
   admitted window.
2. Post `WM_ENTERSIZEMOVE` to the window on `MoveSizeEnterExit{entering}`.
3. Apply each `WindowMove` as a SetWindowPos while the host's compositor
   drives the move or resize.
4. Post `WM_EXITSIZEMOVE` at the end.
5. Report `MoveSizeEnd` with the final frame.

`WindowDragWatcher` becomes part of `WinEventSource` (0x0A, 0x19 and 0x0B
on the same hook). Today's `DragStarted` and the `SPI_SETDRAGFULLWINDOWS`
forcing remain. The forcing becomes a `SysParam` the host sends, as RAIL's
client does.

### 5.7 Close and the other window commands

**Today.** `CloseWindow` posts `WM_CLOSE`. Nothing else exists.

**RAIL** (§2.7):

- `SysCommand` posts `WM_SYSCOMMAND` for SC_CLOSE, MINIMIZE, MAXIMIZE,
  MOVE, SIZE and DEFAULT.
- SC_RESTORE uses `ShowWindowAsync(SW_RESTORE)` for a minimized disabled
  window.
- SC_KEYMENU posts `WM_SYSCHAR` and then `SC_KEYMENU`.
- `SysMenu` posts `WM_POPUPSYSTEMMENU` (0x313).
- Everything is posted. Nothing is sent, so a hung window cannot block
  the guest.

Host-side minimize, a gap in Part 1, becomes `SysCommand(SC_MINIMIZE)`.

### 5.8 Geometry: DWM frame bounds instead of the capture-derived drawn area

**Today.** `WindowSurface` insets and the `drawn_` map turn the captured
picture's content area into the reported position and size.
`note_drawn` then forces a full list resend when it changes.

**RAIL** (§2.4):

- the frame comes from `DWMWA_EXTENDED_FRAME_BOUNDS`;
- the client rect is taken relative to it;
- the region comes from `GetWindowRgn`, or else the frame size;
- maximized windows are clipped to their monitor;
- resize margins are `rcWindow − frame` for THICKFRAME windows.

**The change.**

- The GEOMETRY group comes from these window-manager facts.
- Where the frame sits inside the captured picture is the separate
  `picture inset`, so frames and geometry stop forcing each other's
  resend.
- The host uses the margins for its own resize band instead of inventing
  one (mirror_window.cpp:952).

### 5.9 Smaller changes that follow from RAIL

- **SurfaceLost.** Capture failure sends `SurfaceLost`, not `WindowGone`.
  The window stays tracked. This fixes the reopen loop in Part 1.
- **Hung windows.** Every cross-process query uses `SendMessageTimeout`
  with SMTO_ABORTIFHUNG. The ghost window is reported in place of the hung
  one, and App ID and min/max fall back across the pair (§2.1).
- **Minimized windows** keep their record with SHOW = 2 and stop being
  captured. `SPI_SETMINIMIZEDMETRICS | ARW_HIDE` (§2.1) keeps Windows from
  parking a stub at the bottom-left of a screen that the work-area
  computation still sees.
- **TaskbarButtonCreated.** After resolving the App ID, the guest sends
  `TaskbarButtonCreated` to the window. Applications that wait for their
  taskbar button, for jump lists or progress, then proceed (§2.9).
- **The tray (R6)** is RAIL's rdpinit design: a cloaked `Shell_TrayWnd`
  receiving `WM_COPYDATA`. Explorer already owns that class in the guest,
  however (§6).

## §6 Open questions, and what became of them

Each question as it was first asked, then what building wire 6 settled.
"Live" means checked on the user's guest, Windows 11 build 26200.8037
(25H2), with the guest half of wire 6 against a host on the wire.

1. **Event 0x19.** *Asked:* its idChild meaning was taken from rdpshell's
   mapping. *Settled:* `xxxMoveSize` raises it with its own operation code
   as idChild (§2.5): 0 keyboard size, 1–8 the edges in WMSZ order, 9 move,
   10 keyboard move; 0x80000004/5 are raised only towards a registered
   shell window. Live: a caption drag on Notepad gave kind 9, a drag of its
   right border kind 2. Windows 10 is not supported by remotewin and was
   not checked.
2. **The admission rule in a full shell.** *Settled by a survey of the
   guest:* besides `Progman`, `WorkerW`, `Shell_TrayWnd` and
   `Shell_SecondaryTrayWnd`, the visible top-level windows Explorer keeps
   are tool windows of the shell's own process - `ThumbnailDeviceHelperWnd`,
   `DummyDWMListenerWindow`, `EdgeUiInputTopWndClass`, and the snap bar
   `XamlExplorerHostIslandWindow` Windows 11 shows at the top of the screen
   during a drag. The rule is: a tool window of the process that owns
   `GetShellWindow()`, unless it is a menu (`#32768`, which File Explorer
   windows open in that same process), plus the four classes, plus
   shell-managed windows the shell keeps out of the switchers (Start,
   search, the notification centre). remotewin's own windows (class prefix
   `rwin-`, any guest half's) are never admitted.
3. **Reporting every window in the session.** *Decided:* the guest reports
   the whole session, as RAIL does, and a per-application host filters
   (`MirrorScope`): a window belongs when its process is the launched one,
   its image is the launched program, its AppUserModelID is one already
   claimed by such a window (or given with `--aumid`), or its owner or
   popup anchor belongs. LSW's manager passes the launched pid, the
   program's path and a Store application's AUMID, and groups mirrors by
   the AUMIDs remotewin reports (`APP <aumid>` on stdout): a second mirror
   of an AUMID another mirror shows is closed in favour of the first.
   *Wire 7: the launched program's process tree.* A launcher's one window
   is often a helper's (Steam's is steamwebhelper.exe's, a child of
   steam.exe), so image, pid and AUMID alone lose it. A mirror also covers
   the launched process's whole tree: the topmost ancestor that is no part
   of the session itself - the walk up from a process through verified
   parents stops before the first of explorer, svchost, RuntimeBroker,
   sihost, services, wininit, winlogon, userinit, dllhost, or LSW's own
   agent and window half (`SessionBoundary`, the one list), so a Store
   activation roots at the application itself - and every process under
   that root, however long ago it started and whether or not its parent is
   still there. The guest decides who is in a tree (`ProcessTree`) and
   sends with each window the processes from its own up to its tree's root
   (`WindowFields::AppId::tree`, depth at most 16), each as a pid and its
   creation time: Windows gives a pid out again, so the agent reads the
   launched process's creation time at the launch (`pid_created` in its
   reply, passed on as `--pid-created`) and a window belongs when the
   launched process, pid and time, is among them, or its tree's root is the
   root of the launched process's tree, which the first window of the
   launched process or of a descendant names. `ProcessTree` remembers each
   process it has read as (parent, creation time, boundary, when it was last
   seen alive), learned while it was alive - a window's own process and its
   ancestors as it is asked, and everything alive at most once a second - so
   a child whose parent has exited is still traced to it. A recorded parent
   pid is believed only if a process of that pid was created before the child
   and last seen alive after the child was made: a reused pid, and an earlier
   holder of the pid, end the walk. A process whose image cannot be read (the
   SYSTEM agent's) is a boundary. When the memory fills, dead processes
   nothing alive descends from are forgotten, oldest first. A tree's root is
   claimed once as `tree:<pid>:<time>` (`APP tree:<pid>:<time>` on stdout),
   and the manager closes a second mirror claiming a root another shows, as
   for an AUMID. The conformance harness's `same_app_id = false` turns the tree
   (as the AUMID grouping) off. Wire 7's `appid` feature is version 2, and a
   half with the older one is refused at the hello.
   Left open: a launcher stub that hands over to a process outside its
   tree and of another image and AUMID (an app execution alias, a broker
   that is a boundary) is not followed; LSW passes `--aumid` for Store
   applications, which covers the alias case it launches.
4. **Foreground rights.** *Built:* the hotkey trick, as rdpshell does it
   (§2.6), on the marker's thread, after a plain `SetForegroundWindow`
   that is checked. Live: Activate after the marker held the foreground
   brought Notepad back, confirmed by the next ZOrder.
5. **The V2 loop and docking applications.** *Settled:* V2's client-driven
   move/size is the session shell's privilege (§2.5): the broker is handed
   only to the immersive broker process, and the kernel calls behind it
   want the shell's IAM access. remotewin cannot use it. *Built instead:*
   RAIL V1's detection with a different end. On event 0x19, for every
   window, owned or not, the guest sends MinMax and MoveSizeStart; the
   host starts its compositor's own move or edge resize and at once
   forwards the button-up, which ends Windows' loop before it moved
   anything; a resize then reaches the guest as WindowMove. The guest no
   longer cancels anything (the WM_CANCELMODE trick is gone), and an owned
   window is dragged by the compositor on any desktop, not only KWin.
   What is lost: an application that docks panels by watching WM_MOVING
   sees no move. Those keep Windows' loop through the same opt-out RAIL
   has, `LocalMoveSizeIgnoreList`; the keyboard kinds keep it too.
6. **The marker window.** *Built:* in the guest half's process, made as
   rdpinit makes its own (§2.10) plus WS_EX_TOOLWINDOW, because this
   desktop has Explorer's Alt+Tab. Several applications' mirrors share one
   guest, each with its own guest half and marker, so `Activate` with
   enabled 0 names the window the host's focus left, and the guest makes
   its marker the foreground only while that window (or one of its
   owner's family) still is: a deactivation that arrives after a press in
   another application's mirror moved the foreground on is stale.
7. **Coordinates.** As planned: geometry in guest physical pixels, input
   per window in picture pixels. POPUP carries the anchor and the popup's
   place relative to the anchor's frame; gravity and constraints are 0
   until `TrackPopupMenu`'s intent can be read.
8. **Icons and the tray against Explorer.** Open; nothing of `icons` or
   `tray` is built.
9. **The 256-window cap.** Kept, with one warning when it is reached.
10. **Resize margins.** GEOMETRY carries the DWM frame, and margins outside
    it; the host sizes its own resize band from them.

Also settled while building:

- **Activation timing** (§2.6): the 10–320 ms doubling backoff is copied
  into `ZOrderReport`.
- **Titles** are read with `InternalGetWindowText`, which never enters the
  window's thread, rather than RAIL's 20 s `WM_GETTEXT`: a hung window
  cannot stall the window thread.
- **WindowMove** is applied with `SWP_ASYNCWINDOWPOS`, so a hung window
  cannot stall it either.

## §7 Wire 6 as built

Built on branch `wire6`. Wire 5 is gone: no WireHello, WatchScope,
WindowList, DragStarted, CloseWindow, MoveWindow, ResizeWindow or
FocusWindow, and no code path for a version-5 peer.

### 7.1 Where it departs from §4

- **Features implemented** (`Hello::ours`, version 1 each unless said):
  `windows`, `input`, `frames.pixels`, `frames.shared`, `cursor`,
  `pointer.grab`, `latency`, `movesize`, `appid`, `popup.intent` (version
  2, §7.5), `monitor`, `icons` (§7.5). Not built, and their messages not
  defined: `cloak` (the CLOAKED field is),
  `snap`, `tray`, `exec`, `ime`, `power`, and DesktopLost, DisplayLayout,
  MoveSizeEnterExit (§6.5 explains why the V2 handshake is not used).
- **Strings** are UTF-8 with a u32 length, as §4.1 says; the probe channel,
  which shares the reader, changed with it.
- **STYLE** is masked as RAIL masks it plus WS_CAPTION, WS_EX_TRANSPARENT
  and WS_EX_CONTEXTHELP, which the host's chrome reads; WS_EX_TOOLWINDOW is
  forced on for a window the shell refuses a taskbar button
  (`WindowFields::Style`). §4.2 had said unmasked.
- **GEOMETRY.picture** is a rectangle relative to the frame, not an inset:
  a trimmed capture (a WinUI menu's transparent margin) changes its size as
  well as its corner. It is the frame itself unless the capture trims.
- **TASKBAR** follows Explorer where it and rdpshell disagree: a window
  that asks with WS_EX_APPWINDOW gets a button even as a tool window, and a
  WS_EX_NOACTIVATE window that does not ask gets none. `kInSwitchers` is
  set with the button: only the shell's own view refuses one without the
  other, and that refusal clears both.
- **ZOrder** names the marker as `{0,0}`.
- **Activate** with enabled 0 names the window the host's focus left, and
  is carried out only while that window (or its owner's family) is still
  the guest's foreground (§6.6).
- **SysParam**: guest to host, `WorkArea` (SPI_SETWORKAREA, at connect and
  on WM_SETTINGCHANGE/WM_DISPLAYCHANGE) and `Monitor` (0xF001, the primary
  monitor's whole rectangle, with it: what a fullscreen window covers); host to guest, `DragFullWindows`
  on, once after the hello. Drop shadows are per window, in FRAME.
- **Frames**: `WindowPixels{window}` (host to guest, id 27) sends one
  window back to pixels, and the guest captures it afresh, so its picture
  comes again however still it sits (§7.10). A window region or a colour
  key sends nothing to pixels: Windows' capture hands their outside over
  clear, the ring's textures and dma-bufs are ARGB8888, and a region is
  the surface's opaque region (§7.10).
  `FrameRoute{shared}` said again after a GPU
  device reset makes every ring afresh and forgets the per-window
  fallbacks. `SurfaceLost{window}` stops a window's frames without its
  mirror going; the guest captures it again a second later by itself.
- **Cursor ids** are the handle and a hash of the pictures together; the
  guest keeps the last 64 sent, the host the last 128 shown, both least
  recently shown first.
- **PointerButton** numbers Back 3 and Forward 4 (XBUTTON1/2).
  `PointerLeave` moves the guest's cursor to a corner of the work area no
  reported window covers, when it is still over the window left.

### 7.2 The guest half

| Class | What it is |
|---|---|
| `WindowWatch` | The window thread: an STA apartment, rdpshell's eight WinEvent ranges out of context, the event queue, and the host's window commands, all in order |
| `WindowTracker` | HWND → record, admission, serials, WindowNew/WindowChanged/WindowGone, the 256 cap, the full sync between SyncBegin and SyncEnd, a 1 s sweep for windows destroyed or re-owned or restyled without an event |
| `WindowReader` | One window's fields as RAIL reads them (§2.1–§2.4), the admission rule (§6.2) |
| `ZOrderReport` | ZOrder with rdpshell's readiness rule and 10–320 ms backoff |
| `MoveSizeReport` | Event 0x19 and 0x0B → MinMax, MoveSizeStart, MoveSizeEnd; LocalMoveSizeIgnoreList |
| `MarkerWindow` | The marker, and every foreground change, with the hotkey trick |
| `AppIdResolver`, `ShellViews` | IApplicationResolver; IsShellManagedWindow and the application view's ShowInSwitchers |
| `WindowDirectory` | WindowId ↔ HWND and the trimmed picture, shared with the input and capture threads |
| `DesktopSettings` | Drop shadows and the work area, kept current |
| `window_commands` | SysCommand, SysMenu and WindowMove, posted or asynchronous |

Deleted: `LaunchScope`, `ApplicationProcess`, `ForegroundHandOff`,
`WindowListWatcher`, `WindowEnumerator`, `WindowDragWatcher` (and its
WM_CANCELMODE), `WindowFocus` (and the guards in `give_to`),
`WindowMover`, `WindowResizer`, `WorkArea`, `ShadowSetting`. A window's
frame is read again 60 ms after a location event: DWM moves the extended
frame bounds after the event.

### 7.3 The host half

`GuestLink` speaks the hello and keeps the window registry
(`GuestWindows`, records of `GuestWindow`), applying deltas and sweeping on
SyncEnd. `MirrorScope` is the per-application filter (§6.3);
`ActivationSync` is §5.5's rule (Activate only for focus without a press;
enabled 0 after focus has left every mirror for 100 ms; a reported active
window followed only above the marker and while a mirror has focus).
`MirrorSession` opens a mirror for every belonging window that is not
hidden, hides it while Windows hides the window, and finishes 5 s after
its last window went. `MirrorWindow` sends SysCommand for close,
maximise, restore and minimise, SysMenu from a right press on its bars,
WindowMove for a resize, and takes Windows' move/size loop over with the
compositor (§6.5); it re-applies its transient parent when a restyle
remakes its surface, and the session places a remade surface again. In
`--managed` mode the host prints `APP <aumid>` for each AppUserModelID it
claims. (That was the Qt host; §7.6 is the one that replaced it, with
`WindowMirror` in `MirrorWindow`'s place, which sends no SysMenu: §7.9.)

### 7.4 Checked

On the guest of §6 (build 26200), with both halves of wire 6:

- the guest half alone, against a scripted host: deltas, the sync, the
  marker taking the foreground and handing it back, SC_MAXIMIZE,
  SC_RESTORE, SC_MINIMIZE, WindowMove, SC_CLOSE (Notepad hid, its owned
  popup's owner became the hidden owner, both went), MoveSizeStart kinds
  9 and 2 from a caption and a border press, a WinUI popup owned by
  Notepad;
- the conformance harness (docs/conformance.md), before it was ruled too
  slow to run: 264 of 270 scenarios of the window, relationship, input,
  placement and matrix sets passed. Of the six failures, `input-resize`
  and `place-moved-by-guest` fail the same way on wire 5;
  `mx-shell-013`…`016` (tool window with WS_EX_APPWINDOW) led to the
  TASKBAR rule in §7.1 and were not run again; `mx-render-003-noredirection`
  (a WS_EX_NOREDIRECTIONBITMAP window's mirror never asked to be active)
  is open;
- the managed host against an application launched through LSW's agent:
  `APP` and `READY` reported, the mirror opened and activated the window in
  the guest, and the session ended 5 s after the application was killed.

Not checked live: a compositor other than KWin, Back and Forward, and the
compositor taking over a drag (no pointer could be driven on the host
desktop from here).

### 7.5 Popups by intent, their dismissal, and icons

Built on branch `native`, with the host of §7.6.

**POPUP** (field 12, feature `popup.intent` version 2) says what a popup
was meant to do, not only where Windows put it:

| Part | Type | Meaning |
|---|---|---|
| anchor | WindowId | the window it opens from (none for a window that is no popup) |
| kind | u8 | 0 exact, 1 menu bar menu, 2 submenu, 3 combo list, 4 context menu, 5 tooltip |
| placed | Rect | where Windows put it, on the anchor window's picture |
| opens_from | Rect | the rectangle it opens from, on the anchor's picture: the menu bar item, the parent item, the combo box, the cursor at the press (1x1), or `placed`'s corner for exact |
| edge | u8 | xdg_positioner's anchor edge (its numbering) |
| gravity | u8 | xdg_positioner's gravity |
| adjust | u8 | xdg_positioner's constraint adjustment (slide x 1, slide y 2, flip x 4, flip y 8) |

`PopupIntent` reads it as the popup is shown: a `#32768` menu whose owner
has a menu bar item open (`GetMenuBarInfo`) is a menu bar menu; one opened
from another menu's item (the other `#32768` of the thread, `MN_GETHMENU`,
`GetMenuItemRect` of its highlighted item) a submenu, flipped to open left
in a right-to-left window; a `ComboLBox` belongs to the combo box
`GetComboBoxInfo` names it for; any other menu is a context menu at the
cursor as it was when the popup was shown; `tooltips_class32` is a tooltip
below the cursor; anything else is exact. The host turns it into an
`xdg_positioner` (review-and-redesign.md, Part 4).

**PopupDismiss** (host to guest, id 28, a WindowId): the compositor
dismissed a grabbing popup, and the guest closes it as its kind closes:
WM_CANCELMODE to the owner of a `#32768` menu (which ends the whole menu
loop), CB_SHOWDROPDOWN FALSE to a combo box, Escape otherwise.

**WindowIcon** (guest to host, id 71, feature `icons`): a window id, the
size (0 small, 1 big), width and height (each at most 256) and straight
BGRA rows. The guest asks WM_GETICON (ICON_BIG, then ICON_SMALL2, with a
50 ms SendMessageTimeout), then the class's icons, when a window is
admitted and on each sweep, and sends a picture only when the handle
changed; the host keeps the last per window for a mirror made later.

**Input targets.** Every pointer message names its window already (§4.4).
The guest now raises that window to the top of its own z-order without
activating it (`SetWindowPos(HWND_TOP, SWP_NOACTIVATE)`) when another
window is at the point - before a press or a wheel turn, and asynchronously
before a move, never while a button is held - so input injected at the
window's own point reaches it whatever covers it in the guest.

### 7.6 The host half, native

The Qt host of §7.3 is replaced by a libwayland-client one with no Qt
(review-and-redesign.md, Part 4, for the protocols and the decisions).
What does what:

| Class | What it is |
|---|---|
| `PollLoop`, `EventLoop`, `Signal`, `Deferred` | the loop over the Wayland fd, sockets and timers; callbacks |
| `GuestLink`, `GuestWindows`, `ParentControl` | the wire, the window registry, `--managed`'s stdin and stdout (`RAISE [token]`) |
| `Globals`, `WaylandDisplay`, `Seat`, `PointerEvents`, `KeyboardEvents` | the registry, required and optional globals; pointer frames, value120, xkb keys and repeat |
| `MirrorSurface`, `ToplevelRole`, `PopupRole`, `SubsurfaceRole`, `Backdrop` | one surface and its role; the picture one to one, letterboxed when the compositor sizes it otherwise |
| `DmabufBuffers`, `ShmBuffers`, `CompositorGpu`, `DrmSyncDevice`, `SyncTimeline`, `PresentationTimes` | frames on the GPU or as pixels, explicit sync, presentation stamps |
| `PointerCursor`, `CursorPlayer` | cursor shapes, or a picture on a cursor surface, animated |
| `PointerGrabber`, `LockedPointer`, `ConfinedPointer`, `RelativePointer`, `ShortcutsInhibitor` | the guest's pointer grab, and Ctrl+Alt+G out of it |
| `Activation`, `ToplevelIcon` | xdg-activation tokens; the window's icon |
| `MirrorRole`, `PopupLayout`, `PopupStack` | what a window becomes; a popup's positioner; popups torn down top first |
| `WindowMirror`, `MirrorSession`, `ActivationSync`, `MonitorSession` | one guest window; one application; focus; the `--desktop` mirror |
| `BusConnection`, `KWinScript`, `KWinPlacement`, `PlacementSync`, `WindowReport` | sd-bus; first placement through KWin; `Describe` |
| `MoveRequests`, `StateSync` | a window's moves asked of the guest; maximise and minimise kept in step (§7.8) |

### 7.7 Checked, native

On KDE Plasma 6 (Wayland, fractional scale 1.2, AMD), against a test
program in the guest, both halves of this branch:

- frames as dma-bufs with explicit sync on the compositor's render node,
  no GL ("the compositor composes on 0000:03:00.0", "explicit sync on
  /dev/dri/renderD128", "frames stay on the GPU"); and as `wl_shm` pixels
  without the export socket;
- a menu bar menu (kind 1, anchor on the item, edge 6 gravity 8 adjust
  0xb), a submenu (kind 2, edge 7, adjust 0x7) whose item was chosen in
  the guest, a combo list (kind 3), a context menu at the cursor (kind 4,
  adjust 0xf), a tooltip below the cursor (kind 5, offset 10,17), all
  placed by the compositor;
- the compositor dismissing a grabbing menu, and the guest closing it;
- icons (40x40 for the test program) worn through
  `xdg_toplevel_icon_v1`; first placement through KWin at the guest's
  position; SIGTERM unloading the KWin scripts.

Not checked live: keyboard input (it would have taken the user's focus),
cursor shapes, pointer lock and confinement, fullscreen, move/resize from
the guest, the `--desktop` mirror, activation by token, and compositors
other than KWin.

### 7.8 Resizing, maximising and minimising

Built on branch `resize` (review-and-redesign.md, Part 5, for the causes
and the measurements).

**Wire.** `FrameInSlot` carries the picture's `width` and `height`
(u32 each, after `slot`); a frame of no pixels is refused. A ring
(`SlotRing`) is at least the picture's size, not exactly it: the guest
copies each frame to a slot's top left, and makes the ring again only
when the picture outgrows it or leaves more than 512 pixels of a side
unused (`picture_room.hpp`: the picture rounded up to 128, and 128
more). The host shows the picture from the buffer's top left through
its viewport. `SysCommand` SC_MINIMIZE is now sent by the host (the
compositor minimised the mirror, on KWin).

**Host.**

| Class | What it is |
|---|---|
| `MoveRequests` | the only sender of one window's `WindowMove`: one ask out at a time; the newest size and place asked together once the last lands (the frame the guest reports is an ask, or any other change of the frame); a size keeps the edges a resize does not move; a guest slower than 250 ms asked the newest regardless, its late answers not taken for refusals |
| `StateSync` | one state (maximised, minimised) kept the same on both sides: a change on one side asked of the other; that side's answer not sent back; what a side says while an ask of it is on its way ignored (1 s) |

`WindowMirror`: the window's size is the picture's as the guest reports
it (kept through a minimise); frames are shown in it one to one from the
corner, cropped or over the backdrop. A size the compositor gives
that it did not give before (a drag step, a restore, a tiling) is the
window's at once and asked of the guest, unless the guest window is
maximised or minimised or on its way to or from either; an answer of
another size is the window's. A place KWin reports is asked of the
guest only while neither side is in a state or on its way.
`MirrorSurface`'s fit methods leave the commit to the caller, so a
configure's acknowledgement is followed by one commit carrying its
geometry.

KWin: the placer moves a window (`frameGeometry` at its own size); the
watcher reports frame, user's drag ended, maximised or fullscreen, and
minimised (`maximizedChanged`, `fullScreenChanged`, `minimizedChanged`
connected). `PlacementSync` emits `moved_here` (the guest window's
picture to a point) and `minimized_here`, ignores state frames, does not
follow guest reports that answer the mirror's own moves, and puts KWin's
own placement back only for a move alone, never a resize or a user's
drag. A window the guest resized itself, which KWin re-centres, is put
back at the guest's corner.

**Outputs.** The guest has one monitor and the host may have several. The
manager names the output its main window is on (`--output <name>`, the
Wayland output name, `QScreen::name()`), and that output stands for the
guest's monitor: its pixel size is the guest's, a maximised mirror is sent
to it, and a window not seen yet is placed on it; where the name is not
among KWin's outputs it is the first output. `OutputLayout` holds what the
watcher script reports for each output (name, corner, logical size, scale)
and maps a mirror on *any* output: the output a frame's centre is on gives
its origin and scale, so a spot of DP-2 and the same spot of HDMI-A-3 are
that spot of the guest monitor in device pixels, and the guest window is
clamped to lie whole on the monitor - Windows pushes a window that is off
it back, and stops drawing one dragged off it. A guest move (`follow_guest`)
is placed on the output the mirror is on now, from the same reported frame,
so a window moving between outputs is never a guest move in itself: only
the user's drag changes which output the centre is on, the reported place
maps to a guest point once, the guest's echo of it is consumed
(`awaiting_guest`), and asked back it is the same host place. A rescale at
the new output's scale keeps the logical frame, so the mapping gives the
same place before and after, and the size settles after one request to the
guest.

**Guest.** The capture pool has the same room as the ring: shrunk with
`IDirect3D11CaptureFramePool::Recreate` in its session, grown with a new
session (whose first frame comes at once). A frame larger than its pool
(captured before it grew) is dropped and the pool made for it. After a
window is restored, frames are dropped until one is the window's size
(Windows' capture hands over the frame it took as the window was
minimised). A moved window's frame is read again 20 ms after the move as
well as 60 ms.

**Checked.** The integration tests (README, Tests): twelve cases, all
pass; all twelve fail against the host before this. Live on KDE Plasma
6.8 at 1.2, frames on the GPU, Notepad (build 26200) with this host and
guest, driven through KWin's scripting: a 20-step grow, maximise,
restore, minimise, unminimise and a 20-step shrink settled each within
70-330 ms with no stray move, no stub frame and no snap-back; the
capture made 4 sessions and 4 rings for it (87 and 16 before).

### 7.9 Decisions the conformance review wrote down

docs/rail-conformance.md found these to be the code's deliberate
behaviour, never stated here. Each is now a decision, for the reason given.

- **Z-order** (row 49). The host does not apply the guest's stacking to
  its toplevels: a Wayland client cannot stack them. ZOrder serves the
  activation rule (the marker) and the order windows are walked in.
  Popups and parts stack with what they belong to.
- **SysMenu** (row 65). The native host never sends it. It has no bars of
  its own to right-click, and the compositor's window menu tells a client
  nothing. The guest still carries it out.
- **Move/size end** (row 72). The host logs MoveSizeEnd and does not move
  its mirror to it. The button-up goes at once (§6.5), so the end arrives
  as the compositor's drag *begins*, and its corner is where the drag
  started. Positions come from the GEOMETRY deltas.
- **Move/size start position** (row 67). For kinds other than Move,
  MoveSizeStart's point is the cursor as the event is heard, not RAIL's
  last button-down: the guest half has no RDP stack to record one. The
  host reads it for no kind.
- **WindowMove** (row 73). The guest does nothing to a minimized or
  maximized window (Windows keeps those by their state). It holds a new
  size to the work area, moving the window in by as much as it would hang
  past it.
- **Icons** (row 29). WindowIcon is sent only for a window with a taskbar
  button: no other window has an icon anyone sees.
- **What Explorer does already** (rows 11 and 34). The guest sets no
  `ARW_HIDE` and sends no `TaskbarButtonCreated`: Explorer runs in this
  guest and does both (§5.9 had planned them).

Fixed rather than written down (rows 7, 9, 18, 39, 51, 56, 75 and 88):

- windows made children are forgotten;
- a ghost belongs to its hung window's application;
- the TASKBAR rule reaches KWin's taskbar and switcher;
- mirrors are opened only for shown windows, and given surfaces only once
  sized;
- the guest's active window is followed once its mirror maps;
- a compositor's focus on map is not sent as an activation;
- MinMax is applied side by side;
- WinUI's 0×0-then-sized popups are captured.

Open: the host's resize band lies inside the window, not outside it as
MS-RDPERP 1.3.4 has it (row 25).

### 7.10 A picture refused on the GPU

Affinity's welcome and its close confirmation did not show until the main
window was minimised and restored. Both are WPF windows whose
`WindowChrome` has no glass frame, and WPF rounds such a window's corners
with `SetWindowRgn` (a complex region, 8 px corners; measured in the guest
on a WPF window of its own). The host can cut a region (or a colour key)
out of pixels only - it draws nothing on the GPU - so it refused the first
frame each window sent on the GPU and sent `WindowPixels`. The guest marked
the window pixels-only and waited for the next frame, which Windows'
capture sends only when the window changes: a dialog sitting still never
sent one, and its mirror never mapped (`mapped` false, one frame, no
picture, in `Describe`, for Affinity's close confirmation). The main
window, which redraws as it loads, came back by itself. A minimise and
restore made the dialog redraw.

Fixed on both sides:

- **Host.** A mirror whose picture is cut out (`PictureCutout::why_pixels`)
  has its window sent to pixels before it is subscribed, and again whenever
  a change gives it a region or a key: no ring is made, no frame refused. A
  region one of whose rectangles holds the whole picture (WPF's
  square-cornered `WindowChrome`) cuts nothing and stays on the GPU.
- **Guest.** `WindowPixels` makes the window's capture afresh, as a route
  change makes every capture: its first frame comes at once. That covers a
  refusal for any other reason (the compositor not taking a dma-buf, an
  export failing).

Checked: the integration cases `welcome-in-front`,
`close-confirmation-in-front` and their `-rounded` variants, which play a
GPU device's export socket (`FakeExportDevice`) and send the rounded
windows' pictures on the GPU as the guest does. Against the host before
this and a guest that waits for the next frame, the rounded cases never
map the main window or the dialog; against it with a guest that captures
afresh they map, one ring each later. Live, on a private headless KWin
with the guest of this branch and a WPF window and owned dialog shown by
PowerShell: the host before this logged "a window region, cut out on
pixels" for each and, with the guest before this, never mapped them; with
both halves of this branch both map with no ring, the dialog transient for
the main window and active.

#### Kept on the GPU

The fix above sent every rounded window to pixels from the start, and a
WPF application of rounded windows - Affinity, a 1920x1010 main window -
went through readback, the link and a copy here on every frame. It felt
slow, and was: measured live on a WPF window of that size with
`WindowChrome`, no glass frame, and an animation drawing every frame,
15.5 frames a second.

What Windows' capture hands over for such a window was then measured in
the guest (a capture of its own through `CaptureStream`, per-monitor
aware): the region's outside comes clear, premultiplied (0,0,0,0), pixel
for pixel what `GetWindowRgn` says - 40 pixels of a 1125x750 window's
four 5,2,1,1,1 corners, everything inside opaque (255). A colour-keyed
layered window's keyed texels come clear too; a window's `LWA_ALPHA` is
not in the capture (its pixels are 255), as `wp_alpha_modifier_v1` wants.
A Windows 11 frame's own rounded corners come clear and its one-pixel
edge translucent (alpha 102). So nothing has to be cut, by either side:

- **Nothing cut.** `PictureCutout` is gone: no frame is refused for a
  region or a key, nothing is sent to pixels before it is subscribed, and
  a frame in pixels is shown as it came. The ring's textures are
  B8G8R8A8 and the device exports them as ARGB8888 (`AR24`), premultiplied
  as Wayland's are, so the corners show what is under them. Nothing is
  left on pixels for a cutout; a window that leaves a margin round itself
  still goes as pixels, to be trimmed.
- **Opaque where it can be told.** A window with a region that is not
  `WS_EX_LAYERED` has its region as its surface's opaque region
  (`GuestWindow::opaque_region`), in whole logical units inside it
  (`ViewMapping::logical_box_inside`), so the compositor blends only the
  corners. It is said only while the picture shown is the size the region
  was read with: a region comes before the frame of its new size, and
  claimed against the old picture it would draw its corners black. A
  window without a region gets none - a Windows 11 frame's corners and
  edge are translucent - and neither does a layered one, whose alpha is
  its own. An opaque format (XRGB8888) for any of them would draw those
  corners black, so the format stays ARGB8888.
- **Seen.** A mirror's `on_gpu` in `Describe` says whether its last
  picture came on the GPU.

Measured live, the same window and guest, a private headless KWin, ten
seconds of the animation each:

| | before (pixels) | after (GPU) |
|---|---|---|
| frames shown | 15.5 a second | 56.2 a second |
| host CPU | 26 % of a core, 16.9 ms a frame | 0.7 %, 0.12 ms a frame |
| guest half's CPU | 11.9 %, 7.6 ms a frame | 2.3 %, 0.42 ms a frame |
| capture to presented | - | 6.0-7.5 ms median (copy 0.03 ms) |

KWin's capture of the mirror (`ScreenShot2.CaptureActiveWindow`) has the
same 40 clear corner pixels the guest's capture has, and `on_gpu` true.

Checked: the integration cases `welcome-in-front-rounded` and
`close-confirmation-in-front-rounded` now play the GPU device with
dma-bufs KWin shows (udmabufs of the harness's memory) and pictures clear
outside their region, and judge that each rounded window gets a ring, is
never sent back to pixels and is shown from the GPU, and that KWin's
screen (`ScreenShot2.CaptureArea`) shows the window under it in its
corner and its own colour inside. Against the host before this they fail
on the GPU route; with an opaque region claiming the whole picture they
fail on the corners, drawn black.

## Appendix: binaries, symbols, method

Copied read-only over the guest's SMB share into the session scratchpad. The
copies are not in git.

| Binary | Size | Version | PDB (name, GUID+age) |
|---|---|---|---|
| rdpshell.exe | 1205704 | 10.0.26100.7920 | rdpshell.pdb 880FDBF501E6E675E0372DF132D7B8991 |
| rdpinit.exe | 538016 | 10.0.26100.7920 | rdpinit.pdb 6728F9D4D7B7DCE3FB96C0001ABF71161 |
| RdpShellExt.dll | 1115552 | 10.0.26100.7920 | RdpShellExt.pdb 18ED25AB26CF902824C7E8E955F094D41 |
| RdpShellLoader.dll | 161224 | 10.0.26100.7920 | RdpShellLoader.pdb D55417F973CCED88B0CAEA14012BE4D51 |
| rdpcorets.dll | 1814528 | 10.0.26100.7920 | RdpCoreTS.pdb 2F193AB90262AB8F945A17C1EBAE9C7F1 |
| rdpserverbase.dll | 2348488 | 10.0.26100.7920 | rdpserverbase.pdb 93E966697644C5D1D7B3BBD15BEAA3D01 |
| termsrv.dll | 1253376 | 10.0.26100.7920 | termsrv.pdb 81988017C88A84B5D935A7B31B964BE61 |
| rdpbase.dll | 1844640 | 10.0.26100.7920 | rdpbase.pdb 87D0D1DD42A62D766B80B7C855195D2B1 |
| rdpinput.exe | 253952 | 10.0.26100.7920 | rdpinput.pdb 4DAB58B10B915C48E56B510A9D7B9A171 |
| win32kfull.sys | 4259840 | 10.0.26100.8036 | win32kfull.pdb 68F171C739B3D5FA0D55ACBD2B31C5E31 |
| user32.dll | 1873304 | 10.0.26100.7920 | user32.pdb 5E38C093E8AC8F829B6C5A1A3322781B1 |

**PDBs.** Every PDB came from `https://msdl.microsoft.com/download/symbols`.
The GUID goes into the path in the order the CodeView record prints it.

**Ghidra.**

- rdpshell.exe, rdpinit.exe and RdpShellExt.dll were imported with their
  PDBs and auto-analysed.
- A post-script (`ExportFuncs.java`) decompiled every function of the RAIL
  classes and the named helpers.
- Virtual calls were resolved by reading vtables from the PDB's `??_7…`
  symbols.

**win32kfull.sys** was examined by disassembly around the
`xxxWindowEvent` calls in `xxxMoveSize` only.
