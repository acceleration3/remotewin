#include "conform/scenario_book.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::WindowSpec;
using rwin::conform::Scenario;
using rwin::conform::ScenarioBook;

std::optional<ScenarioBook> book_of(const QByteArray& json, QString& error) {
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("scenarios.json"));
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) return std::nullopt;
    file.close();
    return ScenarioBook::read(path, error);
}

void the_shipped_scenarios_read() {
    QString error;
    const auto book = ScenarioBook::read(QStringLiteral(RWIN_CONFORM_SCENARIOS), error);
    check(book.has_value(), "tests/conformance/scenarios.json reads");
    if (!book.has_value()) {
        std::printf("  %s\n", qPrintable(error));
        return;
    }
    check(book->scenarios().size() >= 15, "and has a first set's worth of scenarios");
    const auto generated = std::count_if(book->scenarios().begin(), book->scenarios().end(),
                                         [](const Scenario& scenario) { return scenario.generated; });
    check(generated >= 200, "and its style matrix, expanded");
    check(book->backdrop().isValid(), "and a backdrop");
}

void words_are_read_by_name() {
    QString error;
    const auto book = book_of(R"({"backdrop": "#1E7860", "scenarios": [{"name": "n", "windows": [
        {"class_style": ["CS_DROPSHADOW"], "style": ["WS_POPUP", "WS_BORDER"],
         "ex_style": ["WS_EX_LAYERED", "WS_EX_TOPMOST"], "client": [320, 200], "at": [400, 300],
         "title": "t", "layered": {"alpha": 160, "color_key": "#FF00FF"}, "shape": "ellipse",
         "show": "noactivate", "dwm": {"corners": "square", "dark": true}},
        {"style": ["WS_OVERLAPPEDWINDOW"], "client": [10, 10], "at": [0, 0], "owner": 0}],
        "input": [{"window": 1, "click": [5, 6]}, {"key": "A"}, {"key": "Left"},
                  {"key": "Shift+1"}, {"key": "RCtrl+C"}]}]})",
                              error);
    check(book.has_value(), "a full scenario reads");
    if (!book.has_value()) {
        std::printf("  %s\n", qPrintable(error));
        return;
    }
    const Scenario& scenario = book->scenarios().front();
    const WindowSpec& spec = scenario.windows[0].spec;
    check(spec.class_style == 0x00020000, "CS_DROPSHADOW");
    check(spec.style == 0x80800000, "WS_POPUP | WS_BORDER");
    check(spec.extended_style == 0x00080008, "WS_EX_LAYERED | WS_EX_TOPMOST");
    check(spec.layering == WindowSpec::Layering::AlphaAndColorKey && spec.alpha == 160,
          "alpha and a key is both");
    check(spec.color_key == 0x00FF00FF, "#FF00FF is the COLORREF 0x00FF00FF");
    check(spec.shape == WindowSpec::Shape::Ellipse, "the shape");
    check(spec.show == WindowSpec::ShowState::NoActivate, "the show state");
    check(spec.corner_preference == 1 && spec.dark_mode == 1
              && spec.nc_rendering_policy == WindowSpec::kUnset,
          "DWM attributes set, and the one not named left alone");
    check(scenario.windows[1].spec.style == 0x00CF0000 && scenario.windows[1].owner == 0,
          "WS_OVERLAPPEDWINDOW, owned by the first");
    check(scenario.input.size() == 5, "five steps");
    check(scenario.input[0].kind == Scenario::Step::Kind::Click && scenario.input[0].window == 1
              && scenario.input[0].client == QPoint(5, 6),
          "a click on the second window");
    const Scenario::Step& letter = scenario.input[1];
    check(letter.key.virtual_key == 0x41 && letter.key.scan_code == 0x1E && letter.key.evdev == 30
              && letter.character == 'a' && letter.key.qt_key == Qt::Key_A
              && letter.text == QStringLiteral("a") && letter.held.empty(),
          "a letter is its key, its scan code and its unshifted character");
    check(scenario.input[2].key.virtual_key == 0x25 && scenario.input[2].key.scan_code == 0xE04B
              && scenario.input[2].character == 0,
          "an arrow is an E0 key with no character");
    const Scenario::Step& bang = scenario.input[3];
    check(bang.held.size() == 1 && bang.held[0].scan_code == 0x2A && bang.character == '!'
              && bang.modifiers == Qt::ShiftModifier,
          "Shift+1 holds the left Shift and types '!'");
    const Scenario::Step& copy = scenario.input[4];
    check(copy.held.size() == 1 && copy.held[0].scan_code == 0xE01D && copy.character == 0x03
              && copy.modifiers == Qt::ControlModifier,
          "RCtrl+C holds the right Ctrl and types ^C");
}

