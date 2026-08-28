#include "TextFormatEngine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

/// JSON engine: builds a document tree from JSON.
///
/// - Objects → child nodes for nested objects/arrays, grid rows for scalars
/// - Arrays of objects with shared keys → tabular grid on parent
/// - Arrays of primitives → 1-column Value grid
class JsonEngine : public TextFormatEngine {
public:
    bool parse(const QByteArray &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    QByteArray serialize() const override;
    QString rawText() const override { return m_rawText; }
    QString formatName() const override { return QStringLiteral("JSON"); }

    /// Also used by CborEngine
    bool parseFromJson(const QJsonValue &root, const QString &rootName);

private:
    void buildTree(DocumentNode *node, const QJsonValue &value);
    QJsonValue treeToJson(const DocumentNode *node) const;

    /// Check if a JSON array is "tabular" (array of objects with shared keys)
    static bool isTabularArray(const QJsonArray &arr, QStringList &columns);

    /// Convert a QJsonValue to a display string
    static QString valueToString(const QJsonValue &v);

    std::unique_ptr<DocumentNode> m_root;
    QString m_rawText;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString JsonEngine::valueToString(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 15);
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Null:   return QStringLiteral("null");
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    default: return QString();
    }
}

bool JsonEngine::isTabularArray(const QJsonArray &arr, QStringList &columns)
{
    if (arr.isEmpty()) return false;

    // All elements must be objects
    for (const auto &elem : arr) {
        if (!elem.isObject()) return false;
    }

    // Collect union of keys preserving first-seen order
    QSet<QString> seen;
    for (const auto &elem : arr) {
        QJsonObject obj = elem.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (!seen.contains(it.key())) {
                seen.insert(it.key());
                columns.append(it.key());
            }
        }
    }

    return columns.size() >= 1;
}

// ---------------------------------------------------------------------------
// Tree building
// ---------------------------------------------------------------------------

void JsonEngine::buildTree(DocumentNode *node, const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();

        // Separate scalars (grid rows) from containers (child nodes)
        QStringList scalarKeys;
        QList<QPair<QString, QJsonValue>> containers;

        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.value().isObject() || it.value().isArray()) {
                containers.append({it.key(), it.value()});
            } else {
                scalarKeys.append(it.key());
            }
        }

        // Scalars → 2-column grid: Key | Value
        if (!scalarKeys.isEmpty()) {
            node->columnNames = {QStringLiteral("Key"), QStringLiteral("Value")};
            for (const auto &key : scalarKeys) {
                node->rows.append({QVariant(key), QVariant(valueToString(obj[key]))});
            }
        }

        // Containers → child nodes
        for (const auto &[key, val] : containers) {
            auto *child = node->addChild(key);
            buildTree(child, val);
        }

    } else if (value.isArray()) {
        QJsonArray arr = value.toArray();
        QStringList tabularCols;

        if (isTabularArray(arr, tabularCols)) {
            // Tabular: display as multi-column grid
            node->columnNames = tabularCols;
            for (const auto &elem : arr) {
                QJsonObject obj = elem.toObject();
                QVector<QVariant> row;
                row.reserve(tabularCols.size());
                for (const auto &col : tabularCols) {
                    row.append(QVariant(valueToString(obj[col])));
                }
                node->rows.append(std::move(row));
            }

            // But also check for nested containers in each object
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr[i].toObject();
                bool hasContainers = false;
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    if (it.value().isObject() || it.value().isArray()) {
                        hasContainers = true;
                        break;
                    }
                }
                if (hasContainers) {
                    auto *child = node->addChild(QStringLiteral("[%1]").arg(i));
                    buildTree(child, arr[i]);
                }
            }
        } else {
            // Non-tabular: individual items
            if (arr.isEmpty()) return;

            // Check if all elements are primitives
            bool allPrimitive = true;
            for (const auto &elem : arr) {
                if (elem.isObject() || elem.isArray()) {
                    allPrimitive = false;
                    break;
                }
            }

            if (allPrimitive) {
                // Value grid
                node->columnNames = {QStringLiteral("Value")};
                for (int i = 0; i < arr.size(); ++i) {
                    node->rows.append({QVariant(valueToString(arr[i]))});
                }
            } else {
                // Mixed: child nodes for each element
                for (int i = 0; i < arr.size(); ++i) {
                    auto *child = node->addChild(QStringLiteral("[%1]").arg(i));
                    buildTree(child, arr[i]);
                }
            }
        }
    }
    // Scalars at root level are handled by the parent
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

bool JsonEngine::parse(const QByteArray &data)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
        return false;

    m_rawText = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

    QJsonValue root;
    QString rootName = QStringLiteral("root");
    if (doc.isObject()) {
        root = doc.object();
    } else if (doc.isArray()) {
        root = doc.array();
    } else {
        return false;
    }

    return parseFromJson(root, rootName);
}

