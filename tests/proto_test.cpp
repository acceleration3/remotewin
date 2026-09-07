#include "proto/frame_decoder.hpp"
#include "proto/message_id.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include <cstdio>
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

}  // namespace

int main() {
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