void steps_name_their_windows_by_place() {
    QString error;
    const auto book = book_of(R"({"backdrop": "#1E7860", "scenarios": [{"name": "n", "windows": [
        {"style": ["WS_OVERLAPPEDWINDOW"], "client": [10, 10], "at": "default"}],
        "steps": [{"do": "modal", "owner": 0, "client": [200, 100], "centred": true, "title": "d"},
                  {"window": 1, "key": "A"},
                  {"do": "position", "window": 0, "at": [5, 6], "z": "topmost", "flags": ["SWP_NOSIZE"]},
                  {"do": "message_box", "flags": ["MB_TASKMODAL"]},
                  {"do": "show", "window": 0, "show": "SW_MINIMIZE"},
                  {"do": "owner", "window": 1, "owner": 0},
                  {"host": "move", "window": 0, "to": [70, 80]},
                  {"do": "end_dialog", "window": 1}]}]})",
                              error);
    check(book.has_value(), "a scenario of steps reads");
    if (!book.has_value()) {
        std::printf("  %s\n", qPrintable(error));
        return;
    }
    const Scenario& scenario = book->scenarios().front();
    using Kind = rwin::ProbeAction::Kind;
    check(scenario.windows[0].spec.x == std::numeric_limits<int>::min(), "\"default\" is CW_USEDEFAULT");
    check(scenario.steps.size() == 8, "eight steps");
    check(scenario.steps[0].action.kind == Kind::OpenModal && scenario.steps[0].window == 0
              && scenario.steps[0].action.centred && scenario.steps[0].action.width == 200,
          "a modal dialog names its owner as its window");
    check(scenario.steps[1].kind == Scenario::Step::Kind::Key && scenario.steps[1].window == 1,
          "and is window 1 from then on");
    check(scenario.steps[2].action.z == rwin::ProbeAction::ZOrder::TopMost && scenario.steps[2].action.flags == 1,
          "SetWindowPos's place and flags by name");
    check(scenario.steps[3].window == -1 && scenario.steps[3].action.flags == 0x2000,
          "an unowned message box names no window");
    check(scenario.steps[4].action.show == 6, "SW_MINIMIZE");
    check(scenario.steps[5].action.kind == Kind::SetOwner && scenario.steps[5].other == 0, "a new owner");
    check(scenario.steps[6].kind == Scenario::Step::Kind::HostMove && scenario.steps[6].to == QPoint(70, 80),
          "a mirror moved on the host");
    check(!book_of(R"({"backdrop": "#000000", "scenarios": [{"name": "n", "windows": [
        {"style": ["WS_POPUP"], "client": [1, 1], "at": [0, 0]}], "steps": [{"do": "end_dialog", "window": 1}]}]})",
                   error)
               .has_value(),
          "a step naming a window no step has made yet is refused");
}


