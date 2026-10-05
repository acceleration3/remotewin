#pragma once

#include "proto/probe/probe_action.hpp"
#include "proto/probe/window_spec.hpp"

#include <QPoint>
#include <QSize>
#include <Qt>
#include <QString>

#include <cstdint>
#include <vector>

namespace rwin::conform {

// One thing to check: the windows the probe makes, in order, and the input
// put into their mirrors once they are up.
struct Scenario {
    struct Window {
        // ``spec.owner`` is filled in when the window is made: the owner is
        // named here by its place in the list, because its handle does not
        // exist until it is.
        WindowSpec spec;
        int owner = -1;
    };

    // One key of a keyboard: as the host's toolkit names it, as Linux
    // numbers it, and what Windows makes of a press of it - the virtual
    // key, and the set 1 scan code (0xE000 for an E0 key) WM_KEYDOWN
    // carries.
    struct Key {
        int qt_key = 0;
        std::uint32_t evdev = 0;
        std::uint32_t virtual_key = 0;
        std::uint16_t scan_code = 0;
    };

    // Input as a user gives it to the mirror: the pointer at a pixel of a
    // window's client area, a key, the mirror sized by its edge, or the held
    // pointer's own motion (``Relative``, in counts, as the relative pointer
    // protocol gives it). Or, in a scenario's ``steps``, something the
    // application does to its windows (``Act``), or the user moving a mirror
    // on this desktop (``HostMove``).
    struct Step {
        // The Host* kinds after HostMove are the user on this desktop too:
        // the mirror losing and getting the focus, the pointer's escape
        // (Ctrl+Alt+G) pressed on it, and a click on it that takes the
        // pointer back - each through Qt's window-system interface, the
        // path the compositor's own takes.
        enum class Kind { Move, Click, Key, Resize, Act, HostMove, Relative, HostUnfocus, HostFocus, HostEscape, HostRegrab };

        Kind kind = Kind::Move;
        int window = 0;
        // Client coordinates of the guest window, in its pixels. A move may
        // go on to ``then`` points after it, in quick succession as a mouse
        // sweeping across gives them; only the first is checked, as what
        // the pointer did after it may be held by then.
        QPoint client;
        std::vector<QPoint> then;
        // A key step, as the scenario names it ("Ctrl+Shift+A"): the
        // modifier keys pressed in order and held, the key pressed and
        // released, and the modifiers released in reverse.
        QString chord;
        std::vector<Key> held;
        Key key;
        Qt::KeyboardModifiers modifiers;
        // What the key types with the modifiers held, as the host's
        // toolkit and as TranslateMessage say it (0 for none).
        QString text;
        std::uint32_t character = 0;
        // A resize step: the size the mirror is dragged to, as the guest
        // pixels of the picture it is then to show.
        QSize size;
        // An Act step's action, its windows named by their place in the
        // scenario: ``window`` is ``action.window``'s and ``other``
        // ``action.other``'s (-1 for none), filled in with their handles
        // when it is done. An action that makes a window adds it to the
        // scenario's windows, at the next place.
        ProbeAction action;
        int other = -1;
        // A HostMove step: where the mirror is put, as the guest screen
        // pixel its picture's corner is then over.
        QPoint to;
        // A Relative step's motion, in counts.
        QPoint motion;
    };

    QString name;
    QString about;
    // Made by the style matrix rather than written: one Windows refuses to
    // make, or makes into the same words as an earlier one, is skipped
    // rather than failed.
    bool generated = false;
    // The cursor and the pointer are looked at too, after every step: what
    // the guest shows and what the mirror wears, and how each holds the
    // pointer (PointerExpectation).
    bool pointer = false;
    std::vector<Window> windows;
    // Put in once the windows have been looked at, and checked for the
    // messages they arrive as.
    std::vector<Step> input;
    // Done one at a time, each followed by both sides settling and being
    // looked at and checked again: how the windows relate as they change.
    std::vector<Step> steps;
};

}  // namespace rwin::conform
