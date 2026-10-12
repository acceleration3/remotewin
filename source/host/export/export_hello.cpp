#include "host/export/export_hello.hpp"

#include "host/export/export_answer.hpp"
#include "host/export/export_request.hpp"

#include <string>
#include <utility>

namespace rwin::host {

namespace {

// How long the device has to answer hello: it answers at once, from a
// thread of its own.
constexpr int kHelloMs = 2000;

}  // namespace

std::optional<ExportHello> ExportHello::greet(const std::string& path, std::uint64_t tag, std::string& why) {
    std::optional<ExportSocket> socket = ExportSocket::connect(path, why);
    if (!socket.has_value()) return std::nullopt;
    bool gone = false;
    std::optional<ExportSocket::Received> answer;
    if (socket->send(ExportRequest{ExportRequest::Operation::Hello, 0, tag}) && socket->wait(kHelloMs)) {
        answer = socket->receive(gone);
    }
    if (!answer.has_value() || answer->answer.operation != static_cast<std::uint32_t>(ExportRequest::Operation::Hello)
        || answer->answer.tag != tag) {
        why = "the GPU device at '" + path + "' did not answer hello";
        return std::nullopt;
    }
    if (!answer->answer.ok()) {
        why = "the GPU device at '" + path + "' exports nothing (" + to_string(answer->answer.status) + ")";
        return std::nullopt;
    }
    // A device of an older minor takes what this side asks of it as
    // malformed, and answers it untagged: frames would go over to pixels then,
    // in the middle of the session, with nothing to say why.
    if (answer->answer.minor < ExportRequest::kMinor) {
        why = "the GPU device at '" + path + "' reads export requests up to 1." + std::to_string(answer->answer.minor)
              + ", this host half sends 1." + std::to_string(ExportRequest::kMinor)
              + ": the VM's QEMU is from an older build; restart the VM";
        return std::nullopt;
    }
    return ExportHello{std::move(*socket), answer->answer.gpu()};
}

}  // namespace rwin::host
