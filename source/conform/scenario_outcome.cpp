#include "conform/scenario_outcome.hpp"

namespace rwin::conform {

namespace {

void add_failures(const std::vector<ScenarioOutcome::Window>& windows,
                  const std::vector<ScenarioOutcome::Input>& input, const QString& prefix,
                  QStringList& failed) {
    for (const ScenarioOutcome::Window& window : windows) {
        for (const Expectation::Check& check : window.checks) {
            if (check.failed()) failed.push_back(QStringLiteral("%1w%2 %3").arg(prefix).arg(window.index).arg(check.property));
        }
        if (window.pixels.has_value() && !window.pixels->matches()) {
            failed.push_back(QStringLiteral("%1w%2 pixels").arg(prefix).arg(window.index));
        }
    }
    for (const ScenarioOutcome::Input& given : input) {
        if (!given.check.passed) failed.push_back(QStringLiteral("%1input %2").arg(prefix, given.step));
    }
}

void add_known_limits(const std::vector<ScenarioOutcome::Window>& windows, const QString& prefix,
                      QStringList& known) {
    for (const ScenarioOutcome::Window& window : windows) {
        for (const Expectation::Check& check : window.checks) {
            if (!check.passed && !check.known_limit.isEmpty()) {
                known.push_back(QStringLiteral("%1w%2 %3").arg(prefix).arg(window.index).arg(check.property));
            }
        }
    }
}

QString stage_prefix(std::size_t index) { return QStringLiteral("step %1: ").arg(index + 1); }

void add_pointer_failures(const std::optional<ScenarioOutcome::Pointer>& pointer, const QString& prefix,
                          QStringList& failed) {
    if (!pointer.has_value()) return;
    for (const Expectation::Check& check : pointer->checks) {
        if (check.failed()) failed.push_back(QStringLiteral("%1pointer %2").arg(prefix, check.property));
    }
}

void add_pointer_limits(const std::optional<ScenarioOutcome::Pointer>& pointer, const QString& prefix,
                        QStringList& known) {
    if (!pointer.has_value()) return;
    for (const Expectation::Check& check : pointer->checks) {
        if (!check.passed && !check.known_limit.isEmpty()) {
            known.push_back(QStringLiteral("%1pointer %2").arg(prefix, check.property));
        }
    }
}

}  // namespace

QStringList ScenarioOutcome::failures() const {
    if (!skipped.isEmpty()) return {};
    QStringList failed = problems;
    add_failures(windows, input, QString(), failed);
    add_pointer_failures(pointer, QString(), failed);
    for (std::size_t i = 0; i < stages.size(); ++i) {
        for (const QString& problem : stages[i].problems) failed.push_back(stage_prefix(i) + problem);
        add_failures(stages[i].windows, stages[i].input, stage_prefix(i), failed);
        add_pointer_failures(stages[i].pointer, stage_prefix(i), failed);
    }
    // A step is named once however many of its messages went astray.
    failed.removeDuplicates();
    return failed;
}

QStringList ScenarioOutcome::known_limits() const {
    QStringList known;
    if (!skipped.isEmpty()) return known;
    add_known_limits(windows, QString(), known);
    add_pointer_limits(pointer, QString(), known);
    for (std::size_t i = 0; i < stages.size(); ++i) {
        add_known_limits(stages[i].windows, stage_prefix(i), known);
        add_pointer_limits(stages[i].pointer, stage_prefix(i), known);
    }
    known.removeDuplicates();
    return known;
}

bool ScenarioOutcome::passed() const { return failures().isEmpty(); }

}  // namespace rwin::conform
