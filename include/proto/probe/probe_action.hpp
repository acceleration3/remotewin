#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {

class MessageReader;
class MessageWriter;

struct ProbeAction {
    enum class Kind : std::uint8_t {
        Enable,
        Show,
        SetOwner,
        Foreground,
        SetPosition,
        Move,
        OpenModal,
        OpenModeless,
        EndDialog,
        ShowMessageBox,
        CloseMessageBox,
        Destroy,
        Click,
        ClassCursor,
        AnswerCursor,
        ShowCursor,
        ClipCursor,
        SetCursorPos,
        Capture,
        RawInput,
        Recentre,
        GuestMove,
        ActivateShell,
        PointerPrecision,
    };
    enum class MadeCursor : std::uint32_t { None = 1, Colour, Monochrome, Animated, Large };
    enum class ZOrder : std::uint8_t { Keep, Top, Bottom, TopMost, NoTopMost };

    Kind kind = Kind::Enable;
    std::uint64_t window = 0;
    std::uint64_t other = 0;
    bool on = false;
    std::int32_t show = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool centred = false;
    ZOrder z = ZOrder::Keep;
    std::uint32_t flags = 0;
    std::uint32_t cursor = 0;
    std::string title{};

    [[nodiscard]] bool operator==(const ProbeAction&) const = default;

    [[nodiscard]] bool makes_window() const noexcept {
        return kind == Kind::OpenModal || kind == Kind::OpenModeless || kind == Kind::ShowMessageBox;
    }

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ProbeAction> read(MessageReader& in);
};

}
