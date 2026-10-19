#include "host/input/pointer_grabber.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"
#include "host/wayland/confined_pointer.hpp"
#include "host/wayland/locked_pointer.hpp"
#include "host/wayland/pointer_globals.hpp"
#include "host/wayland/relative_pointer.hpp"
#include "logging/logger.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QWindow>

#include <cmath>

namespace rwin::host {

namespace {

const char* kind_name(PointerGrab::Kind kind) {
    switch (kind) {
        case PointerGrab::Kind::Confine:
            return "confine";
        case PointerGrab::Kind::Lock:
            return "lock";
        case PointerGrab::Kind::None:
            break;
    }
    return "none";
}

}  // namespace

PointerGrabber::PointerGrabber(GuestLink& link, const PointerGlobals& globals, QObject* parent)
    : QObject(parent), link_(link), globals_(globals) {}

PointerGrabber::~PointerGrabber() = default;

bool PointerGrabber::wanted() const noexcept {
    return grab_.kind != PointerGrab::Kind::None && !mirror_.isNull();
}

bool PointerGrabber::locking() const noexcept { return lock_ != nullptr && !escaped_; }

bool PointerGrabber::held() const noexcept { return locking() && lock_->engaged(); }

void PointerGrabber::motion_while_held(QPointF unaccelerated) {
    if (held()) take_motion(unaccelerated);
}

void PointerGrabber::follow(const PointerGrab& grab, MirrorWindow* mirror, QPoint guest_origin) {
    if (grab == grab_ && mirror == mirror_.data() && guest_origin == origin_) return;
    RWIN_INFO("pointer", "guest wants {} for {}", kind_name(grab.kind), grab.window);
    const bool new_pattern = grab.kind != grab_.kind || grab.window != grab_.window;
    let_go();
    grab_ = grab;
    origin_ = guest_origin;
    if (new_pattern) escaped_ = false;
    watch(mirror);
    if (wanted() && !escaped_) hold();
}

void PointerGrabber::watch(MirrorWindow* mirror) {
    if (mirror_.data() == mirror) return;
    if (!mirror_.isNull()) mirror_->removeEventFilter(this);
    mirror_ = mirror;
    if (mirror != nullptr) mirror->installEventFilter(this);
}

// The part of the guest's clip the mirror shows, rounded inward to whole
// logical units: a pointer kept inside it is over guest pixels inside the
// clip.
QRegion PointerGrabber::confine_region() const {
    const QRect picture(origin_, mirror_->guest_picture_size());
    const QRect clip = QRect(grab_.area.left, grab_.area.top, grab_.area.width(), grab_.area.height()) & picture;
    if (clip.isEmpty()) return {};
    const QRect inside = mirror_->logical_rect_inside(clip.translated(-origin_));
    return inside.isEmpty() ? QRegion() : QRegion(inside);
}

std::optional<QPointF> PointerGrabber::lock_hint() const {
    const QPoint at = QPoint(grab_.x, grab_.y) - origin_;
    if (!QRect(QPoint(0, 0), mirror_->guest_picture_size()).contains(at)) return std::nullopt;
    return mirror_->logical_point_of(at);
}

void PointerGrabber::hold() {
    wl_surface* surface = PointerGlobals::surface_of(mirror_->windowHandle());
    wl_pointer* pointer = PointerGlobals::pointer();
    unavailable_ = why_unavailable(surface, pointer);
    if (!unavailable_.isEmpty()) {
        RWIN_INFO("pointer", "cannot {} the pointer on {}: {}", kind_name(grab_.kind), grab_.window,
                  unavailable_.toStdString());
        return;
    }
    ++requests_;
    lost_focus_ = false;
    if (grab_.kind == PointerGrab::Kind::Confine) {
        confinement_ = std::make_unique<ConfinedPointer>(globals_.constraints(), surface, pointer, asked_region_);
    } else {
        lock_ = std::make_unique<LockedPointer>(globals_.constraints(), surface, pointer, asked_hint_);
        if (relative_ == nullptr) {
            relative_ = std::make_unique<RelativePointer>(globals_.relative(), pointer);
            connect(relative_.get(), &RelativePointer::moved, this, &PointerGrabber::motion_while_held);
        }
    }
    const QRect box = asked_region_.boundingRect();
    RWIN_INFO("pointer", "asked the compositor to {} the pointer on {} ({},{} {}x{})", kind_name(grab_.kind),
              grab_.window, box.x(), box.y(), box.width(), box.height());
}

// Also settles what would be asked. A lock without the relative pointer
// would hold the pointer still and pass on nothing - a camera that cannot
// turn - where positions at least move it until the pointer reaches the
// edge of the desktop. An empty region is the whole surface to the
// compositor, which is not the clip.
QString PointerGrabber::why_unavailable(wl_surface* surface, wl_pointer* pointer) {
    asked_region_ = QRegion();
    asked_hint_.reset();
    if (surface == nullptr || pointer == nullptr) return QStringLiteral("no Wayland surface or pointer");
    if (globals_.constraints() == nullptr) return QStringLiteral("the compositor has no pointer constraints");
    if (grab_.kind == PointerGrab::Kind::Lock) {
        if (globals_.relative() == nullptr) return QStringLiteral("the compositor has no relative pointer");
        asked_hint_ = lock_hint();
        return {};
    }
    asked_region_ = confine_region();
    if (asked_region_.isEmpty()) return QStringLiteral("the clip covers no whole unit of the mirror");
    return {};
}

void PointerGrabber::let_go() {
    if (lock_ == nullptr && confinement_ == nullptr) return;
    RWIN_INFO("pointer", "let go of the pointer on {}", grab_.window);
    lock_.reset();
    confinement_.reset();
    owed_ = QPointF();
}

// The compositor ended a hold of its own accord while the mirror still has
// the focus: its own unlock, which counts as the user's escape.
void PointerGrabber::notice_compositor_let_go() {
    const bool over = (lock_ != nullptr && lock_->over()) || (confinement_ != nullptr && confinement_->over());
    if (!over) return;
    let_go();
    if (!mirror_.isNull() && mirror_->isActiveWindow()) escaped_ = true;
}

void PointerGrabber::take_motion(QPointF unaccelerated) {
    notice_compositor_let_go();
    if (!locking()) return;
    owed_ += unaccelerated;
    const int dx = static_cast<int>(std::trunc(owed_.x()));
    const int dy = static_cast<int>(std::trunc(owed_.y()));
    if (dx == 0 && dy == 0) return;
    owed_ -= QPointF(dx, dy);
    ++motions_;
    moved_ += QPoint(dx, dy);
    link_.send_pointer_delta(grab_.window, dx, dy);
}

bool PointerGrabber::escape_pressed(const QEvent* event) const {
    const auto* key = static_cast<const QKeyEvent*>(event);
    const Qt::KeyboardModifiers chord = Qt::ControlModifier | Qt::AltModifier;
    return key->key() == Qt::Key_G && (key->modifiers() & chord) == chord;
}

bool PointerGrabber::eventFilter(QObject* watched, QEvent* event) {
    if (watched != mirror_.data()) return QObject::eventFilter(watched, event);
    notice_compositor_let_go();
    switch (event->type()) {
        case QEvent::WindowDeactivate:
            lost_focus_ = lock_ != nullptr || confinement_ != nullptr;
            let_go();
            break;
        case QEvent::WindowActivate:
            if (wanted() && !escaped_ && lock_ == nullptr && confinement_ == nullptr) hold();
            break;
        case QEvent::WinIdChange:
            let_go();
            if (wanted() && !escaped_ && mirror_->windowHandle() != nullptr) hold();
            break;
        case QEvent::KeyPress:
            if (!wanted() || !escape_pressed(event)) break;
            RWIN_INFO("pointer", "escape pressed: the pointer is let go until the mirror is clicked");
            escaped_ = true;
            swallow_escape_release_ = true;
            let_go();
            return true;
        case QEvent::KeyRelease:
            if (!swallow_escape_release_ || static_cast<QKeyEvent*>(event)->key() != Qt::Key_G) break;
            swallow_escape_release_ = false;
            return true;
        case QEvent::MouseButtonPress:
            if (!escaped_ || !wanted()) break;
            escaped_ = false;
            hold();
            return true;
        case QEvent::MouseMove:
            return held();
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

void PointerGrabber::describe(WindowId window, MirrorState& state) const {
    if (window != grab_.window || grab_.kind == PointerGrab::Kind::None) return;
    MirrorState::PointerHold hold;
    hold.wanted = grab_.kind;
    hold.asked = lock_ != nullptr ? PointerGrab::Kind::Lock
                 : confinement_ != nullptr ? PointerGrab::Kind::Confine
                                           : PointerGrab::Kind::None;
    hold.region = asked_region_;
    hold.hint = asked_hint_;
    hold.engaged = (lock_ != nullptr && lock_->engaged()) || (confinement_ != nullptr && confinement_->engaged());
    hold.escaped = escaped_;
    hold.lost_focus = lost_focus_;
    hold.requests = requests_;
    hold.motions = motions_;
    hold.moved = moved_;
    hold.unavailable = unavailable_;
    state.pointer_hold = hold;
}

}  // namespace rwin::host
