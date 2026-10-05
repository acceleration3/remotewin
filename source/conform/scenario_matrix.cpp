#include "conform/scenario_matrix.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <array>
#include <set>

namespace rwin::conform {

namespace {

// One pair still to be covered: factor a at value va with factor b at
// value vb, a < b.
using Pair = std::array<int, 4>;

// Lists append, objects merge, the rest is replaced: "style": ["WS_POPUP"]
// and "style": ["WS_BORDER"] make a bordered popup.
void merge_into(QJsonObject& window, const QJsonObject& fragment) {
    for (auto it = fragment.constBegin(); it != fragment.constEnd(); ++it) {
        const QJsonValue existing = window.value(it.key());
        if (existing.isArray() && it.value().isArray()) {
            QJsonArray joined = existing.toArray();
            for (const QJsonValue& item : it.value().toArray()) joined.push_back(item);
            window.insert(it.key(), joined);
        } else if (existing.isObject() && it.value().isObject()) {
            QJsonObject merged = existing.toObject();
            merge_into(merged, it.value().toObject());
            window.insert(it.key(), merged);
        } else {
            window.insert(it.key(), it.value());
        }
    }
}

// The client point a click aims at: a quarter in and three quarters down,
// which is inside an ellipse cut to the window and off the colour-keyed
// patch in the middle of the test pattern.
QJsonArray click_point(const QJsonObject& window) {
    const QJsonArray client = window.value(QStringLiteral("client")).toArray();
    return QJsonArray{client.at(0).toInt() / 4, client.at(1).toInt() * 3 / 4};
}

bool takes_a_click(const QJsonObject& window) {
    const QString show = window.value(QStringLiteral("show")).toString(QStringLiteral("normal"));
    return show != QStringLiteral("minimized") && show != QStringLiteral("hidden");
}

}  // namespace

std::optional<ScenarioMatrix> ScenarioMatrix::read(const QJsonObject& matrix, QString& error) {
    ScenarioMatrix read;
    read.prefix_ = matrix.value(QStringLiteral("prefix")).toString(QStringLiteral("mx"));
    read.subject_ = matrix.value(QStringLiteral("window")).toObject();
    read.owner_ = matrix.value(QStringLiteral("owner")).toObject();
    if (read.subject_.isEmpty() || read.owner_.isEmpty()) {
        error = QStringLiteral("matrix: needs a 'window' and an 'owner'");
        return std::nullopt;
    }
    for (const QJsonValue& group : matrix.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject described = group.toObject();
        const QString name = described.value(QStringLiteral("name")).toString();
        const int index = static_cast<int>(read.groups_.size());
        read.groups_.push_back(name);
        for (const QJsonValue& factor : described.value(QStringLiteral("factors")).toArray()) {
            Factor made{factor.toObject().value(QStringLiteral("name")).toString(), index, {}};
            for (const QJsonValue& value : factor.toObject().value(QStringLiteral("values")).toArray()) {
                QJsonObject fragment = value.toObject();
                const QString label = fragment.take(QStringLiteral("label")).toString();
                made.values.push_back(Value{label, fragment});
            }
            if (made.values.size() < 2 || made.name.isEmpty()) {
                error = QStringLiteral("matrix group '%1': factor '%2' needs a name and two values or more")
                            .arg(name, made.name);
                return std::nullopt;
            }
            read.factors_.push_back(std::move(made));
        }
    }
    if (read.factors_.empty()) {
        error = QStringLiteral("matrix: no factors");
        return std::nullopt;
    }
    for (const QJsonValue& pair : matrix.value(QStringLiteral("excluded")).toArray()) {
        std::vector<std::pair<int, int>> named;
        const QJsonObject described = pair.toObject();
        for (auto it = described.constBegin(); it != described.constEnd(); ++it) {
            const std::optional<std::pair<int, int>> found = read.value_named(it.key(), it.value().toString());
            if (!found.has_value()) {
                error = QStringLiteral("matrix: excluded names %1=%2, which no factor has")
                            .arg(it.key(), it.value().toString());
                return std::nullopt;
            }
            named.push_back(*found);
        }
        if (named.size() != 2) {
            error = QStringLiteral("matrix: an excluded entry names two factors");
            return std::nullopt;
        }
        read.excluded_.push_back(Excluded{named[0].first, named[0].second, named[1].first, named[1].second});
    }
    return read;
}

std::optional<std::pair<int, int>> ScenarioMatrix::value_named(const QString& factor,
                                                               const QString& label) const {
    for (std::size_t f = 0; f < factors_.size(); ++f) {
        if (factors_[f].name != factor) continue;
        for (std::size_t v = 0; v < factors_[f].values.size(); ++v) {
            if (factors_[f].values[v].label == label) {
                return std::pair{static_cast<int>(f), static_cast<int>(v)};
            }
        }
    }
    return std::nullopt;
}

// Whether giving ``factor`` ``value`` makes a pair Windows refuses with a
// factor the row has already set (-1 is unset).
bool ScenarioMatrix::clashes(const Row& row, int factor, int value) const {
    return std::any_of(excluded_.begin(), excluded_.end(), [&](const Excluded& pair) {
        return (pair.a == factor && pair.va == value && row[pair.b] == pair.vb)
               || (pair.b == factor && pair.vb == value && row[pair.a] == pair.va);
    });
}

bool ScenarioMatrix::allowed(const Row& row) const {
    return std::none_of(excluded_.begin(), excluded_.end(), [&](const Excluded& pair) {
        return row[pair.a] == pair.va && row[pair.b] == pair.vb;
    });
}

// Every combination of the group's factors, every other factor at its
// first value.
std::vector<ScenarioMatrix::Row> ScenarioMatrix::group_rows(int group) const {
    std::vector<Row> rows{Row(factors_.size(), 0)};
    for (std::size_t f = 0; f < factors_.size(); ++f) {
        if (factors_[f].group != group) continue;
        std::vector<Row> grown;
        grown.reserve(rows.size() * factors_[f].values.size());
        for (const Row& row : rows) {
            for (std::size_t v = 0; v < factors_[f].values.size(); ++v) {
                Row next = row;
                next[f] = static_cast<int>(v);
                grown.push_back(std::move(next));
            }
        }
        rows = std::move(grown);
    }
    std::erase_if(rows, [this](const Row& row) { return !allowed(row); });
    return rows;
}

// Greedy all-pairs over factors of different groups: start a row from the
// first pair still uncovered, give every other factor the value that
// covers most uncovered pairs with what the row has so far, repeat.
// Deterministic, so a run is the same run every time.
std::vector<ScenarioMatrix::Row> ScenarioMatrix::pairwise_rows(const std::vector<Row>& already) const {
    const int count = static_cast<int>(factors_.size());
    std::set<Pair> uncovered;
    for (int a = 0; a < count; ++a) {
        for (int b = a + 1; b < count; ++b) {
            if (factors_[a].group == factors_[b].group) continue;
            for (int va = 0; va < static_cast<int>(factors_[a].values.size()); ++va) {
                for (int vb = 0; vb < static_cast<int>(factors_[b].values.size()); ++vb) {
                    Row row(factors_.size(), -1);
                    row[a] = va;
                    if (!clashes(row, b, vb)) uncovered.insert(Pair{a, va, b, vb});
                }
            }
        }
    }
    const auto cover = [&](const Row& row) {
        for (int a = 0; a < count; ++a) {
            for (int b = a + 1; b < count; ++b) uncovered.erase(Pair{a, row[a], b, row[b]});
        }
    };
    for (const Row& row : already) cover(row);

    std::vector<Row> rows;
    while (!uncovered.empty()) {
        const Pair first = *uncovered.begin();
        Row row(factors_.size(), -1);
        row[first[0]] = first[1];
        row[first[2]] = first[3];
        for (int f = 0; f < count; ++f) {
            if (row[f] >= 0) continue;
            int best = 0;
            int best_gain = -1;
            for (int v = 0; v < static_cast<int>(factors_[f].values.size()); ++v) {
                if (clashes(row, f, v)) continue;
                int gain = 0;
                for (int g = 0; g < count; ++g) {
                    if (row[g] < 0 || factors_[g].group == factors_[f].group) continue;
                    const Pair pair = g < f ? Pair{g, row[g], f, v} : Pair{f, v, g, row[g]};
                    gain += uncovered.count(pair) != 0 ? 1 : 0;
                }
                if (gain > best_gain) {
                    best_gain = gain;
                    best = v;
                }
            }
            row[f] = best;
        }
        cover(row);
        rows.push_back(std::move(row));
    }
    return rows;
}

QJsonObject ScenarioMatrix::scenario_of(const Row& row, const QString& name) const {
    QJsonObject subject = subject_;
    QStringList about;
    for (std::size_t f = 0; f < factors_.size(); ++f) {
        const Value& value = factors_[f].values[static_cast<std::size_t>(row[f])];
        merge_into(subject, value.fragment);
        if (row[f] != 0) about.push_back(QStringLiteral("%1=%2").arg(factors_[f].name, value.label));
    }
    const bool owned = subject.take(QStringLiteral("owned")).toBool(false);
    QJsonArray windows;
    if (owned) {
        windows.push_back(owner_);
        subject.insert(QStringLiteral("owner"), 0);
    }
    windows.push_back(subject);
    QJsonArray input;
    if (takes_a_click(subject)) {
        input.push_back(QJsonObject{{QStringLiteral("window"), windows.size() - 1},
                                    {QStringLiteral("click"), click_point(subject)}});
    }
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("about"), about.isEmpty() ? QStringLiteral("matrix: every factor at its first value")
                                                  : QStringLiteral("matrix: ") + about.join(QStringLiteral(", "))},
        {QStringLiteral("generated"), true},
        {QStringLiteral("windows"), windows},
        {QStringLiteral("input"), input}};
}

