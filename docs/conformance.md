# Conformance

> **The harness is gone** (branch `native`, review-and-redesign.md Part 4):
> it ran the Qt mirror in-process, and the host is now a Wayland client of
> its own with no Qt. What follows is the design it had, kept for when it
> is written again against `Describe` and the compositor's view. The probe
> (`remotewin-probe`) remains.

Does a mirror look and behave like the window it mirrors? The conformance
harness answers that by asking Windows. A probe in the guest makes windows
to a spec and reports what Windows made of them; the harness runs the
mirror in its own process, looks at what the mirror decided and what it
paints, and compares both with the truth. Differences are written up as a
report with pictures.

    guest (Windows)                              host (Linux)
    ┌─────────────────────────────┐              ┌──────────────────────────────────┐
    │ remotewin-probe.exe         │◀── probe ───▶│ remotewin-conform                │
    │   makes windows to a spec   │   channel    │   ProbeLink                      │
    │   reports truth + screen    │   (TCP)      │   ScenarioRunner                 │
    │   journals their input      │              │     Expectation, PixelComparison │
    │ remotewin-probe --backdrop  │              │     InputExpectation, HostScene  │
    │   (own process, not         │              │   RunReport                      │
    │    mirrored)                │              │                                  │
    │ remotewin-guest.exe ────────┼── mirror ───▶│   GuestLink + MirrorSession      │
    │   (the real guest half)     │   (TCP)      │   (the real mirror, in-process)  │
    └─────────────────────────────┘              └──────────────────────────────────┘
                  ▲                                       │
                  └── LSW agent: launch, start_window_mirror ┘  (GuestAgent, the only LSW glue)

## Running it

From the LSW checkout, with the VM up:

    scripts/remotewin-conform.sh                       # build, deploy the probe, run everything
    scripts/remotewin-conform.sh --no-build -- --filter '^input-' --out /tmp/conform
    scripts/remotewin-conform.sh --guest               # also redeploy remotewin-guest.exe

The script builds `remotewin` and `remotewin-conform` in `build/` and
`remotewin-guest`/`remotewin-probe` in `build-agent/` (by name: none of
them are in a default build), copies the probe to
`C:\ProgramData\LSW\remotewin-probe.exe` over SMB (renaming a copy in use
aside), and runs the harness. Harness options after `--`:

| option | default | |
|---|---|---|
| `--scenarios <json>` | `tests/conformance/scenarios.json` | the scenario file |
| `--filter <regex>` | `.` | run the scenarios whose name matches |
| `--out <dir>` | `./conform-<timestamp>` | report folder |
| `--tolerance <n>` | 8 | largest channel difference that still matches |
| `--agent <host:port>` | `127.0.0.1:5102` | LSW's guest agent |
| `--probe-exe`, `--probe-log` | under `C:\ProgramData\LSW` | where the probe is, and logs |
| `--no-launch` | | wait for a probe started by hand |

Windows appear on the host desktop while it runs. Everything is taken
down at the end: the probe destroys its windows and exits on `quit` (its
backdrop process watches it and exits with it), and the guest half exits
when the harness closes the mirror connection. The exit code is non-zero
when any scenario failed.

The report folder has, per scenario, `guest.png` (the guest's screen
around the windows), `host.png` (the mirrors as they paint, laid over the
same backdrop), `diff.png` and `scenario.json` (every truth, mirror state,
check and pixel metric); `report.html` lists every scenario, failures
first, and opens from the folder with nothing else; `run.json` is the
summary.

## Parts

`include/proto/probe/` - the probe channel's wire, compiled for both ends
and tested host-native (`tests/probe_wire_test.cpp`): `WindowSpec`,
`WindowTruth`, `ScreenPixels`, `InputRecord`, `ProbeHello`. Same framing
as the mirror (`MessageWriter`/`MessageReader`/`FrameDecoder`), ids in a
range of their own: harness commands from 128, probe answers from 192.
Every answer starts `[ok:u8][error:string]`.

| command | fields | answer |
|---|---|---|
| `ProbeCreateWindow` | `WindowSpec` | `ProbeWindowCreated` handle:u64 |
| `ProbeObserve` | handle:u64 | `ProbeTruth` `WindowTruth` |
| `ProbeCaptureScreen` | left, top:i32, width, height:u32 | `ProbeScreenPixels` `ScreenPixels` |
| `ProbeDestroyWindow` | handle:u64 | `ProbeDone` |
| `ProbeDestroyAll` | | `ProbeDone` |
| `ProbeDrainInput` | | `ProbeInput` count:u32, `InputRecord`... |
| `ProbeSetBackdrop` | rgb:u32 | `ProbeDone` |
| `ProbeQuit` | | `ProbeDone` |
| `ProbeAct` | `ProbeAction` | `ProbeActed` handle:u64 (the window it made, or 0) |
| `ProbeObserveCursor` | | `ProbeCursorTruth` `CursorTruth` |

The probe sends `ProbeHello` unasked when it connects.

`source/probe/` - `remotewin-probe.exe`, per-monitor DPI aware v2 like the
guest half:

* `ProbeSession` serves the harness from a message-only window of its
  own. `CommandReader` reads the socket on a thread of its own and posts
  that window a message as commands arrive, so whichever message loop the
  windows' thread is in dispatches them - its own, or the modal loop
  Windows runs for a dialog or a message box. A command that opens a modal
  dialog is answered from inside the modal loop it starts, once a timer
  finds the new dialog up (`ProbeWindows::new_dialog`).
* `ProbeWindows` has every window: those made to a spec (owned), and those
  Windows made for an action - dialogs, message boxes - (followed). A
  window's truth is read by `read_window` (any window of the thread) with
  its z-order rank among the rest added.
* `WindowActions` does a `ProbeAction`: `EnableWindow`, `ShowWindow`,
  `SetWindowLongPtr(GWLP_HWNDPARENT)`, `SetForegroundWindow`,
  `SetWindowPos` (with its insert-after and `SWP_` flags), `MoveWindow`,
  a modal dialog (`DialogBoxIndirectParam`, so Windows' own modal loop
  disables the owner), a modeless one (`CreateDialogIndirectParam`),
  `EndDialog`, `MessageBox` (`MB_APPLMODAL`/`MB_TASKMODAL`/`MB_SYSTEMMODAL`,
  owned or not; closed later by `WM_CLOSE`), `DestroyWindow`, and a click
  by `SendInput` - what Windows does with a click the mirror does not pass
  on. `DialogBoxes` makes the dialogs from in-memory templates (no
  controls) and journals their input like a probe window's.
