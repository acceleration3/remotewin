#include "proto/activate.hpp"
#include "proto/clock_echo.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/feature_set.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/frame_route.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/hello.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/min_max.hpp"
#include "proto/move_size_end.hpp"
#include "proto/move_size_start.hpp"
#include "proto/picture_rect.hpp"
#include "proto/pointer_at.hpp"
#include "proto/pointer_button.hpp"
#include "proto/pointer_delta.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/pointer_move.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/slot_free.hpp"
#include "proto/slot_ring.hpp"
#include "proto/surface_lost.hpp"
#include "proto/sync_begin.hpp"
#include "proto/sys_command.hpp"
#include "proto/sys_menu.hpp"
#include "proto/sys_param.hpp"
#include "proto/system_cursor.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_icon.hpp"
#include "proto/window_id.hpp"
#include "proto/window_move.hpp"
#include "proto/z_order.hpp"

#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <optional>
#include <utility>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

std::vector<std::uint8_t> sample_window_list() {
    rwin::MessageWriter w{rwin::MessageId::WindowChanged};
    w.put_u32(1);
    w.put_u64(0x1234'5678'9abc'def0ULL);
    w.put_u32(4242);
    w.put_i32(-7);
    w.put_u16(1024);
    w.put_i64(-9);
    w.put_string("Untitled - Notepad");
    return w.take();
}

void round_trip() {
    const std::vector<std::uint8_t> bytes = sample_window_list();

    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());

    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "a whole message decodes");

    rwin::MessageReader r{payload, size};
    check(r.id() == rwin::MessageId::WindowChanged, "id survives");
    check(r.u32() == 1, "u32 survives");
    check(r.u64() == 0x1234'5678'9abc'def0ULL, "u64 survives");
    check(r.u32() == 4242, "second u32 survives");
    check(r.i32() == -7, "negative i32 survives");
    check(r.u16() == 1024, "u16 survives");
    check(r.i64() == -9, "negative i64 survives");
    check(r.string() == "Untitled - Notepad", "string survives");
    check(r.ok(), "reader is unpoisoned after an exact read");
    check(r.remaining() == 0, "payload fully consumed");
}

// A socket hands over whatever arrived, which is not a message.
void arrives_one_byte_at_a_time() {
    const std::vector<std::uint8_t> bytes = sample_window_list();
    rwin::FrameDecoder decoder;
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;

    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        decoder.feed(&bytes[i], 1);
        check(!decoder.next(&payload, &size), "a partial message yields nothing");
    }
    decoder.feed(&bytes.back(), 1);
    check(decoder.next(&payload, &size), "the last byte completes it");
    check(!decoder.next(&payload, &size), "and there is not a second one");
}

void two_messages_in_one_read() {
    std::vector<std::uint8_t> both = sample_window_list();
    const std::vector<std::uint8_t> second = sample_window_list();
    both.insert(both.end(), second.begin(), second.end());

    rwin::FrameDecoder decoder;
    decoder.feed(both.data(), both.size());

    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "first of two decodes");
    check(decoder.next(&payload, &size), "second of two decodes");
    check(!decoder.next(&payload, &size), "and no third");
}

// Reading past the end is what a malformed peer produces. It must poison
// the reader rather than read our memory or throw across the boundary.
void short_payload_poisons_rather_than_throws() {
    rwin::MessageWriter w{rwin::MessageId::Subscribe};
    w.put_u32(1);
    const std::vector<std::uint8_t> bytes = w.take();

    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "short message still frames");

    rwin::MessageReader r{payload, size};
    check(r.u32() == 1, "the field that is there reads");
    check(r.ok(), "still fine at the end");
    (void)r.u64();
    check(!r.ok(), "reading past the end poisons the reader");
    check(r.u32() == 0, "a poisoned reader yields zeroes");
}

void absurd_length_is_refused() {
    std::vector<std::uint8_t> bytes = {0xFF, 0xFF, 0xFF, 0xFF};
    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(!decoder.next(&payload, &size), "a 4 GiB message is not decoded");
    check(decoder.overflowed(), "and the stream is marked unusable");
}

// The bytes after the id, as the other end reads them.
std::vector<std::uint8_t> body_of(rwin::MessageWriter writer) {
    std::vector<std::uint8_t> bytes = writer.take();
    bytes.erase(bytes.begin(), bytes.begin() + 5);
    return bytes;
}

template <typename T>
std::optional<T> read_back(const std::vector<std::uint8_t>& message) {
    rwin::FrameDecoder decoder;
    decoder.feed(message.data(), message.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    if (!decoder.next(&payload, &size)) return std::nullopt;
    rwin::MessageReader reader{payload, size};
    std::optional<T> value = T::read(reader);
    if (reader.remaining() != 0) return std::nullopt;
    return value;
}

void a_key_stroke_crosses_whole() {
    const rwin::KeyStroke right_ctrl{.window = rwin::WindowId::of_key(0x1234'5678'9abcULL), .scan_code = 0xE01D,
                                     .virtual_key = 0, .character = 0, .pressed = true};
    rwin::MessageWriter writer{rwin::MessageId::Key};
    right_ctrl.write(writer);
    const std::vector<std::uint8_t> message = writer.take();
    check(read_back<rwin::KeyStroke>(message) == right_ctrl, "a key stroke reads back as written");
    check(right_ctrl.extended() && right_ctrl.code() == 0x1D, "E0 1D is the extended 1D");

    // Pinned byte for byte: the guest is built by another compiler.
    rwin::MessageWriter pinned{rwin::MessageId::Key};
    rwin::KeyStroke{.window = rwin::WindowId{0, 7}, .scan_code = 0xE048, .virtual_key = 0x26, .character = 0x1F600,
                    .pressed = false}
        .write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 7, 0xE0, 0x48, 0, 0, 0, 0x26,
                                             0, 1, 0xF6, 0x00, 0};
    check(body_of(std::move(pinned)) == expected,
          "window hwnd u32 serial u32, scan code u16, virtual key u32, character u32, pressed u8");
}

