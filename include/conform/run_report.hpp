#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace rwin::conform {

struct ScenarioOutcome;

// A run, written down: a folder per scenario with the guest's picture, the
// host's, their difference and a JSON record of everything checked; and
// one static report.html over all of them, failures first, that opens from
// the folder with nothing else.
class RunReport {
public:
    // ``run`` says what the run was against; it heads both the JSON and
    // the page.
    RunReport(QString directory, QJsonObject run);

    [[nodiscard]] bool open(QString& error);
    [[nodiscard]] bool add(const ScenarioOutcome& outcome, QString& error);
    [[nodiscard]] bool finish(QString& error);

    [[nodiscard]] int failed() const;
    // Generated scenarios not looked at: refused by Windows, or the same
    // as one that was.
    [[nodiscard]] int skipped() const;
    [[nodiscard]] int total() const noexcept { return static_cast<int>(entries_.size()); }

private:
    struct Entry {
        QString name;
        QString folder;
        bool passed = false;
        QStringList failures;
        QString skipped;
        QStringList known_limits;
        QString pixels;
        QString detail;
    };

    [[nodiscard]] QString row_of(const Entry& entry) const;

    QString directory_;
    QJsonObject run_;
    std::vector<Entry> entries_;
};

}  // namespace rwin::conform