* `ProbeWindow` makes one window: registers a class per class style,
  grows the window around the client size (`AdjustWindowRectExForDpi`),
  sets the layered attributes, region and DWM attributes, then shows it.
  It paints `TestPattern` and notes every input message, with its window
  procedure's answer, in the `InputJournal`.
* `TestPattern`: four flat quadrants, a one-pixel checker strip along the
  top (any resampling greys it), and a patch of the colour key for keyed
  windows. No text.
* `capture_screen`: `BitBlt` of the screen DC with `CAPTUREBLT` after a
  `DwmFlush`: what DWM composed, layered windows included, no cursor.
  Checked against translucent and keyed windows over the backdrop.
* `PointerActions` does what an application does to the cursor and the
  pointer: `SetClassLongPtr(GCLP_HCURSOR)`, a window answering
  `WM_SETCURSOR` with a cursor (and `SetCursor` at once), `ShowCursor`,
  `ClipCursor`, `SetCursorPos`, `SetCapture`, `RegisterRawInputDevices`,
  putting the cursor back at the client middle on every `WM_MOUSEMOVE`
  (mouse look without raw input), a `SendInput` move or click, the
  taskbar taking the foreground, and "Enhance pointer precision" for the
  session only (`SPI_SETMOUSE` without writing the profile). The cursor and
  pointer are the desktop's, not a window's, so all of it is undone before
  the next scenario (`reset`: count, clip, capture, raw input, class
  cursors, ballistics).
* `ProbeCursors` has the cursors a scenario names: Windows' own by IDC_
  number, and the probe's - a 32x32 colour cursor with soft alpha, a
  monochrome one (`CreateCursor`) whose middle inverts, an animated one
  written as an `.ani` file (`AniFile`: four steps of 6/12/6/30 jiffies)
  and loaded from it, and a 96x96 one.
* `CursorReader` reads the cursor truth (`CursorTruth`): `GetCursorInfo`,
  the system cursor it is (LoadCursor's shared handles), ShowCursor's
  count, `GetClipCursor`, `GetCapture`, the raw input registration, the
  ballistics (`SPI_GETMOUSESPEED`, `SPI_GETMOUSE`), `SM_CXCURSOR`, and
  every step of the cursor (`GetCursorFrameInfo`) drawn by `DrawIconEx`
  over white and over black - what the user sees, inversion included.
  `ProbeWindow` decodes `WM_INPUT` into its journal (`GetRawInputData`:
  `InputRecord` says how) and counts the moves it puts back.
* `BackdropWindow`/`BackdropProcess`: a flat full-screen window behind the
  probe's windows, in a process of its own so the mirror (scoped to the
  probe's pid) does not mirror it over the host desktop.

**Geometry.** A capture of a window (Windows.Graphics.Capture) is not
its window rectangle: measured on Windows 11 at 120%, a 378x285 overlapped
window at 400,300 arrives as 362x277, the size of
`DWMWA_EXTENDED_FRAME_BOUNDS` (408,300-770,577), at the frame's corner -
the invisible resize border (8 px left, right and below) is not in it, and
the frame's content size says so. So the guest half measures everything
the host is told or tells it from the corner of the extended frame bounds
(`WindowSurface`): the window list's position and size, regions (moved
from window coordinates), pointer input, and resize requests (the window
rectangle is the asked-for picture plus the border). A transparent margin
is trimmed off the picture only for a frameless, unshaped window, where it
is room reserved for a shadow (WinUI menus) rather than part of the
window.

`source/host/` is now `rwin_host` (a library) plus `main.cpp`; the
`remotewin` executable is unchanged. Two narrow additions let a harness
look at a mirror from outside: `MirrorSession::find(handle)` and
`MirrorWindow::state()`, which returns a `MirrorState` - the window flags
and type, popup parent, activation, visibility and min/max state,
whether input is forwarded, whether it is resizable, geometry (logical
size, device pixel ratio, fit, picture rectangle in device pixels),
opacity, colour key, mask, cursor shape, and frames presented.

`source/conform/` - `remotewin-conform`:

* `GuestAgent` - LSW's agent wire (`[type=1][len:u32be][JSON]`), `launch`
  and `start_window_mirror`. Direct rather than through the manager's MCP:
  it needs neither the manager running nor an MCP session for two calls.
  remotewin itself still knows nothing of LSW; this class is all the glue.
* `ProbeLink` - the probe connection; each call waits for its answer with
  the event loop running, so the mirror keeps working meanwhile.
* `ScenarioBook` - reads the scenario file (style words by their Win32
  names), the `matrix` section expanded by `ScenarioMatrix`.
* `ScenarioRunner` - one scenario: make the windows, read their truth
  (`as_made`), wait for the mirrors to settle, read the truth again, look
  at both sides, check, put input in, tear down.
* `HostScene` - the host's picture: each mirror's `QWidget::grab()` (its
  own `paintEvent`, at its own device pixel ratio, into a transparent
  buffer), cropped to its picture rectangle and laid at the guest window's
  visible bounds over the backdrop, in the guest's stacking order.
* `PixelComparison` - per-pixel max channel difference within a region:
  count, max delta, bounding box, difference image.
* `Expectation` - the property table (below).
* `PointerExpectation` - the cursor and pointer rows (below);
  `CursorPictureOnGlass` lays a guest cursor and a mirror's hotspot on
  hotspot, each as its screen shows it; `MotionExpectation` - what a held
  pointer's motion must arrive as.
* `InputExpectation` - what one input step must arrive as.
* `RunReport` - the report folder.

Tests: `probe_wire_test`, `pixel_comparison_test`, `expectation_test`,
`input_expectation_test`, `scenario_book_test` (which also reads the
shipped scenario file, matrix expanded), `scenario_matrix_test`; and on
the mirror's side `key_translator_test` (the evdev to set 1 table) and
`proto_test` (`KeyStroke`, `GuestDesktop`, byte for byte).

`WindowTruth` carries, besides the words and rectangles: whether the
window still exists, its class name, `GetActiveWindow`/`GetFocus` as they
touch it, whether its owner chain is disabled, `GW_ENABLEDPOPUP`, and its
rank in the z-order among the probe's windows. The journal also keeps
`WM_NCACTIVATE`: a caption toggled on and off is `FlashWindowEx`.

