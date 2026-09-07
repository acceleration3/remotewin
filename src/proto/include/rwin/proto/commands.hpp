#pragma once

// The window-remoting vocabulary, once.
//
// Both ends name these constants rather than writing the strings, so a
// rename is a compile error on the side that missed it instead of a
// runtime "unknown command" from whichever copy was not updated.
//
// Scope: everything here is about windows — enumerating them, reading
// their properties, moving pixels, and sending input back. Installing
// software, running scripts, opening consoles and reporting the guest's
// RAM are somebody else's protocol. That separation is the whole reason
// this project exists apart from its first caller.

namespace rwin::proto::cmd {

// -- enumeration and properties -------------------------------------------
inline constexpr const char* kListWindows   = "list_windows";
inline constexpr const char* kWindowProps   = "window_props";
inline constexpr const char* kGetWindowIcon = "get_window_icon";

// -- lifecycle and state ---------------------------------------------------
inline constexpr const char* kCloseWindow   = "close_window";
inline constexpr const char* kFocusWindow   = "focus_window";
inline constexpr const char* kUnfocusWindow = "unfocus_window";
inline constexpr const char* kResizeWindow  = "resize_window";
// Minimize / maximize / restore. Carried as a WM_SYSCOMMAND, which every
// window understands whether its chrome is the system's or its own —
// unlike synthesising a click on a button whose position would have to
// be discovered first.
inline constexpr const char* kWindowCommand = "window_command";

// -- hit testing -----------------------------------------------------------
// Where a point falls in a window's non-client area, asked of the window
// itself. The host cannot infer it: a window that draws its own chrome
// answers for regions the host has no way to see.
inline constexpr const char* kNcHitTest = "nchittest";

// -- pixels ----------------------------------------------------------------
inline constexpr const char* kSubscribeWindow   = "subscribe_window";
inline constexpr const char* kUnsubscribeWindow = "unsubscribe_window";
inline constexpr const char* kCaptureDebug      = "capture_debug";

// -- input -----------------------------------------------------------------
inline constexpr const char* kInjectInput = "inject_input";

// -- UI Automation ---------------------------------------------------------
// Window lifecycle arrives this way rather than by polling: a window
// that opens between two enumerations is a window that appears late.
inline constexpr const char* kUiaState                = "uia_state";
inline constexpr const char* kUiaInvoke               = "uia_invoke";
inline constexpr const char* kUiaSetValue             = "uia_set_value";
inline constexpr const char* kUiaGetText              = "uia_get_text";
inline constexpr const char* kUiaTreeDump             = "uia_tree_dump";
inline constexpr const char* kUiaSubscribeLifecycle   = "uia_subscribe_lifecycle";
inline constexpr const char* kUiaUnsubscribeLifecycle = "uia_unsubscribe_lifecycle";
inline constexpr const char* kUiaSubscribeFocus       = "uia_subscribe_focus";
inline constexpr const char* kUiaSubscribeProperties  = "uia_subscribe_properties";
inline constexpr const char* kUiaUnsubscribe          = "uia_unsubscribe";

// -- session ---------------------------------------------------------------
// What the guest answers to prove it is there and to say what it can do.
// Version first, so an old guest against a new host fails with a sentence
// rather than a malformed region.
inline constexpr const char* kHello = "hello";

// -- scoping ---------------------------------------------------------------
// Restrict everything above to the processes of one launch. The caller
// names a Job Object; the guest resolves membership through it, which is
// exact — a portable build whose stub exits before its payload's window
// appears is still attributed correctly, and parentage alone would have
// lost it.
inline constexpr const char* kScopeToJob = "scope_to_job";

}  // namespace rwin::proto::cmd

namespace rwin::proto::topic {

// Guest-pushed event topics. Carried in the ``event`` field of a control
// envelope; the rest of the object is the per-topic payload.

// A caption press the guest decided was a drag. The host takes the drag
// over, because a window being dragged by the compositor cannot also be
// being dragged by the application inside it.
inline constexpr const char* kCaptionDragStarted = "caption_drag_started";

// Sent as soon as the guest maps the shared region: proof the channel is
// live, and the ring size the host may write. Absent means the session is
// running on the socket alone.
inline constexpr const char* kChannelHello = "channel_hello";

inline constexpr const char* kWindowOpened  = "window_opened";
inline constexpr const char* kWindowClosed  = "window_closed";
inline constexpr const char* kFocusChanged  = "focus_changed";
inline constexpr const char* kStateChanged  = "state_changed";
inline constexpr const char* kTitleChanged  = "title_changed";
inline constexpr const char* kModalChanged  = "modal_changed";

}  // namespace rwin::proto::topic