bool JsonEngine::parseFromJson(const QJsonValue &root, const QString &rootName)
{
    m_root = std::make_unique<DocumentNode>(rootName);
    buildTree(m_root.get(), root);
    return true;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

QJsonValue JsonEngine::treeToJson(const DocumentNode *node) const
{
    // If node has child nodes, it's a container
    if (!node->children.isEmpty()) {
        // Check if children are array-indexed
        bool isArray = !node->children.isEmpty()
            && node->children[0]->name.startsWith('[');

        if (isArray) {
            QJsonArray arr;
            for (const auto *child : node->children) {
                arr.append(treeToJson(child));
            }
            return arr;
        } else {
            QJsonObject obj;
            // First add grid rows (Key | Value scalars)
            if (node->columnNames.size() == 2
                && node->columnNames[0] == QStringLiteral("Key")) {
                for (const auto &row : node->rows) {
                    QString key = row[0].toString();
                    QString val = row[1].toString();
                    // Try to preserve types
                    if (val == QStringLiteral("null"))
                        obj[key] = QJsonValue::Null;
                    else if (val == QStringLiteral("true"))
                        obj[key] = true;
                    else if (val == QStringLiteral("false"))
                        obj[key] = false;
                    else {
                        bool ok;
                        double num = val.toDouble(&ok);
                        if (ok) obj[key] = num;
                        else obj[key] = val;
                    }
                }
            }
            // Then add child containers
            for (const auto *child : node->children) {
                obj[child->name] = treeToJson(child);
            }
            return obj;
        }
    }

    // Leaf node with tabular grid → array of objects
    bool isTabular = false;
    if (node->columnNames.size() > 2) {
        isTabular = true;
    } else if (node->columnNames.size() == 2
               && node->columnNames[0] != QStringLiteral("Key")
               && node->columnNames[0] != QStringLiteral("Index")) {
        isTabular = true;
    } else if (node->columnNames.size() == 1
               && node->columnNames[0] != QStringLiteral("Value")) {
        isTabular = true;
    }

    if (isTabular) {
        QJsonArray arr;
        for (const auto &row : node->rows) {
            QJsonObject obj;
            for (int c = 0; c < node->columnNames.size() && c < row.size(); ++c) {
                QString val = row[c].toString();
                if (val == QStringLiteral("null"))
                    obj[node->columnNames[c]] = QJsonValue::Null;
                else if (val == QStringLiteral("true"))
                    obj[node->columnNames[c]] = true;
                else if (val == QStringLiteral("false"))
                    obj[node->columnNames[c]] = false;
                else {
                    bool ok;
                    double num = val.toDouble(&ok);
                    if (ok) obj[node->columnNames[c]] = num;
                    else obj[node->columnNames[c]] = val;
                }
            }
            arr.append(obj);
        }
        return arr;
    }

    // Key | Value → object
    if (node->columnNames.size() == 2
        && node->columnNames[0] == QStringLiteral("Key")) {
        QJsonObject obj;
        for (const auto &row : node->rows) {
            QString key = row[0].toString();
            QString val = row[1].toString();
            if (val == QStringLiteral("null"))
                obj[key] = QJsonValue::Null;
            else if (val == QStringLiteral("true"))
                obj[key] = true;
            else if (val == QStringLiteral("false"))
                obj[key] = false;
            else {
                bool ok;
                double num = val.toDouble(&ok);
                if (ok) obj[key] = num;
                else obj[key] = val;
            }
        }
        return obj;
    }

    // Value only → array
    if (node->columnNames.size() == 1
        && node->columnNames[0] == QStringLiteral("Value")) {
        QJsonArray arr;
        for (const auto &row : node->rows) {
            QString val = row[0].toString();
            if (val == QStringLiteral("null"))
                arr.append(QJsonValue::Null);
            else if (val == QStringLiteral("true"))
                arr.append(true);
            else if (val == QStringLiteral("false"))
                arr.append(false);
            else {
                bool ok;
                double num = val.toDouble(&ok);
                if (ok) arr.append(num);
                else arr.append(val);
            }
        }
        return arr;
    }

    return QJsonValue();
}

QByteArray JsonEngine::serialize() const
{
    if (!m_root) return {};

    QJsonValue val = treeToJson(m_root.get());
    QJsonDocument doc;
    if (val.isObject())
        doc = QJsonDocument(val.toObject());
    else if (val.isArray())
        doc = QJsonDocument(val.toArray());

    return doc.toJson(QJsonDocument::Indented);
}

// Factory helper
std::unique_ptr<TextFormatEngine> createJsonEngine()
{
    return std::make_unique<JsonEngine>();
}
