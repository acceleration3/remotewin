#include "conform/scenario_book.hpp"

#include "conform/scenario_matrix.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPoint>
#include <QStringList>

#include <Qt>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <linux/input-event-codes.h>
#include <string_view>

namespace rwin::conform {

namespace {

struct Named {
    std::string_view name;
    std::uint32_t value;
};

// The words as winuser.h defines them; the compound ones too, because
// that is how they are written in real code.
constexpr std::array kStyles{
    Named{"WS_OVERLAPPED", 0x00000000},  Named{"WS_POPUP", 0x80000000},
    Named{"WS_CHILD", 0x40000000},       Named{"WS_MINIMIZE", 0x20000000},
    Named{"WS_VISIBLE", 0x10000000},     Named{"WS_DISABLED", 0x08000000},
    Named{"WS_CLIPSIBLINGS", 0x04000000}, Named{"WS_CLIPCHILDREN", 0x02000000},
    Named{"WS_MAXIMIZE", 0x01000000},    Named{"WS_CAPTION", 0x00C00000},
    Named{"WS_BORDER", 0x00800000},      Named{"WS_DLGFRAME", 0x00400000},
    Named{"WS_VSCROLL", 0x00200000},     Named{"WS_HSCROLL", 0x00100000},
    Named{"WS_SYSMENU", 0x00080000},     Named{"WS_THICKFRAME", 0x00040000},
    Named{"WS_MINIMIZEBOX", 0x00020000}, Named{"WS_MAXIMIZEBOX", 0x00010000},
    Named{"WS_OVERLAPPEDWINDOW", 0x00CF0000}, Named{"WS_POPUPWINDOW", 0x80880000},
};

constexpr std::array kExtendedStyles{
    Named{"WS_EX_DLGMODALFRAME", 0x00000001}, Named{"WS_EX_NOPARENTNOTIFY", 0x00000004},
    Named{"WS_EX_TOPMOST", 0x00000008},       Named{"WS_EX_ACCEPTFILES", 0x00000010},
    Named{"WS_EX_TRANSPARENT", 0x00000020},   Named{"WS_EX_MDICHILD", 0x00000040},
    Named{"WS_EX_TOOLWINDOW", 0x00000080},    Named{"WS_EX_WINDOWEDGE", 0x00000100},
    Named{"WS_EX_CLIENTEDGE", 0x00000200},    Named{"WS_EX_CONTEXTHELP", 0x00000400},
    Named{"WS_EX_RIGHT", 0x00001000},         Named{"WS_EX_RTLREADING", 0x00002000},
    Named{"WS_EX_LEFTSCROLLBAR", 0x00004000}, Named{"WS_EX_CONTROLPARENT", 0x00010000},
    Named{"WS_EX_STATICEDGE", 0x00020000},    Named{"WS_EX_APPWINDOW", 0x00040000},
    Named{"WS_EX_LAYERED", 0x00080000},       Named{"WS_EX_NOINHERITLAYOUT", 0x00100000},
    Named{"WS_EX_NOREDIRECTIONBITMAP", 0x00200000}, Named{"WS_EX_LAYOUTRTL", 0x00400000},
    Named{"WS_EX_COMPOSITED", 0x02000000},    Named{"WS_EX_NOACTIVATE", 0x08000000},
    Named{"WS_EX_OVERLAPPEDWINDOW", 0x00000300}, Named{"WS_EX_PALETTEWINDOW", 0x00000188},
};

constexpr std::array kClassStyles{
    Named{"CS_VREDRAW", 0x0001},     Named{"CS_HREDRAW", 0x0002},
    Named{"CS_DBLCLKS", 0x0008},     Named{"CS_OWNDC", 0x0020},
    Named{"CS_CLASSDC", 0x0040},     Named{"CS_PARENTDC", 0x0080},
    Named{"CS_NOCLOSE", 0x0200},     Named{"CS_SAVEBITS", 0x0800},
    Named{"CS_BYTEALIGNCLIENT", 0x1000}, Named{"CS_BYTEALIGNWINDOW", 0x2000},
    Named{"CS_GLOBALCLASS", 0x4000}, Named{"CS_DROPSHADOW", 0x00020000},
};

constexpr std::array kShows{
    Named{"normal", static_cast<std::uint32_t>(WindowSpec::ShowState::Normal)},
    Named{"noactivate", static_cast<std::uint32_t>(WindowSpec::ShowState::NoActivate)},
    Named{"maximized", static_cast<std::uint32_t>(WindowSpec::ShowState::Maximized)},
    Named{"minimized", static_cast<std::uint32_t>(WindowSpec::ShowState::Minimized)},
    Named{"hidden", static_cast<std::uint32_t>(WindowSpec::ShowState::Hidden)},
};

constexpr std::array kShapes{
    Named{"none", static_cast<std::uint32_t>(WindowSpec::Shape::None)},
    Named{"rounded", static_cast<std::uint32_t>(WindowSpec::Shape::RoundedRect)},
    Named{"ellipse", static_cast<std::uint32_t>(WindowSpec::Shape::Ellipse)},
    Named{"frame", static_cast<std::uint32_t>(WindowSpec::Shape::Frame)},
};

// ShowWindow's commands.
constexpr std::array kShowCommands{
    Named{"SW_HIDE", 0},          Named{"SW_SHOWNORMAL", 1},    Named{"SW_SHOWMINIMIZED", 2},
    Named{"SW_MAXIMIZE", 3},      Named{"SW_SHOWNOACTIVATE", 4}, Named{"SW_SHOW", 5},
    Named{"SW_MINIMIZE", 6},      Named{"SW_SHOWMINNOACTIVE", 7}, Named{"SW_SHOWNA", 8},
    Named{"SW_RESTORE", 9},
};

constexpr std::array kPositionFlags{
    Named{"SWP_NOSIZE", 0x0001},       Named{"SWP_NOMOVE", 0x0002},
    Named{"SWP_NOZORDER", 0x0004},     Named{"SWP_NOACTIVATE", 0x0010},
    Named{"SWP_SHOWWINDOW", 0x0040},   Named{"SWP_HIDEWINDOW", 0x0080},
    Named{"SWP_NOOWNERZORDER", 0x0200},
};

constexpr std::array kMessageBoxFlags{
    Named{"MB_APPLMODAL", 0x00000000}, Named{"MB_SYSTEMMODAL", 0x00001000},
    Named{"MB_TASKMODAL", 0x00002000}, Named{"MB_TOPMOST", 0x00040000},
    Named{"MB_SETFOREGROUND", 0x00010000},
};

constexpr std::array kZOrders{
    Named{"keep", static_cast<std::uint32_t>(ProbeAction::ZOrder::Keep)},
    Named{"top", static_cast<std::uint32_t>(ProbeAction::ZOrder::Top)},
    Named{"bottom", static_cast<std::uint32_t>(ProbeAction::ZOrder::Bottom)},
    Named{"topmost", static_cast<std::uint32_t>(ProbeAction::ZOrder::TopMost)},
    Named{"notopmost", static_cast<std::uint32_t>(ProbeAction::ZOrder::NoTopMost)},
};

// What a scenario calls each action.
constexpr std::array kActions{
    Named{"enable", static_cast<std::uint32_t>(ProbeAction::Kind::Enable)},
    Named{"show", static_cast<std::uint32_t>(ProbeAction::Kind::Show)},
    Named{"owner", static_cast<std::uint32_t>(ProbeAction::Kind::SetOwner)},
    Named{"foreground", static_cast<std::uint32_t>(ProbeAction::Kind::Foreground)},
    Named{"position", static_cast<std::uint32_t>(ProbeAction::Kind::SetPosition)},
    Named{"move", static_cast<std::uint32_t>(ProbeAction::Kind::Move)},
    Named{"modal", static_cast<std::uint32_t>(ProbeAction::Kind::OpenModal)},
    Named{"modeless", static_cast<std::uint32_t>(ProbeAction::Kind::OpenModeless)},
    Named{"end_dialog", static_cast<std::uint32_t>(ProbeAction::Kind::EndDialog)},
    Named{"message_box", static_cast<std::uint32_t>(ProbeAction::Kind::ShowMessageBox)},
    Named{"close_box", static_cast<std::uint32_t>(ProbeAction::Kind::CloseMessageBox)},
    Named{"destroy", static_cast<std::uint32_t>(ProbeAction::Kind::Destroy)},
    Named{"guest_click", static_cast<std::uint32_t>(ProbeAction::Kind::Click)},
    Named{"class_cursor", static_cast<std::uint32_t>(ProbeAction::Kind::ClassCursor)},
    Named{"answer_cursor", static_cast<std::uint32_t>(ProbeAction::Kind::AnswerCursor)},
    Named{"show_cursor", static_cast<std::uint32_t>(ProbeAction::Kind::ShowCursor)},
    Named{"clip_cursor", static_cast<std::uint32_t>(ProbeAction::Kind::ClipCursor)},
    Named{"set_cursor_pos", static_cast<std::uint32_t>(ProbeAction::Kind::SetCursorPos)},
    Named{"capture", static_cast<std::uint32_t>(ProbeAction::Kind::Capture)},
    Named{"raw_input", static_cast<std::uint32_t>(ProbeAction::Kind::RawInput)},
    Named{"recentre", static_cast<std::uint32_t>(ProbeAction::Kind::Recentre)},
    Named{"guest_move", static_cast<std::uint32_t>(ProbeAction::Kind::GuestMove)},
    Named{"activate_shell", static_cast<std::uint32_t>(ProbeAction::Kind::ActivateShell)},
    Named{"pointer_precision", static_cast<std::uint32_t>(ProbeAction::Kind::PointerPrecision)},
};

// A cursor a step names: Windows' own by its IDC_ name, or one the probe
// makes (ProbeAction::MadeCursor).
constexpr std::array kCursors{
    Named{"IDC_ARROW", 32512},       Named{"IDC_IBEAM", 32513},    Named{"IDC_WAIT", 32514},
    Named{"IDC_CROSS", 32515},       Named{"IDC_UPARROW", 32516},  Named{"IDC_SIZENWSE", 32642},
    Named{"IDC_SIZENESW", 32643},    Named{"IDC_SIZEWE", 32644},   Named{"IDC_SIZENS", 32645},
    Named{"IDC_SIZEALL", 32646},     Named{"IDC_NO", 32648},       Named{"IDC_HAND", 32649},
    Named{"IDC_APPSTARTING", 32650}, Named{"IDC_HELP", 32651},     Named{"IDC_PIN", 32671},
    Named{"IDC_PERSON", 32672},
    Named{"none", static_cast<std::uint32_t>(ProbeAction::MadeCursor::None)},
    Named{"colour", static_cast<std::uint32_t>(ProbeAction::MadeCursor::Colour)},
    Named{"monochrome", static_cast<std::uint32_t>(ProbeAction::MadeCursor::Monochrome)},
    Named{"animated", static_cast<std::uint32_t>(ProbeAction::MadeCursor::Animated)},
    Named{"large", static_cast<std::uint32_t>(ProbeAction::MadeCursor::Large)},
};

constexpr std::array kRawInputFlags{
    Named{"RIDEV_NOLEGACY", 0x00000030}, Named{"RIDEV_INPUTSINK", 0x00000100},
    Named{"RIDEV_CAPTUREMOUSE", 0x00000200},
};

// DWM_WINDOW_CORNER_PREFERENCE.
constexpr std::array kCorners{
    Named{"default", 0}, Named{"square", 1}, Named{"round", 2}, Named{"small", 3},
};

// DWMNCRENDERINGPOLICY.
constexpr std::array kNcRendering{
    Named{"default", 0}, Named{"disabled", 1}, Named{"enabled", 2},
};

// A key as Qt names it and Linux numbers it, the virtual key and scan code
// Windows gives a press of it, and the character TranslateMessage makes of
// that press on a US layout, without and with Shift.
struct Key {
    std::string_view name;
    int qt_key;
    std::uint32_t evdev;
    std::uint32_t virtual_key;
    std::uint16_t scan_code;
    char32_t plain;
    char32_t shifted;
};

constexpr std::uint16_t kE0 = 0xE000;

// CW_USEDEFAULT, (int)0x80000000.
constexpr int kUseDefault = std::numeric_limits<int>::min();

constexpr std::array kKeys{
    Key{"A", Qt::Key_A, KEY_A, 0x41, 0x1E, 'a', 'A'},
    Key{"B", Qt::Key_B, KEY_B, 0x42, 0x30, 'b', 'B'},
    Key{"C", Qt::Key_C, KEY_C, 0x43, 0x2E, 'c', 'C'},
    Key{"D", Qt::Key_D, KEY_D, 0x44, 0x20, 'd', 'D'},
    Key{"E", Qt::Key_E, KEY_E, 0x45, 0x12, 'e', 'E'},
    Key{"F", Qt::Key_F, KEY_F, 0x46, 0x21, 'f', 'F'},
    Key{"G", Qt::Key_G, KEY_G, 0x47, 0x22, 'g', 'G'},
    Key{"H", Qt::Key_H, KEY_H, 0x48, 0x23, 'h', 'H'},
    Key{"I", Qt::Key_I, KEY_I, 0x49, 0x17, 'i', 'I'},
    Key{"J", Qt::Key_J, KEY_J, 0x4A, 0x24, 'j', 'J'},
    Key{"K", Qt::Key_K, KEY_K, 0x4B, 0x25, 'k', 'K'},
    Key{"L", Qt::Key_L, KEY_L, 0x4C, 0x26, 'l', 'L'},
    Key{"M", Qt::Key_M, KEY_M, 0x4D, 0x32, 'm', 'M'},
    Key{"N", Qt::Key_N, KEY_N, 0x4E, 0x31, 'n', 'N'},
    Key{"O", Qt::Key_O, KEY_O, 0x4F, 0x18, 'o', 'O'},
    Key{"P", Qt::Key_P, KEY_P, 0x50, 0x19, 'p', 'P'},
    Key{"Q", Qt::Key_Q, KEY_Q, 0x51, 0x10, 'q', 'Q'},
    Key{"R", Qt::Key_R, KEY_R, 0x52, 0x13, 'r', 'R'},
    Key{"S", Qt::Key_S, KEY_S, 0x53, 0x1F, 's', 'S'},
    Key{"T", Qt::Key_T, KEY_T, 0x54, 0x14, 't', 'T'},
    Key{"U", Qt::Key_U, KEY_U, 0x55, 0x16, 'u', 'U'},
    Key{"V", Qt::Key_V, KEY_V, 0x56, 0x2F, 'v', 'V'},
    Key{"W", Qt::Key_W, KEY_W, 0x57, 0x11, 'w', 'W'},
    Key{"X", Qt::Key_X, KEY_X, 0x58, 0x2D, 'x', 'X'},
    Key{"Y", Qt::Key_Y, KEY_Y, 0x59, 0x15, 'y', 'Y'},
    Key{"Z", Qt::Key_Z, KEY_Z, 0x5A, 0x2C, 'z', 'Z'},
    Key{"1", Qt::Key_1, KEY_1, 0x31, 0x02, '1', '!'},
    Key{"2", Qt::Key_2, KEY_2, 0x32, 0x03, '2', '@'},
    Key{"3", Qt::Key_3, KEY_3, 0x33, 0x04, '3', '#'},
    Key{"4", Qt::Key_4, KEY_4, 0x34, 0x05, '4', '$'},
    Key{"5", Qt::Key_5, KEY_5, 0x35, 0x06, '5', '%'},
    Key{"6", Qt::Key_6, KEY_6, 0x36, 0x07, '6', '^'},
    Key{"7", Qt::Key_7, KEY_7, 0x37, 0x08, '7', '&'},
    Key{"8", Qt::Key_8, KEY_8, 0x38, 0x09, '8', '*'},
    Key{"9", Qt::Key_9, KEY_9, 0x39, 0x0A, '9', '('},
    Key{"0", Qt::Key_0, KEY_0, 0x30, 0x0B, '0', ')'},
    Key{"Minus", Qt::Key_Minus, KEY_MINUS, 0xBD, 0x0C, '-', '_'},
    Key{"Equal", Qt::Key_Equal, KEY_EQUAL, 0xBB, 0x0D, '=', '+'},
    Key{"LeftBracket", Qt::Key_BracketLeft, KEY_LEFTBRACE, 0xDB, 0x1A, '[', '{'},
    Key{"RightBracket", Qt::Key_BracketRight, KEY_RIGHTBRACE, 0xDD, 0x1B, ']', '}'},
    Key{"Semicolon", Qt::Key_Semicolon, KEY_SEMICOLON, 0xBA, 0x27, ';', ':'},
    Key{"Apostrophe", Qt::Key_Apostrophe, KEY_APOSTROPHE, 0xDE, 0x28, '\'', '"'},
    Key{"Grave", Qt::Key_QuoteLeft, KEY_GRAVE, 0xC0, 0x29, '`', '~'},
    Key{"Backslash", Qt::Key_Backslash, KEY_BACKSLASH, 0xDC, 0x2B, '\\', '|'},
    Key{"Comma", Qt::Key_Comma, KEY_COMMA, 0xBC, 0x33, ',', '<'},
    Key{"Period", Qt::Key_Period, KEY_DOT, 0xBE, 0x34, '.', '>'},
    Key{"Slash", Qt::Key_Slash, KEY_SLASH, 0xBF, 0x35, '/', '?'},
    Key{"Space", Qt::Key_Space, KEY_SPACE, 0x20, 0x39, ' ', ' '},
    Key{"Escape", Qt::Key_Escape, KEY_ESC, 0x1B, 0x01, 0x1B, 0x1B},
    Key{"Return", Qt::Key_Return, KEY_ENTER, 0x0D, 0x1C, 0x0D, 0x0D},
    Key{"Tab", Qt::Key_Tab, KEY_TAB, 0x09, 0x0F, 0x09, 0x09},
    Key{"Backspace", Qt::Key_Backspace, KEY_BACKSPACE, 0x08, 0x0E, 0x08, 0x08},
    Key{"Left", Qt::Key_Left, KEY_LEFT, 0x25, kE0 | 0x4B, 0, 0},
    Key{"Up", Qt::Key_Up, KEY_UP, 0x26, kE0 | 0x48, 0, 0},
    Key{"Right", Qt::Key_Right, KEY_RIGHT, 0x27, kE0 | 0x4D, 0, 0},
    Key{"Down", Qt::Key_Down, KEY_DOWN, 0x28, kE0 | 0x50, 0, 0},
    Key{"Home", Qt::Key_Home, KEY_HOME, 0x24, kE0 | 0x47, 0, 0},
    Key{"End", Qt::Key_End, KEY_END, 0x23, kE0 | 0x4F, 0, 0},
    Key{"PageUp", Qt::Key_PageUp, KEY_PAGEUP, 0x21, kE0 | 0x49, 0, 0},
    Key{"PageDown", Qt::Key_PageDown, KEY_PAGEDOWN, 0x22, kE0 | 0x51, 0, 0},
    Key{"Insert", Qt::Key_Insert, KEY_INSERT, 0x2D, kE0 | 0x52, 0, 0},
    Key{"Delete", Qt::Key_Delete, KEY_DELETE, 0x2E, kE0 | 0x53, 0, 0},
    Key{"KpEnter", Qt::Key_Enter, KEY_KPENTER, 0x0D, kE0 | 0x1C, 0x0D, 0x0D},
    Key{"KpDivide", Qt::Key_Slash, KEY_KPSLASH, 0x6F, kE0 | 0x35, '/', '/'},
    Key{"KpMultiply", Qt::Key_Asterisk, KEY_KPASTERISK, 0x6A, 0x37, '*', '*'},
    Key{"KpMinus", Qt::Key_Minus, KEY_KPMINUS, 0x6D, 0x4A, '-', '-'},
    Key{"KpPlus", Qt::Key_Plus, KEY_KPPLUS, 0x6B, 0x4E, '+', '+'},
    Key{"Shift", Qt::Key_Shift, KEY_LEFTSHIFT, 0x10, 0x2A, 0, 0},
    Key{"RShift", Qt::Key_Shift, KEY_RIGHTSHIFT, 0x10, 0x36, 0, 0},
    Key{"Ctrl", Qt::Key_Control, KEY_LEFTCTRL, 0x11, 0x1D, 0, 0},
    Key{"RCtrl", Qt::Key_Control, KEY_RIGHTCTRL, 0x11, kE0 | 0x1D, 0, 0},
    Key{"F1", Qt::Key_F1, KEY_F1, 0x70, 0x3B, 0, 0},
    Key{"F2", Qt::Key_F2, KEY_F2, 0x71, 0x3C, 0, 0},
    Key{"F3", Qt::Key_F3, KEY_F3, 0x72, 0x3D, 0, 0},
    Key{"F4", Qt::Key_F4, KEY_F4, 0x73, 0x3E, 0, 0},
    Key{"F5", Qt::Key_F5, KEY_F5, 0x74, 0x3F, 0, 0},
    Key{"F6", Qt::Key_F6, KEY_F6, 0x75, 0x40, 0, 0},
    Key{"F7", Qt::Key_F7, KEY_F7, 0x76, 0x41, 0, 0},
    Key{"F8", Qt::Key_F8, KEY_F8, 0x77, 0x42, 0, 0},
    Key{"F9", Qt::Key_F9, KEY_F9, 0x78, 0x43, 0, 0},
    Key{"F10", Qt::Key_F10, KEY_F10, 0x79, 0x44, 0, 0},
    Key{"F11", Qt::Key_F11, KEY_F11, 0x7A, 0x57, 0, 0},
    Key{"F12", Qt::Key_F12, KEY_F12, 0x7B, 0x58, 0, 0},
};

// The keys a chord holds: "Ctrl+Shift+A".
constexpr std::array<std::string_view, 4> kModifiers{"Shift", "RShift", "Ctrl", "RCtrl"};

template <std::size_t N>
std::optional<std::uint32_t> lookup(const std::array<Named, N>& table, const QString& name) {
    const QByteArray wanted = name.toLatin1();
    for (const Named& entry : table) {
        if (entry.name == std::string_view{wanted.constData(),
                                           static_cast<std::size_t>(wanted.size())}) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <std::size_t N>
std::optional<std::uint32_t> word_from(const QJsonValue& value, const std::array<Named, N>& table,
                                       const QString& where, QString& error) {
    std::uint32_t word = 0;
    for (const QJsonValue& item : value.toArray()) {
        const std::optional<std::uint32_t> bit = lookup(table, item.toString());
        if (!bit.has_value()) {
            error = QStringLiteral("%1: unknown flag '%2'").arg(where, item.toString());
            return std::nullopt;
        }
        word |= *bit;
    }
    return word;
}

// "#RRGGBB" as the COLORREF Windows wants, 0x00BBGGRR.
std::optional<std::uint32_t> colorref_from(const QString& text) {
    const QColor colour = QColor::fromString(text);
    if (!colour.isValid()) return std::nullopt;
    return static_cast<std::uint32_t>(colour.red()) | (static_cast<std::uint32_t>(colour.green()) << 8)
           | (static_cast<std::uint32_t>(colour.blue()) << 16);
}

std::optional<QPoint> point_from(const QJsonValue& value) {
    const QJsonArray pair = value.toArray();
    if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) return std::nullopt;
    return QPoint(pair[0].toInt(), pair[1].toInt());
}

bool read_layering(const QJsonObject& layered, WindowSpec& spec, QString& error) {
    const bool alpha = layered.contains(QStringLiteral("alpha"));
    const bool keyed = layered.contains(QStringLiteral("color_key"));
    if (alpha) spec.alpha = static_cast<std::uint8_t>(layered.value(QStringLiteral("alpha")).toInt(255));
    if (keyed) {
        const auto key = colorref_from(layered.value(QStringLiteral("color_key")).toString());
        if (!key.has_value()) {
            error = QStringLiteral("'%1': color_key is not a colour").arg(QString::fromStdString(spec.title));
            return false;
        }
        spec.color_key = *key;
    }
    spec.layering = alpha && keyed ? WindowSpec::Layering::AlphaAndColorKey
                    : alpha        ? WindowSpec::Layering::Alpha
                    : keyed        ? WindowSpec::Layering::ColorKey
                                   : WindowSpec::Layering::None;
    return true;
}

bool read_dwm(const QJsonObject& dwm, WindowSpec& spec, QString& error) {
    const QString where = QString::fromStdString(spec.title);
    if (dwm.contains(QStringLiteral("corners"))) {
        const auto corners = lookup(kCorners, dwm.value(QStringLiteral("corners")).toString());
        if (!corners.has_value()) {
            error = QStringLiteral("'%1': unknown corner preference").arg(where);
            return false;
        }
        spec.corner_preference = static_cast<std::int32_t>(*corners);
    }
    if (dwm.contains(QStringLiteral("dark"))) {
        spec.dark_mode = dwm.value(QStringLiteral("dark")).toBool() ? 1 : 0;
    }
    if (dwm.contains(QStringLiteral("nc_rendering"))) {
        const auto policy = lookup(kNcRendering, dwm.value(QStringLiteral("nc_rendering")).toString());
        if (!policy.has_value()) {
            error = QStringLiteral("'%1': unknown nc_rendering policy").arg(where);
            return false;
        }
        spec.nc_rendering_policy = static_cast<std::int32_t>(*policy);
    }
    return true;
}

std::optional<Scenario::Window> window_from(const QJsonObject& object, QString& error) {
    Scenario::Window window;
    WindowSpec& spec = window.spec;
    spec.title = object.value(QStringLiteral("title")).toString().toStdString();
    const QString where = QStringLiteral("window '%1'").arg(QString::fromStdString(spec.title));
    const auto style = word_from(object.value(QStringLiteral("style")), kStyles, where, error);
    const auto extended = word_from(object.value(QStringLiteral("ex_style")), kExtendedStyles, where, error);
    const auto class_style = word_from(object.value(QStringLiteral("class_style")), kClassStyles, where, error);
    if (!style.has_value() || !extended.has_value() || !class_style.has_value()) return std::nullopt;
    spec.style = *style;
    spec.extended_style = *extended;
    spec.class_style = *class_style;

    const std::optional<QPoint> client = point_from(object.value(QStringLiteral("client")));
    // "default": CW_USEDEFAULT, where Windows chooses.
    const bool chosen = object.value(QStringLiteral("at")).toString() == QLatin1String("default");
    const std::optional<QPoint> at = chosen ? QPoint(kUseDefault, kUseDefault) : point_from(object.value(QStringLiteral("at")));
    if (!client.has_value() || !at.has_value() || client->x() <= 0 || client->y() <= 0) {
        error = QStringLiteral("%1 needs 'client': [w, h] and 'at': [x, y] or \"default\"").arg(where);
        return std::nullopt;
    }
    spec.client_width = static_cast<std::uint32_t>(client->x());
    spec.client_height = static_cast<std::uint32_t>(client->y());
    spec.x = at->x();
    spec.y = at->y();

    const auto show = lookup(kShows, object.value(QStringLiteral("show")).toString(QStringLiteral("normal")));
    const auto shape = lookup(kShapes, object.value(QStringLiteral("shape")).toString(QStringLiteral("none")));
    if (!show.has_value() || !shape.has_value()) {
        error = QStringLiteral("%1: unknown show state or shape").arg(where);
        return std::nullopt;
    }
    spec.show = static_cast<WindowSpec::ShowState>(*show);
    spec.shape = static_cast<WindowSpec::Shape>(*shape);
    if (!read_layering(object.value(QStringLiteral("layered")).toObject(), spec, error)) return std::nullopt;
    if (!read_dwm(object.value(QStringLiteral("dwm")).toObject(), spec, error)) return std::nullopt;
    window.owner = object.value(QStringLiteral("owner")).toInt(-1);
    return window;
}

const Key* key_named(QStringView name) {
    const QByteArray wanted = name.toLatin1();
    const std::string_view text{wanted.constData(), static_cast<std::size_t>(wanted.size())};
    for (const Key& key : kKeys) {
        if (key.name == text) return &key;
    }
    return nullptr;
}

bool is_modifier(QStringView name) {
    const QByteArray wanted = name.toLatin1();
    const std::string_view text{wanted.constData(), static_cast<std::size_t>(wanted.size())};
    return std::find(kModifiers.begin(), kModifiers.end(), text) != kModifiers.end();
}

Scenario::Key physical(const Key& key) {
    return Scenario::Key{key.qt_key, key.evdev, key.virtual_key, key.scan_code};
}

// What TranslateMessage makes of the key with these modifiers held, on a
// US layout. Ctrl with a letter is the control character in its place
// (Ctrl+A is 0x01); Ctrl with anything else is left out rather than
// modelled.
std::optional<char32_t> character_of(const Key& key, Qt::KeyboardModifiers modifiers) {
    const bool letter = key.plain >= U'a' && key.plain <= U'z';
    if (modifiers.testFlag(Qt::ControlModifier)) {
        if (letter) return key.plain - U'a' + 1;
        if (key.plain == 0) return char32_t{0};
        return std::nullopt;
    }
    return modifiers.testFlag(Qt::ShiftModifier) ? key.shifted : key.plain;
}

// "Ctrl+Shift+A": the modifiers in the order they go down, then the key.
bool read_chord(const QString& chord, Scenario::Step& step, QString& error) {
    const QStringList parts = chord.split(QLatin1Char('+'));
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        const Key* modifier = is_modifier(parts[i]) ? key_named(parts[i]) : nullptr;
        if (modifier == nullptr) {
            error = QStringLiteral("key '%1': '%2' is not a modifier this knows").arg(chord, parts[i]);
            return false;
        }
        step.held.push_back(physical(*modifier));
        step.modifiers |= modifier->qt_key == Qt::Key_Control ? Qt::ControlModifier : Qt::ShiftModifier;
    }
    const Key* key = key_named(parts.last());
    if (key == nullptr) {
        error = QStringLiteral("unknown step (want move, click, or key; key '%1' is not one this knows)")
                    .arg(chord);
        return false;
    }
    step.key = physical(*key);
    const std::optional<char32_t> character = character_of(*key, step.modifiers);
    if (!character.has_value()) {
        error = QStringLiteral("key '%1': what it types is not modelled").arg(chord);
        return false;
    }
    step.character = *character;
    if (*character != 0) step.text = QString::fromUcs4(&*character, 1);
    return true;
}

std::optional<Scenario::Step> step_from(const QJsonObject& object, QString& error) {
    Scenario::Step step;
    step.window = object.value(QStringLiteral("window")).toInt(0);
    if (object.contains(QStringLiteral("move")) || object.contains(QStringLiteral("click"))) {
        const bool click = object.contains(QStringLiteral("click"));
        const std::optional<QPoint> at =
            point_from(object.value(click ? QStringLiteral("click") : QStringLiteral("move")));
        if (!at.has_value()) {
            error = QStringLiteral("a pointer step needs [x, y]");
            return std::nullopt;
        }
        step.kind = click ? Scenario::Step::Kind::Click : Scenario::Step::Kind::Move;
        step.client = *at;
        for (const QJsonValue& point : object.value(QStringLiteral("then")).toArray()) {
            const std::optional<QPoint> then = point_from(point);
            if (!then.has_value()) {
                error = QStringLiteral("a move's 'then' is a list of [x, y]");
                return std::nullopt;
            }
            step.then.push_back(*then);
        }
        return step;
    }
    if (object.contains(QStringLiteral("relative"))) {
        const std::optional<QPoint> motion = point_from(object.value(QStringLiteral("relative")));
        if (!motion.has_value()) {
            error = QStringLiteral("a relative step needs [dx, dy]");
            return std::nullopt;
        }
        step.kind = Scenario::Step::Kind::Relative;
        step.motion = *motion;
        return step;
    }
    if (object.contains(QStringLiteral("resize"))) {
        const std::optional<QPoint> size = point_from(object.value(QStringLiteral("resize")));
        if (!size.has_value() || size->x() <= 0 || size->y() <= 0) {
            error = QStringLiteral("a resize step needs [width, height]");
            return std::nullopt;
        }
        step.kind = Scenario::Step::Kind::Resize;
        step.size = QSize(size->x(), size->y());
        return step;
    }
    const QString chord = object.value(QStringLiteral("key")).toString();
    step.kind = Scenario::Step::Kind::Key;
    step.chord = chord;
    if (!read_chord(chord, step, error)) return std::nullopt;
    return step;
}

// A dialog's place and client size: "at" [x, y], or "centred" for
// DS_CENTER; "client" [w, h].
bool read_dialog(const QJsonObject& object, ProbeAction& action, QString& error) {
    const std::optional<QPoint> client = point_from(object.value(QStringLiteral("client")));
    action.centred = object.value(QStringLiteral("centred")).toBool(false);
    const std::optional<QPoint> at = point_from(object.value(QStringLiteral("at")));
    if (!client.has_value() || (!action.centred && !at.has_value())) {
        error = QStringLiteral("a dialog needs 'client': [w, h] and 'at': [x, y] or 'centred': true");
        return false;
    }
    action.width = client->x();
    action.height = client->y();
    if (at.has_value()) {
        action.x = at->x();
        action.y = at->y();
    }
    return true;
}

// "at" and "size", as SetWindowPos and MoveWindow take them.
bool read_rectangle(const QJsonObject& object, ProbeAction& action, QString& error) {
    const std::optional<QPoint> at = point_from(object.value(QStringLiteral("at")));
    const std::optional<QPoint> size = point_from(object.value(QStringLiteral("size")));
    if (at.has_value()) {
        action.x = at->x();
        action.y = at->y();
    }
    if (size.has_value()) {
        action.width = size->x();
        action.height = size->y();
    }
    if (action.kind == ProbeAction::Kind::Move && (!at.has_value() || !size.has_value())) {
        error = QStringLiteral("a move needs 'at' and 'size'");
        return false;
    }
    return true;
}

bool read_action_fields(const QJsonObject& object, Scenario::Step& step, QString& error) {
    ProbeAction& action = step.action;
    using Kind = ProbeAction::Kind;
    action.title = object.value(QStringLiteral("title")).toString().toStdString();
    action.on = object.value(QStringLiteral("on")).toBool(false);
    step.other = object.value(QStringLiteral("owner")).toInt(-1);
    const QString where = QStringLiteral("step '%1'").arg(object.value(QStringLiteral("do")).toString());
    switch (action.kind) {
        case Kind::Show: {
            const auto show = lookup(kShowCommands, object.value(QStringLiteral("show")).toString());
            if (!show.has_value()) {
                error = QStringLiteral("%1: 'show' is not an SW_ command").arg(where);
                return false;
            }
            action.show = static_cast<std::int32_t>(*show);
            return true;
        }
        case Kind::SetPosition: {
            const auto z = lookup(kZOrders, object.value(QStringLiteral("z")).toString(QStringLiteral("keep")));
            const auto flags = word_from(object.value(QStringLiteral("flags")), kPositionFlags, where, error);
            if (!z.has_value() || !flags.has_value()) {
                if (error.isEmpty()) error = QStringLiteral("%1: unknown 'z'").arg(where);
                return false;
            }
            action.z = static_cast<ProbeAction::ZOrder>(*z);
            action.flags = *flags;
            return read_rectangle(object, action, error);
        }
        case Kind::Move:
        case Kind::Click:
        case Kind::SetCursorPos:
        case Kind::GuestMove:
        case Kind::ClipCursor:
            return read_rectangle(object, action, error);
        case Kind::ClassCursor:
        case Kind::AnswerCursor: {
            const auto cursor = lookup(kCursors, object.value(QStringLiteral("cursor")).toString());
            if (!cursor.has_value()) {
                error = QStringLiteral("%1: 'cursor' is not an IDC_ name, none, colour, monochrome, animated or large")
                            .arg(where);
                return false;
            }
            action.cursor = *cursor;
            return true;
        }
        case Kind::RawInput: {
            const auto flags = word_from(object.value(QStringLiteral("flags")), kRawInputFlags, where, error);
            if (!flags.has_value()) return false;
            action.flags = *flags;
            return true;
        }
        case Kind::OpenModal:
        case Kind::OpenModeless:
            return read_dialog(object, action, error);
        case Kind::ShowMessageBox: {
            const auto flags = word_from(object.value(QStringLiteral("flags")), kMessageBoxFlags, where, error);
            if (!flags.has_value()) return false;
            action.flags = *flags;
            return true;
        }
        default:
            return true;
    }
}

// An Act step names its window as "window", or, for one that makes a
// window, its owner as "owner"; SetOwner names the new owner as "owner".
std::optional<Scenario::Step> action_from(const QJsonObject& object, QString& error) {
    Scenario::Step step;
    step.kind = Scenario::Step::Kind::Act;
    const auto kind = lookup(kActions, object.value(QStringLiteral("do")).toString());
    if (!kind.has_value()) {
        error = QStringLiteral("unknown action '%1'").arg(object.value(QStringLiteral("do")).toString());
        return std::nullopt;
    }
    step.action.kind = static_cast<ProbeAction::Kind>(*kind);
    if (!read_action_fields(object, step, error)) return std::nullopt;
    const bool by_owner = step.action.makes_window();
    step.window = by_owner ? step.other : object.value(QStringLiteral("window")).toInt(-1);
    if (by_owner) step.other = -1;
    return step;
}

constexpr std::array kHostSteps{
    Named{"unfocus", static_cast<std::uint32_t>(Scenario::Step::Kind::HostUnfocus)},
    Named{"focus", static_cast<std::uint32_t>(Scenario::Step::Kind::HostFocus)},
    Named{"escape", static_cast<std::uint32_t>(Scenario::Step::Kind::HostEscape)},
    Named{"regrab", static_cast<std::uint32_t>(Scenario::Step::Kind::HostRegrab)},
};

std::optional<Scenario::Step> host_step_from(const QJsonObject& object, QString& error) {
    Scenario::Step step;
    step.window = object.value(QStringLiteral("window")).toInt(0);
    const QString what = object.value(QStringLiteral("host")).toString();
    if (const auto kind = lookup(kHostSteps, what)) {
        step.kind = static_cast<Scenario::Step::Kind>(*kind);
        return step;
    }
    const std::optional<QPoint> to = point_from(object.value(QStringLiteral("to")));
    if (what != QLatin1String("move") || !to.has_value()) {
        error = QStringLiteral("a host step is {\"host\": \"move\", \"to\": [x, y]}, or unfocus, focus, escape, regrab");
        return std::nullopt;
    }
    step.kind = Scenario::Step::Kind::HostMove;
    step.to = *to;
    return step;
}

// The windows a step may name: those made up front, and those earlier
// steps made. An unowned message box names no window at all.
bool read_steps(const QJsonArray& steps, Scenario& scenario, QString& error) {
    int windows = static_cast<int>(scenario.windows.size());
    for (const QJsonValue& value : steps) {
        const QJsonObject object = value.toObject();
        std::optional<Scenario::Step> step = object.contains(QStringLiteral("do"))     ? action_from(object, error)
                                             : object.contains(QStringLiteral("host")) ? host_step_from(object, error)
                                                                                       : step_from(object, error);
        if (!step.has_value()) return false;
        const bool may_be_none = step->kind == Scenario::Step::Kind::Act
                                 && (step->action.kind == ProbeAction::Kind::ShowMessageBox
                                     || step->action.kind == ProbeAction::Kind::ShowCursor
                                     || step->action.kind == ProbeAction::Kind::ActivateShell
                                     || step->action.kind == ProbeAction::Kind::PointerPrecision);
        if (step->window >= windows || (step->window < 0 && !may_be_none) || step->other >= windows) {
            error = QStringLiteral("a step names window %1 of %2").arg(step->window).arg(windows);
            return false;
        }
        if (step->kind == Scenario::Step::Kind::Act && step->action.makes_window()) ++windows;
        scenario.steps.push_back(std::move(*step));
    }
    return true;
}

std::optional<Scenario> scenario_from(const QJsonObject& object, QString& error) {
    Scenario scenario;
    scenario.name = object.value(QStringLiteral("name")).toString();
    scenario.about = object.value(QStringLiteral("about")).toString();
    scenario.generated = object.value(QStringLiteral("generated")).toBool(false);
    scenario.pointer = object.value(QStringLiteral("pointer")).toBool(false);
    if (scenario.name.isEmpty()) {
        error = QStringLiteral("a scenario has no name");
        return std::nullopt;
    }
    for (const QJsonValue& value : object.value(QStringLiteral("windows")).toArray()) {
        std::optional<Scenario::Window> window = window_from(value.toObject(), error);
        if (!window.has_value()) {
            error = QStringLiteral("scenario '%1': %2").arg(scenario.name, error);
            return std::nullopt;
        }
        if (window->owner >= static_cast<int>(scenario.windows.size())) {
            error = QStringLiteral("scenario '%1': an owner must come before what it owns")
                        .arg(scenario.name);
            return std::nullopt;
        }
        scenario.windows.push_back(std::move(*window));
    }
    for (const QJsonValue& value : object.value(QStringLiteral("input")).toArray()) {
        std::optional<Scenario::Step> step = step_from(value.toObject(), error);
        if (!step.has_value()) {
            error = QStringLiteral("scenario '%1': %2").arg(scenario.name, error);
            return std::nullopt;
        }
        if (step->window < 0 || step->window >= static_cast<int>(scenario.windows.size())) {
            error = QStringLiteral("scenario '%1': input names window %2, which it has not")
                        .arg(scenario.name)
                        .arg(step->window);
            return std::nullopt;
        }
        scenario.input.push_back(std::move(*step));
    }
    if (!read_steps(object.value(QStringLiteral("steps")).toArray(), scenario, error)) {
        error = QStringLiteral("scenario '%1': %2").arg(scenario.name, error);
        return std::nullopt;
    }
    return scenario;
}

}  // namespace

std::optional<ScenarioBook> ScenarioBook::read(const QString& path, QString& error) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) {
        error = QStringLiteral("%1 at byte %2: %3").arg(path).arg(parse.offset).arg(parse.errorString());
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    ScenarioBook book;
    book.backdrop_ = QColor::fromString(root.value(QStringLiteral("backdrop")).toString());
    if (!book.backdrop_.isValid()) {
        error = QStringLiteral("%1: 'backdrop' is not a colour").arg(path);
        return std::nullopt;
    }
    std::vector<QJsonObject> objects;
    for (const QJsonValue& value : root.value(QStringLiteral("scenarios")).toArray()) {
        objects.push_back(value.toObject());
    }
    if (root.contains(QStringLiteral("matrix"))) {
        const std::optional<ScenarioMatrix> matrix =
            ScenarioMatrix::read(root.value(QStringLiteral("matrix")).toObject(), error);
        if (!matrix.has_value()) {
            error = QStringLiteral("%1: %2").arg(path, error);
            return std::nullopt;
        }
        std::vector<QJsonObject> generated = matrix->scenarios();
        objects.insert(objects.end(), std::make_move_iterator(generated.begin()),
                       std::make_move_iterator(generated.end()));
    }
    book.scenarios_.reserve(objects.size());
    for (const QJsonObject& object : objects) {
        std::optional<Scenario> scenario = scenario_from(object, error);
        if (!scenario.has_value()) {
            error = QStringLiteral("%1: %2").arg(path, error);
            return std::nullopt;
        }
        book.scenarios_.push_back(std::move(*scenario));
    }
    return book;
}

}  // namespace rwin::conform
