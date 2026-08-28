#include "TextFormatEngine.h"

#include <yaml-cpp/yaml.h>
#include <sstream>

/// YAML engine: builds a document tree from YAML mappings/sequences.
///
/// - Mapping → child nodes for nested mappings, grid rows for scalars
/// - Sequence → numbered children [0], [1], ...
/// - Scalar → leaf value in parent's grid
class YamlEngine : public TextFormatEngine {
public:
    bool parse(const QByteArray &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    QByteArray serialize() const override;
    QString rawText() const override { return m_rawText; }
    QString formatName() const override { return QStringLiteral("YAML"); }

private:
    void buildTree(DocumentNode *node, const YAML::Node &yamlNode);
    YAML::Node treeToYaml(const DocumentNode *node) const;

    std::unique_ptr<DocumentNode> m_root;
    QString m_rawText;
};

void YamlEngine::buildTree(DocumentNode *node, const YAML::Node &yamlNode)
{
    if (yamlNode.IsMap()) {
        QStringList scalarKeys;
        QStringList scalarValues;
        QList<QPair<QString, YAML::Node>> containers;

        for (auto it = yamlNode.begin(); it != yamlNode.end(); ++it) {
            QString key = QString::fromStdString(it->first.as<std::string>());
            const YAML::Node &val = it->second;

            if (val.IsMap() || val.IsSequence()) {
                containers.append({key, val});
            } else {
                scalarKeys.append(key);
                scalarValues.append(
                    val.IsNull() ? QStringLiteral("null")
                                 : QString::fromStdString(val.as<std::string>()));
            }
        }

        if (!scalarKeys.isEmpty()) {
            node->columnNames = {QStringLiteral("Key"), QStringLiteral("Value")};
            for (int i = 0; i < scalarKeys.size(); ++i) {
                node->rows.append({QVariant(scalarKeys[i]), QVariant(scalarValues[i])});
            }
        }

        for (const auto &[key, val] : containers) {
            auto *child = node->addChild(key);
            buildTree(child, val);
        }

    } else if (yamlNode.IsSequence()) {
        // Check if all elements are maps with shared keys (tabular)
        bool allMaps = true;
        QStringList columns;
        QSet<QString> seen;

        for (size_t i = 0; i < yamlNode.size(); ++i) {
            if (!yamlNode[i].IsMap()) {
                allMaps = false;
                break;
            }
            for (auto it = yamlNode[i].begin(); it != yamlNode[i].end(); ++it) {
                QString key = QString::fromStdString(it->first.as<std::string>());
                if (!seen.contains(key)) {
                    seen.insert(key);
                    columns.append(key);
                }
            }
        }

        if (allMaps && !columns.isEmpty()) {
            // Tabular grid
            node->columnNames = columns;
            for (size_t i = 0; i < yamlNode.size(); ++i) {
                QVector<QVariant> row;
                row.reserve(columns.size());
                for (const auto &col : columns) {
                    auto val = yamlNode[i][col.toStdString()];
                    if (val && val.IsScalar())
                        row.append(QVariant(QString::fromStdString(val.as<std::string>())));
                    else if (val && val.IsNull())
                        row.append(QVariant(QStringLiteral("null")));
                    else
                        row.append(QVariant());
                }
                node->rows.append(std::move(row));
            }
        } else {
            // Check if all scalars
            bool allScalar = true;
            for (size_t i = 0; i < yamlNode.size(); ++i) {
                if (!yamlNode[i].IsScalar() && !yamlNode[i].IsNull()) {
                    allScalar = false;
                    break;
                }
            }

            if (allScalar) {
                node->columnNames = {QStringLiteral("Value")};
                for (size_t i = 0; i < yamlNode.size(); ++i) {
                    QString val = yamlNode[i].IsNull()
                        ? QStringLiteral("null")
                        : QString::fromStdString(yamlNode[i].as<std::string>());
                    node->rows.append({QVariant(val)});
                }
            } else {
                // Mixed: numbered children
                for (size_t i = 0; i < yamlNode.size(); ++i) {
                    auto *child = node->addChild(QStringLiteral("[%1]").arg(i));
                    buildTree(child, yamlNode[i]);
                }
            }
        }
    }
}

bool YamlEngine::parse(const QByteArray &data)
{
    try {
        fprintf(stderr, "[structview/yaml] parse() called, data size=%d\n", data.size());
        m_rawText = QString::fromUtf8(data);

        YAML::Node yamlRoot = YAML::Load(data.toStdString());
        fprintf(stderr, "[structview/yaml] YAML::Load OK, type=%d IsNull=%d\n",
                static_cast<int>(yamlRoot.Type()), yamlRoot.IsNull());
        if (yamlRoot.IsNull()) return false;

        m_root = std::make_unique<DocumentNode>(QStringLiteral("root"));
        buildTree(m_root.get(), yamlRoot);
        fprintf(stderr, "[structview/yaml] parse() succeeded\n");
        return true;

    } catch (const YAML::Exception &e) {
        fprintf(stderr, "[structview/yaml] YAML::Exception: %s\n", e.what());
        return false;
    } catch (const std::exception &e) {
        fprintf(stderr, "[structview/yaml] std::exception: %s\n", e.what());
        return false;
    } catch (...) {
        fprintf(stderr, "[structview/yaml] unknown exception\n");
        return false;
    }
}

YAML::Node YamlEngine::treeToYaml(const DocumentNode *node) const
{
    // Key | Value grid → map
    if (node->columnNames.size() == 2
        && node->columnNames[0] == QStringLiteral("Key")) {
        YAML::Node map(YAML::NodeType::Map);
        for (const auto &row : node->rows) {
            std::string key = row[0].toString().toStdString();
            std::string val = row[1].toString().toStdString();
            map[key] = val;
        }
        // Add child containers
        for (const auto *child : node->children) {
            map[child->name.toStdString()] = treeToYaml(child);
        }
        return map;
    }

    // Value only → sequence
    if (node->columnNames.size() == 1
        && node->columnNames[0] == QStringLiteral("Value")) {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (const auto &row : node->rows) {
            seq.push_back(row[0].toString().toStdString());
        }
        return seq;
    }

    // Tabular → sequence of maps
    if (!node->columnNames.isEmpty()) {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (const auto &row : node->rows) {
            YAML::Node map(YAML::NodeType::Map);
            for (int c = 0; c < node->columnNames.size() && c < row.size(); ++c) {
                map[node->columnNames[c].toStdString()] = row[c].toString().toStdString();
            }
            seq.push_back(map);
        }
        return seq;
    }

    // Container with children only
    if (!node->children.isEmpty()) {
        bool isArray = node->children[0]->name.startsWith('[');
        if (isArray) {
            YAML::Node seq(YAML::NodeType::Sequence);
            for (const auto *child : node->children)
                seq.push_back(treeToYaml(child));
            return seq;
        } else {
            YAML::Node map(YAML::NodeType::Map);
            for (const auto *child : node->children)
                map[child->name.toStdString()] = treeToYaml(child);
            return map;
        }
    }

    return YAML::Node();
}

QByteArray YamlEngine::serialize() const
{
    if (!m_root) return {};

    YAML::Node yamlRoot = treeToYaml(m_root.get());

    YAML::Emitter out;
    out << yamlRoot;

    return QByteArray(out.c_str());
}

std::unique_ptr<TextFormatEngine> createYamlEngine()
{
    return std::make_unique<YamlEngine>();
}
