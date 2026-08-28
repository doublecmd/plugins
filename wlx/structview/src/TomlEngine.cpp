#include "TextFormatEngine.h"

#define TOML_IMPLEMENTATION
#include <toml++/toml.hpp>

#include <sstream>

/// TOML engine: builds a document tree from TOML tables.
///
/// - Tables → child nodes
/// - Arrays of tables → repeated children with tabular grid
/// - Values → Key | Value grid rows
class TomlEngine : public TextFormatEngine {
public:
    bool parse(const QByteArray &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    QByteArray serialize() const override;
    QString rawText() const override { return m_rawText; }
    QString formatName() const override { return QStringLiteral("TOML"); }

private:
    void buildTree(DocumentNode *node, const toml::table &tbl);
    void buildFromArray(DocumentNode *node, const toml::array &arr);
    toml::table treeToToml(const DocumentNode *node) const;

    static QString tomlValueToString(const toml::node &val);

    std::unique_ptr<DocumentNode> m_root;
    QString m_rawText;
};

QString TomlEngine::tomlValueToString(const toml::node &val)
{
    if (val.is_string()) return QString::fromStdString(*val.value<std::string>());
    if (val.is_integer()) return QString::number(*val.value<int64_t>());
    if (val.is_floating_point()) return QString::number(*val.value<double>(), 'g', 15);
    if (val.is_boolean()) return *val.value<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    if (val.is_date()) {
        std::ostringstream ss; ss << *val.as_date();
        return QString::fromStdString(ss.str());
    }
    if (val.is_time()) {
        std::ostringstream ss; ss << *val.as_time();
        return QString::fromStdString(ss.str());
    }
    if (val.is_date_time()) {
        std::ostringstream ss; ss << *val.as_date_time();
        return QString::fromStdString(ss.str());
    }
    return QString();
}

void TomlEngine::buildTree(DocumentNode *node, const toml::table &tbl)
{
    QStringList scalarKeys;
    QStringList scalarValues;

    for (auto it = tbl.begin(); it != tbl.end(); ++it) {
        QString key = QString::fromStdString(std::string(it->first.str()));
        const toml::node &val = it->second;

        if (val.is_table()) {
            auto *child = node->addChild(key);
            buildTree(child, *val.as_table());
        } else if (val.is_array()) {
            const toml::array &arr = *val.as_array();
            // Check if it's an array of tables
            if (!arr.empty() && arr.front().is_table()) {
                auto *child = node->addChild(QStringLiteral("[[%1]]").arg(key));
                buildFromArray(child, arr);
            } else {
                // Simple array → display as comma-separated string
                QStringList items;
                for (size_t i = 0; i < arr.size(); ++i) {
                    const auto &elem = arr[i];
                    if (elem.is_string())
                        items << QString::fromStdString(*elem.value<std::string>());
                    else if (elem.is_integer())
                        items << QString::number(*elem.value<int64_t>());
                    else if (elem.is_floating_point())
                        items << QString::number(*elem.value<double>(), 'g', 15);
                    else if (elem.is_boolean())
                        items << (*elem.value<bool>() ? QStringLiteral("true") : QStringLiteral("false"));
                    else
                        items << QStringLiteral("?");
                }
                scalarKeys.append(key);
                scalarValues.append(QStringLiteral("[%1]").arg(items.join(QStringLiteral(", "))));
            }
        } else {
            scalarKeys.append(key);
            scalarValues.append(tomlValueToString(val));
        }
    }

    if (!scalarKeys.isEmpty()) {
        node->columnNames = {QStringLiteral("Key"), QStringLiteral("Value")};
        for (int i = 0; i < scalarKeys.size(); ++i) {
            node->rows.append({QVariant(scalarKeys[i]), QVariant(scalarValues[i])});
        }
    }
}

void TomlEngine::buildFromArray(DocumentNode *node, const toml::array &arr)
{
    // Array of tables → tabular grid
    QStringList columns;
    QSet<QString> seen;

    // Collect all keys
    for (size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].is_table()) continue;
        const toml::table &tbl = *arr[i].as_table();
        for (auto it = tbl.begin(); it != tbl.end(); ++it) {
            QString key = QString::fromStdString(std::string(it->first.str()));
            if (!it->second.is_table() && !it->second.is_array()) {
                if (!seen.contains(key)) {
                    seen.insert(key);
                    columns.append(key);
                }
            }
        }
    }

    if (!columns.isEmpty()) {
        node->columnNames = columns;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (!arr[i].is_table()) continue;
            const toml::table &tbl = *arr[i].as_table();
            QVector<QVariant> row;
            row.reserve(columns.size());
            for (const auto &col : columns) {
                auto val = tbl[col.toStdString()];
                if (val) {
                    // node_view → get the underlying node by reference
                    const toml::node &n = *val.node();
                    row.append(QVariant(tomlValueToString(n)));
                } else {
                    row.append(QVariant());
                }
            }
            node->rows.append(std::move(row));
        }
    }

    // Also add child nodes for tables that have nested tables
    for (size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].is_table()) continue;
        const toml::table &tbl = *arr[i].as_table();
        bool hasNested = false;
        for (auto it = tbl.begin(); it != tbl.end(); ++it) {
            if (it->second.is_table() || it->second.is_array()) {
                hasNested = true;
                break;
            }
        }
        if (hasNested) {
            auto *child = node->addChild(QStringLiteral("[%1]").arg(i));
            buildTree(child, tbl);
        }
    }
}

