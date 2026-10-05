#pragma once

#include "conform/scenario.hpp"

#include <QColor>
#include <QString>

#include <optional>
#include <vector>

namespace rwin::conform {

// The scenarios of a run, read from a JSON file, and the backdrop colour
// they are seen against.
//
// Style words are written by their Win32 names rather than as numbers, so
// the file says what it tests: "style": ["WS_POPUP", "WS_BORDER"].
class ScenarioBook {
public:
    // Nothing when the file cannot be read or says something this does not
    // understand; ``error`` names the file and the place.
    [[nodiscard]] static std::optional<ScenarioBook> read(const QString& path, QString& error);

    [[nodiscard]] const std::vector<Scenario>& scenarios() const noexcept { return scenarios_; }
    [[nodiscard]] QColor backdrop() const noexcept { return backdrop_; }

private:
    std::vector<Scenario> scenarios_;
    QColor backdrop_;
};

}  // namespace rwin::conform
