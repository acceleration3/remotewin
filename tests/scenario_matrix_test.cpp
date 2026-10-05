#include "conform/scenario_matrix.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::conform::ScenarioMatrix;

// Two groups: the first of two factors (2 x 3), the second of two (2 x 2).
QJsonObject small_matrix() {
    return QJsonDocument::fromJson(R"({
        "prefix": "t",
        "window": {"style": ["WS_POPUP"], "client": [100, 80], "at": [10, 20], "title": "s"},
        "owner": {"style": ["WS_OVERLAPPEDWINDOW"], "client": [300, 200], "at": [0, 0], "title": "o"},
        "groups": [
          {"name": "a", "factors": [
            {"name": "p", "values": [{"label": "p0"}, {"label": "p1", "style": ["WS_BORDER"]}]},
            {"name": "q", "values": [{"label": "q0"}, {"label": "q1", "ex_style": ["WS_EX_TOPMOST"]},
                                     {"label": "q2", "layered": {"alpha": 9}}]}]},
          {"name": "b", "factors": [
            {"name": "r", "values": [{"label": "r0"}, {"label": "r1", "owned": true}]},
            {"name": "s", "values": [{"label": "s0"}, {"label": "s1", "show": "hidden"}]}]}
        ]})")
        .object();
}

// The labels of a scenario's subject, read back from the words it got.
struct Seen {
    bool p1;
    int q;
    bool r1;
    bool s1;
};

Seen seen_in(const QJsonObject& scenario) {
    const QJsonArray windows = scenario.value(QStringLiteral("windows")).toArray();
    const QJsonObject subject = windows.last().toObject();
    const bool border = subject.value(QStringLiteral("style")).toArray().contains(QStringLiteral("WS_BORDER"));
    const int q = subject.value(QStringLiteral("ex_style")).toArray().contains(QStringLiteral("WS_EX_TOPMOST")) ? 1
                  : subject.contains(QStringLiteral("layered"))                                            ? 2
                                                                                                         : 0;
    return Seen{border, q, windows.size() == 2,
                subject.value(QStringLiteral("show")).toString() == QStringLiteral("hidden")};
}

void groups_are_exhaustive_and_pairs_are_covered() {
    QString error;
    const auto matrix = ScenarioMatrix::read(small_matrix(), error);
    check(matrix.has_value(), "a matrix reads");
    if (!matrix.has_value()) return;
    const auto scenarios = matrix->scenarios();
    std::set<std::string> within_a;
    std::set<std::string> within_b;
    std::set<std::string> across;
    std::set<QString> names;
    for (const QJsonObject& scenario : scenarios) {
        names.insert(scenario.value(QStringLiteral("name")).toString());
        check(scenario.value(QStringLiteral("generated")).toBool(), "every scenario is marked generated");
        const Seen s = seen_in(scenario);
        within_a.insert(std::to_string(s.p1) + std::to_string(s.q));
        within_b.insert(std::to_string(s.r1) + std::to_string(s.s1));
        for (const std::string& a : {"p" + std::to_string(s.p1), "q" + std::to_string(s.q)}) {
            for (const std::string& b : {"r" + std::to_string(s.r1), "s" + std::to_string(s.s1)}) {
                across.insert(a + b);
            }
        }
    }
    check(within_a.size() == 6, "every combination inside the first group");
    check(within_b.size() == 4, "every combination inside the second");
    check(across.size() == (2 + 3) * (2 + 2), "every value of one group with every value of the other");
    check(names.size() == scenarios.size(), "and no two scenarios share a name");
    check(scenarios.size() < 6 * 4, "in fewer scenarios than the full product");
}

void fragments_merge_and_an_owned_window_gets_its_owner() {
    QString error;
    const auto scenarios = ScenarioMatrix::read(small_matrix(), error)->scenarios();
    bool saw_owned = false;
    bool saw_hidden = false;
    for (const QJsonObject& scenario : scenarios) {
        const QJsonArray windows = scenario.value(QStringLiteral("windows")).toArray();
        const QJsonObject subject = windows.last().toObject();
        check(subject.value(QStringLiteral("style")).toArray().contains(QStringLiteral("WS_POPUP")),
              "a fragment's words are added to the window's, not put in their place");
        if (windows.size() == 2) {
            saw_owned = true;
            check(windows[0].toObject().value(QStringLiteral("title")).toString() == QStringLiteral("o")
                      && subject.value(QStringLiteral("owner")).toInt(-1) == 0
                      && !subject.contains(QStringLiteral("owned")),
                  "owned: the owner is made first and named by its place");
        }
        const QJsonArray input = scenario.value(QStringLiteral("input")).toArray();
        if (subject.value(QStringLiteral("show")).toString() == QStringLiteral("hidden")) {
            saw_hidden = true;
            check(input.isEmpty(), "a hidden window is not clicked");
        } else {
            check(input.size() == 1
                      && input[0].toObject().value(QStringLiteral("click")).toArray() == QJsonArray{25, 60}
                      && input[0].toObject().value(QStringLiteral("window")).toInt() == windows.size() - 1,
                  "a shown one is clicked a quarter in and three quarters down");
        }
    }
    check(saw_owned && saw_hidden, "both kinds came up");
}

// A border with an owner stands in for a pair Windows refuses: never made,
// and every other pair still covered without it.
void an_excluded_pair_is_never_made() {
    QJsonObject matrix = small_matrix();
    matrix.insert(QStringLiteral("excluded"),
                  QJsonArray{QJsonObject{{QStringLiteral("p"), QStringLiteral("p1")},
                                         {QStringLiteral("r"), QStringLiteral("r1")}}});
    QString error;
    const auto read = ScenarioMatrix::read(matrix, error);
    check(read.has_value(), "a matrix with an excluded pair reads");
    if (!read.has_value()) return;
    std::set<std::string> across;
    for (const QJsonObject& scenario : read->scenarios()) {
        const Seen s = seen_in(scenario);
        check(!(s.p1 && s.r1), "no scenario has the excluded pair");
        for (const std::string& a : {"p" + std::to_string(s.p1), "q" + std::to_string(s.q)}) {
            for (const std::string& b : {"r" + std::to_string(s.r1), "s" + std::to_string(s.s1)}) {
                across.insert(a + b);
            }
        }
    }
    check(across.size() == (2 + 3) * (2 + 2) - 1, "every other pair across the groups is covered");
    matrix.insert(QStringLiteral("excluded"),
                  QJsonArray{QJsonObject{{QStringLiteral("p"), QStringLiteral("nothing")},
                                         {QStringLiteral("r"), QStringLiteral("r1")}}});
    check(!ScenarioMatrix::read(matrix, error).has_value() && error.contains(QStringLiteral("nothing")),
          "an exclusion naming a value no factor has is refused by name");
}

void a_matrix_without_factors_is_refused() {
    QString error;
    QJsonObject empty = small_matrix();
    empty.insert(QStringLiteral("groups"), QJsonArray{});
    check(!ScenarioMatrix::read(empty, error).has_value() && !error.isEmpty(),
          "a matrix with nothing to vary is refused, and says so");
}

}  // namespace

int main() {
    groups_are_exhaustive_and_pairs_are_covered();
    fragments_merge_and_an_owned_window_gets_its_owner();
    an_excluded_pair_is_never_made();
    a_matrix_without_factors_is_refused();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("scenario matrix: all checks passed\n");
    return EXIT_SUCCESS;
}
