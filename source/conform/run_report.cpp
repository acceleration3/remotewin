#include "conform/run_report.hpp"

#include "conform/scenario_outcome.hpp"
#include "proto/system_cursor.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace rwin::conform {

namespace {

QString hex(std::uint64_t value) { return QStringLiteral("0x%1").arg(value, 0, 16); }

QJsonArray rect_json(int left, int top, int width, int height) {
    return QJsonArray{left, top, width, height};
}

QJsonArray rect_json(const WindowTruth::Rect& rect) {
    return rect_json(rect.left, rect.top, rect.width(), rect.height());
}

QJsonArray rect_json(const QRect& rect) {
    return rect_json(rect.x(), rect.y(), rect.width(), rect.height());
}

QJsonObject truth_json(const WindowTruth& truth) {
    return QJsonObject{
        {QStringLiteral("handle"), hex(truth.handle)},
        {QStringLiteral("owner"), hex(truth.owner)},
        {QStringLiteral("style"), hex(truth.style)},
        {QStringLiteral("ex_style"), hex(truth.extended_style)},
        {QStringLiteral("class_style"), hex(truth.class_style)},
        {QStringLiteral("title"), QString::fromStdString(truth.title)},
        {QStringLiteral("window_rect"), rect_json(truth.window)},
        {QStringLiteral("visible_bounds"), rect_json(truth.visible)},
        {QStringLiteral("client_rect"), rect_json(truth.client)},
        {QStringLiteral("shown"), truth.shown},
        {QStringLiteral("iconic"), truth.iconic},
        {QStringLiteral("zoomed"), truth.zoomed},
        {QStringLiteral("enabled"), truth.enabled},
        {QStringLiteral("foreground"), truth.foreground},
        {QStringLiteral("dwm_frame"), truth.dwm_frame},
        {QStringLiteral("exists"), truth.exists},
        {QStringLiteral("class"), QString::fromStdString(truth.class_name)},
        {QStringLiteral("active"), truth.active},
        {QStringLiteral("focused"), truth.focused},
        {QStringLiteral("owner_disabled"), truth.owner_disabled},
        {QStringLiteral("enabled_popup"), hex(truth.enabled_popup)},
        {QStringLiteral("z_rank"), truth.z_rank},
        {QStringLiteral("dpi"), static_cast<int>(truth.dpi)},
        {QStringLiteral("layered_flags"), static_cast<int>(truth.layered_flags)},
        {QStringLiteral("layered_alpha"), truth.layered_alpha},
        {QStringLiteral("color_key"), hex(truth.color_key)},
        {QStringLiteral("region"), static_cast<int>(truth.region)},
        {QStringLiteral("region_rects"), static_cast<int>(truth.region_rects)},
        {QStringLiteral("region_box"), rect_json(truth.region_box)},
    };
}

QString kind_name(PointerGrab::Kind kind) {
    switch (kind) {
        case PointerGrab::Kind::Confine:
            return QStringLiteral("confine");
        case PointerGrab::Kind::Lock:
            return QStringLiteral("lock");
        case PointerGrab::Kind::None:
            break;
    }
    return QStringLiteral("none");
}

QJsonValue hold_json(const std::optional<host::MirrorState::PointerHold>& hold) {
    if (!hold.has_value()) return QJsonValue();
    return QJsonObject{{QStringLiteral("wanted"), kind_name(hold->wanted)},
                       {QStringLiteral("asked"), kind_name(hold->asked)},
                       {QStringLiteral("region"), rect_json(hold->region.boundingRect())},
                       {QStringLiteral("hint"), hold->hint.has_value() ? QJsonValue(QJsonArray{hold->hint->x(), hold->hint->y()})
                                                                       : QJsonValue()},
                       {QStringLiteral("engaged"), hold->engaged},
                       {QStringLiteral("escaped"), hold->escaped},
                       {QStringLiteral("requests"), static_cast<int>(hold->requests)},
                       {QStringLiteral("motions"), static_cast<int>(hold->motions)},
                       {QStringLiteral("moved"), QJsonArray{hold->moved.x(), hold->moved.y()}},
                       {QStringLiteral("unavailable"), hold->unavailable}};
}

QJsonObject cursor_truth_json(const CursorTruth& truth) {
    QJsonArray frames;
    for (const CursorTruth::Frame& frame : truth.frames) {
        frames.push_back(QJsonObject{{QStringLiteral("size"), QJsonArray{static_cast<int>(frame.width), static_cast<int>(frame.height)}},
                                     {QStringLiteral("hotspot"), QJsonArray{static_cast<int>(frame.hotspot_x), static_cast<int>(frame.hotspot_y)}},
                                     {QStringLiteral("delay_ms"), static_cast<int>(frame.delay_ms)}});
    }
    return QJsonObject{{QStringLiteral("flags"), static_cast<int>(truth.flags)},
                       {QStringLiteral("shown"), truth.shown()},
                       {QStringLiteral("handle"), hex(truth.handle)},
                       {QStringLiteral("at"), QJsonArray{truth.x, truth.y}},
                       {QStringLiteral("under"), hex(truth.under)},
                       {QStringLiteral("system_idc"), static_cast<int>(truth.system_idc)},
                       {QStringLiteral("display_count"), truth.display_count},
                       {QStringLiteral("clipped"), truth.clipped},
                       {QStringLiteral("clip"), rect_json(truth.clip)},
                       {QStringLiteral("capture"), hex(truth.capture)},
                       {QStringLiteral("raw_mouse"), truth.raw_mouse},
                       {QStringLiteral("raw_flags"), hex(truth.raw_flags)},
                       {QStringLiteral("raw_target"), hex(truth.raw_target)},
                       {QStringLiteral("recentring"), hex(truth.recentring)},
                       {QStringLiteral("mouse_speed"), truth.mouse_speed},
                       {QStringLiteral("enhance_precision"), truth.enhance_precision},
                       {QStringLiteral("cursor_metric"), QJsonArray{static_cast<int>(truth.metric_width), static_cast<int>(truth.metric_height)}},
                       {QStringLiteral("frames"), frames}};
}

QJsonArray checks_json(const std::vector<Expectation::Check>& checks) {
    QJsonArray json;
    for (const Expectation::Check& check : checks) {
        json.push_back(QJsonObject{{QStringLiteral("property"), check.property},
                                   {QStringLiteral("rule"), check.rule},
                                   {QStringLiteral("expected"), check.expected},
                                   {QStringLiteral("actual"), check.actual},
                                   {QStringLiteral("passed"), check.passed},
                                   {QStringLiteral("known_limit"), check.known_limit}});
    }
    return json;
}

QJsonValue pointer_json(const std::optional<ScenarioOutcome::Pointer>& pointer) {
    if (!pointer.has_value()) return QJsonValue();
    QJsonObject json{{QStringLiteral("window"), pointer->window},
                     {QStringLiteral("wanted"), kind_name(pointer->wanted)},
                     {QStringLiteral("checks"), checks_json(pointer->checks)}};
    if (pointer->truth.has_value()) json.insert(QStringLiteral("truth"), cursor_truth_json(*pointer->truth));
    return json;
}

QJsonObject mirror_json(const host::MirrorState& mirror) {
    return QJsonObject{
        {QStringLiteral("popup_of"), hex(mirror.popup_of)},
        {QStringLiteral("attached_to"), hex(mirror.attached_to)},
        {QStringLiteral("modal"), mirror.modal},
        {QStringLiteral("activation_requests"), static_cast<int>(mirror.activation_requests)},
        {QStringLiteral("alerts"), static_cast<int>(mirror.alerts)},
        {QStringLiteral("window_flags"), hex(static_cast<std::uint64_t>(mirror.flags.toInt()))},
        {QStringLiteral("shows_without_activating"), mirror.shows_without_activating},
        {QStringLiteral("visible"), mirror.visible},
        {QStringLiteral("minimized"), mirror.minimized},
        {QStringLiteral("maximized"), mirror.maximized},
        {QStringLiteral("active"), mirror.active},
        {QStringLiteral("forwards_input"), mirror.forwards_input},
        {QStringLiteral("resizable"), mirror.resizable},
        {QStringLiteral("title"), mirror.title},
        {QStringLiteral("position"), QJsonArray{mirror.position.x(), mirror.position.y()}},
        {QStringLiteral("placed"), QJsonArray{mirror.placed.x(), mirror.placed.y()}},
        {QStringLiteral("logical_size"), QJsonArray{mirror.logical_size.width(), mirror.logical_size.height()}},
        {QStringLiteral("device_pixel_ratio"), mirror.device_pixel_ratio},
        {QStringLiteral("fit"), mirror.fit},
        {QStringLiteral("guest_size"), QJsonArray{mirror.guest_size.width(), mirror.guest_size.height()}},
        {QStringLiteral("picture"), rect_json(mirror.picture)},
        {QStringLiteral("opacity"), mirror.opacity},
        {QStringLiteral("color_keyed"), mirror.color_keyed},
        {QStringLiteral("color_key"), hex(mirror.color_key)},
        {QStringLiteral("mask_bounds"), rect_json(mirror.mask.boundingRect())},
        {QStringLiteral("mask_rects"), static_cast<int>(mirror.mask.rectCount())},
        {QStringLiteral("cursor_shape"), static_cast<int>(mirror.cursor_shape)},
        {QStringLiteral("cursor_id"), hex(mirror.cursor_id)},
        {QStringLiteral("cursor_pixels"), QJsonArray{mirror.cursor_pixels.width(), mirror.cursor_pixels.height()}},
        {QStringLiteral("cursor_pixmap_ratio"), mirror.cursor_pixmap_ratio},
        {QStringLiteral("cursor_hotspot"), QJsonArray{mirror.cursor_hotspot.x(), mirror.cursor_hotspot.y()}},
        {QStringLiteral("cursor_surface_size"), mirror.cursor_surface_size.has_value()
                                                    ? QJsonValue(QJsonArray{mirror.cursor_surface_size->width(),
                                                                            mirror.cursor_surface_size->height()})
                                                    : QJsonValue()},
        {QStringLiteral("cursor_steps"), static_cast<int>(mirror.cursor_delays_ms.size())},
        {QStringLiteral("cursor_steps_taken"), static_cast<qint64>(mirror.cursor_steps_taken)},
        {QStringLiteral("pointer_hold"), hold_json(mirror.pointer_hold)},
        {QStringLiteral("frames_presented"), static_cast<qint64>(mirror.frames_presented)},
        {QStringLiteral("placement_asked"), mirror.placement_asked.has_value()
                                                ? QJsonValue(QJsonArray{mirror.placement_asked->x(), mirror.placement_asked->y()})
                                                : QJsonValue()},
        {QStringLiteral("placement_requests"), static_cast<int>(mirror.placement_requests)},
        {QStringLiteral("placement_round_trip_ms"), static_cast<qint64>(mirror.placement_round_trip_ms)},
        {QStringLiteral("compositor_frame"), mirror.compositor_frame.has_value()
                                                 ? QJsonValue(QJsonArray{mirror.compositor_frame->x(), mirror.compositor_frame->y(),
                                                                         mirror.compositor_frame->width(), mirror.compositor_frame->height()})
                                                 : QJsonValue()},
        {QStringLiteral("guest_moves"), static_cast<int>(mirror.guest_moves)},
        {QStringLiteral("guest_moved_to"), mirror.guest_moved_to.has_value()
                                               ? QJsonValue(QJsonArray{mirror.guest_moved_to->x(), mirror.guest_moved_to->y()})
                                               : QJsonValue()},
    };
}

QJsonObject reported_json(const host::GuestWindowInfo& window) {
    return QJsonObject{
        {QStringLiteral("owner"), hex(window.owner)},
        {QStringLiteral("rect"), rect_json(window.x, window.y, static_cast<int>(window.width),
                                           static_cast<int>(window.height))},
        {QStringLiteral("style"), hex(window.style)},
        {QStringLiteral("ex_style"), hex(window.extended_style)},
        {QStringLiteral("class_style"), hex(window.class_style)},
        {QStringLiteral("minimized"), window.minimized},
        {QStringLiteral("maximized"), window.maximized},
        {QStringLiteral("dwm_frame"), window.dwm_frame},
        {QStringLiteral("foreground"), window.foreground},
        {QStringLiteral("layered_flags"), static_cast<int>(window.layered_flags)},
        {QStringLiteral("layered_alpha"), window.layered_alpha},
        {QStringLiteral("band"), static_cast<int>(window.band)},
        {QStringLiteral("region_rects"), static_cast<int>(window.region.size())},
    };
}

QJsonObject pixels_json(const PixelComparison& pixels) {
    return QJsonObject{
        {QStringLiteral("compared"), pixels.compared()},
        {QStringLiteral("mismatched"), pixels.mismatched()},
        {QStringLiteral("max_delta"), pixels.max_delta()},
        {QStringLiteral("bounds"), rect_json(pixels.bounds())},
    };
}

QJsonObject window_json(const ScenarioOutcome::Window& window) {
    QJsonObject json{{QStringLiteral("index"), window.index},
                     {QStringLiteral("title"), window.title},
                     {QStringLiteral("handle"), hex(window.handle)},
                     {QStringLiteral("resampled"), window.resampled}};
    if (window.as_made.has_value()) json.insert(QStringLiteral("as_made"), truth_json(*window.as_made));
    if (window.truth.has_value()) json.insert(QStringLiteral("truth"), truth_json(*window.truth));
    if (window.mirror.has_value()) json.insert(QStringLiteral("mirror"), mirror_json(*window.mirror));
    if (window.reported.has_value()) json.insert(QStringLiteral("reported"), reported_json(*window.reported));
    if (window.casts_shadow.has_value()) json.insert(QStringLiteral("casts_shadow"), *window.casts_shadow);
    if (window.pixels.has_value()) json.insert(QStringLiteral("pixels"), pixels_json(*window.pixels));
    json.insert(QStringLiteral("checks"), checks_json(window.checks));
    return json;
}

QJsonArray input_json(const std::vector<ScenarioOutcome::Input>& given_input) {
    QJsonArray input;
    for (const ScenarioOutcome::Input& given : given_input) {
        input.push_back(QJsonObject{{QStringLiteral("step"), given.step},
                                    {QStringLiteral("expected"), given.check.expected},
                                    {QStringLiteral("actual"), given.check.actual},
                                    {QStringLiteral("passed"), given.check.passed}});
    }
    return input;
}

QJsonArray windows_json(const std::vector<ScenarioOutcome::Window>& windows) {
    QJsonArray json;
    for (const ScenarioOutcome::Window& window : windows) json.push_back(window_json(window));
    return json;
}

QJsonArray stages_json(const std::vector<ScenarioOutcome::Stage>& stages) {
    QJsonArray json;
    for (const ScenarioOutcome::Stage& stage : stages) {
        QJsonObject entry{{QStringLiteral("step"), stage.step},
                          {QStringLiteral("problems"), QJsonArray::fromStringList(stage.problems)},
                          {QStringLiteral("notes"), QJsonArray::fromStringList(stage.notes)},
                          {QStringLiteral("scene"), rect_json(stage.scene)},
                          {QStringLiteral("windows"), windows_json(stage.windows)},
                          {QStringLiteral("input"), input_json(stage.input)},
                          {QStringLiteral("pointer"), pointer_json(stage.pointer)}};
        if (stage.scene_pixels.has_value()) entry.insert(QStringLiteral("pixels"), pixels_json(*stage.scene_pixels));
        json.push_back(entry);
    }
    return json;
}

QJsonObject outcome_json(const ScenarioOutcome& outcome) {
    const QJsonArray windows = windows_json(outcome.windows);
    const QJsonArray input = input_json(outcome.input);
    QJsonObject json{{QStringLiteral("name"), outcome.name},
                     {QStringLiteral("about"), outcome.about},
                     {QStringLiteral("passed"), outcome.passed()},
                     {QStringLiteral("failures"), QJsonArray::fromStringList(outcome.failures())},
                     {QStringLiteral("known_limits"), QJsonArray::fromStringList(outcome.known_limits())},
                     {QStringLiteral("problems"), QJsonArray::fromStringList(outcome.problems)},
                     {QStringLiteral("scene"), rect_json(outcome.scene)},
                     {QStringLiteral("looks"), outcome.looks},
                     {QStringLiteral("skipped"), outcome.skipped},
                     {QStringLiteral("windows"), windows},
                     {QStringLiteral("input"), input},
                     {QStringLiteral("stages"), stages_json(outcome.stages)},
                     {QStringLiteral("pointer"), pointer_json(outcome.pointer)}};
    if (outcome.scene_pixels.has_value()) {
        json.insert(QStringLiteral("pixels"), pixels_json(*outcome.scene_pixels));
    }
    return json;
}

QString escaped(const QString& text) { return text.toHtmlEscaped(); }

QString verdict(bool passed) {
    return passed ? QStringLiteral("<span class=\"pass\">pass</span>")
                  : QStringLiteral("<span class=\"fail\">fail</span>");
}

QString verdict(const Expectation::Check& check) {
    if (check.passed || check.known_limit.isEmpty()) return verdict(check.passed);
    return QStringLiteral("<span class=\"limit\">known limit</span>");
}

QString rows_html(const std::vector<Expectation::Check>& checks) {
    QString html;
    for (const Expectation::Check& check : checks) {
        const QString rule = check.known_limit.isEmpty()
                                 ? escaped(check.rule)
                                 : escaped(check.rule) + QStringLiteral("<br><b>known limit:</b> ") + escaped(check.known_limit);
        html += QStringLiteral("<tr class=\"%1\"><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td class=\"rule\">%6</td></tr>")
                    .arg(check.passed ? QStringLiteral("ok") : check.known_limit.isEmpty() ? QStringLiteral("bad") : QStringLiteral("limit"),
                         escaped(check.property), escaped(check.expected), escaped(check.actual), verdict(check),
                         rule);
    }
    return html;
}

constexpr const char* kChecksHead = "<table class=\"checks\"><tr><th>property</th><th>expected</th>"
                                    "<th>mirror</th><th></th><th>rule</th></tr>";

QString checks_html(const ScenarioOutcome::Window& window) {
    QString html = QString::fromLatin1(kChecksHead) + rows_html(window.checks);
    if (window.pixels.has_value()) {
        const PixelComparison& pixels = *window.pixels;
        html += QStringLiteral("<tr class=\"%1\"><td>pixels</td><td>0 differing</td><td>%2 of %3 differ, max &Delta; %4%5</td><td>%6</td><td class=\"rule\">the guest's screen at the visible bounds</td></tr>")
                    .arg(pixels.matches() ? QStringLiteral("ok") : QStringLiteral("bad"))
                    .arg(pixels.mismatched())
                    .arg(pixels.compared())
                    .arg(pixels.max_delta())
                    .arg(window.resampled ? QStringLiteral(" (host picture resampled)") : QString())
                    .arg(verdict(pixels.matches()));
    }
    return html + QStringLiteral("</table>");
}

// What a window is at a stage, in a line: enough to follow a scenario's
// relationships from step to step without opening scenario.json.
QString state_line(const ScenarioOutcome::Window& window) {
    if (!window.truth.has_value()) return {};
    const WindowTruth& truth = *window.truth;
    if (!truth.exists) return QStringLiteral("<p class=\"state\">destroyed</p>");
    QStringList words;
    words << (truth.shown ? QStringLiteral("visible") : QStringLiteral("hidden"));
    if (truth.iconic) words << QStringLiteral("minimized");
    if (truth.zoomed) words << QStringLiteral("maximized");
    words << (truth.enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    if (truth.foreground) words << QStringLiteral("foreground");
    if (truth.active) words << QStringLiteral("active");
    if (truth.focused) words << QStringLiteral("focused");
    if (truth.owner_disabled) words << QStringLiteral("owner disabled");
    if (truth.enabled_popup != 0) words << QStringLiteral("enabled popup %1").arg(hex(truth.enabled_popup));
    words << QStringLiteral("owner %1").arg(hex(truth.owner)) << QStringLiteral("z %1").arg(truth.z_rank)
          << QStringLiteral("at %1,%2 %3x%4")
                 .arg(truth.visible.left).arg(truth.visible.top).arg(truth.visible.width()).arg(truth.visible.height());
    QString line = QStringLiteral("guest: ") + words.join(QStringLiteral(" &middot; "));
    if (window.mirror.has_value()) {
        const host::MirrorState& mirror = *window.mirror;
        QStringList seen;
        seen << (mirror.visible ? QStringLiteral("visible") : QStringLiteral("hidden"));
        if (mirror.minimized) seen << QStringLiteral("minimized");
        if (mirror.maximized) seen << QStringLiteral("maximized");
        if (mirror.active) seen << QStringLiteral("active");
        seen << (mirror.forwards_input ? QStringLiteral("takes input") : QStringLiteral("takes no input"));
        if (mirror.popup_of != 0) seen << QStringLiteral("popup of %1").arg(hex(mirror.popup_of));
        if (mirror.attached_to != 0) seen << QStringLiteral("attached to %1").arg(hex(mirror.attached_to));
        if (mirror.modal) seen << QStringLiteral("modal");
        seen << QStringLiteral("%1 activation requests, %2 alerts").arg(mirror.activation_requests).arg(mirror.alerts);
        if (mirror.compositor_frame.has_value()) {
            seen << QStringLiteral("frame at %1,%2 (logical)")
                        .arg(mirror.compositor_frame->x(), 0, 'f', 2)
                        .arg(mirror.compositor_frame->y(), 0, 'f', 2);
        }
        if (mirror.placement_requests != 0) {
            seen << QStringLiteral("%1 placements, last %2 ms").arg(mirror.placement_requests).arg(mirror.placement_round_trip_ms);
        }
        if (mirror.guest_moves != 0) seen << QStringLiteral("%1 guest moves").arg(mirror.guest_moves);
        line += QStringLiteral("<br>mirror: ") + seen.join(QStringLiteral(" &middot; "));
    }
    return QStringLiteral("<p class=\"state\">%1</p>").arg(line);
}

// The cursor and the pointer in a line, as Windows has them, then the rows.
QString pointer_html(const std::optional<ScenarioOutcome::Pointer>& pointer, const QString& folder,
                     const QString& prefix) {
    if (!pointer.has_value()) return {};
    QString html = QStringLiteral("<h3>cursor and pointer (w%1)</h3>").arg(pointer->window);
    if (pointer->truth.has_value()) {
        const CursorTruth& truth = *pointer->truth;
        QStringList words;
        words << (truth.shown() ? QStringLiteral("shown") : QStringLiteral("hidden"))
              << QStringLiteral("flags %1").arg(truth.flags)
              << (truth.system_idc != 0 ? QString::fromLatin1(name_of(system_cursor_of(truth.system_idc)))
                                        : QStringLiteral("cursor %1").arg(hex(truth.handle)))
              << QStringLiteral("count %1").arg(truth.display_count)
              << QStringLiteral("at %1,%2").arg(truth.x).arg(truth.y);
        if (!truth.frames.empty()) {
            words << QStringLiteral("%1x%2 hotspot %3,%4, %5 step(s)")
                         .arg(truth.frames.front().width).arg(truth.frames.front().height)
                         .arg(truth.frames.front().hotspot_x).arg(truth.frames.front().hotspot_y).arg(truth.frames.size());
        }
        if (truth.clipped) {
            words << QStringLiteral("clipped to %1,%2 %3x%4").arg(truth.clip.left).arg(truth.clip.top)
                         .arg(truth.clip.width()).arg(truth.clip.height());
        }
        if (truth.capture != 0) words << QStringLiteral("captured by %1").arg(hex(truth.capture));
        if (truth.raw_mouse) words << QStringLiteral("raw input flags %1").arg(hex(truth.raw_flags));
        if (truth.recentring != 0) words << QStringLiteral("put back by %1").arg(hex(truth.recentring));
        words << QStringLiteral("speed %1, enhance precision %2").arg(truth.mouse_speed).arg(truth.enhance_precision ? "on" : "off")
              << QStringLiteral("wanted: %1").arg(kind_name(pointer->wanted));
        html += QStringLiteral("<p class=\"state\">guest: %1</p>").arg(words.join(QStringLiteral(" &middot; ")));
    }
    if (!pointer->guest.isNull()) {
        html += QStringLiteral("<div class=\"shots cursor\"><figure><img src=\"%1/%2cursor-guest.png\"><figcaption>guest cursor "
                               "(over white, hotspot aligned)</figcaption></figure>")
                    .arg(escaped(folder), prefix);
        if (!pointer->host.isNull()) {
            html += QStringLiteral("<figure><img src=\"%1/%2cursor-host.png\"><figcaption>mirror cursor on the glass</figcaption></figure>")
                        .arg(escaped(folder), prefix);
        }
        html += QStringLiteral("</div>");
    }
    return html + QString::fromLatin1(kChecksHead) + rows_html(pointer->checks) + QStringLiteral("</table>");
}

QString shots_html(const QString& folder, const QString& prefix) {
    return QStringLiteral(
               "<div class=\"shots\"><figure><img src=\"%1/%2guest.png\"><figcaption>guest screen</figcaption></figure>"
               "<figure><img src=\"%1/%2host.png\"><figcaption>host mirror</figcaption></figure>"
               "<figure><img src=\"%1/%2diff.png\"><figcaption>difference</figcaption></figure></div>")
        .arg(escaped(folder), prefix);
}

QString input_html(const std::vector<ScenarioOutcome::Input>& input) {
    if (input.empty()) return {};
    QString html = QStringLiteral("<h3>input</h3><table class=\"checks\"><tr><th>step</th><th>expected</th>"
                                  "<th>window got</th><th></th></tr>");
    for (const ScenarioOutcome::Input& given : input) {
        html += QStringLiteral("<tr class=\"%1\"><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(given.check.passed ? QStringLiteral("ok") : QStringLiteral("bad"),
                         escaped(given.step), escaped(given.check.expected),
                         escaped(given.check.actual), verdict(given.check.passed));
    }
    return html + QStringLiteral("</table>");
}

QString windows_html(const std::vector<ScenarioOutcome::Window>& windows) {
    QString html;
    for (const ScenarioOutcome::Window& window : windows) {
        html += QStringLiteral("<h3>w%1 &ldquo;%2&rdquo; %3</h3>")
                    .arg(window.index)
                    .arg(escaped(window.title), escaped(hex(window.handle)));
        html += state_line(window) + checks_html(window);
    }
    return html;
}

QString stage_html(const ScenarioOutcome::Stage& stage, std::size_t index, const QString& folder) {
    const QString prefix = QStringLiteral("step%1-").arg(index + 1);
    QString html = QStringLiteral("<div class=\"stage\"><h3 class=\"step\">step %1: %2</h3>").arg(index + 1).arg(escaped(stage.step));
    for (const QString& problem : stage.problems) html += QStringLiteral("<p class=\"problem\">%1</p>").arg(escaped(problem));
    for (const QString& note : stage.notes) html += QStringLiteral("<p class=\"state\">guest: %1</p>").arg(escaped(note));
    if (!stage.guest.isNull()) html += shots_html(folder, prefix);
    return html + windows_html(stage.windows) + input_html(stage.input) + pointer_html(stage.pointer, folder, prefix)
           + QStringLiteral("</div>");
}

QString detail_html(const ScenarioOutcome& outcome, const QString& folder) {
    QString html = QStringLiteral("<section id=\"%1\"><h2>%2 %3</h2><p class=\"about\">%4</p>")
                       .arg(escaped(folder), escaped(outcome.name), verdict(outcome.passed()),
                            escaped(outcome.about));
    for (const QString& problem : outcome.problems) {
        html += QStringLiteral("<p class=\"problem\">%1</p>").arg(escaped(problem));
    }
    if (!outcome.guest.isNull()) html += shots_html(folder, QString());
    html += windows_html(outcome.windows) + input_html(outcome.input) + pointer_html(outcome.pointer, folder, QString());
    for (std::size_t i = 0; i < outcome.stages.size(); ++i) html += stage_html(outcome.stages[i], i, folder);
    html += QStringLiteral("<p class=\"record\"><a href=\"%1/scenario.json\">scenario.json</a></p></section>")
                .arg(escaped(folder));
    return html;
}

constexpr const char* kStyle = R"(
:root { --bg: #f6f6f4; --fg: #1c1c1c; --muted: #6b6b6b; --line: #d8d8d4; --card: #ffffff;
        --pass: #1d7a3e; --fail: #b3261e; --bad: #fbeceb; --limit: #8a5a00; --limit-bg: #fdf3dc; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #161616; --fg: #e8e8e6; --muted: #9a9a96; --line: #333331; --card: #1f1f1f;
          --pass: #6fcf8f; --fail: #ff8a80; --bad: #3a1f1d; --limit: #f0c060; --limit-bg: #33290f; }
}
body { background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; margin: 0 auto;
       max-width: 1400px; padding: 16px; }
h1 { font-size: 20px; margin: 0 0 4px; } h2 { font-size: 17px; margin: 0 0 4px; } h3 { font-size: 14px; margin: 14px 0 4px; }
.meta { color: var(--muted); margin-bottom: 16px; }
table { border-collapse: collapse; width: 100%; background: var(--card); }
th, td { border-bottom: 1px solid var(--line); padding: 4px 8px; text-align: left; vertical-align: top; }
tr.bad td { background: var(--bad); } tr.limit td { background: var(--limit-bg); }
.limit { color: var(--limit); font-weight: 600; }
.pass { color: var(--pass); font-weight: 600; } .fail { color: var(--fail); font-weight: 600; }
.rule, .about { color: var(--muted); } .problem { color: var(--fail); }
.thumb { white-space: nowrap; }
.thumb img { height: 48px; image-rendering: pixelated; border: 1px solid var(--line); margin-right: 4px; }
section { background: var(--card); border: 1px solid var(--line); border-radius: 6px; padding: 12px; margin: 16px 0; }
.shots { display: flex; gap: 8px; flex-wrap: wrap; } figure { margin: 0; }
.shots img { max-width: 440px; width: 100%; image-rendering: pixelated; border: 1px solid var(--line); }
.shots.cursor img { width: auto; height: 96px; }
figcaption { color: var(--muted); font-size: 12px; }
.checks td:nth-child(2), .checks td:nth-child(3) { font-family: ui-monospace, monospace; }
.overflow { overflow-x: auto; }
.state { font-family: ui-monospace, monospace; font-size: 12px; color: var(--muted); margin: 2px 0 4px; }
.stage { border-top: 2px solid var(--line); margin-top: 14px; padding-top: 6px; }
h3.step { font-size: 15px; }
)";

}  // namespace