## Steps

A scenario's `steps` are done one at a time, and after each both sides
settle and are read, looked at and checked again, like the scenario first
was: every window's rows, its pixels, and the step's own input checks. The
report shows each step as a stage (`step3-guest.png`...), with each
window's state on both sides in a line. A step is an input step (as in
`input`), an action of the application (`{"do": ...}`), or the user moving
a mirror on this desktop (`{"host": "move"}`). Windows an action makes are
added to the scenario at the next place, and later steps name them so:

    {"do": "enable", "window": 0, "on": false}
    {"do": "show", "window": 0, "show": "SW_MINIMIZE"}        (any SW_ name)
    {"do": "owner", "window": 2, "owner": 1}                  (no "owner": none)
    {"do": "foreground", "window": 1}
    {"do": "position", "window": 0, "at": [x, y], "size": [w, h], "z": "top|bottom|topmost|notopmost",
     "flags": ["SWP_NOSIZE", "SWP_NOMOVE", "SWP_NOZORDER", "SWP_NOACTIVATE", ...]}
    {"do": "move", "window": 0, "at": [x, y], "size": [w, h]}
    {"do": "modal", "owner": 0, "client": [w, h], "at": [x, y] | "centred": true, "title": "..."}
    {"do": "modeless", ...as modal}
    {"do": "end_dialog", "window": 1}
    {"do": "message_box", "owner": 0, "flags": ["MB_TASKMODAL"], "title": "..."}   (no owner: unowned)
    {"do": "close_box", "window": 2}
    {"do": "destroy", "window": 1}
    {"do": "guest_click", "window": 0, "at": [x, y]}           (SendInput in the guest)
    {"host": "move", "window": 0, "to": [x, y]}               (the mirror moved by KWin)
    {"host": "unfocus" | "focus" | "escape" | "regrab", "window": 0}
    {"do": "class_cursor" | "answer_cursor", "window": 0, "cursor": "IDC_IBEAM"}
                         (any IDC_ name, or none, colour, monochrome, animated, large)
    {"do": "show_cursor", "on": false}
    {"do": "clip_cursor", "window": 0, "at": [x, y], "size": [w, h]}   (client; no size: ClipCursor(NULL))
    {"do": "set_cursor_pos", "window": 0, "at": [x, y]}
    {"do": "capture", "window": 0, "on": true}
    {"do": "raw_input", "window": 0, "flags": ["RIDEV_INPUTSINK"], "on": true}
    {"do": "recentre", "window": 0, "on": true}
    {"do": "guest_move", "window": 0, "at": [x, y]}           (SendInput in the guest; may be outside)
    {"do": "activate_shell"}                                  (the taskbar takes the foreground)
    {"do": "pointer_precision", "on": true}
    {"window": 0, "move": [x, y], "then": [[x, y], ...]}     (a sweep: only the first point is checked)
    {"window": 0, "relative": [dx, dy]}                       (the held pointer's motion, in counts)

The host steps other than `move` are the user on this desktop, through
Qt's window-system interface as the platform plugin reports them: the
mirror losing and getting the keyboard focus
(`handleFocusWindowChanged`), the pointer's escape (Ctrl+Alt+G) pressed on
it, and a click on it that takes the pointer back. A `relative` step is the
held pointer's motion fed to the mirror where the relative pointer
protocol feeds it (`PointerGrabber::take_motion`), in four parts: no
client can make a compositor move a pointer.

The undisturbed row of a stage compares the window as the step left it,
before the mirror reacted, with the settled one. A `guest_click` is noted
in its stage rather than checked: which windows it activated, and how
many times each caption was toggled. A click on a disabled window whose
`GW_ENABLEDPOPUP` is mirrored is checked for that window being brought
forward (asked to be active, alerted); a host move for the guest window
being under the mirror afterwards.

## What is compared

**Settling.** A scenario is looked at only when every window that should
have a mirror has one that has shown a frame, and nothing about any mirror
- frames presented, geometry, state, device pixel ratio, picture - has
changed for 700 ms (a window that must not be mirrored is given 1.5 s to
be mirrored anyway). The guest's screen and the host's paint are then
taken, and the mirrors' signature is read again 300 ms later; if it moved,
the look is repeated (up to three times; the report says how many it
took).

**Pixels.** The guest picture is the screen over the union of the windows'
visible bounds (`DWMWA_EXTENDED_FRAME_BOUNDS`) plus 16 px, clipped to the
work area. The host picture lays each mirror's paint at the same visible
bounds over the same backdrop colour, one to one when the mirror draws one
to one (fit 1) and resampled to the bounds otherwise (reported as
`resampled`). Only pixels inside some window's visible bounds are counted;
the margin is drawn, dimmed, in the difference image. Rounded corners,
alpha and colour keys are in both pictures, so they are compared like
everything else. A pixel matches when no channel differs by more than the
tolerance (8; observed differences on matching pictures are 0-1).

Where a toplevel mirror sits on the host desktop is the compositor's
choice (Wayland gives a client no global position) and is not compared;
where a popup sits against its owner is, as the `owner_offset` property.

**The shell's overlay.** Before any scenario the harness reads the bare
work area, backdrop only: whatever is not the backdrop there is drawn by
the guest's shell over every window - on the test VM, the "Activate
Windows" watermark of a copy whose activation lapsed (2160,1264 287x52 at
120%). Those pixels are neither side's window and are left out of every
comparison; `run.json` names the region.

**Shadow.** A band 8 px wide around each window (less any other window) is
compared with the backdrop; if 15% of it is shaded, the window casts a
shadow on the guest. That is measured truth, not a rule. Where too little
of the band can be seen (another window covers it), Windows' rule stands
in: `CS_DROPSHADOW` or a frame DWM draws, and neither when
`SPI_GETDROPSHADOW` - "Show shadows under windows", reported in the
probe's hello - is off. Maximised windows are not measured (the band is
off the work area). The guest half reports `SPI_GETDROPSHADOW` too, at the
head of every window list (`GuestDesktop`), read at start and again on
`WM_SETTINGCHANGE`, and `Win32Style::casts_shadow` honours it.

**Properties.** `Expectation` is a table of rows, each a Windows behaviour
with the rule it comes from, computed from the truth's words directly -
not through `Win32Style`, which is what the mirror decides with and which
a check must be able to catch. Rows that do not apply to a window are left
out.

