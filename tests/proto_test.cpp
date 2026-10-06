#include "proto/cursor_picture.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/frame_route.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/guest_desktop.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/owned_window.hpp"
#include "proto/picture_rect.hpp"
#include "proto/pointer_delta.hpp"
#include "proto/pointer_grab.hpp"
#include "proto/slot_free.hpp"
#include "proto/slot_ring.hpp"
#include "proto/system_cursor.hpp"
#include "proto/wire_hello.hpp"

#include <cstdio>
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
    rwin::MessageWriter w{rwin::MessageId::WindowList};
    w.put_u32(1);
    w.put_u64(0x1234'5678'9abc'def0ULL);
    w.put_u32(4242);
    w.put_i32(-7);
    w.put_u16(1024);
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
    check(r.id() == rwin::MessageId::WindowList, "id survives");
    check(r.u32() == 1, "u32 survives");
    check(r.u64() == 0x1234'5678'9abc'def0ULL, "u64 survives");
    check(r.u32() == 4242, "second u32 survives");
    check(r.i32() == -7, "negative i32 survives");
    check(r.u16() == 1024, "u16 survives");
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
    const rwin::KeyStroke right_ctrl{.window = 0x1234'5678'9abcULL, .scan_code = 0xE01D,
                                     .virtual_key = 0, .character = 0, .pressed = true};
    rwin::MessageWriter writer{rwin::MessageId::Key};
    right_ctrl.write(writer);
    const std::vector<std::uint8_t> message = writer.take();
    check(read_back<rwin::KeyStroke>(message) == right_ctrl, "a key stroke reads back as written");
    check(right_ctrl.extended() && right_ctrl.code() == 0x1D, "E0 1D is the extended 1D");

    // Pinned byte for byte: the guest is built by another compiler.
    rwin::MessageWriter pinned{rwin::MessageId::Key};
    rwin::KeyStroke{.window = 7, .scan_code = 0xE048, .virtual_key = 0x26, .character = 0x1F600,
                    .pressed = false}
        .write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 7, 0xE0, 0x48, 0, 0, 0, 0x26,
                                             0, 1, 0xF6, 0x00, 0};
    check(body_of(std::move(pinned)) == expected,
          "window u64, scan code u16, virtual key u32, character u32, pressed u8");
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

void the_guest_desktop_crosses_whole() {
    const rwin::GuestDesktop desktop{2560, 1392, true};
    rwin::MessageWriter writer{rwin::MessageId::WindowList};
    desktop.write(writer);
    check(read_back<rwin::GuestDesktop>(writer.take()) == desktop, "the desktop reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::WindowList};
    rwin::GuestDesktop{0x0A00, 0x0570, false}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0x0A, 0, 0, 0, 0x05, 0x70, 0};
    check(body_of(std::move(pinned)) == expected, "room width u32, height u32, shadows u8");
}

void a_hello_crosses_whole() {
    rwin::MessageWriter writer{rwin::MessageId::GuestHello};
    rwin::WireHello{}.write(writer);
    check(read_back<rwin::WireHello>(writer.take()) == rwin::WireHello{}, "a hello reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::HostHello};
    rwin::WireHello{0x01020304}.write(pinned);
    const std::vector<std::uint8_t> expected{1, 2, 3, 4};
    check(body_of(std::move(pinned)) == expected, "version u32");

    // What a guest half older than the hello sends first: a window list,
    // which is no hello however it is read.
    rwin::MessageWriter list{rwin::MessageId::WindowList};
    list.put_u8(1);
    const std::vector<std::uint8_t> bytes = list.take();
    rwin::FrameDecoder decoder;
    decoder.feed(bytes.data(), bytes.size());
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    check(decoder.next(&payload, &size), "an old guest's first message frames");
    rwin::MessageReader reader{payload, size};
    check(reader.id() != rwin::MessageId::GuestHello, "and is not a hello by its id");
    check(!rwin::WireHello::read(reader).has_value(), "nor by its body");
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
    const rwin::PointerGrab lock{rwin::PointerGrab::Kind::Lock, 0x500c4, {100, 200, 101, 201}, 100, 200};
    rwin::MessageWriter writer{rwin::MessageId::PointerGrabbed};
    lock.write(writer);
    check(read_back<rwin::PointerGrab>(writer.take()) == lock, "a pointer grab reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::PointerGrabbed};
    rwin::PointerGrab{rwin::PointerGrab::Kind::Confine, 2, {-1, 0, 3, 4}, 5, 6}.write(pinned);
    const std::vector<std::uint8_t> expected{1, 0, 0, 0, 0, 0, 0, 0, 2, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
                                             0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6};
    check(body_of(std::move(pinned)) == expected, "kind u8, window u64, area i32 x4, point i32 x2");
    rwin::MessageWriter bad{rwin::MessageId::PointerGrabbed};
    rwin::PointerGrab{}.write(bad);
    std::vector<std::uint8_t> bytes = bad.take();
    bytes[5] = 7;
    check(!read_back<rwin::PointerGrab>(bytes).has_value(), "a grab of no kind is refused, not cast");
}

void a_pointer_delta_crosses_whole() {
    const rwin::PointerDelta delta{0x500c4, -7, 300};
    rwin::MessageWriter writer{rwin::MessageId::PointerDelta};
    delta.write(writer);
    check(read_back<rwin::PointerDelta>(writer.take()) == delta, "a pointer delta reads back as written");
    rwin::MessageWriter pinned{rwin::MessageId::PointerDelta};
    rwin::PointerDelta{3, -2, 258}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 3, 0xFF, 0xFF, 0xFF, 0xFE, 0, 0, 1, 2};
    check(body_of(std::move(pinned)) == expected, "window u64, dx i32, dy i32");
    check(static_cast<int>(rwin::MessageId::PointerDelta) < 64, "PointerDelta is the host's to send");
    rwin::MessageWriter short_one{rwin::MessageId::PointerDelta};
    short_one.put_u64(3);
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

// Both ends decide by it - the host what surface a mirror is, the guest who
// takes a drag - so it is pinned here, where both are tested.
void an_owned_window_with_a_caption_stands_alone() {
    constexpr std::uint32_t kCaption = 0x00C00000;
    constexpr std::uint32_t kPopup = 0x80000000;
    constexpr std::uint32_t kBorder = 0x00800000;
    constexpr std::uint32_t kNoActivate = 0x08000000;
    check(rwin::owned_window_stands_alone(kCaption, 0), "a dialog's caption: a window of its own");
    check(rwin::owned_window_stands_alone(0x00CF0000, 0x80), "a tool window with a caption too");
    check(!rwin::owned_window_stands_alone(kPopup | kBorder, 0), "a bordered popup is a popup");
    check(!rwin::owned_window_stands_alone(kPopup, 0), "a menu is a popup");
    check(!rwin::owned_window_stands_alone(kCaption, kNoActivate),
          "one that never activates stays a popup: it is never typed into");
    // Affinity's floating tool panel, as the guest listed it.
    check(rwin::owned_window_stands_alone(0x16080000, 0x00080000),
          "an overlapped window with its caption taken away is still a window");
    check(!rwin::owned_window_stands_alone(0x96000000, 0x08080088),
          "a WPF dropdown (WS_POPUP, no-activate, topmost) is a popup");
    check(!rwin::owned_window_stands_alone(0x9e000028, 0x00080000),
          "a disabled WS_POPUP overlay is a popup");
    check(static_cast<int>(rwin::MessageId::MoveWindow) < 64, "MoveWindow is the host's to send");
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
    const rwin::SlotRing ring{.window = 0x0102'0304'0506'0708ULL,
                              .ring = 9,
                              .width = 1003,
                              .height = 701,
                              .surfaces = {0x11, 0x2200, 0x0033'0000'0000'0000ULL}};
    rwin::MessageWriter writer{rwin::MessageId::SlotRing};
    ring.write(writer);
    check(read_back<rwin::SlotRing>(writer.take()) == ring, "a slot ring reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::SlotRing};
    rwin::SlotRing{.window = 7, .ring = 0x0A0B, .width = 0x0500, .height = 0x02D0, .surfaces = {1, 2, 0x0300}}
        .write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 7,     0, 0, 0x0A, 0x0B, 0, 0, 0x05, 0,
                                             0, 0, 0x02, 0xD0, 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 0,
                                             0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0x03, 0};
    check(body_of(std::move(pinned)) == expected,
          "window u64, ring u32, width u32, height u32, three surfaces u64");

    rwin::MessageWriter unnamed{rwin::MessageId::SlotRing};
    rwin::SlotRing{.window = 7, .ring = 1, .width = 8, .height = 8, .surfaces = {1, 0, 3}}.write(unnamed);
    check(!read_back<rwin::SlotRing>(unnamed.take()).has_value(), "a ring naming surface 0 is refused");
    rwin::MessageWriter empty{rwin::MessageId::SlotRing};
    rwin::SlotRing{.window = 7, .ring = 1, .width = 0, .height = 8, .surfaces = {1, 2, 3}}.write(empty);
    check(!read_back<rwin::SlotRing>(empty.take()).has_value(), "a ring of no pixels is refused");
}

void a_frame_in_a_slot_crosses_whole() {
    const rwin::FrameInSlot frame{.window = 0xFFFF'0000'1234ULL, .ring = 3, .slot = 2, .copies = 0x1'0000'0001ULL};
    rwin::MessageWriter writer{rwin::MessageId::FrameInSlot};
    frame.write(writer);
    check(read_back<rwin::FrameInSlot>(writer.take()) == frame, "a frame in a slot reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{.window = 5, .ring = 0x0102, .slot = 1, .copies = 0x0304}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 1, 2, 0, 0, 0, 1,
                                             0, 0, 0, 0, 0, 0, 3, 4};
    check(body_of(std::move(pinned)) == expected, "window u64, ring u32, slot u32, copies u64");

    rwin::MessageWriter outside{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{.window = 5, .ring = 1, .slot = 3, .copies = 1}.write(outside);
    check(!read_back<rwin::FrameInSlot>(outside.take()).has_value(), "a slot past the ring is refused");
    rwin::MessageWriter uncopied{rwin::MessageId::FrameInSlot};
    rwin::FrameInSlot{.window = 5, .ring = 1, .slot = 0, .copies = 0}.write(uncopied);
    check(!read_back<rwin::FrameInSlot>(uncopied.take()).has_value(), "copy 0 is no frame and is refused");
}

void a_freed_slot_crosses_whole() {
    const rwin::SlotFree freed{.window = 0xABCDULL, .ring = 77, .slot = 0};
    rwin::MessageWriter writer{rwin::MessageId::SlotFree};
    freed.write(writer);
    check(read_back<rwin::SlotFree>(writer.take()) == freed, "a freed slot reads back as written");

    rwin::MessageWriter pinned{rwin::MessageId::SlotFree};
    rwin::SlotFree{.window = 0x0100, .ring = 2, .slot = 2}.write(pinned);
    const std::vector<std::uint8_t> expected{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, 2};
    check(body_of(std::move(pinned)) == expected, "window u64, ring u32, slot u32");
    rwin::MessageWriter outside{rwin::MessageId::SlotFree};
    rwin::SlotFree{.window = 1, .ring = 1, .slot = 7}.write(outside);
    check(!read_back<rwin::SlotFree>(outside.take()).has_value(), "freeing a slot past the ring is refused");
}

int main() {
    a_frame_route_crosses_whole();
    a_slot_ring_crosses_whole();
    a_frame_in_a_slot_crosses_whole();
    a_freed_slot_crosses_whole();
    a_key_stroke_crosses_whole();
    a_short_key_stroke_is_refused();
    the_guest_desktop_crosses_whole();
    a_hello_crosses_whole();
    a_picture_rect_crosses_whole();
    an_owned_window_with_a_caption_stands_alone();
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
