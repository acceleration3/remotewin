#pragma once

#include "host/export/export_answer.hpp"
#include "host/export/unique_fd.hpp"

#include <optional>
#include <string>

namespace rwin::host {

struct ExportRequest;

class ExportSocket {
public:
    struct Received {
        ExportAnswer answer;
        UniqueFd descriptor;
    };

    [[nodiscard]] static std::optional<ExportSocket> connect(const std::string& path, std::string& why);

    [[nodiscard]] int descriptor() const noexcept { return socket_.get(); }

    [[nodiscard]] bool send(const ExportRequest& request) const;

    [[nodiscard]] std::optional<Received> receive(bool& gone) const;

    [[nodiscard]] bool wait(int milliseconds) const;

private:
    explicit ExportSocket(UniqueFd socket) noexcept : socket_(std::move(socket)) {}

    UniqueFd socket_;
};

}