RunReport::RunReport(QString directory, QJsonObject run)
    : directory_(std::move(directory)), run_(std::move(run)) {}

bool RunReport::open(QString& error) {
    if (QDir().mkpath(directory_)) return true;
    error = QStringLiteral("cannot make the report folder %1").arg(directory_);
    return false;
}

bool RunReport::add(const ScenarioOutcome& outcome, QString& error) {
    Entry entry;
    entry.name = outcome.name;
    entry.folder = QStringLiteral("%1-%2").arg(entries_.size() + 1, 2, 10, QLatin1Char('0')).arg(outcome.name);
    entry.passed = outcome.passed();
    entry.failures = outcome.failures();
    entry.skipped = outcome.skipped;
    entry.known_limits = outcome.known_limits();
    if (outcome.scene_pixels.has_value()) {
        entry.pixels = QStringLiteral("%1 / %2 (max &Delta; %3)")
                           .arg(outcome.scene_pixels->mismatched())
                           .arg(outcome.scene_pixels->compared())
                           .arg(outcome.scene_pixels->max_delta());
    }
    entry.detail = detail_html(outcome, entry.folder);

    const QDir folder{QDir(directory_).filePath(entry.folder)};
    if (!QDir().mkpath(folder.path())) {
        error = QStringLiteral("cannot make %1").arg(folder.path());
        return false;
    }
    const auto save = [&](const QImage& guest, const QImage& host, const std::optional<PixelComparison>& pixels,
                          const QString& prefix) {
        if (guest.isNull()) return true;
        return guest.save(folder.filePath(prefix + QStringLiteral("guest.png")))
               && host.save(folder.filePath(prefix + QStringLiteral("host.png")))
               && (!pixels.has_value() || pixels->difference().save(folder.filePath(prefix + QStringLiteral("diff.png"))));
    };
    const auto save_cursor = [&](const std::optional<ScenarioOutcome::Pointer>& pointer, const QString& prefix) {
        if (!pointer.has_value() || pointer->guest.isNull()) return true;
        return pointer->guest.save(folder.filePath(prefix + QStringLiteral("cursor-guest.png")))
               && (pointer->host.isNull() || pointer->host.save(folder.filePath(prefix + QStringLiteral("cursor-host.png"))));
    };
    bool saved = save(outcome.guest, outcome.host, outcome.scene_pixels, QString()) && save_cursor(outcome.pointer, QString());
    for (std::size_t i = 0; i < outcome.stages.size() && saved; ++i) {
        const ScenarioOutcome::Stage& stage = outcome.stages[i];
        const QString prefix = QStringLiteral("step%1-").arg(i + 1);
        saved = save(stage.guest, stage.host, stage.scene_pixels, prefix) && save_cursor(stage.pointer, prefix);
    }
    if (!saved) {
        error = QStringLiteral("cannot write the pictures of %1").arg(folder.path());
        return false;
    }
    QFile record{folder.filePath(QStringLiteral("scenario.json"))};
    if (!record.open(QIODevice::WriteOnly)
        || record.write(QJsonDocument(outcome_json(outcome)).toJson()) < 0) {
        error = QStringLiteral("cannot write %1").arg(record.fileName());
        return false;
    }
    entries_.push_back(std::move(entry));
    return true;
}