bool TomlEngine::parse(const QByteArray &data)
{
    try {
        m_rawText = QString::fromUtf8(data);

        std::string str = data.toStdString();
        toml::table tbl = toml::parse(str);

        m_root = std::make_unique<DocumentNode>(QStringLiteral("root"));
        buildTree(m_root.get(), tbl);
        return true;

    } catch (const toml::parse_error &) {
        return false;
    }
}

toml::table TomlEngine::treeToToml(const DocumentNode *node) const
{
    toml::table tbl;

    // Grid rows (Key | Value)
    if (node->columnNames.size() == 2
        && node->columnNames[0] == QStringLiteral("Key")) {
        for (const auto &row : node->rows) {
            std::string key = row[0].toString().toStdString();
            std::string val = row[1].toString().toStdString();
            // Try to preserve types
            if (val == "true") tbl.insert(key, true);
            else if (val == "false") tbl.insert(key, false);
            else {
                try {
                    size_t pos;
                    int64_t ival = std::stoll(val, &pos);
                    if (pos == val.size()) { tbl.insert(key, ival); continue; }
                } catch (...) {}
                try {
                    size_t pos;
                    double dval = std::stod(val, &pos);
                    if (pos == val.size()) { tbl.insert(key, dval); continue; }
                } catch (...) {}
                tbl.insert(key, val);
            }
        }
    }

    // Child nodes
    for (const auto *child : node->children) {
        std::string childKey = child->name.toStdString();
        // Strip [[ ]] for array of tables
        if (child->name.startsWith(QStringLiteral("[["))) {
            childKey = child->name.mid(2, child->name.size() - 4).toStdString();
            toml::array arr;
            // If child has tabular data, each row is a table
            if (!child->columnNames.isEmpty()) {
                for (const auto &row : child->rows) {
                    toml::table entry;
                    for (int c = 0; c < child->columnNames.size() && c < row.size(); ++c) {
                        entry.insert(child->columnNames[c].toStdString(),
                                     row[c].toString().toStdString());
                    }
                    arr.push_back(std::move(entry));
                }
            }
            tbl.insert(childKey, std::move(arr));
        } else {
            tbl.insert(childKey, treeToToml(child));
        }
    }

    return tbl;
}

QByteArray TomlEngine::serialize() const
{
    if (!m_root) return {};

    toml::table tbl = treeToToml(m_root.get());

    std::ostringstream ss;
    ss << toml::toml_formatter(tbl);
    return QByteArray::fromStdString(ss.str());
}

std::unique_ptr<TextFormatEngine> createTomlEngine()
{
    return std::make_unique<TomlEngine>();
}