void a_short_key_stroke_is_refused() {
    rwin::MessageWriter writer{rwin::MessageId::Key};
    writer.put_u64(1);
    writer.put_u16(0x1E);
    const std::vector<std::uint8_t> bytes = writer.take();
    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "a short key message still frames");
    rwin::MessageReader reader{payload, size};
    check(!rwin::KeyStroke::read(reader).has_value(), "but is no key stroke");
}

void a_picture_rect_crosses_whole() {
    rwin::MessageWriter pinned{rwin::MessageId::FramePatch};
    rwin::PictureRect{0x40, 0x80, 0x100, 0x3F}.write(pinned);
    const std::vector<std::uint8_t> bytes = body_of(std::move(pinned));
    const std::vector<std::uint8_t> expected{0, 0, 0, 0x40, 0, 0, 0, 0x80, 0, 0, 1, 0, 0, 0, 0, 0x3F};
    check(bytes == expected, "x, y, width, height, each u32");
    rwin::MessageWriter writer{rwin::MessageId::FramePatch};
    const rwin::PictureRect rect{1, 2, 3, 4};
    rect.write(writer);
    check(read_back<rwin::PictureRect>(writer.take()) == rect, "a picture rect reads back as written");
}

void a_cursor_picture_crosses_whole() {
    rwin::CursorPicture picture;
    picture.id = 0x0001'0003'0005ULL;
    picture.system = rwin::SystemCursor::AppStarting;
    picture.inverts = true;
    for (std::uint32_t step = 0; step < 3; ++step) {
        rwin::CursorPicture::Frame frame{2, 1, 1, 0, 50 * step, {}};
        for (std::uint8_t byte = 0; byte < 8; ++byte) frame.bgra.push_back(static_cast<std::uint8_t>(byte + step));
        picture.frames.push_back(std::move(frame));
    }
    rwin::MessageWriter writer{rwin::MessageId::CursorShape};
    picture.write(writer);
    check(read_back<rwin::CursorPicture>(writer.take()) == picture, "a cursor picture reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::CursorShape};
    rwin::CursorPicture{9, rwin::SystemCursor::IBeam, false,
                        {rwin::CursorPicture::Frame{1, 1, 0, 0, 16, {1, 2, 3, 4}}}}
        .write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 9, 2, 0, 0, 0, 0, 1,
                                             0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                                             0, 0, 0, 16, 1, 2, 3, 4};
    check(body_of(std::move(pinned)) == expected,
          "id u64, system u8, inverts u8, frames u32, then per frame w h hx hy delay u32 and BGRA");
}

void a_cursor_picture_of_absurd_size_is_refused() {
    rwin::MessageWriter writer{rwin::MessageId::CursorShape};
    writer.put_u64(1);
    writer.put_u8(0);
    writer.put_u8(0);
    writer.put_u32(1);
    writer.put_u32(1u << 20);
    writer.put_u32(1u << 20);
    writer.put_u32(0);
    writer.put_u32(0);
    writer.put_u32(0);
    check(!read_back<rwin::CursorPicture>(writer.take()).has_value(),
          "a cursor larger than any Windows makes is refused before it is allocated");
}

void a_hidden_cursor_says_so() {
    const rwin::GuestCursor hidden{0x10045, false};
    rwin::MessageWriter writer{rwin::MessageId::CursorChanged};
    hidden.write(writer);
    check(read_back<rwin::GuestCursor>(writer.take()) == hidden, "a hidden cursor reads back hidden");
    rwin::MessageWriter pinned{rwin::MessageId::CursorChanged};
    rwin::GuestCursor{0x0102, true}.write(pinned);
    check(body_of(std::move(pinned)) == std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 1, 2, 1},
          "id u64, shown u8");
}

void a_pointer_grab_crosses_whole() {
    const rwin::PointerGrab lock{rwin::PointerGrab::Kind::Lock, rwin::WindowId{0x500c4, 3}, {100, 200, 101, 201}, 100, 200};
    rwin::MessageWriter writer{rwin::MessageId::PointerGrab};
    lock.write(writer);
    check(read_back<rwin::PointerGrab>(writer.take()) == lock, "a pointer grab reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::PointerGrab};
    rwin::PointerGrab{rwin::PointerGrab::Kind::Confine, rwin::WindowId{0, 2}, {-1, 0, 3, 4}, 5, 6}.write(pinned);
    const std::vector<std::uint8_t> expected{1, 0, 0, 0, 0, 0, 0, 0, 2, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
                                             0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6};
    check(body_of(std::move(pinned)) == expected, "kind u8, window hwnd u32 serial u32, area i32 x4, point i32 x2");
    rwin::MessageWriter bad{rwin::MessageId::PointerGrab};
    rwin::PointerGrab{}.write(bad);
    std::vector<std::uint8_t> bytes = bad.take();
    bytes[5] = 7;
    check(!read_back<rwin::PointerGrab>(bytes).has_value(), "a grab of no kind is refused, not cast");
}

void a_pointer_delta_crosses_whole() {
    const rwin::PointerDelta delta{rwin::WindowId{0x500c4, 9}, -7, 300};
    rwin::MessageWriter writer{rwin::MessageId::PointerDelta};
    delta.write(writer);
    check(read_back<rwin::PointerDelta>(writer.take()) == delta, "a pointer delta reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::PointerDelta};
    rwin::PointerDelta{rwin::WindowId{0, 3}, -2, 258}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 3, 0xFF, 0xFF, 0xFF, 0xFE, 0, 0, 1, 2};
    check(body_of(std::move(pinned)) == expected, "window hwnd u32 serial u32, dx i32, dy i32");
    check(static_cast<int>(rwin::MessageId::PointerDelta) < 64, "PointerDelta is the host's to send");
    rwin::MessageWriter short_one{rwin::MessageId::PointerDelta};
    rwin::WindowId{0, 3}.write(short_one);
    short_one.put_i32(1);
    check(!read_back<rwin::PointerDelta>(short_one.take()).has_value(), "a delta without its dy is refused");
}

void system_cursors_are_named_as_windows_numbers_them() {
    check(rwin::idc_of(rwin::SystemCursor::Arrow) == 32512 && rwin::idc_of(rwin::SystemCursor::Person) == 32672,
          "IDC_ARROW is 32512, IDC_PERSON 32672");
    check(rwin::system_cursor_of(32649) == rwin::SystemCursor::Hand
              && rwin::system_cursor_of(32640) == rwin::SystemCursor::None,
          "IDC_HAND is known, the obsolete IDC_SIZE is not one shown");
    for (auto cursor = static_cast<int>(rwin::SystemCursor::Arrow); cursor <= static_cast<int>(rwin::SystemCursor::Person);
         ++cursor) {
        const auto named = static_cast<rwin::SystemCursor>(cursor);
        check(rwin::system_cursor_of(rwin::idc_of(named)) == named, "every system cursor round-trips its IDC_ number");
    }
}

}  // namespace

