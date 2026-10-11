#include "host/export/copy_wait.hpp"
#include "host/export/export_answer.hpp"
#include "host/export/export_request.hpp"
#include "host/export/pci_address.hpp"
#include "host/export/surface_memory.hpp"
#include "host/export/window_ring.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::ExportAnswer;
using rwin::host::ExportRequest;

template <typename Number>
void put(std::vector<std::uint8_t>& bytes, std::size_t at, Number value) {
    for (std::size_t byte = 0; byte < sizeof(Number); ++byte) {
        bytes[at + byte] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * byte));
    }
}

// An answer as the device lays one out (accel-virt's ExportAnswer::encode).
std::vector<std::uint8_t> answer(std::uint32_t operation, std::uint32_t status, std::uint64_t surface,
                                 std::uint64_t tag) {
    std::vector<std::uint8_t> bytes(ExportAnswer::kBytes, 0);
    put(bytes, 0, ExportAnswer::kMagic);
    put(bytes, 4, std::uint16_t{1});
    put(bytes, 6, std::uint16_t{0});
    put(bytes, 8, operation);
    put(bytes, 12, status);
    put(bytes, 16, surface);
    put(bytes, 24, tag);
    return bytes;
}

// Pinned byte for byte: the other end is accel-virt's, built apart.
void a_request_is_laid_out_as_the_device_reads_it() {
    const std::array<std::uint8_t, 32> bytes =
        ExportRequest{ExportRequest::Operation::Sync, 0x0102'0304'0506'0708ULL, 0x1122'3344'5566'7788ULL}.encode();
    const std::array<std::uint8_t, 32> expected{0x41, 0x56, 0x58, 0x51, 1,    0,    0,    0,    3,    0,    0,
                                                0,    0,    0,    0,    0,    0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
                                                0x02, 0x01, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    check(bytes == expected, "magic AVXQ, major 1, minor 0, operation, reserved, surface, tag; little-endian");
    check(ExportRequest{}.encode()[8] == 1, "a request is hello unless it says otherwise");
}

void a_hello_names_the_gpu() {
    std::vector<std::uint8_t> bytes = answer(1, 0, 0, 7);
    for (std::size_t byte = 0; byte < 16; ++byte) bytes[32 + byte] = static_cast<std::uint8_t>(0xA0 + byte);
    put(bytes, 48, std::uint32_t{0});
    bytes[52] = 0x03;
    bytes[53] = 0x00;
    bytes[54] = 0x00;
    bytes[55] = 1;
    const std::optional<ExportAnswer> hello = ExportAnswer::decode(bytes);
    check(hello.has_value() && hello->ok() && hello->tag == 7, "a hello decodes");
    const rwin::host::ExportGpu gpu = hello->gpu();
    check(gpu.uuid[0] == 0xA0 && gpu.uuid[15] == 0xAF, "its UUID is the 16 bytes at 32");
    check(gpu.pci == rwin::host::PciAddress{0, 3, 0, 0}, "its PCI address is at 48, on PCI by bit 0 at 55");
    bytes[55] = 0;
    check(!ExportAnswer::decode(bytes)->gpu().pci.has_value(), "without the flag it is on no PCI bus");
}

void an_export_says_how_the_picture_lies() {
    std::vector<std::uint8_t> bytes = answer(2, 0, 0x5100'0000'0000'0077ULL, 9);
    put(bytes, 32, std::uint32_t{1003});
    put(bytes, 36, std::uint32_t{701});
    put(bytes, 40, rwin::host::ExportedLayout::kArgb8888);
    put(bytes, 44, std::uint32_t{4096});
    put(bytes, 48, std::uint32_t{0});
    put(bytes, 56, rwin::host::ExportedLayout::kImplicitModifier);
    const std::optional<ExportAnswer> exported = ExportAnswer::decode(bytes);
    check(exported.has_value() && exported->surface == 0x5100'0000'0000'0077ULL, "an export decodes");
    check(exported->layout()
              == rwin::host::ExportedLayout{1003, 701, rwin::host::ExportedLayout::kArgb8888, 4096, 0,
                                            rwin::host::ExportedLayout::kImplicitModifier},
          "width, height, fourcc, stride, offset at 32..52, the modifier at 56");
}

void a_sync_counts_copies_and_a_revocation_says_why() {
    std::vector<std::uint8_t> sync = answer(3, 0, 5, 10);
    put(sync, 32, std::uint64_t{0x1'0000'0002ULL});
    check(ExportAnswer::decode(sync)->copies() == 0x1'0000'0002ULL, "a sync's copies are the u64 at 32");
    std::vector<std::uint8_t> revoked = answer(ExportAnswer::kRevoked, 0, 5, 0);
    put(revoked, 32, std::uint32_t{1});
    const std::optional<ExportAnswer> event = ExportAnswer::decode(revoked);
    check(event.has_value() && event->operation == ExportAnswer::kRevoked && event->reason() == 1,
          "a revocation is operation 0x80 with its reason at 32");
    const std::optional<ExportAnswer> refused = ExportAnswer::decode(answer(2, 3, 5, 11));
    check(refused.has_value() && refused->status == ExportAnswer::Status::NotFound, "a refusal keeps its status");
}

void what_is_no_answer_is_refused() {
    std::vector<std::uint8_t> bytes = answer(1, 0, 0, 1);
    check(!ExportAnswer::decode(std::span<const std::uint8_t>(bytes.data(), 63)).has_value(), "too short");
    bytes.push_back(0);
    check(!ExportAnswer::decode(bytes).has_value(), "too long");
    bytes.pop_back();
    put(bytes, 4, std::uint16_t{2});
    check(!ExportAnswer::decode(bytes).has_value(), "another major version");
    put(bytes, 4, std::uint16_t{1});
    put(bytes, 0, ExportRequest::kMagic);
    check(!ExportAnswer::decode(bytes).has_value(), "a request's magic");
    check(!ExportAnswer::decode(answer(1, 6, 0, 1)).has_value(), "a status version 1 does not have");
    std::vector<std::uint8_t> minor = answer(1, 0, 0, 1);
    put(minor, 6, std::uint16_t{4});
    check(ExportAnswer::decode(minor).has_value(), "any minor of major 1 reads as this layout");
}

void pci_addresses_read_as_sysfs_writes_them() {
    check(rwin::host::PciAddress::parse("0000:03:00.0") == rwin::host::PciAddress{0, 3, 0, 0}, "a dGPU");
    check(rwin::host::PciAddress::parse("0001:7b:1f.7") == rwin::host::PciAddress{1, 0x7B, 0x1F, 7},
          "every field, in hex");
    check(!rwin::host::PciAddress::parse("0000:03:00").has_value(), "no function");
    check(!rwin::host::PciAddress::parse("0000:03:20.0").has_value(), "a device past 0x1f");
    check(!rwin::host::PciAddress::parse("card1").has_value(), "a name that is no address");
    check(rwin::host::PciAddress{0, 0x7B, 0, 0}.to_string() == "0000:7b:00.0", "written as sysfs writes it");
    check(rwin::host::PciAddress{1, 0x7B, 0x1F, 7}.prime_name() == "pci-0001_7b_1f_7",
          "and as Mesa's DRI_PRIME names a GPU");
}

// Frames become ready out of the order they were named; an older one shown
// after a newer would put the window back a frame.
void a_ring_shows_only_newer_frames() {
    rwin::host::WindowRing ring{rwin::SlotRing{.window = 1, .ring = 4, .width = 8, .height = 8, .surfaces = {10, 11, 12}}};
    check(ring.slot_of(11) == 1u && !ring.slot_of(13).has_value(), "a surface is found by its id");
    const std::uint64_t first = ring.named();
    const std::uint64_t second = ring.named();
    check(ring.show(second), "the newer frame is shown");
    check(!ring.show(first), "the older one, ready after it, is not");
    check(ring.show(ring.named()), "the next one is");
    check(ring.memory(0) == nullptr, "a slot has no memory until exported");
    ring.exported(0, std::make_shared<const rwin::host::SurfaceMemory>(10, rwin::host::ExportedLayout{},
                                                                       rwin::host::UniqueFd{}));
    check(ring.memory(0) != nullptr, "and has it once exported");
    ring.revoke(0);
    check(ring.memory(0) == nullptr, "and not once the guest let go of the surface");
    check(!ring.revoked(), "a ring with surfaces still held is not let go of");
    ring.revoke(1);
    ring.revoke(2);
    check(ring.revoked(), "one whose every surface the guest let go of is");
}

void a_copy_is_waited_for_until_it_ran() {
    rwin::host::CopyWait wait{3};
    check(wait.answered(2) == rwin::host::CopyWait::Verdict::AskAgain, "two copies run is not the third");
    check(wait.answered(3) == rwin::host::CopyWait::Verdict::Ready, "three is");
    check(rwin::host::CopyWait{3}.answered(9) == rwin::host::CopyWait::Verdict::Ready,
          "and so is any count past it: later copies ran after it");
    rwin::host::CopyWait never{1};
    rwin::host::CopyWait::Verdict verdict = rwin::host::CopyWait::Verdict::AskAgain;
    int asks = 0;
    while (verdict == rwin::host::CopyWait::Verdict::AskAgain) {
        verdict = never.answered(0);
        ++asks;
    }
    check(verdict == rwin::host::CopyWait::Verdict::GiveUp && asks == rwin::host::CopyWait::kMostAsks,
          "a copy that never runs is given up on after the most asks");
}

}  // namespace

int main() {
    a_request_is_laid_out_as_the_device_reads_it();
    a_hello_names_the_gpu();
    an_export_says_how_the_picture_lies();
    a_sync_counts_copies_and_a_revocation_says_why();
    what_is_no_answer_is_refused();
    pci_addresses_read_as_sysfs_writes_them();
    a_ring_shows_only_newer_frames();
    a_copy_is_waited_for_until_it_ran();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("export wire: all checks passed\n");
    return EXIT_SUCCESS;
}
