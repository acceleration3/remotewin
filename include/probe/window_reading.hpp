#pragma once

#include "proto/probe/window_truth.hpp"

struct HWND__;

namespace rwin::probe {

// What Windows says about one window, read from Windows: its words, its
// rectangles, its state, and the state of the thread's activation and
// focus as they touch it. Any window of the probe's thread - one it made
// to a spec, a dialog, a message box. Where it stands among the probe's
// other windows (``z_rank``) is the caller's to fill in; a window that no
// longer exists comes back with ``exists`` false and nothing else read.
[[nodiscard]] WindowTruth read_window(HWND__* window);

}  // namespace rwin::probe