// The frames that stay on the GPU: pinned byte for byte, as everything on
// this wire is, because the guest half is built by another compiler.
void a_frame_route_crosses_whole() {
    rwin::MessageWriter writer{rwin::MessageId::FrameRoute};
    rwin::FrameRoute{true}.write(writer);
    check(read_back<rwin::FrameRoute>(writer.take()) == rwin::FrameRoute{true}, "a frame route reads back");
    rwin::MessageWriter pinned{rwin::MessageId::FrameRoute};
    rwin::FrameRoute{false}.write(pinned);
    check(body_of(std::move(pinned)) == std::vector<std::uint8_t>{0}, "shared surfaces u8");
    rwin::MessageWriter strange{rwin::MessageId::FrameRoute};
    strange.put_u8(2);
    check(!read_back<rwin::FrameRoute>(strange.take()).has_value(), "a route that is neither is refused");
}

void a_slot_ring_crosses_whole() {
    const rwin::SlotRing ring{.window = rwin::WindowId{0x01020304, 0x05060708},
                              .ring = 9,
                              .width = 1003,
                              .height = 701,
                              .surfaces = {0x11, 0x2200, 0x0033'0000'0000'0000ULL}};
    rwin::MessageWriter writer{rwin::MessageId::SlotRing};
    ring.write(writer);
    check(read_back<rwin::SlotRing>(writer.take()) == ring, "a slot ring reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::SlotRing};
    rwin::SlotRing{.window = rwin::WindowId{0, 7}, .ring = 0x0A0B, .width = 0x0500, .height = 0x02D0, .surfaces = {1, 2, 0x0300}}
        .write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 7,     0, 0, 0x0A, 0x0B, 0, 0, 0x05, 0,
                                             0, 0, 0x02, 0xD0, 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0,
                                             0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0x03, 0};
    check(body_of(std::move(pinned)) == expected,
          "window hwnd u32 serial u32, ring u32, width u32, height u32, three surfaces u64");

    rwin::MessageWriter unnamed{rwin::MessageId::SlotRing};
    rwin::SlotRing{.window = rwin::WindowId{0, 7}, .ring = 1, .width = 8, .height = 8, .surfaces = {1, 0, 3}}.write(unnamed);
    check(!read_back<rwin::SlotRing>(unnamed.take()).has_value(), "a ring naming surface 0 is refused");
    rwin::MessageWriter empty{rwin::MessageId::SlotRing};
    rwin::SlotRing{.window = rwin::WindowId{0, 7}, .ring = 1, .width = 0, .height = 8, .surfaces = {1, 2, 3}}.write(empty);
    check(!read_back<rwin::SlotRing>(empty.take()).has_value(), "a ring of no pixels is refused");
}

void a_frame_in_a_slot_crosses_whole() {
    const rwin::FrameInSlot frame{.window = rwin::WindowId{0xFFFF, 0x1234},
                                  .ring = 3,
                                  .slot = 2,
                                  .width = 1001,
                                  .height = 77,
                                  .copies = 0x1'0000'0001ULL,
                                  .composed = 0x7FFF'0000'0000'0001LL,
                                  .taken = 3,
                                  .copied = -2};
    rwin::MessageWriter writer{rwin::MessageId::FrameInSlot};
    frame.write(writer);
    check(read_back<rwin::FrameInSlot>(writer.take()) == frame, "a frame in a slot reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{
        .window = rwin::WindowId{0, 5}, .ring = 0x0102, .slot = 1, .width = 0x0A0B, .height = 0x0C0D, .copies = 0x0304,
        .composed = 0x0506, .taken = 0x0607, .copied = 0x0708}
        .write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 1, 2, 0, 0, 0, 1, 0, 0, 0x0A, 0x0B,
                                             0, 0, 0x0C, 0x0D, 0, 0, 0, 0, 0, 0, 3, 4, 0, 0, 0, 0, 0, 0, 5, 6,
                                             0, 0, 0, 0, 0, 0, 6, 7, 0, 0, 0, 0, 0, 0, 7, 8};
    check(body_of(std::move(pinned)) == expected,
          "window hwnd u32 serial u32, ring u32, slot u32, width u32, height u32, copies u64, composed i64, taken "
          "i64, copied i64");

    rwin::MessageWriter outside{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{.window = rwin::WindowId{0, 5}, .ring = 1, .slot = 3, .width = 1, .height = 1, .copies = 1}
        .write(outside);
    check(!read_back<rwin::FrameInSlot>(outside.take()).has_value(), "a slot past the ring is refused");
    rwin::MessageWriter uncopied{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{.window = rwin::WindowId{0, 5}, .ring = 1, .slot = 0, .width = 1, .height = 1, .copies = 0}
        .write(uncopied);
    check(!read_back<rwin::FrameInSlot>(uncopied.take()).has_value(), "copy 0 is no frame and is refused");
    rwin::MessageWriter empty{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{.window = rwin::WindowId{0, 5}, .ring = 1, .slot = 0, .width = 0, .height = 4, .copies = 1}
        .write(empty);
    check(!read_back<rwin::FrameInSlot>(empty.take()).has_value(), "a picture of no pixels is refused");
}

void a_clock_echo_crosses_whole() {
    const rwin::ClockEcho echo{.probed = 0xFEDC'BA98'7654'3210ULL, .guest = -5};
    rwin::MessageWriter writer{rwin::MessageId::ClockEcho};
    echo.write(writer);
    check(read_back<rwin::ClockEcho>(writer.take()) == echo, "a clock echo reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::ClockEcho};
    rwin::ClockEcho{.probed = 0x0102, .guest = 0x0304}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 3, 4};
    check(body_of(std::move(pinned)) == expected, "probed u64, guest i64");

    rwin::MessageWriter cut{rwin::MessageId::ClockEcho};
    cut.put_u64(1);
    check(!read_back<rwin::ClockEcho>(cut.take()).has_value(), "an echo without the guest's clock is refused");
}

void a_freed_slot_crosses_whole() {
    const rwin::SlotFree freed{.window = rwin::WindowId{0, 0xABCD}, .ring = 77, .slot = 0};
    rwin::MessageWriter writer{rwin::MessageId::SlotFree};
    freed.write(writer);
    check(read_back<rwin::SlotFree>(writer.take()) == freed, "a freed slot reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::SlotFree};
    rwin::SlotFree{.window = rwin::WindowId{0, 0x0100}, .ring = 2, .slot = 2}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, 2};
    check(body_of(std::move(pinned)) == expected, "window hwnd u32 serial u32, ring u32, slot u32");
    rwin::MessageWriter outside{rwin::MessageId::SlotFree};
    rwin::SlotFree{.window = rwin::WindowId{0, 1}, .ring = 1, .slot = 7}.write(outside);
    check(!read_back<rwin::SlotFree>(outside.take()).has_value(), "freeing a slot past the ring is refused");
}

namespace {

std::vector<std::uint8_t> bytes_of(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    out.reserve(values.size());
    for (const int value : values) out.push_back(static_cast<std::uint8_t>(value));
    return out;
}

void append(std::vector<std::uint8_t>& to, std::string_view text) {
    to.insert(to.end(), text.begin(), text.end());
}

void a_window_id_crosses_whole() {
    const rwin::WindowId id{0x00010002, 7};
    rwin::MessageWriter writer{rwin::MessageId::WindowGone};
    id.write(writer);
    check(read_back<rwin::WindowId>(writer.take()) == id, "a window id reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::WindowGone};
    id.write(pinned);
    check(body_of(std::move(pinned)) == bytes_of({0, 1, 0, 2, 0, 0, 0, 7}), "hwnd u32, serial u32");
    check(rwin::WindowId::of_key(id.key()) == id && id.key() == 0x0001'0002'0000'0007ULL,
          "a window id's key is its wire bytes as one number");
    check(rwin::WindowId::hidden_owner().hwnd == 0xFFFFFFFE && rwin::WindowId::hidden_owner().serial == 0,
          "the hidden owner is RAIL's 0xFFFFFFFE");
    check(rwin::kMonitorWindow.none() && !id.none(), "only {0, 0} is no window");
}

void a_hello_names_its_features() {
    const rwin::Hello ours = rwin::Hello::ours();
    rwin::MessageWriter writer{rwin::MessageId::GuestHello};
    ours.write(writer);
    check(read_back<rwin::Hello>(writer.take()) == ours, "this build's hello reads back as written");
    check(rwin::FeatureSet(ours.features, ours.features).has(rwin::Feature::kWindows),
          "this build speaks windows to itself");

    rwin::MessageWriter pinned{rwin::MessageId::HostHello};
    rwin::Hello{"rw", {rwin::Feature{"windows", 1}}}.write(pinned);
    std::vector<std::uint8_t> expected = bytes_of({0, 0, 0, 2});
    append(expected, "rw");
    const std::vector<std::uint8_t> count = bytes_of({0, 1, 0, 0, 0, 7});
    expected.insert(expected.end(), count.begin(), count.end());
    append(expected, "windows");
    expected.push_back(0);
    expected.push_back(1);
    check(body_of(std::move(pinned)) == expected,
          "build string, feature count u16, then per feature name string and version u16");

    rwin::MessageWriter unversioned{rwin::MessageId::HostHello};
    rwin::Hello{"rw", {rwin::Feature{"windows", 0}}}.write(unversioned);
    check(!read_back<rwin::Hello>(unversioned.take()).has_value(), "a feature at version 0 is refused");

    rwin::MessageWriter crowded{rwin::MessageId::HostHello};
    crowded.put_string("rw");
    crowded.put_u16(static_cast<std::uint16_t>(rwin::Hello::kMaxFeatures + 1));
    check(!read_back<rwin::Hello>(crowded.take()).has_value(), "a hello listing too many features is refused");

    // What a guest half from before wire 6 sends first: the old hello, id 71
    // with a version u32, which is not this hello by its id.
    rwin::MessageWriter old{static_cast<rwin::MessageId>(71)};
    old.put_u32(5);
    const std::vector<std::uint8_t> old_bytes = old.take();
    rwin::FrameDecoder decoder;
    decoder.feed(old_bytes.data(), old_bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "a wire 5 guest's first message frames");
    const rwin::MessageReader reader{payload, size};
    check(reader.id() != rwin::MessageId::GuestHello, "and is not a wire 6 hello");
}

void features_are_active_at_the_lower_version() {
    const std::vector<rwin::Feature> ours{{"windows", 2}, {"input", 1}, {"cursor", 3}};
    const std::vector<rwin::Feature> theirs{{"windows", 1}, {"cursor", 1}, {"latency", 1}};
    const rwin::FeatureSet active{ours, theirs};
    check(active.version("windows") == 1 && active.version("cursor") == 1, "both list it: the lower version");
    check(!active.has("input") && !active.has("latency"), "one lists it: not active");
    check(active.refusal().empty(), "a link with windows goes on");
    const rwin::FeatureSet bare{ours, {{"input", 1}}};
    check(!bare.refusal().empty() && bare.refusal().find("windows") != std::string::npos,
          "a peer without windows is refused, naming what it lacks");
    check(!rwin::FeatureSet{}.has("windows"), "before the hellos nothing is active");
}

rwin::WindowFields sample_fields() {
    rwin::WindowFields fields;
    fields.owner = rwin::WindowId::hidden_owner();
    fields.style = {0x16CF0000, 0x00000100, 0x0008};
    fields.show = rwin::WindowFields::Show::Maximized;
    fields.title = "Untitled - Notepad";
    fields.geometry.frame = {-8, 20, 1024, 768};
    fields.geometry.client = {0, 31, 1024, 737};
    fields.geometry.margin_left = 7;
    fields.geometry.margin_bottom = 7;
    fields.geometry.region = {{0, 0, 10, 10}, {10, 0, 5, 5}};
    fields.geometry.picture = {2, 3, 1000, 760};
    fields.taskbar = rwin::WindowFields::kTaskbarButton | rwin::WindowFields::kInSwitchers;
    fields.z_glue = true;
    fields.app_id = {"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", 4242, "C:\\Program Files\\notepad.exe"};
    fields.class_name = "Notepad";
    fields.layering = {2, 128, 0x00FF00FF};
    fields.band = 1;
    fields.frame = rwin::WindowFields::kDwmFrame;
    using Popup = rwin::WindowFields::Popup;
    fields.popup = {rwin::WindowId{0x30, 2}, Popup::Kind::Submenu, {5, 6, 120, 300}, {0, 40, 200, 22},
                    Popup::Edge::TopRight, Popup::Edge::BottomRight, Popup::kFlipX | Popup::kSlideY};
    fields.dpi = 120;
    fields.cloaked = true;
    return fields;
}

void window_fields_cross_whole() {
    const rwin::WindowFields fields = sample_fields();
    rwin::MessageWriter writer{rwin::MessageId::WindowNew};
    rwin::WindowId{0x30, 1}.write(writer);
    fields.write(writer, rwin::WindowFields::kAll);
    const std::vector<std::uint8_t> bytes = writer.take();
    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "a new window frames");
    rwin::MessageReader reader{payload, size};
    const std::optional<rwin::WindowId> id = rwin::WindowId::read(reader);
    rwin::WindowFields read;
    std::uint32_t mask = 0;
    check(id == rwin::WindowId{0x30, 1} && rwin::WindowFields::read(reader, read, mask), "a new window reads");
    check(mask == rwin::WindowFields::kAll && read == fields && reader.remaining() == 0,
          "every field reads back as written");
}

void a_changed_window_carries_only_what_changed() {
    rwin::WindowFields known = sample_fields();
    rwin::WindowFields now = known;
    now.title = "a";
    now.show = rwin::WindowFields::Show::Shown;
    const std::uint32_t mask = now.differences(known);
    check(mask == (rwin::WindowFields::kTitle | rwin::WindowFields::kShow), "only the title and show state differ");

    rwin::MessageWriter pinned{rwin::MessageId::WindowChanged};
    now.write(pinned, mask);
    check(body_of(std::move(pinned)) == bytes_of({0, 0, 0, 0x0C, 5, 0, 0, 0, 1, 'a'}),
          "mask u32, then show u8 and title string in bit order");

    rwin::MessageWriter writer{rwin::MessageId::WindowChanged};
    now.write(writer, mask);
    const std::vector<std::uint8_t> bytes = writer.take();
    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "a change frames");
    rwin::MessageReader reader{payload, size};
    std::uint32_t read_mask = 0;
    check(rwin::WindowFields::read(reader, known, read_mask) && read_mask == mask, "a change reads over the record");
    check(known == now, "and leaves the record as the guest's");

    rwin::WindowFields reshaped = now;
    reshaped.geometry.region.pop_back();
    check(reshaped.differences(now) == rwin::WindowFields::kGeometry, "a region alone is the whole geometry");
    check(now.differences(now) == 0, "nothing changed is an empty mask");
}

void a_popup_says_where_it_was_meant_to_open() {
    using Popup = rwin::WindowFields::Popup;
    rwin::WindowFields fields;
    fields.popup = {rwin::WindowId{0x30, 2}, Popup::Kind::MenuBar, {10, 30, 100, 200}, {10, 8, 40, 22},
                    Popup::Edge::BottomLeft, Popup::Edge::BottomRight, Popup::kFlipY | Popup::kSlideX};
    rwin::MessageWriter pinned{rwin::MessageId::WindowChanged};
    fields.write(pinned, rwin::WindowFields::kPopup);
    std::vector<std::uint8_t> expected = bytes_of({0, 0, 0x10, 0, 0, 0, 0, 0x30, 0, 0, 0, 2, 1});
    for (const std::uint32_t word : {10u, 30u, 100u, 200u, 10u, 8u, 40u, 22u}) {
        const std::vector<std::uint8_t> bytes = bytes_of({0, 0, 0, static_cast<int>(word)});
        expected.insert(expected.end(), bytes.begin(), bytes.end());
    }
    for (const int tail : {6, 8, 9}) expected.push_back(static_cast<std::uint8_t>(tail));
    check(body_of(std::move(pinned)) == expected,
          "mask, anchor id, kind u8, placed rect, opens-from rect, edge u8, gravity u8, adjustment u8");

    rwin::MessageWriter strange{rwin::MessageId::WindowChanged};
    strange.put_u32(rwin::WindowFields::kPopup);
    rwin::WindowId{0x30, 2}.write(strange);
    strange.put_u8(9);
    for (int word = 0; word < 8; ++word) strange.put_u32(0);
    for (int tail = 0; tail < 3; ++tail) strange.put_u8(0);
    const std::vector<std::uint8_t> strange_bytes = strange.take();
    rwin::FrameDecoder decoder;
    decoder.feed(strange_bytes.data(), strange_bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "a popup of no known kind frames");
    rwin::MessageReader reader{payload, size};
    rwin::WindowFields into;
    std::uint32_t mask = 0;
    check(!rwin::WindowFields::read(reader, into, mask), "and is refused");
}

void a_window_icon_crosses_whole() {
    rwin::WindowIcon icon{rwin::WindowId{0x40, 3}, rwin::WindowIcon::Size::Big, 2, 1, {1, 2, 3, 4, 5, 6, 7, 8}};
    rwin::MessageWriter writer{rwin::MessageId::WindowIcon};
    icon.write(writer);
    check(read_back<rwin::WindowIcon>(writer.take()) == icon, "a window icon reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::WindowIcon};
    icon.write(pinned);
    check(body_of(std::move(pinned)) == bytes_of({0, 0, 0, 0x40, 0, 0, 0, 3, 1, 0, 2, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8}),
          "window, size u8, width u16, height u16, then BGRA");
    rwin::MessageWriter huge{rwin::MessageId::WindowIcon};
    rwin::WindowId{0x40, 3}.write(huge);
    huge.put_u8(1);
    huge.put_u16(rwin::WindowIcon::kMaxSide + 1);
    huge.put_u16(1);
    check(!read_back<rwin::WindowIcon>(huge.take()).has_value(), "an icon wider than any is refused");
    rwin::WindowIcon cut = icon;
    cut.bgra.pop_back();
    rwin::MessageWriter short_one{rwin::MessageId::WindowIcon};
    cut.write(short_one);
    check(!read_back<rwin::WindowIcon>(short_one.take()).has_value(), "an icon cut short is refused");
}

void malformed_window_fields_are_refused() {
    const auto refused = [](rwin::MessageWriter writer) {
        const std::vector<std::uint8_t> bytes = writer.take();
        rwin::FrameDecoder decoder;
        decoder.feed(bytes.data(), bytes.size());
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        if (!decoder.next(&payload, &size)) return false;
        rwin::MessageReader reader{payload, size};
        rwin::WindowFields into;
        std::uint32_t mask = 0;
        return !rwin::WindowFields::read(reader, into, mask);
    };
    rwin::MessageWriter unknown_field{rwin::MessageId::WindowChanged};
    unknown_field.put_u32(1u << 15);
    check(refused(std::move(unknown_field)), "a field past the last known is refused");
    rwin::MessageWriter strange_show{rwin::MessageId::WindowChanged};
    strange_show.put_u32(rwin::WindowFields::kShow);
    strange_show.put_u8(4);
    check(refused(std::move(strange_show)), "a show state RAIL has no value for is refused");
    rwin::MessageWriter huge_region{rwin::MessageId::WindowChanged};
    huge_region.put_u32(rwin::WindowFields::kGeometry);
    rwin::Rect{}.write(huge_region);
    rwin::Rect{}.write(huge_region);
    for (int margin = 0; margin < 4; ++margin) huge_region.put_u16(0);
    huge_region.put_u16(static_cast<std::uint16_t>(rwin::WindowFields::kMaxRegionRects + 1));
    check(refused(std::move(huge_region)), "a region past the cap is refused before it is allocated");
    rwin::MessageWriter cut{rwin::MessageId::WindowChanged};
    cut.put_u32(rwin::WindowFields::kTitle);
    cut.put_u32(10);
    check(refused(std::move(cut)), "a title cut short is refused");
}

void a_z_order_crosses_whole() {
    const rwin::WindowId a{0x10, 1};
    const rwin::WindowId b{0x20, 2};
    const rwin::ZOrder order{a, {a, rwin::kMonitorWindow, b}};
    rwin::MessageWriter writer{rwin::MessageId::ZOrder};
    order.write(writer);
    check(read_back<rwin::ZOrder>(writer.take()) == order, "a z-order reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::ZOrder};
    rwin::ZOrder{b, {b}}.write(pinned);
    check(body_of(std::move(pinned)) == bytes_of({0, 0, 0, 0x20, 0, 0, 0, 2, 0, 1, 0, 0, 0, 0x20, 0, 0, 0, 2}),
          "active id, count u16, ids top first");
    check(order.above_marker(a) && !order.above_marker(b) && !order.above_marker(rwin::WindowId{0x30, 1}),
          "above the marker is above it in the list");
    check(!rwin::ZOrder{a, {a, b}}.above_marker(a), "with no marker listed nothing is above it");
    rwin::MessageWriter crowded{rwin::MessageId::ZOrder};
    rwin::WindowId{}.write(crowded);
    crowded.put_u16(static_cast<std::uint16_t>(rwin::ZOrder::kMaxWindows + 1));
    check(!read_back<rwin::ZOrder>(crowded.take()).has_value(), "a list past RAIL's cap is refused");
}

void the_desktop_messages_cross_whole() {
    rwin::MessageWriter begin{rwin::MessageId::SyncBegin};
    rwin::SyncBegin{rwin::SyncBegin::Reason::Resync}.write(begin);
    check(body_of(std::move(begin)) == bytes_of({1}), "sync reason u8");
    rwin::MessageWriter strange{rwin::MessageId::SyncBegin};
    strange.put_u8(9);
    check(!read_back<rwin::SyncBegin>(strange.take()).has_value(), "a sync of no known reason is refused");

    rwin::MessageWriter area{rwin::MessageId::GuestSysParam};
    rwin::SysParam{rwin::SysParam::Param::WorkArea, {0, 0, 0x0A00, 0x0570}, false}.write(area);
    check(body_of(std::move(area)) == bytes_of({0, 0, 0, 0x2F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0A, 0, 0, 0, 0x05, 0x70}),
          "SPI_SETWORKAREA u32, then the rect");
    rwin::MessageWriter monitor{rwin::MessageId::GuestSysParam};
    const rwin::SysParam whole{rwin::SysParam::Param::Monitor, {0, 0, 2560, 1440}, false};
    whole.write(monitor);
    const std::vector<std::uint8_t> monitor_bytes = monitor.take();
    check(read_back<rwin::SysParam>(monitor_bytes) == whole, "the guest's monitor reads back");
    rwin::MessageWriter drag{rwin::MessageId::HostSysParam};
    const rwin::SysParam full{rwin::SysParam::Param::DragFullWindows, {}, true};
    full.write(drag);
    const std::vector<std::uint8_t> drag_bytes = drag.take();
    check(read_back<rwin::SysParam>(drag_bytes) == full, "drag full windows reads back");
    rwin::MessageWriter unknown{rwin::MessageId::HostSysParam};
    unknown.put_u32(0x1001);
    unknown.put_u8(1);
    check(!read_back<rwin::SysParam>(unknown.take()).has_value(), "a setting not spoken here is refused");
}

void the_move_size_messages_cross_whole() {
    const rwin::WindowId window{0x40, 3};
    rwin::MessageWriter info{rwin::MessageId::MinMax};
    const rwin::MinMax limits{window, 1920, 1080, -8, -8, 136, 39, 1940, 1100};
    limits.write(info);
    check(read_back<rwin::MinMax>(info.take()) == limits, "min/max info reads back");
    rwin::MessageWriter pinned_info{rwin::MessageId::MinMax};
    rwin::MinMax{window, 1, 2, 3, 4, 5, 6, 7, 8}.write(pinned_info);
    check(body_of(std::move(pinned_info))
              == bytes_of({0, 0, 0, 0x40, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5,
                           0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0, 8}),
          "window, max w h, max pos x y, min track w h, max track w h, each i32");

    rwin::MessageWriter start{rwin::MessageId::MoveSizeStart};
    rwin::MoveSizeStart{window, rwin::MoveSizeKind::BottomRight, 10, -1}.write(start);
    const std::vector<std::uint8_t> start_bytes = start.take();
    check(read_back<rwin::MoveSizeStart>(start_bytes)
              == rwin::MoveSizeStart{window, rwin::MoveSizeKind::BottomRight, 10, -1},
          "a move/size start reads back");
    check(std::vector<std::uint8_t>(start_bytes.begin() + 5, start_bytes.end())
              == bytes_of({0, 0, 0, 0x40, 0, 0, 0, 3, 8, 0, 0, 0, 10, 0xFF, 0xFF, 0xFF, 0xFF}),
          "window, kind u8 (RAIL_WMSZ), x i32, y i32");
    for (const int kind : {0, 12}) {
        rwin::MessageWriter strange{rwin::MessageId::MoveSizeStart};
        window.write(strange);
        strange.put_u8(static_cast<std::uint8_t>(kind));
        strange.put_i32(0);
        strange.put_i32(0);
        check(!read_back<rwin::MoveSizeStart>(strange.take()).has_value(), "a kind RAIL does not number is refused");
    }

    rwin::MessageWriter end{rwin::MessageId::MoveSizeEnd};
    const rwin::MoveSizeEnd ended{window, rwin::MoveSizeKind::Move, {1, 2, 3, 4}};
    ended.write(end);
    check(read_back<rwin::MoveSizeEnd>(end.take()) == ended, "a move/size end reads back with its frame");

    rwin::MessageWriter move{rwin::MessageId::WindowMove};
    rwin::WindowMove{window, {-5, 6, 800, 600}}.write(move);
    check(body_of(std::move(move))
              == bytes_of({0, 0, 0, 0x40, 0, 0, 0, 3, 0xFF, 0xFF, 0xFF, 0xFB, 0, 0, 0, 6, 0, 0, 3, 0x20, 0, 0, 2, 0x58}),
          "window, frame x y i32, w h u32");
    rwin::MessageWriter empty{rwin::MessageId::WindowMove};
    rwin::WindowMove{window, {0, 0, 0, 600}}.write(empty);
    check(!read_back<rwin::WindowMove>(empty.take()).has_value(), "a window moved to no size is refused");
}

void the_window_commands_cross_whole() {
    const rwin::WindowId window{0x50, 4};
    rwin::MessageWriter activate{rwin::MessageId::Activate};
    rwin::Activate{window, false}.write(activate);
    check(body_of(std::move(activate)) == bytes_of({0, 0, 0, 0x50, 0, 0, 0, 4, 0}), "window, enabled u8");
    rwin::MessageWriter strange{rwin::MessageId::Activate};
    window.write(strange);
    strange.put_u8(2);
    check(!read_back<rwin::Activate>(strange.take()).has_value(), "an activation neither on nor off is refused");

    rwin::MessageWriter close{rwin::MessageId::SysCommand};
    rwin::SysCommand{window, rwin::SysCommand::Command::Close}.write(close);
    const std::vector<std::uint8_t> close_bytes = close.take();
    check(std::vector<std::uint8_t>(close_bytes.begin() + 5, close_bytes.end())
              == bytes_of({0, 0, 0, 0x50, 0, 0, 0, 4, 0xF0, 0x60}),
          "window, SC_CLOSE u16");
    check(read_back<rwin::SysCommand>(close_bytes) == rwin::SysCommand{window, rwin::SysCommand::Command::Close},
          "a close reads back as SC_CLOSE");
    rwin::MessageWriter other{rwin::MessageId::SysCommand};
    window.write(other);
    other.put_u16(0xF180);
    check(!read_back<rwin::SysCommand>(other.take()).has_value(), "a command RAIL ignores is refused");

    rwin::MessageWriter menu{rwin::MessageId::SysMenu};
    rwin::SysMenu{window, 3, -4}.write(menu);
    check(body_of(std::move(menu)) == bytes_of({0, 0, 0, 0x50, 0, 0, 0, 4, 0, 0, 0, 3, 0xFF, 0xFF, 0xFF, 0xFC}),
          "window, x i32, y i32");

    rwin::MessageWriter lost{rwin::MessageId::SurfaceLost};
    rwin::SurfaceLost{window, rwin::SurfaceLost::Reason::CaptureFailed}.write(lost);
    check(body_of(std::move(lost)) == bytes_of({0, 0, 0, 0x50, 0, 0, 0, 4, 0}), "window, reason u8");
}

void the_pointer_messages_cross_whole() {
    const rwin::WindowId window{0x60, 5};
    rwin::MessageWriter move{rwin::MessageId::PointerMove};
    rwin::PointerMove{window, 1, 2}.write(move);
    check(body_of(std::move(move)) == bytes_of({0, 0, 0, 0x60, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 2}),
          "window, x i32, y i32");

    for (const auto button : {rwin::PointerButton::Button::Back, rwin::PointerButton::Button::Forward}) {
        rwin::MessageWriter press{rwin::MessageId::PointerButton};
        const rwin::PointerButton pressed{window, 7, 8, button, true};
        pressed.write(press);
        check(read_back<rwin::PointerButton>(press.take()) == pressed, "Back and Forward cross as themselves");
    }
    rwin::MessageWriter pinned{rwin::MessageId::PointerButton};
    rwin::PointerButton{window, 0, 0, rwin::PointerButton::Button::Forward, false}.write(pinned);
    check(body_of(std::move(pinned)) == bytes_of({0, 0, 0, 0x60, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0}),
          "window, x, y, button u8 (Forward 4), pressed u8");
    rwin::MessageWriter sixth{rwin::MessageId::PointerButton};
    window.write(sixth);
    sixth.put_i32(0);
    sixth.put_i32(0);
    sixth.put_u8(5);
    sixth.put_u8(1);
    check(!read_back<rwin::PointerButton>(sixth.take()).has_value(), "a button with no number is refused");

    rwin::MessageWriter held{rwin::MessageId::PointerAt};
    const rwin::PointerAt back{window, 100, 200, static_cast<std::uint8_t>(rwin::PointerButton::Button::Back), false};
    back.write(held);
    check(read_back<rwin::PointerAt>(held.take()) == back, "a held Back button comes up at a screen point");
    rwin::MessageWriter odd{rwin::MessageId::PointerAt};
    rwin::PointerAt{window, 0, 0, 9, true}.write(odd);
    check(!read_back<rwin::PointerAt>(odd.take()).has_value(), "a held button with no number is refused");

    rwin::MessageWriter wheel{rwin::MessageId::PointerWheel};
    const rwin::PointerWheel turned{window, 1, 2, -120, 240};
    turned.write(wheel);
    check(read_back<rwin::PointerWheel>(wheel.take()) == turned, "a wheel reads back as written");
}

void each_end_has_its_own_ids() {
    for (const rwin::MessageId host :
         {rwin::MessageId::HostHello, rwin::MessageId::HostSysParam, rwin::MessageId::Activate,
          rwin::MessageId::SysCommand, rwin::MessageId::SysMenu, rwin::MessageId::WindowMove,
          rwin::MessageId::FrameRoute, rwin::MessageId::SlotFree, rwin::MessageId::Subscribe,
          rwin::MessageId::Unsubscribe, rwin::MessageId::ClockProbe, rwin::MessageId::WatchMonitor,
          rwin::MessageId::WindowPixels, rwin::MessageId::PointerMove, rwin::MessageId::PointerButton,
          rwin::MessageId::PointerWheel, rwin::MessageId::PointerDelta, rwin::MessageId::PointerAt,
          rwin::MessageId::Key, rwin::MessageId::PointerLeave, rwin::MessageId::PopupDismiss}) {
        check(static_cast<int>(host) >= 1 && static_cast<int>(host) < 64, "a host message is below 64");
    }
    for (const rwin::MessageId guest :
         {rwin::MessageId::GuestHello, rwin::MessageId::SyncBegin, rwin::MessageId::SyncEnd,
          rwin::MessageId::WindowNew, rwin::MessageId::WindowChanged, rwin::MessageId::WindowGone,
          rwin::MessageId::ZOrder, rwin::MessageId::MinMax, rwin::MessageId::MoveSizeStart,
          rwin::MessageId::MoveSizeEnd, rwin::MessageId::GuestSysParam, rwin::MessageId::SlotRing,
          rwin::MessageId::FrameInSlot, rwin::MessageId::Frame, rwin::MessageId::FramePatch,
          rwin::MessageId::SurfaceLost, rwin::MessageId::CursorShape, rwin::MessageId::CursorChanged,
          rwin::MessageId::PointerGrab, rwin::MessageId::ClockEcho, rwin::MessageId::WindowIcon}) {
        check(static_cast<int>(guest) >= 64 && static_cast<int>(guest) < 128, "a guest message is from 64 to 127");
    }
}

}  // namespace

int main() {
    a_window_id_crosses_whole();
    a_hello_names_its_features();
    features_are_active_at_the_lower_version();
    window_fields_cross_whole();
    a_changed_window_carries_only_what_changed();
    malformed_window_fields_are_refused();
    a_popup_says_where_it_was_meant_to_open();
    a_window_icon_crosses_whole();
    a_z_order_crosses_whole();
    the_desktop_messages_cross_whole();
    the_move_size_messages_cross_whole();
    the_window_commands_cross_whole();
    the_pointer_messages_cross_whole();
    each_end_has_its_own_ids();
    a_frame_route_crosses_whole();
    a_slot_ring_crosses_whole();
    a_frame_in_a_slot_crosses_whole();
    a_freed_slot_crosses_whole();
    a_clock_echo_crosses_whole();
    a_key_stroke_crosses_whole();
    a_short_key_stroke_is_refused();
    a_picture_rect_crosses_whole();
    a_cursor_picture_crosses_whole();
    a_cursor_picture_of_absurd_size_is_refused();
    a_hidden_cursor_says_so();
    a_pointer_grab_crosses_whole();
    a_pointer_delta_crosses_whole();
    system_cursors_are_named_as_windows_numbers_them();
    round_trip();
    arrives_one_byte_at_a_time();
    two_messages_in_one_read();
    short_payload_poisons_rather_than_throws();
    absurd_length_is_refused();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("proto: all checks passed\n");
    return EXIT_SUCCESS;
}
