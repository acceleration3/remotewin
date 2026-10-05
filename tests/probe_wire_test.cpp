#include "proto/frame_decoder.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/probe/cursor_truth.hpp"
#include "proto/probe/input_record.hpp"
#include "proto/probe/probe_action.hpp"
#include "proto/probe/probe_hello.hpp"
#include "proto/probe/screen_pixels.hpp"
#include "proto/probe/window_spec.hpp"
#include "proto/probe/window_truth.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

// Through the writer, the length prefix and the decoder, as the bytes
// would cross the socket; the reader comes back positioned after the id.
template <typename Write>
std::vector<std::uint8_t> framed(rwin::MessageId id, Write write) {
    rwin::MessageWriter out{id};
    write(out);
    const std::vector<std::uint8_t> bytes = out.take();
    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    if (!decoder.next(&payload, &size)) return {};
    return std::vector<std::uint8_t>(payload, payload + size);
}

rwin::WindowSpec every_field_set() {
    rwin::WindowSpec spec;
    spec.class_style = 0x00020000;
    spec.style = 0x80880000;
    spec.extended_style = 0x08080088;
    spec.layering = rwin::WindowSpec::Layering::AlphaAndColorKey;
    spec.alpha = 160;
    spec.color_key = 0x00FF00FF;
    spec.shape = rwin::WindowSpec::Shape::Frame;
    spec.owner = 0x0000'0001'0002'0003ULL;
    spec.show = rwin::WindowSpec::ShowState::Minimized;
    spec.x = -32000;
    spec.y = 7;
    spec.client_width = 360;
    spec.client_height = 240;
    spec.title = "caf\xc3\xa9";
    spec.corner_preference = 2;
    spec.dark_mode = 1;
    spec.nc_rendering_policy = rwin::WindowSpec::kUnset;
    return spec;
}

