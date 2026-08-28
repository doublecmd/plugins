#include "core/DocumentModel.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <set>

using json = nlohmann::json;

/// JSON engine: builds a document tree from JSON (direct port of the
/// original QJsonValue-based engine onto nlohmann::json).
class JsonEngine : public TextFormatEngine {
public:
    bool parse(const std::string &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    std::string serialize() const override;
    std::string rawText() const override { return m_rawText; }
    std::string formatName() const override { return "JSON"; }

    bool parseFromJson(const json &root, const std::string &rootName);

private:
    void buildTree(DocumentNode *node, const json &value);
    json treeToJson(const DocumentNode *node) const;
    static bool isTabularArray(const json &arr, std::vector<std::string> &columns);
    static std::string valueToString(const json &v);

    std::unique_ptr<DocumentNode> m_root;
    std::string m_rawText;
};

std::string JsonEngine::valueToString(const json &v)
{
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_float()) {
        std::ostringstream ss; ss.precision(15); ss << v.get<double>();
        return ss.str();
    }
    if (v.is_number_integer() || v.is_number_unsigned()) return v.dump();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_null()) return "null";
    if (v.is_object() || v.is_array()) return v.dump();
    return "";
}

bool JsonEngine::isTabularArray(const json &arr, std::vector<std::string> &columns)
{
    if (arr.empty()) return false;
    for (const auto &elem : arr)
        if (!elem.is_object()) return false;

    std::set<std::string> seen;
    for (const auto &elem : arr) {
        for (auto it = elem.begin(); it != elem.end(); ++it) {
            if (!seen.count(it.key())) { seen.insert(it.key()); columns.push_back(it.key()); }
        }
    }
    return !columns.empty();
}

void JsonEngine::buildTree(DocumentNode *node, const json &value)
{
    if (value.is_object()) {
        std::vector<std::string> scalarKeys;
        std::vector<std::pair<std::string, json>> containers;

        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.value().is_object() || it.value().is_array())
                containers.push_back({it.key(), it.value()});
            else
                scalarKeys.push_back(it.key());
        }

        if (!scalarKeys.empty()) {
            node->columnNames = {"Key", "Value"};
            for (auto &key : scalarKeys)
                node->rows.push_back({key, valueToString(value[key])});
        }

        for (auto &[key, val] : containers) {
            auto *child = node->addChild(key);
            buildTree(child, val);
        }

    } else if (value.is_array()) {
        std::vector<std::string> tabularCols;

        if (isTabularArray(value, tabularCols)) {
            node->columnNames = tabularCols;
            for (const auto &elem : value) {
                std::vector<std::string> row;
                row.reserve(tabularCols.size());
                for (auto &col : tabularCols)
                    row.push_back(elem.contains(col) ? valueToString(elem[col]) : "");
                node->rows.push_back(std::move(row));
            }

            for (size_t i = 0; i < value.size(); ++i) {
                const auto &elem = value[i];
                bool hasContainers = false;
                for (auto it = elem.begin(); it != elem.end(); ++it) {
                    if (it.value().is_object() || it.value().is_array()) { hasContainers = true; break; }
                }
                if (hasContainers) {
                    auto *child = node->addChild("[" + std::to_string(i) + "]");
                    buildTree(child, elem);
                }
            }
        } else {
            if (value.empty()) return;

            bool allPrimitive = true;
            for (const auto &elem : value)
                if (elem.is_object() || elem.is_array()) { allPrimitive = false; break; }

            if (allPrimitive) {
                node->columnNames = {"Value"};
                for (const auto &elem : value)
                    node->rows.push_back({valueToString(elem)});
            } else {
                for (size_t i = 0; i < value.size(); ++i) {
                    auto *child = node->addChild("[" + std::to_string(i) + "]");
                    buildTree(child, value[i]);
                }
            }
        }
    }
}

bool JsonEngine::parse(const std::string &data)
{
    json doc;
    try {
        doc = json::parse(data);
    } catch (const json::exception &) {
        return false;
    }
    m_rawText = doc.dump(2);

    if (!doc.is_object() && !doc.is_array()) return false;
    return parseFromJson(doc, "root");
}

bool JsonEngine::parseFromJson(const json &root, const std::string &rootName)
{
    m_root = std::make_unique<DocumentNode>(rootName);
    buildTree(m_root.get(), root);
    return true;
}

namespace {
bool tryParseScalar(const std::string &val, json &out)
{
    if (val == "null") { out = nullptr; return true; }
    if (val == "true") { out = true; return true; }
    if (val == "false") { out = false; return true; }
    try {
        size_t pos;
        double d = std::stod(val, &pos);
        if (pos == val.size()) { out = d; return true; }
    } catch (...) {}
    out = val;
    return true;
}
}

json JsonEngine::treeToJson(const DocumentNode *node) const
{
    if (!node->children.empty()) {
        bool isArray = !node->children.empty() && !node->children[0]->name.empty() &&
                       node->children[0]->name[0] == '[';

        if (isArray) {
            json arr = json::array();
            for (auto *child : node->children) arr.push_back(treeToJson(child));
            return arr;
        }

        json obj = json::object();
        if (node->columnNames.size() == 2 && node->columnNames[0] == "Key") {
            for (auto &row : node->rows) {
                json v; tryParseScalar(row[1], v);
                obj[row[0]] = v;
            }
        }
        for (auto *child : node->children)
            obj[child->name] = treeToJson(child);
        return obj;
    }

    bool isTabular = false;
    if (node->columnNames.size() > 2) isTabular = true;
    else if (node->columnNames.size() == 2 && node->columnNames[0] != "Key" && node->columnNames[0] != "Index") isTabular = true;
    else if (node->columnNames.size() == 1 && node->columnNames[0] != "Value") isTabular = true;

    if (isTabular) {
        json arr = json::array();
        for (auto &row : node->rows) {
            json obj = json::object();
            for (size_t c = 0; c < node->columnNames.size() && c < row.size(); ++c) {
                json v; tryParseScalar(row[c], v);
                obj[node->columnNames[c]] = v;
            }
            arr.push_back(obj);
        }
        return arr;
    }

    if (node->columnNames.size() == 2 && node->columnNames[0] == "Key") {
        json obj = json::object();
        for (auto &row : node->rows) {
            json v; tryParseScalar(row[1], v);
            obj[row[0]] = v;
        }
        return obj;
    }

    if (node->columnNames.size() == 1 && node->columnNames[0] == "Value") {
        json arr = json::array();
        for (auto &row : node->rows) {
            json v; tryParseScalar(row[0], v);
            arr.push_back(v);
        }
        return arr;
    }

    return json();
}

std::string JsonEngine::serialize() const
{
    if (!m_root) return {};
    json val = treeToJson(m_root.get());
    if (!val.is_object() && !val.is_array()) return {};
    return val.dump(2);
}

std::unique_ptr<TextFormatEngine> createJsonEngine()
{
    return std::make_unique<JsonEngine>();
}