int RunReport::skipped() const {
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
                                          [](const Entry& entry) { return !entry.skipped.isEmpty(); }));
}

int RunReport::failed() const {
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
                                          [](const Entry& entry) { return !entry.passed; }));
}

QString RunReport::row_of(const Entry& entry) const {
    QString thumbs;
    if (!entry.pixels.isEmpty()) {
        thumbs = QStringLiteral("<img src=\"%1/guest.png\"><img src=\"%1/host.png\"><img src=\"%1/diff.png\">")
                     .arg(escaped(entry.folder));
    }
    QString differs = escaped(entry.failures.join(QStringLiteral(", ")));
    if (!entry.known_limits.isEmpty()) {
        differs += QStringLiteral(" <span class=\"limit\">known limit: %1</span>")
                       .arg(escaped(entry.known_limits.join(QStringLiteral(", "))));
    }
    return QStringLiteral("<tr class=\"%1\"><td><a href=\"#%2\">%3</a></td><td>%4</td><td>%5</td><td>%6</td><td class=\"thumb\">%7</td></tr>")
        .arg(entry.passed ? QStringLiteral("ok") : QStringLiteral("bad"), escaped(entry.folder),
             escaped(entry.name), verdict(entry.passed), differs, entry.pixels, thumbs);
}

bool RunReport::finish(QString& error) {
    std::vector<const Entry*> order;
    std::vector<const Entry*> passed_over;
    order.reserve(entries_.size());
    for (const Entry& entry : entries_) (entry.skipped.isEmpty() ? order : passed_over).push_back(&entry);
    std::stable_sort(order.begin(), order.end(),
                     [](const Entry* a, const Entry* b) { return !a->passed && b->passed; });

    QString about;
    for (auto it = run_.constBegin(); it != run_.constEnd(); ++it) {
        about += QStringLiteral("%1: %2 &middot; ").arg(escaped(it.key()), escaped(it.value().toVariant().toString()));
    }
    QString html = QStringLiteral("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                                  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                                  "<title>remotewin conformance</title><style>%1</style></head><body>")
                       .arg(QString::fromLatin1(kStyle));
    html += QStringLiteral("<h1>remotewin conformance: %1 of %2 failed</h1><p class=\"meta\">%3 skipped &middot; %4%5</p>")
                .arg(failed())
                .arg(total() - skipped())
                .arg(skipped())
                .arg(about, QDateTime::currentDateTime().toString(Qt::ISODate));
    html += QStringLiteral("<div class=\"overflow\"><table><tr><th>scenario</th><th></th><th>what differs</th>"
                           "<th>pixels differing / compared</th><th>guest &middot; host &middot; diff</th></tr>");
    for (const Entry* entry : order) html += row_of(*entry);
    html += QStringLiteral("</table></div>");
    QString limits;
    for (const Entry* entry : order) {
        if (entry->known_limits.isEmpty()) continue;
        limits += QStringLiteral("<tr class=\"limit\"><td><a href=\"#%1\">%2</a></td><td>%3</td></tr>")
                      .arg(escaped(entry->folder), escaped(entry->name),
                           escaped(entry->known_limits.join(QStringLiteral(", "))));
    }
    if (!limits.isEmpty()) {
        html += QStringLiteral("<section><h2>known limits</h2><p class=\"about\">Rows the mirror cannot pass "
                               "for a reason below it, given with each row; they fail nothing.</p>"
                               "<table><tr><th>scenario</th><th>rows</th></tr>%1</table></section>")
                    .arg(limits);
    }
    for (const Entry* entry : order) html += entry->detail;
    if (!passed_over.empty()) {
        html += QStringLiteral("<section><h2>skipped</h2><table><tr><th>scenario</th><th>why</th></tr>");
        for (const Entry* entry : passed_over) {
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(escaped(entry->name), escaped(entry->skipped));
        }
        html += QStringLiteral("</table></section>");
    }
    html += QStringLiteral("</body></html>");

    QFile page{QDir(directory_).filePath(QStringLiteral("report.html"))};
    if (!page.open(QIODevice::WriteOnly) || page.write(html.toUtf8()) < 0) {
        error = QStringLiteral("cannot write %1").arg(page.fileName());
        return false;
    }
    QJsonArray scenarios;
    for (const Entry& entry : entries_) {
        scenarios.push_back(QJsonObject{{QStringLiteral("name"), entry.name},
                                        {QStringLiteral("folder"), entry.folder},
                                        {QStringLiteral("passed"), entry.passed},
                                        {QStringLiteral("skipped"), entry.skipped},
                                        {QStringLiteral("known_limits"), QJsonArray::fromStringList(entry.known_limits)},
                                        {QStringLiteral("failures"), QJsonArray::fromStringList(entry.failures)}});
    }
    QJsonObject run = run_;
    run.insert(QStringLiteral("failed"), failed());
    run.insert(QStringLiteral("total"), total());
    run.insert(QStringLiteral("skipped"), skipped());
    run.insert(QStringLiteral("scenarios"), scenarios);
    QFile record{QDir(directory_).filePath(QStringLiteral("run.json"))};
    if (!record.open(QIODevice::WriteOnly) || record.write(QJsonDocument(run).toJson()) < 0) {
        error = QStringLiteral("cannot write %1").arg(record.fileName());
        return false;
    }
    return true;
}

}  // namespace rwin::conform
