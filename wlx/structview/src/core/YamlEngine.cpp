#include "core/DocumentModel.h"

#include <yaml-cpp/yaml.h>
#include <set>
#include <sstream>

/// YAML engine: builds a document tree from YAML mappings/sequences
/// (mechanical port of the original off QString/QVariant onto std::string).
class YamlEngine : public TextFormatEngine {
public:
    bool parse(const std::string &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    std::string serialize() const override;
    std::string rawText() const override { return m_rawText; }
    std::string formatName() const override { return "YAML"; }

private:
    void buildTree(DocumentNode *node, const YAML::Node &yamlNode);
    YAML::Node treeToYaml(const DocumentNode *node) const;

    std::unique_ptr<DocumentNode> m_root;
    std::string m_rawText;
};

void YamlEngine::buildTree(DocumentNode *node, const YAML::Node &yamlNode)
{
    if (yamlNode.IsMap()) {
        std::vector<std::string> scalarKeys, scalarValues;
        std::vector<std::pair<std::string, YAML::Node>> containers;

        for (auto it = yamlNode.begin(); it != yamlNode.end(); ++it) {
            std::string key = it->first.as<std::string>();
            const YAML::Node &val = it->second;

            if (val.IsMap() || val.IsSequence()) {
                containers.push_back({key, val});
            } else {
                scalarKeys.push_back(key);
                scalarValues.push_back(val.IsNull() ? "null" : val.as<std::string>());
            }
        }

        if (!scalarKeys.empty()) {
            node->columnNames = {"Key", "Value"};
            for (size_t i = 0; i < scalarKeys.size(); ++i)
                node->rows.push_back({scalarKeys[i], scalarValues[i]});
        }

        for (auto &[key, val] : containers) {
            auto *child = node->addChild(key);
            buildTree(child, val);
        }

    } else if (yamlNode.IsSequence()) {
        bool allMaps = true;
        std::vector<std::string> columns;
        std::set<std::string> seen;

        for (size_t i = 0; i < yamlNode.size(); ++i) {
            if (!yamlNode[i].IsMap()) { allMaps = false; break; }
            for (auto it = yamlNode[i].begin(); it != yamlNode[i].end(); ++it) {
                std::string key = it->first.as<std::string>();
                if (!seen.count(key)) { seen.insert(key); columns.push_back(key); }
            }
        }

        if (allMaps && !columns.empty()) {
            node->columnNames = columns;
            for (size_t i = 0; i < yamlNode.size(); ++i) {
                std::vector<std::string> row;
                row.reserve(columns.size());
                for (auto &col : columns) {
                    auto val = yamlNode[i][col];
                    if (val && val.IsScalar()) row.push_back(val.as<std::string>());
                    else if (val && val.IsNull()) row.push_back("null");
                    else row.push_back("");
                }
                node->rows.push_back(std::move(row));
            }
        } else {
            bool allScalar = true;
            for (size_t i = 0; i < yamlNode.size(); ++i)
                if (!yamlNode[i].IsScalar() && !yamlNode[i].IsNull()) { allScalar = false; break; }

            if (allScalar) {
                node->columnNames = {"Value"};
                for (size_t i = 0; i < yamlNode.size(); ++i)
                    node->rows.push_back({yamlNode[i].IsNull() ? "null" : yamlNode[i].as<std::string>()});
            } else {
                for (size_t i = 0; i < yamlNode.size(); ++i) {
                    auto *child = node->addChild("[" + std::to_string(i) + "]");
                    buildTree(child, yamlNode[i]);
                }
            }
        }
    }
}

bool YamlEngine::parse(const std::string &data)
{
    try {
        m_rawText = data;
        YAML::Node yamlRoot = YAML::Load(data);
        if (yamlRoot.IsNull()) return false;

        m_root = std::make_unique<DocumentNode>("root");
        buildTree(m_root.get(), yamlRoot);
        return true;
    } catch (const YAML::Exception &) {
        return false;
    } catch (const std::exception &) {
        return false;
    }
}

YAML::Node YamlEngine::treeToYaml(const DocumentNode *node) const
{
    if (node->columnNames.size() == 2 && node->columnNames[0] == "Key") {
        YAML::Node map(YAML::NodeType::Map);
        for (auto &row : node->rows) map[row[0]] = row[1];
        for (auto *child : node->children) map[child->name] = treeToYaml(child);
        return map;
    }

    if (node->columnNames.size() == 1 && node->columnNames[0] == "Value") {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (auto &row : node->rows) seq.push_back(row[0]);
        return seq;
    }

    if (!node->columnNames.empty()) {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (auto &row : node->rows) {
            YAML::Node map(YAML::NodeType::Map);
            for (size_t c = 0; c < node->columnNames.size() && c < row.size(); ++c)
                map[node->columnNames[c]] = row[c];
            seq.push_back(map);
        }
        return seq;
    }

    if (!node->children.empty()) {
        bool isArray = !node->children[0]->name.empty() && node->children[0]->name[0] == '[';
        if (isArray) {
            YAML::Node seq(YAML::NodeType::Sequence);
            for (auto *child : node->children) seq.push_back(treeToYaml(child));
            return seq;
        } else {
            YAML::Node map(YAML::NodeType::Map);
            for (auto *child : node->children) map[child->name] = treeToYaml(child);
            return map;
        }
    }

    return YAML::Node();
}

std::string YamlEngine::serialize() const
{
    if (!m_root) return {};
    YAML::Node yamlRoot = treeToYaml(m_root.get());
    YAML::Emitter out;
    out << yamlRoot;
    return out.c_str();
}

std::unique_ptr<TextFormatEngine> createYamlEngine()
{
    return std::make_unique<YamlEngine>();
}
