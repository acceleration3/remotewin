#include "host/chrome/non_client_area.hpp"

namespace rwin::host {

namespace {

// The HT* codes, named here because the host does not include windows.h.
constexpr std::int32_t kClient = 1;
constexpr std::int32_t kCaption = 2;
constexpr std::int32_t kGrowBox = 4;
constexpr std::int32_t kMinButton = 8;
constexpr std::int32_t kMaxButton = 9;
constexpr std::int32_t kLeft = 10;
constexpr std::int32_t kRight = 11;
constexpr std::int32_t kTop = 12;
constexpr std::int32_t kTopLeft = 13;
constexpr std::int32_t kTopRight = 14;
constexpr std::int32_t kBottom = 15;
constexpr std::int32_t kBottomLeft = 16;
constexpr std::int32_t kBottomRight = 17;
constexpr std::int32_t kClose = 20;

}  // namespace

bool NonClientArea::is_client() const noexcept { return code_ == kClient; }
bool NonClientArea::is_caption() const noexcept { return code_ == kCaption; }
bool NonClientArea::is_close() const noexcept { return code_ == kClose; }
bool NonClientArea::is_minimize() const noexcept { return code_ == kMinButton; }
bool NonClientArea::is_maximize() const noexcept { return code_ == kMaxButton; }

Qt::Edges NonClientArea::resize_edges() const noexcept {
    switch (code_) {
        case kLeft:        return Qt::LeftEdge;
        case kRight:       return Qt::RightEdge;
        case kTop:         return Qt::TopEdge;
        case kBottom:      return Qt::BottomEdge;
        case kTopLeft:     return Qt::TopEdge | Qt::LeftEdge;
        case kTopRight:    return Qt::TopEdge | Qt::RightEdge;
        case kBottomLeft:  return Qt::BottomEdge | Qt::LeftEdge;
        case kGrowBox:
        case kBottomRight: return Qt::BottomEdge | Qt::RightEdge;
        default:           return {};
    }
}

bool NonClientArea::is_host_action() const noexcept {
    return is_caption() || is_close() || is_minimize() || is_maximize()
           || resize_edges() != Qt::Edges{};
}

}  // namespace rwin::host