void pointer_steps_read() {
    QString error;
    const auto book = book_of(R"({"backdrop": "#1E7860", "scenarios": [{"name": "p", "pointer": true, "windows": [
        {"style": ["WS_OVERLAPPEDWINDOW"], "client": [360, 240], "at": [400, 300]}],
        "steps": [{"window": 0, "move": [10, 20], "then": [[30, 40], [50, 60]]},
                  {"do": "answer_cursor", "window": 0, "cursor": "IDC_APPSTARTING"},
                  {"do": "class_cursor", "window": 0, "cursor": "animated"},
                  {"do": "show_cursor", "on": false},
                  {"do": "clip_cursor", "window": 0, "at": [1, 2], "size": [3, 4]},
                  {"do": "clip_cursor", "window": 0},
                  {"do": "raw_input", "window": 0, "flags": ["RIDEV_NOLEGACY", "RIDEV_INPUTSINK"], "on": true},
                  {"do": "recentre", "window": 0, "on": true},
                  {"do": "activate_shell"},
                  {"do": "pointer_precision", "on": true},
                  {"window": 0, "relative": [-7, 9]},
                  {"host": "unfocus", "window": 0}, {"host": "escape", "window": 0}, {"host": "regrab", "window": 0}]}]})",
                              error);
    check(book.has_value(), "a pointer scenario reads");
    if (!book.has_value()) {
        std::printf("  %s\n", qPrintable(error));
        return;
    }
    const Scenario& scenario = book->scenarios().front();
    using Kind = rwin::ProbeAction::Kind;
    check(scenario.pointer, "it looks at the pointer");
    check(scenario.steps.size() == 14, "every step is read");
    check(scenario.steps[0].client == QPoint(10, 20) && scenario.steps[0].then.size() == 2
              && scenario.steps[0].then[1] == QPoint(50, 60),
          "a move goes on through the points after it");
    check(scenario.steps[1].action.kind == Kind::AnswerCursor && scenario.steps[1].action.cursor == 32650,
          "a system cursor is named by its IDC_ name");
    check(scenario.steps[2].action.cursor == static_cast<std::uint32_t>(rwin::ProbeAction::MadeCursor::Animated),
          "a made cursor by its own name");
    check(scenario.steps[3].action.kind == Kind::ShowCursor && !scenario.steps[3].action.on && scenario.steps[3].window < 0,
          "ShowCursor names no window");
    check(scenario.steps[4].action.width == 3 && scenario.steps[4].action.y == 2 && scenario.steps[5].action.width == 0,
          "a clip is a client rectangle, and none is a release");
    check(scenario.steps[6].action.flags == 0x130, "raw input flags are RIDEV_ words");
    check(scenario.steps[10].kind == Scenario::Step::Kind::Relative && scenario.steps[10].motion == QPoint(-7, 9),
          "a held pointer's motion is in counts");
    check(scenario.steps[11].kind == Scenario::Step::Kind::HostUnfocus
              && scenario.steps[12].kind == Scenario::Step::Kind::HostEscape
              && scenario.steps[13].kind == Scenario::Step::Kind::HostRegrab,
          "the user here loses the focus, escapes, and takes the pointer back");

    QString refused;
    check(!book_of(R"({"backdrop": "#1E7860", "scenarios": [{"name": "p", "windows": [
        {"style": [], "client": [1, 1], "at": [0, 0]}],
        "steps": [{"do": "answer_cursor", "window": 0, "cursor": "IDC_NOPE"}]}]})",
                   refused)
               .has_value(),
          "an unknown cursor is refused");
}

void mistakes_are_refused_by_name() {
    QString error;
    check(!book_of(R"({"backdrop": "#000000", "scenarios": [{"name": "n", "windows": [
        {"style": ["WS_POPUPP"], "client": [1, 1], "at": [0, 0], "title": "x"}]}]})", error)
               .has_value(),
          "a misspelt style is refused");
    check(error.contains(QStringLiteral("WS_POPUPP")), "and the error names it");
    check(!book_of(R"({"backdrop": "#000000", "scenarios": [{"name": "n", "windows": [
        {"style": ["WS_POPUP"], "client": [1, 1], "at": [0, 0], "owner": 0}]}]})", error)
               .has_value(),
          "a window owning itself is refused");
    check(!book_of(R"({"backdrop": "#000000", "scenarios": [{"name": "n", "windows": [
        {"style": ["WS_POPUP"], "client": [1, 1], "at": [0, 0]}], "input": [{"window": 3, "click": [1, 1]}]}]})",
                   error)
               .has_value(),
          "input to a window the scenario has not is refused");
    check(!book_of(R"({"backdrop": "#000000", "scenarios": [{"name": "n", "windows": [
        {"style": ["WS_POPUP"], "client": [1, 1], "at": [0, 0]}], "input": [{"key": "A+B"}]}]})",
                   error)
               .has_value(),
          "a chord held by something not a modifier is refused");
}

}  // namespace

int main() {
    pointer_steps_read();
    the_shipped_scenarios_read();
    words_are_read_by_name();
    steps_name_their_windows_by_place();
    mistakes_are_refused_by_name();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("scenario book: all checks passed\n");
    return EXIT_SUCCESS;
}