| row | Windows says | mirror side |
|---|---|---|
| `mirrored` | a shown window is on screen, a hidden one is not | a mirror exists |
| `undisturbed` | a window nobody touched keeps its size, place and state | the truth when made vs once settled |
| `shown` | `IsWindowVisible` | `isVisible()` |
| `owner` | `GW_OWNER`: kept above its owner | popup of the owner's mirror |
| `owner_offset` | placed where its owner put it | where the popup asked to be vs its owner, in guest px (±1) |
| `taskbar` | unowned, not `WS_EX_TOOLWINDOW`/`WS_EX_NOACTIVATE`; `WS_EX_APPWINDOW` forces | toplevel of type `Qt::Window` |
| `stays_on_top` | `WS_EX_TOPMOST` | `Qt::WindowStaysOnTopHint` |
| `takes_focus` | not `WS_EX_NOACTIVATE` | no `Qt::WindowDoesNotAcceptFocus` |
| `click_through` | `WS_EX_LAYERED` + `WS_EX_TRANSPARENT` | `Qt::WindowTransparentForInput` |
| `takes_input` | `IsWindowEnabled` | forwards input |
| `resizable` | `WS_THICKFRAME` | minimum != maximum |
| `minimized`, `maximized` | `IsIconic`, `IsZoomed` | the mirror's state (toplevels) |
| `picture_size` | visible bounds | picture size / fit |
| `picture_origin` | the visible edge is the mirror's edge | picture at 0,0 |
| `fit` | one to one unless maximised | fit 1 |
| `opacity` | `LWA_ALPHA` | painted opacity |
| `color_key` | `LWA_COLORKEY` | keyed colour |
| `shape` | `SetWindowRgn` | a mask |
| `shadow` | a shadow seen on the guest's screen (the rule above where unseen) | no `Qt::NoDropShadowWindowHint` |
| `title` | `GetWindowText` | window title |
| `modal` | the owner is disabled while this enabled owned window is up (DialogBox, MessageBox) | marked modal: attached to, or a popup of, a mirror whose window is disabled |
| `activation` | the foreground window (SetForegroundWindow, a dialog opening, a dialog closing back to its owner) | active, or asked to be; a `WS_EX_NOACTIVATE` window never asks |
| `host_position` | the visible frame's place on the guest's screen | the compositor's frame corner (as KWin reports it), mapped to guest pixels (±1); toplevels |

`owner` accepts either kind of owned mirror: a popup of the owner's mirror,
or a toplevel attached to it (its transient parent).

**Cursor and pointer.** A scenario with `"pointer": true` is also looked at
for the cursor and the pointer after every step: the cursor truth is read
and `PointerExpectation` gives the rows below - again from the truth
directly, not through the guest half's `PointerGrabRule`. The cursor rows
are of the mirror the scenario's last pointer step put the pointer over;
the hold rows of the foreground window's mirror. The settle signature of
such a scenario includes the cursor the mirrors wear and what they hold.

| row | Windows says | mirror side |
|---|---|---|
| `cursor_hidden` | `GetCursorInfo`: shown while `CURSOR_SHOWING`, not suppressed, with a cursor (ShowCursor's count below 0, or `SetCursor(NULL)`, shows none) | `Qt::BlankCursor` |
| `cursor_shape` | a system cursor (`IDC_*`) is this desktop's own of the same meaning (the CSS name cursor-shape-v1 draws: `IDC_IBEAM` text, `IDC_HAND` pointer, `IDC_APPSTARTING` progress...); any other is the guest's pixels as `DrawIconEx` draws them over white; over the mirror's resize band, the size cursor DefWindowProc shows on that sizing border | the shape worn, or its pixels on the glass, hotspot on hotspot (tolerance 8) |
| `cursor_over_dark` | the guest's pixels over black | the same, over black |
| `cursor_hotspot` | ICONINFO's hotspot, as a place in the drawn picture | the hotspot Qt is told, at the screen's ratio |
| `cursor_size` | one guest pixel to one screen pixel | the cursor surface's size times the ratio over the pixmap's |
| `cursor_animated` | every step for its own time (`GetCursorFrameInfo`) | the same steps and times, stepping |
| `cursor_outside` | `SetCapture`: the capturing window's cursor stays outside it | (known limit, below) |
| `confine` | `ClipCursor` with the cursor shown, in the foreground | a confinement asked of the compositor, to the clip's part of the mirror in logical units, rounded inward (±1) |
| `pointer_lock` | mouse look: a hidden cursor clipped, put back on every move (seen to be: three in a row), or read raw - or a clip at most 16 px square | a lock asked of the compositor; nothing for a cursor that is only hidden |
| `release` | `ClipCursor(NULL)`, the cursor shown, another application in front; or the user here let go (the mirror lost the focus, Ctrl+Alt+G) | nothing asked |

A `relative` step is checked for the mirror passing the motion on whole,
for `WM_INPUT` carrying the very counts (within one) when raw input is
registered, and - unless `RIDEV_NOLEGACY` - for the cursor moving the same
way (`WM_MOUSEMOVE`; each move away from the middle of a window that puts it
back is added up), or not at all when a one-pixel clip holds it. How far it
moves is Windows' ballistics' and is written down, not compared.

**Input.** A step is put into the mirror's `QWindow` through Qt's
window-system interface (`qt_handleMouseEvent`,
`QWindowSystemInterface::handleExtendedKeyEvent`) - the path a
compositor's event takes from the platform plugin onwards. A pointer step
aims at the centre of the guest pixel under a client point (counted from
the right in a `WS_EX_LAYOUTRTL` window), mapped through the mirror's own
picture rectangle, fit and device pixel ratio. A key step is a chord -
modifiers pressed in order, the key pressed and released, the modifiers
released in reverse - each key carrying the native scan code the platform
would give it (on Wayland and X11 the evdev code plus 8) and the text it
types. The probe's journal must then hold what a real mouse or keyboard
gives a window, in order (other messages - hit tests, cursor queries,
activation - may come between): a move is `WM_MOUSEMOVE` at that client
point (approached from the next pixel, as a pointer already resting there
would not move); a click is `WM_LBUTTONDOWN` and `WM_LBUTTONUP` there (no
move is required: the last scenario's click may have left the pointer on
the spot); every key going down or up is `WM_KEYDOWN`/`WM_KEYUP` (F10:
`WM_SYSKEYDOWN`/`WM_SYSKEYUP`) with its virtual key and, in lParam bits
16-24, its scan code and extended bit, and the key's `WM_CHAR` follows its
press if `TranslateMessage` makes one on a US layout. A window that is active with nothing focused in it - `EnableWindow(FALSE)`
takes the focus, and enabling it again does not give it back - is given
its keys as `WM_SYSKEYDOWN`/`WM_SYSKEYUP` and `WM_SYSCHAR` ("If no window
has the keyboard focus, the system posts WM_SYSKEYDOWN to the active
window"); a dialog box is given no `WM_CHAR` (`IsDialogMessage` takes it as
a mnemonic). A disabled window
must be given no mouse or keyboard message at all, and a `WS_EX_LAYERED |
WS_EX_TRANSPARENT` one no mouse message (the pointer passes through it).
A resize step sizes the mirror as an edge drag would and expects the guest
window's visible frame to become the size the mirror holds whole; the
mirrors are then let settle before the next step.

The probe answers `WM_SYSCOMMAND SC_KEYMENU` itself: F10 (or Alt alone)
would otherwise start DefWindowProc's modal menu loop on the probe's only
thread, which then stops answering the harness.

After a click the window's foreground is checked too (`activation`):
Windows activates the top-level window clicked, owned or not, unless it
is `WS_EX_NOACTIVATE`, disabled, or a click-through window the click
passes; those leave the foreground where it was.

What this cannot see: the compositor's own output (its shadow, its input
region) - Wayland gives no client a screenshot, so the host side is the
buffer the mirror hands the compositor and the hints it asks with. A
click-through mirror is therefore checked by its hint, not by clicking,
since input injected through Qt arrives past the compositor's input
region.

