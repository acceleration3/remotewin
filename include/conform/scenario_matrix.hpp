#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace rwin::conform {

// The style matrix: scenarios generated from factors rather than written
// out, each factor a Win32 word (or a few) that changes what Windows does
// with a window, each value a fragment of a window in the scenario file's
// own spelling.
//
// Factors are in behaviour groups - the frame words together, the shell's
// together, layering together - and the matrix is every combination
// inside a group, the other groups at their first value, and on top of
// that pairwise across groups: every value of any factor appears with
// every value of any factor of another group in at least one scenario.
// Within a group the words act on one another (a sizing border on a
// popup, a maximise box without a window menu) and are tried exhaustively;
// across groups they mostly do not, and a pair is the size of most
// interactions that do. The full product is millions; this is a few
// hundred, and Windows' own normalising of the words merges more of them
// at run time (ScenarioRunner skips a generated scenario whose windows
// came out with words an earlier one already had).
//
// A value's fragment merges into the subject window: lists of words are
// appended, objects merged, anything else replaced. "owned": true makes
// the subject owned by an owner window made before it.
//
// "excluded" names pairs of values Windows refuses together
// (CreateWindowEx fails): no scenario has both, and the pair is not one
// the cover owes - a row spent on a window that is never made would leave
// every other pair in it uncovered.
class ScenarioMatrix {
public:
    // Nothing when the matrix section is malformed; ``error`` says where.
    [[nodiscard]] static std::optional<ScenarioMatrix> read(const QJsonObject& matrix,
                                                            QString& error);

    // Every scenario, in the scenario file's spelling, marked generated.
    [[nodiscard]] std::vector<QJsonObject> scenarios() const;

private:
    struct Value {
        QString label;
        QJsonObject fragment;
    };
    struct Factor {
        QString name;
        int group = 0;
        std::vector<Value> values;
    };
    // The value each factor takes, by index.
    using Row = std::vector<int>;
    // Factor a at value va with factor b at value vb.
    struct Excluded {
        int a;
        int va;
        int b;
        int vb;
    };

    [[nodiscard]] std::optional<std::pair<int, int>> value_named(const QString& factor,
                                                                 const QString& label) const;
    [[nodiscard]] bool allowed(const Row& row) const;
    [[nodiscard]] bool clashes(const Row& row, int factor, int value) const;
    [[nodiscard]] std::vector<Row> group_rows(int group) const;
    [[nodiscard]] std::vector<Row> pairwise_rows(const std::vector<Row>& already) const;
    [[nodiscard]] QJsonObject scenario_of(const Row& row, const QString& name) const;

    QString prefix_;
    QJsonObject subject_;
    QJsonObject owner_;
    std::vector<QString> groups_;
    std::vector<Factor> factors_;
    std::vector<Excluded> excluded_;
};

}  // namespace rwin::conform