void a_spec_survives_the_wire() {
    const rwin::WindowSpec spec = every_field_set();
    const auto bytes = framed(rwin::MessageId::ProbeCreateWindow, [&](auto& out) { spec.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    check(in.id() == rwin::MessageId::ProbeCreateWindow, "the spec's id survives");
    const auto read = rwin::WindowSpec::read(in);
    check(read.has_value() && *read == spec, "every field of a spec survives");
    check(in.remaining() == 0, "and nothing is left over");
}

void a_spec_naming_no_layering_is_refused() {
    rwin::WindowSpec spec = every_field_set();
    auto bytes = framed(rwin::MessageId::ProbeCreateWindow, [&](auto& out) { spec.write(out); });
    // id, class style, style, extended style: the layering byte is next.
    bytes[1 + 4 * 3] = 9;
    rwin::MessageReader in{bytes.data(), bytes.size()};
    check(!rwin::WindowSpec::read(in).has_value(), "an unknown layering is refused, not cast");
}

void a_short_spec_is_refused() {
    const rwin::WindowSpec spec = every_field_set();
    const auto bytes = framed(rwin::MessageId::ProbeCreateWindow, [&](auto& out) { spec.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size() - 3};
    check(!rwin::WindowSpec::read(in).has_value(), "a spec cut short is refused");
}

void a_truth_survives_the_wire() {
    rwin::WindowTruth truth;
    truth.handle = 0x500c4;
    truth.owner = 0x300d2;
    truth.process_id = 6488;
    truth.style = 0x14cf0000;
    truth.extended_style = 0x100;
    truth.class_style = 0x20000;
    truth.title = "overlapped";
    truth.window = {400, 300, 778, 585};
    truth.visible = {408, 300, 770, 577};
    truth.client = {409, 336, 769, 576};
    truth.shown = true;
    truth.zoomed = true;
    truth.foreground = true;
    truth.dwm_frame = true;
    truth.dpi = 115;
    truth.layered_flags = 3;
    truth.layered_alpha = 128;
    truth.color_key = 0xFF00FF;
    truth.region = rwin::WindowTruth::RegionKind::Complex;
    truth.region_rects = 145;
    truth.region_box = {0, 0, 320, 200};
    truth.class_name = "#32770";
    truth.active = true;
    truth.owner_disabled = true;
    truth.enabled_popup = 0x700aa;
    truth.z_rank = 3;
    const auto bytes = framed(rwin::MessageId::ProbeTruth, [&](auto& out) { truth.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::WindowTruth::read(in);
    check(read.has_value(), "a truth reads back");
    if (!read.has_value()) return;
    check(read->handle == truth.handle && read->owner == truth.owner, "handles survive");
    check(read->title == truth.title, "the title survives");
    check(read->window == truth.window && read->visible == truth.visible
              && read->client == truth.client,
          "the three rectangles survive");
    check(read->shown && !read->iconic && read->zoomed && !read->enabled && read->foreground
              && read->dwm_frame,
          "each state bit survives on its own");
    check(read->layered_alpha == 128 && read->color_key == 0xFF00FF, "layering survives");
    check(read->region == rwin::WindowTruth::RegionKind::Complex && read->region_rects == 145
              && read->region_box == truth.region_box,
          "the region survives");
    check(truth.visible.width() == 362 && truth.visible.height() == 277,
          "a rectangle's size is right minus left");
    check(read->exists && read->active && !read->focused && read->owner_disabled,
          "the relationship bits survive on their own");
    check(read->class_name == "#32770" && read->enabled_popup == 0x700aa && read->z_rank == 3,
          "the class, the enabled popup and the z-order rank survive");
}

void a_gone_window_says_so() {
    rwin::WindowTruth gone;
    gone.handle = 0x500c4;
    gone.exists = false;
    const auto bytes = framed(rwin::MessageId::ProbeTruth, [&](auto& out) { gone.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::WindowTruth::read(in);
    check(read.has_value() && !read->exists && read->z_rank == -1, "a destroyed window reads back destroyed");
}

void an_action_survives_the_wire() {
    rwin::ProbeAction action;
    action.kind = rwin::ProbeAction::Kind::SetPosition;
    action.window = 0x500c4;
    action.other = 0x600aa;
    action.on = true;
    action.show = 9;
    action.x = -7;
    action.y = 300;
    action.width = 480;
    action.height = 320;
    action.centred = true;
    action.z = rwin::ProbeAction::ZOrder::NoTopMost;
    action.flags = 0x0213;
    action.cursor = 32650;
    action.title = "caf\xc3\xa9";
    const auto bytes = framed(rwin::MessageId::ProbeAct, [&](auto& out) { action.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::ProbeAction::read(in);
    check(read.has_value() && *read == action, "every field of an action survives");
    auto unknown = bytes;
    unknown[1] = 200;
    rwin::MessageReader bad{unknown.data(), unknown.size()};
    check(!rwin::ProbeAction::read(bad).has_value(), "an action of no kind is refused");
    check(rwin::ProbeAction{.kind = rwin::ProbeAction::Kind::OpenModal}.makes_window()
              && rwin::ProbeAction{.kind = rwin::ProbeAction::Kind::ShowMessageBox}.makes_window()
              && !rwin::ProbeAction{.kind = rwin::ProbeAction::Kind::EndDialog}.makes_window(),
          "the actions that make a window say so");
}

void screen_pixels_survive_and_must_be_whole() {
    rwin::ScreenPixels pixels;
    pixels.left = -8;
    pixels.top = 16;
    pixels.width = 3;
    pixels.height = 2;
    for (std::uint8_t i = 0; i < 24; ++i) pixels.bgra.push_back(i);
    const auto bytes = framed(rwin::MessageId::ProbeScreenPixels, [&](auto& out) { pixels.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::ScreenPixels::read(in);
    check(read.has_value() && read->bgra == pixels.bgra && read->left == -8 && read->top == 16,
          "screen pixels survive");

    rwin::MessageReader cut{bytes.data(), bytes.size() - 1};
    check(!rwin::ScreenPixels::read(cut).has_value(), "pixels one byte short are refused");
}

void input_records_survive_and_read_signed_coordinates() {
    rwin::InputRecord record;
    record.message = 0x0201;
    record.window = 0x500c4;
    record.wparam = 1;
    // GET_X_LPARAM -3, GET_Y_LPARAM 183: a point left of the client area.
    record.lparam = (183u << 16) | 0xFFFDu;
    record.outcome = -2;
    const auto bytes = framed(rwin::MessageId::ProbeInput, [&](auto& out) { record.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::InputRecord::read(in);
    check(read.has_value() && *read == record, "an input record survives");
    check(record.x() == -3 && record.y() == 183, "client coordinates read signed");

    rwin::InputRecord raw;
    raw.message = 0x00FF;
    raw.wparam = 0;
    raw.lparam = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(-7)) << 32) | 12u;
    check(raw.raw_dx() == 12 && raw.raw_dy() == -7 && !raw.raw_absolute(), "a WM_INPUT record reads its motion signed");
}

void a_cursor_truth_survives_the_wire() {
    rwin::CursorTruth truth;
    truth.flags = 1;
    truth.handle = 0x10003;
    truth.x = -4;
    truth.y = 900;
    truth.under = 0x500c4;
    truth.system_idc = 32513;
    truth.display_count = -2;
    truth.clipped = true;
    truth.clip = {10, 20, 30, 40};
    truth.capture = 0x600aa;
    truth.raw_mouse = true;
    truth.raw_flags = 0x00000100;
    truth.raw_target = 0x500c4;
    truth.recentring = 0x500c4;
    truth.put_back_by = 0x500c4;
    truth.put_back = 7;
    truth.mouse_speed = 10;
    truth.enhance_precision = true;
    truth.metric_width = 40;
    truth.metric_height = 40;
    rwin::CursorTruth::Frame frame{1, 2, 0, 1, 100, {}, {}};
    frame.over_white.assign(8, 0xFF);
    frame.over_black.assign(8, 0x01);
    truth.frames = {frame, frame};
    const auto bytes = framed(rwin::MessageId::ProbeCursorTruth, [&](auto& out) { truth.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::CursorTruth::read(in);
    check(read.has_value() && *read == truth, "every field of a cursor truth survives");
    check(in.remaining() == 0, "and nothing is left over");
    check(truth.shown() && !rwin::CursorTruth{.flags = 0, .handle = 2}.shown()
              && !rwin::CursorTruth{.flags = 3, .handle = 2}.shown(),
          "a cursor is shown when showing, not suppressed, and named");
    rwin::MessageReader cut{bytes.data(), bytes.size() - 1};
    check(!rwin::CursorTruth::read(cut).has_value(), "a truth one byte short is refused");
}

void a_hello_survives() {
    const rwin::ProbeHello hello{6488, 115, 2560, 1440, 2560, 1382, true};
    const auto bytes = framed(rwin::MessageId::ProbeHello, [&](auto& out) { hello.write(out); });
    rwin::MessageReader in{bytes.data(), bytes.size()};
    const auto read = rwin::ProbeHello::read(in);
    check(read.has_value() && *read == hello, "the hello survives");
}

void the_probe_range_is_its_own() {
    for (const auto id : {rwin::MessageId::ProbeCreateWindow, rwin::MessageId::ProbeQuit,
                          rwin::MessageId::ProbeAct, rwin::MessageId::ProbeHello,
                          rwin::MessageId::ProbeInput, rwin::MessageId::ProbeActed}) {
        check(static_cast<int>(id) >= 128, "probe ids are above every mirror id");
    }
    check(static_cast<int>(rwin::MessageId::ProbeAct) < 192
              && static_cast<int>(rwin::MessageId::ProbeQuit) < 192
              && static_cast<int>(rwin::MessageId::ProbeHello) >= 192,
          "harness commands and probe answers do not overlap");
}

}  // namespace

int main() {
    a_spec_survives_the_wire();
    a_spec_naming_no_layering_is_refused();
    a_short_spec_is_refused();
    a_truth_survives_the_wire();
    a_gone_window_says_so();
    an_action_survives_the_wire();
    screen_pixels_survive_and_must_be_whole();
    input_records_survive_and_read_signed_coordinates();
    a_hello_survives();
    a_cursor_truth_survives_the_wire();
    the_probe_range_is_its_own();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("probe wire: all checks passed\n");
    return EXIT_SUCCESS;
}