## Scenario file

    {"backdrop": "#1E7860",
     "scenarios": [
       {"name": "owned-popup", "about": "...",
        "windows": [
          {"style": ["WS_OVERLAPPEDWINDOW"], "client": [360, 240], "at": [400, 300], "title": "owner"},
          {"class_style": ["CS_DROPSHADOW"], "style": ["WS_POPUP", "WS_BORDER"], "ex_style": [],
           "client": [200, 120], "at": [560, 420], "title": "owned", "owner": 0,
           "show": "normal|noactivate|maximized|minimized|hidden",
           "layered": {"alpha": 128, "color_key": "#FF00FF"},
           "shape": "none|rounded|ellipse|frame",
           "dwm": {"corners": "default|square|round|small", "dark": true,
                   "nc_rendering": "default|disabled|enabled"}}],
        "input": [{"window": 1, "move": [10, 20]}, {"window": 1, "click": [150, 90]},
                  {"window": 0, "key": "Ctrl+Shift+A"}, {"window": 0, "resize": [480, 360]}]}],
     "matrix": {...}}

`at` is the window rectangle's corner and `client` the client size, both
in the guest's physical pixels. `owner` is the index of an earlier window.
A key is `A`-`Z`, `0`-`9`, `Minus`, `Equal`, `LeftBracket`,
`RightBracket`, `Semicolon`, `Apostrophe`, `Grave`, `Backslash`, `Comma`,
`Period`, `Slash`, `Space`, `Escape`, `Return`, `Tab`, `Backspace`, the
arrows, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`, `F1`-`F12`,
`KpEnter`, `KpDivide`, `KpMultiply`, `KpMinus`, `KpPlus`, `Shift`, `RShift`,
`Ctrl`, `RCtrl`; a chord prefixes it with modifiers (`Shift`, `RShift`,
`Ctrl`, `RCtrl`) and `+`. Ctrl is modelled with letters only (Ctrl+A types
0x01). Alt and the Windows keys are not scenario keys: Alt opens the menu
loop and the Windows key the Start menu, both outside the probe's window.

## When the guest stalls

On the LSW VM (the `avgpu` virtual GPU, QEMU user networking) the guest
side has stalled now and then, for a minute or for good: the guest half's
Direct3D device once took 46 s to make; the probe once did not answer for
about 100 s (its screen capture is a `DwmFlush` and a `BitBlt`); and in
some full runs the guest half went silent mid-run (its log stops between
two frames). Every scenario after that fails with "the mirrors did not
settle" and no mirror at all. Nothing in the scenario at hand differs
from the many runs that pass every scenario, so it is the environment and
not a check: run again. The harness keeps such a stall from spreading -
the guest half makes its device before it dials in (and the harness waits
90 s for it), and an answer the probe gives after the harness gave up on
it is dropped rather than read as the answer to the next command.

## Known limits

A row the mirror cannot pass for a reason below it is marked a known
limit (`Expectation::Check::known_limit`, with the reason): the report
lists it, in yellow and in a section of its own, and it fails nothing.

| row | when | why |
|---|---|---|
| `taskbar` | an owned window with `WS_EX_APPWINDOW` | Windows gives it a taskbar button *and* keeps it with its owner. The mirror is a popup of the owner's (no taskbar entry on Wayland) or a toplevel attached to it, which the task manager leaves out for the window it is attached to. |
| `owner` | the owner is hidden (`SW_HIDE`) and the owned window is not | Windows hides an owner without hiding what it owns. A Wayland surface is placed against, or attached to, a mapped surface only; the owned window's mirror stands alone until its owner is shown again. |
| `host_position` | not on KWin | Wayland gives a client neither a place for its window nor word of where the compositor put it; only KWin's scripting does both. Elsewhere the compositor places mirrors. |
| `cursor_over_dark` | a monochrome cursor with inverting pixels (AND 1, XOR 1) | A Wayland cursor is a picture with alpha; nothing composes it as an inversion of what is under it. Those pixels are drawn black, edged in white where they meet transparency: right over white, visible but not inverted over black. |
| `cursor_outside` | a window holds the capture and the pointer has left it | A Wayland client's cursor shows only while the pointer is over one of its surfaces, or held to one by a button pressed on it; a capture without a button has no counterpart, and outside the mirror the desktop's own cursor shows. |
| `pointer_lock` | a hidden cursor read raw, neither clipped nor put back | No process can read another's raw input registration, and a hidden cursor that is neither clipped nor put back is also a video player's. The guest half cannot tell this game from one, and gives it absolute moves. |
| `confine`, `pointer_lock` | not Wayland, or no `zwp_pointer_constraints_v1` | Nothing can be asked of the compositor. |

Not rows, but limits of what is compared, for the same reason:

* **Z-order without activation.** `SetWindowPos(HWND_TOP | HWND_BOTTOM,
  SWP_NOACTIVATE)` restacks windows without activating any; a Wayland
  client can only raise a window by activating it, so the mirrors' stacking
  is not compared (the guest's rank is in each truth, and the host scene
  is laid in it). `HWND_TOPMOST`/`HWND_NOTOPMOST` are followed: they are
  `WS_EX_TOPMOST`, the `stays_on_top` row.
* **The flash.** `FlashWindowEx` on the guest is noted, not compared; the
  mirror asks the desktop for attention (`QApplication::alert`), which
  the compositor shows its own way.

* **Popup placement.** `owner_offset` compares where the mirror *asked*
  its popup to be (`MirrorState::placed`), in whole logical units, which is
  all `xdg_positioner` takes. The compositor answers with a position on
  its own grid: at 120% KWin put a popup asked for at 127 logical units
  (152.4 device pixels, for 152) at 128 (153.6) - up to a logical unit
  away, which a client can neither prevent nor see exactly (Qt's `pos()`
  is that answer, rounded). Input is unaffected: it is measured from where
  the guest put the window, not from where the popup ended up.
* **Shadows.** Whether the compositor shades a surface is the
  compositor's; the mirror is checked by the hint it asks with. On a guest
  with "Show shadows under windows" off (as the test VM is) every window
  is unshaded, so the matrix exercises the "off" half of the rule only.
* **Non-client hit testing.** A press on a caption or border starts
  Windows' modal move or size loop, which the guest half cancels and hands
  to the compositor (`startSystemMove`/`startSystemResize`); where the
  compositor then takes the window is not observable to a client, so the
  matrix clicks client areas only.
* **The taskbar on the guest.** No API reports whether a window has a
  taskbar button, so `taskbar` is Windows' documented rule, not a
  measurement.
* **Input methods.** A mirror does not enable Qt's input method (with
  ibus or fcitx running that would route every key through it and lose
  the physical key); text an input method composes therefore does not
  reach the mirror at all. Keys without a physical key behind them that do
  arrive (a platform with no XKB keycodes) are sent as Unicode text.

## Relationships and placement on the host

How the mirror follows what Windows does to windows that belong
together (measured on the guest, `rel-*` and `place-*` scenarios):

* **Owned windows.** An owned window with a caption that activates
  (`owned_window_stands_alone`, in `proto/` because the guest decides by it
  too) is a toplevel attached to its owner's mirror (`setTransientParent`):
  the compositor keeps it above its owner, groups it with it and leaves it
  off the taskbar, it takes keyboard focus, and the compositor drags it
  (the guest hands the drag over, as for any toplevel). Every other owned
  window - menus, dropdowns, tooltips, borderless popups - stays an
  `xdg_popup` placed exactly against its owner. A popup whose owner goes
  away on this side (hidden in the guest, or the owner changed to none)
  becomes a toplevel; an owner change re-parents or re-attaches.
* **Modal dialogs.** `DialogBox` disables the owner; the owner's mirror
  takes no input from then on (live, no new surface). A press on it is
  not passed on: the session brings forward the topmost enabled window it
  owns, at any remove (`GW_ENABLEDPOPUP`) - raised, asked to be active,
  and `QApplication::alert`ed. Measured on Windows 11: the owner is
  activated for an instant, then the dialog is, and its caption toggles
  sixteen times (`FlashWindowEx`); the click itself reaches nothing. The
  dialog mirror is not made `Qt::WindowModal`: Qt then drops every press
  on the owner's surface before any widget or event filter sees it (a
  press injected into the owner never arrived), so the dialog could not be
  brought forward, and Qt ignores a modality change on a shown window
  (KWin still had the dialog modal after it was cleared).
* **Activation.** The guest's foreground window's mirror asks once to be
  the active window when the guest makes it so (a refusal is the
  compositor's). KWin activates a window as it maps it when its client is
  the active one, whatever the window asked; a focus-in shortly after a
  show or a minimise, on a window the user has not touched and the guest
  did not make its foreground, is taken as that and not passed to the
  guest - the guest's foreground mirror asks for the activation back.
* **Placement (KWin).** `PlacementSync` puts each toplevel mirror where
  its guest window is, and moves the guest window when the mirror is put
  somewhere else here. `KWinPlacement` does it through KWin's scripting:
  a script that stays loaded reports every place one of the process's
  windows is put (not during an interactive drag: where it ends), and a
  script per placement sets the frame - at once, or in `windowAdded` for a
  window not yet mapped, so a new mirror is never seen elsewhere. A
  window is known to the scripts by a title suffix of zero-width
  characters encoding its handle (KWin keeps them; it trims white space).
  A script cannot be sent anything once it runs, so each placement is a
  script; loading and running one costs 1-3 ms (measured: `placed` comes
  back that soon). The output scale comes from KWin too: Qt's
  `QScreen::devicePixelRatio` is the scale rounded up (2 for 1.2). The
  guest's screen is the primary output, pixel for pixel
  (`ScreenMapping`). The guest's `WindowResizer` now keeps a window's
  corner where it is (moving it in only as far as the room needs), since
  that corner is where the mirror is.
  KWin places a new window on the output its user is on, and that place
  can be reported after this side's placement is done: a report within a
  second of a placement, of a place it did not ask for, is KWin's own and is
  put back once, not passed to the guest as a move (a user's drag is
  reported only when it ends). A mirror maximised for the guest is sent to
  the guest's output (`workspace.sendClientToScreen`) as KWin maps it, as
  Windows maximises a window on the monitor it is on; KWin would maximise
  it on its user's output, where the guest's screen is fitted at another
  scale.

## Cursors and the pointer

What Windows does, measured on the test guest (Windows 11 at 120%, 115
dpi, `SM_CXCURSOR` 32, pointer speed 10, "Enhance pointer precision" off),
and what the mirror does about it (`cursor-*`, `clip-*` and `lock-*`
scenarios).

**Which cursor.** `GetCursorInfo` names the cursor the window under the
pointer set (`WM_SETCURSOR`, or its class cursor at the next move after
`SetClassLongPtr`). A system cursor's handle is LoadCursor's shared one,
the same in every process, so the guest half knows it by handle
(`CursorDecoder`) and says which (`SystemCursor`, in `CursorPicture`). The
mirror wears this desktop's own cursor of the same meaning
(`native_shape_for`, unit-tested against the CSS name Qt 6.11 sends for each
Qt shape): Qt asks the compositor for it by name (`wp_cursor_shape_v1`), so
it is sharp at any scale, in the user's theme and size, and animated by the
compositor (`IDC_WAIT` and `IDC_APPSTARTING` are
18-step animations of 50 ms a step on Windows). `IDC_PIN` and `IDC_PERSON`
mean nothing this desktop has a cursor for and are worn as the guest's
pixels. The guest's own cursor size setting therefore does not carry over
for system cursors; the host's does. The shape is asked through Qt, not
through a `wp_cursor_shape_device_v1` of the mirror's own: Qt 6.11 binds the
protocol itself and sets the cursor of its `wl_pointer` on every enter and
every `setCursor`, so a second device on the same pointer would race it,
mirror by mirror. Without the protocol, Qt draws the same name from the
user's cursor theme at the output's scale - still this desktop's cursor,
which is better than the guest's pixels.

**Hidden.** `ShowCursor(FALSE)` and `SetCursor(NULL)` both leave
`GetCursorInfo` with flags 0 and no handle. ShowCursor counts: FALSE, FALSE,
TRUE leaves the cursor hidden (-1); the second TRUE shows it (0). The guest
half says "none" outright (`GuestCursor`); the mirror wears
`Qt::BlankCursor`. Before, the host was never told and showed the last
cursor.

**An application's own cursor** is sent as its pixels (`CursorPicture`), each
step of an animated one with its time (`GetCursorFrameInfo`, which user32
exports undeclared; `DrawIconEx`'s step draws one but says neither how many
nor how long). The host steps through them itself (`CursorAnimation`), all
mirrors at once. Measured: the probe's `.ani` of 100/200/100/500 ms arrives
as exactly that and steps. A monochrome cursor's inverting pixels are drawn
black edged in white (`MonochromePicture`, unit-tested): exact over white,
the inversion itself a known limit.

**Scale.** A guest pixel is a screen pixel here; a cursor surface is sized
in logical units and Qt 6.11 gives a pixel cursor a whole buffer scale only
(the ratio truncated: at 1.2 a cursor was drawn 1.2 times too large and
blurred). The mirror pads the picture with transparency so that its edges
and hotspot are whole logical units at the screen's ratio p/q (1.2 = 6/5:
a 32x32 cursor with its hotspot at 4,4 becomes 36x36 with it at 6,6 - 30
units, hotspot at unit 5; `unit_padding`) and sizes Qt's cursor surface to
that with a viewport (`wp_viewport.set_destination` on the surface Qt hands
the compositor as the cursor, reached through QtWaylandClient's private
API: `CursorSurfaceViewport`). Measured on the glass side: every probe
cursor - 32x32, 96x96, monochrome, each animated step - is one to one at
1.2 (`cursor_size` 1.000, `cursor_shape` 0 pixels differing, hotspot on the
same pixel). That surface is used by Qt for pixel cursors only where the
compositor draws named cursors itself, which is when the viewport is used;
elsewhere the picture is resampled to the ratio rounded up, which Qt takes:
the right size, a little soft (its buffer a whole number of units at that
scale: a buffer that is not is `wl_surface.invalid_size`, which ends the
connection). The viewport reaches into QtWaylandClient's private classes,
so it is used only on the Qt the mirror was built against; on any other the
cursor is resampled. The viewport is put on again as the pointer
enters a mirror, so mirrors on outputs of different scales each get their
own; between Qt's commit on enter and that, the cursor is shown at the
other output's size for one frame.

**Clip, capture, foreground.** `ClipCursor` holds for as long as the
application's window is the foreground one: when the taskbar took the
foreground, Windows released the clip itself (measured; the hidden cursor's
count stays, but the clip is gone). A captured window's cursor stays as the
pointer leaves it (no `WM_SETCURSOR` goes elsewhere); on this side a
client's cursor shows only over its surfaces, a known limit.

**Holding the pointer.** The guest half judges what the foreground
application wants from what any process can see (`PointerGrabRule`, in
`proto/` and unit-tested):

    lock     the foreground window is mirrored, and either
             - the cursor is hidden and clipped (ClipCursor), or
             - the cursor is hidden and is put back at one point after the
               pointer moves it, 3 times in a row, each within 1 s of the
               last (SetCursorPos to the middle on every move), or
             - the clip is at most 16 px square, shown or not
    confine  the foreground window is mirrored and the cursor is clipped
    none     otherwise: a cursor that is only hidden is a video player's

"Put back" is judged on every move the host's pointer makes: put at a
point and found elsewhere is a return (however soon), found where it was
put 25 ms later is a move left alone; two left alone end a mouse look that
has no clip. No other process can read a game's raw input registration, so
a hidden cursor read raw, neither clipped nor put back, looks like a video
player's: it gets absolute moves (a known limit).

The guest half sends the judgement (`PointerGrab`: kind, window, clip, the
point the cursor is held at) when it changes. The mirror (`PointerGrabber`)
asks the compositor, on the mirror's own `wl_surface`, through protocols it
binds itself on Qt's connection (`PointerGlobals`, client code generated by
`wayland-scanner` from the system's protocol files):
`zwp_pointer_constraints_v1.confine_pointer` with the clip's part of the
mirror as the region (rounded inward to logical units), or `lock_pointer`
with the held point as the cursor position hint, both one-shot. While the
compositor holds a lock it sends no positions and the relative pointer
(`zwp_relative_pointer_v1`) gives the pointer's unaccelerated motion; the
mirror passes that on in whole counts (`PointerDelta`, fractions carried)
and the guest moves its cursor by it with a relative `SendInput`, which is
what a mouse does: raw input gets the counts as they are, the legacy cursor
moves by what Windows' ballistics make of them. Buttons pressed while held
act where the cursor is held (no move). Asked is not held: until the
compositor says the lock is in force the pointer moves freely, as positions,
and its motion is not passed on - a pointer crossing a mirror whose lock is
pending does not turn the game's camera (this was found the hard way: a
user's pointer crossing a mirror mid-run passed 95,-302 counts on).

Let go when the guest stops asking, when the mirror loses the keyboard focus
(held again when it gets it back), and when the user presses Ctrl+Alt+G on it
(QEMU's release chord; not passed to the guest), after which a click on the
mirror holds it again for as long as the guest still asks. A one-shot
constraint the compositor ends on its own while the mirror still has the
focus counts as the user's escape.

No `MOUSEEVENTF_MOVE_NOCOALESCE`, deliberately: raw input gets every move
apart either way, and coalescing only merges `WM_MOUSEMOVE`s still queued.
A game that puts its cursor back on each `WM_MOUSEMOVE` reads each against
the point it put it at; a second message queued before that carries a
position taken before the put, and would count the first move twice.

Measured with a relative step fed in four parts: `WM_INPUT` carries the
counts exactly (25,-10 → 25,-10 in four messages; -7,3 → -7,3). The legacy
cursor, at speed 10 with "Enhance pointer precision" off, moves about 1.2
times the counts (80,-40 → 95,-47; 40,-24 → 47,-28) - Windows' own, for any
relative mouse on this guest; with it on, slow motion is slowed (8,4 in
2-count moves → 6,3) and fast motion sped up (80,-40 in 20-count moves →
164,-82). Games reading raw input see the host mouse's own counts; games
reading `WM_MOUSEMOVE` see Windows' ballistics on top, as on Windows.

**Protocols on this desktop** (KWin 6.7, Qt 6.11): `zwp_pointer_constraints_v1`,
`zwp_relative_pointer_manager_v1`, `wp_viewporter` and
`wp_cursor_shape_manager_v1` v2 are all offered (the session logs what it
found). A lock needs the relative pointer too: held still with no motion
passed on, a game's camera could not turn, so without it nothing is asked
and the pointer moves the cursor by positions. A confinement whose clip
covers no whole logical unit of the mirror is not asked either (an empty
region is the whole surface to the compositor). Elsewhere: on X11, or a
compositor without pointer constraints,
nothing is asked and the rows say why (known limit); without cursor shapes
pixel cursors are resampled. The compositor engages a constraint only on a
surface that has the pointer and the focus; the conformance harness cannot
put the real pointer there, so `engaged` is reported, not checked. The
relative pointer's real events were seen arriving (above).

**The user's own pointer.** The harness runs on the user's desktop. A real
pointer crossing a mirror sends it real moves, which reach the guest; a
real click elsewhere takes the focus from a mirror. Rows about the cursor,
activation or the held pointer can then fail with nothing wrong in the
mirror: run again with the pointer and keyboard left alone.

## The style matrix

The `matrix` section of the scenario file is expanded by `ScenarioMatrix`
into scenarios named `mx-<group>-<n>-<labels>` and `mx-pairs-<n>` (run
them alone with `--filter '^mx-'`, or leave them out with
`--filter '^(?!mx-)'`). It names a subject window, an owner window, and
factors in behaviour groups; each factor's values are fragments of a
window in the file's own spelling, merged into the subject (lists of words
appended, objects merged, the rest replaced; `"owned": true` makes the
subject owned by the owner, made first). The first value of every factor
is its default.

| group | factors (default first) |
|---|---|
| frame | kind: overlapped, popup · edge: caption, none, border, dlgframe · sizing: thickframe, fixed · sysmenu: yes, no · boxes: min+max, none, min, max |
| state | show: normal, noactivate, maximized, minimized, hidden · disabled: no, yes |
| shell | `WS_EX_TOOLWINDOW` · `WS_EX_APPWINDOW` · `WS_EX_TOPMOST` · `WS_EX_NOACTIVATE` |
| layering | layered: opaque, alpha 160, colour key, both · `WS_EX_TRANSPARENT` |
| edges | `WS_EX_DLGMODALFRAME` · `WS_EX_CLIENTEDGE` · `WS_EX_WINDOWEDGE` · `WS_EX_STATICEDGE` |
| render | `WS_EX_COMPOSITED` · `WS_EX_NOREDIRECTIONBITMAP` · `WS_EX_LAYOUTRTL` |
| class | `CS_DROPSHADOW` · `CS_NOCLOSE` (`CS_OWNDC` and the like change nothing a mirror shows, and are left out) |
| owner | owned: no, yes · shape: none, ellipse |
| dwm | corners: default, square, round, small · dark: unset, yes |

**Scheme.** Every combination inside each group, with every other factor
at its default (the words of a group act on one another - a sizing border
on a popup, a maximise box without a window menu - and are tried
exhaustively); then pairwise across groups: a greedy all-pairs cover
(start a row from the first uncovered pair, give every other factor the
value that covers most uncovered pairs with what the row has, repeat) so
that every value of every factor meets every value of every factor of
every other group at least once. Rows already generated count towards the
cover, and identical rows are made once. Pairs Windows refuses
(`"excluded"`: `WS_EX_COMPOSITED` with `WS_EX_NOREDIRECTIONBITMAP`,
`CreateWindowEx` error 87) are never generated and not owed by the cover -
a row spent on a window that is never made leaves every other pair in it
uncovered. The full product is about 2.7 billion windows; this is 208
scenarios (202 group rows less the eight repeats of the all-default row
and the two refused, plus 16 pairwise rows), of which Windows' normalising
merges 58 into earlier ones at run time: 150 are looked at. A full run -
the 36 written scenarios and the matrix - takes about six minutes. Each scenario with
its subject on screen ends with a click a quarter in and three quarters
down its client area - inside an ellipse cut to the window, and off the
colour-keyed patch.

**Dedupe.** Windows rewrites words (an overlapped window always gets
`WS_CAPTION` and `WS_CLIPSIBLINGS`; `WS_EX_DLGMODALFRAME` brings
`WS_EX_WINDOWEDGE`; a caption without a window menu shows no boxes...),
so many generated scenarios make the same window. The runner reads what
each generated scenario's windows came out as - the normalised words,
layering, region, state and DWM attributes - and skips one identical to an
earlier one, naming it; one whose windows Windows refuses to make is
skipped too. Skipped scenarios are listed apart in the report and do not
count as run.

## Extending it

More matrix factors are more values in the file. Rows read only
`WindowTruth` and `MirrorState`; a behaviour neither carries yet means a
field on one of them (and the wire test).

What the conformance harness still cannot see of the pointer is the
compositor's own: whether a lock or confinement is in force needs the real
pointer over the mirror (the rows check what was asked; `engaged` is
reported), and the cursor on the glass is checked by what Qt was handed
and the size its surface was given. Both were measured once by hand (see
Cursors and the pointer).