std::vector<QJsonObject> ScenarioMatrix::scenarios() const {
    std::vector<Row> rows;
    std::vector<QJsonObject> out;
    const auto add = [&](const Row& row, const QString& name) {
        if (std::find(rows.begin(), rows.end(), row) != rows.end()) return;
        rows.push_back(row);
        out.push_back(scenario_of(row, name));
    };
    for (int group = 0; group < static_cast<int>(groups_.size()); ++group) {
        int index = 0;
        for (const Row& row : group_rows(group)) {
            QStringList labels;
            for (std::size_t f = 0; f < factors_.size(); ++f) {
                if (factors_[f].group == group && row[f] != 0) {
                    labels.push_back(factors_[f].values[static_cast<std::size_t>(row[f])].label);
                }
            }
            const QString suffix = labels.isEmpty() ? QStringLiteral("default") : labels.join(QLatin1Char('-'));
            add(row, QStringLiteral("%1-%2-%3-%4").arg(prefix_, groups_[static_cast<std::size_t>(group)])
                         .arg(++index, 3, 10, QLatin1Char('0'))
                         .arg(suffix));
        }
    }
    int index = 0;
    for (const Row& row : pairwise_rows(rows)) {
        add(row, QStringLiteral("%1-pairs-%2").arg(prefix_).arg(++index, 3, 10, QLatin1Char('0')));
    }
    return out;
}

}  // namespace rwin::conform
