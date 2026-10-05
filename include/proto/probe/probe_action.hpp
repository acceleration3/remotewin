#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {

class MessageReader;
class MessageWriter;

// Something the probe is asked to do to a window it has, or with one: the
// Win32 call an application makes to change a window while it lives, so
// what Windows does next - to it and to the windows it owns or is owned by
// - can be read back and compared with what the mirror did.
//
// Which fields an action reads:
//
//   Enable         window, on                      EnableWindow
//   Show           window, show (SW_*)             ShowWindow
//   SetOwner       window, other (0: none)         SetWindowLongPtr(GWLP_HWNDPARENT)
//   Foreground     window                          SetForegroundWindow
//   SetPosition    window, x y width height, z,    SetWindowPos (flags SWP_*; the
//                  flags                           size and place as given)
//   Move           window, x y width height        MoveWindow
//   OpenModal      window (owner), x y width height, DialogBoxIndirectParam: a modal
//                  centred, title                   dialog with that client size
//   OpenModeless   as OpenModal                    CreateDialogIndirectParam
//   EndDialog      window                          EndDialog, or DestroyWindow for a
//                                                  modeless one
//   ShowMessageBox window (owner, 0: none),        MessageBox, MB_OK | flags
//                  flags (MB_*), title
//   CloseMessageBox window                         WM_CLOSE, which a box with only
//                                                  OK answers as OK
//   Destroy        window                          DestroyWindow
//   Click          window, x y (client)            a left click by SendInput, as a
//                                                  user's mouse gives it: what
//                                                  Windows does with a click the
//                                                  mirror may not pass on
//   ClassCursor    window, cursor                  SetClassLongPtr(GCLP_HCURSOR)
//   AnswerCursor   window, cursor                  the window answers WM_SETCURSOR
//                                                  over its client area with
//                                                  SetCursor(that), TRUE - and
//                                                  sets it now if the pointer is
//                                                  over it
//   ShowCursor     on                              ShowCursor(on)
//   ClipCursor     window, x y width height        ClipCursor of that client
//                  (client; width 0: none)         rectangle, or of nothing
//   SetCursorPos   window, x y (client)            SetCursorPos
//   Capture        window, on                      SetCapture, or ReleaseCapture
//   RawInput       window, flags (RIDEV_*), on     RegisterRawInputDevices for the
//                                                  mouse, to the window; off: removed
//   Recentre       window, on                      the window puts the cursor back
//                                                  at its client middle on every
//                                                  WM_MOUSEMOVE: mouse look
//   GuestMove      window, x y (client, may be     the pointer moved there by
//                  outside it)                     SendInput, with no button
//   ActivateShell                                  SetForegroundWindow of the
//                                                  taskbar: the application loses
//                                                  the foreground to another's
//   PointerPrecision on                            "Enhance pointer precision" on
//                                                  or off (SPI_SETMOUSE), for this
//                                                  session only, and put back
//
// ``cursor`` is an IDC_ number (IDC_ARROW is 32512) for a cursor Windows
// ships, or a MadeCursor for one the probe makes.
//
// The actions that make a window answer with its handle.
struct ProbeAction {
    enum class Kind : std::uint8_t {
        Enable,
        Show,
        SetOwner,
        Foreground,
        SetPosition,
        Move,
        OpenModal,
        OpenModeless,
        EndDialog,
        // Not MessageBox: windows.h makes that MessageBoxW.
        ShowMessageBox,
        CloseMessageBox,
        Destroy,
        Click,
        ClassCursor,
        AnswerCursor,
        ShowCursor,
        ClipCursor,
        SetCursorPos,
        Capture,
        RawInput,
        Recentre,
        GuestMove,
        ActivateShell,
        PointerPrecision,
    };
    // The cursors the probe makes itself, below any IDC_ number: none at
    // all (SetCursor(NULL)), a 32-bit one with alpha, a monochrome one
    // whose middle inverts, an animated one loaded from an .ani file, and
    // one much larger than the system's.
    enum class MadeCursor : std::uint32_t { None = 1, Colour, Monochrome, Animated, Large };
    // SetWindowPos's hWndInsertAfter: left alone (SWP_NOZORDER), or one
    // of the four places it names.
    enum class ZOrder : std::uint8_t { Keep, Top, Bottom, TopMost, NoTopMost };

    Kind kind = Kind::Enable;
    std::uint64_t window = 0;
    std::uint64_t other = 0;
    bool on = false;
    std::int32_t show = 0;
    // Physical pixels. For a dialog, where its window rectangle goes and
    // the client size it is grown around.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    // A dialog placed by DS_CENTER rather than at x, y.
    bool centred = false;
    ZOrder z = ZOrder::Keep;
    std::uint32_t flags = 0;
    std::uint32_t cursor = 0;
    std::string title{};

    [[nodiscard]] bool operator==(const ProbeAction&) const = default;

    // Whether doing it makes a window, whose handle is the answer.
    [[nodiscard]] bool makes_window() const noexcept {
        return kind == Kind::OpenModal || kind == Kind::OpenModeless || kind == Kind::ShowMessageBox;
    }

    void write(MessageWriter& out) const;
    // Nothing when the message is short or names a kind or place no enum
    // has.
    [[nodiscard]] static std::optional<ProbeAction> read(MessageReader& in);
};

}  // namespace rwin
