#include "core/DocumentModel.h"

#define TOML_IMPLEMENTATION
#include <toml++/toml.hpp>

#include <set>
#include <sstream>

/// TOML engine: builds a document tree from TOML tables (mechanical port
/// of the original off QString/QVariant onto std::string).
class TomlEngine : public TextFormatEngine {
public:
    bool parse(const std::string &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    std::string serialize() const override;
    std::string rawText() const override { return m_rawText; }
    std::string formatName() const override { return "TOML"; }

private:
    void buildTree(DocumentNode *node, const toml::table &tbl);
    void buildFromArray(DocumentNode *node, const toml::array &arr);
    toml::table treeToToml(const DocumentNode *node) const;
    static std::string tomlValueToString(const toml::node &val);

    std::unique_ptr<DocumentNode> m_root;
    std::string m_rawText;
};

std::string TomlEngine::tomlValueToString(const toml::node &val)
{
    if (val.is_string()) return *val.value<std::string>();
    if (val.is_integer()) return std::to_string(*val.value<int64_t>());
    if (val.is_floating_point()) {
        std::ostringstream ss; ss.precision(15); ss << *val.value<double>();
        return ss.str();
    }
    if (val.is_boolean()) return *val.value<bool>() ? "true" : "false";
    if (val.is_date()) { std::ostringstream ss; ss << *val.as_date(); return ss.str(); }
    if (val.is_time()) { std::ostringstream ss; ss << *val.as_time(); return ss.str(); }
    if (val.is_date_time()) { std::ostringstream ss; ss << *val.as_date_time(); return ss.str(); }
    return "";
}

void TomlEngine::buildTree(DocumentNode *node, const toml::table &tbl)
{
    std::vector<std::string> scalarKeys, scalarValues;

    for (auto it = tbl.begin(); it != tbl.end(); ++it) {
        std::string key(it->first.str());
        const toml::node &val = it->second;

        if (val.is_table()) {
            auto *child = node->addChild(key);
            buildTree(child, *val.as_table());
        } else if (val.is_array()) {
            const toml::array &arr = *val.as_array();
            if (!arr.empty() && arr.front().is_table()) {
                auto *child = node->addChild("[[" + key + "]]");
                buildFromArray(child, arr);
            } else {
                std::string items;
                for (size_t i = 0; i < arr.size(); ++i) {
                    const auto &elem = arr[i];
                    std::string s;
                    if (elem.is_string()) s = *elem.value<std::string>();
                    else if (elem.is_integer()) s = std::to_string(*elem.value<int64_t>());
                    else if (elem.is_floating_point()) { std::ostringstream ss; ss.precision(15); ss << *elem.value<double>(); s = ss.str(); }
                    else if (elem.is_boolean()) s = *elem.value<bool>() ? "true" : "false";
                    else s = "?";
                    if (i) items += ", ";
                    items += s;
                }
                scalarKeys.push_back(key);
                scalarValues.push_back("[" + items + "]");
            }
        } else {
            scalarKeys.push_back(key);
            scalarValues.push_back(tomlValueToString(val));
        }
    }

    if (!scalarKeys.empty()) {
        node->columnNames = {"Key", "Value"};
        for (size_t i = 0; i < scalarKeys.size(); ++i)
            node->rows.push_back({scalarKeys[i], scalarValues[i]});
    }
}

void TomlEngine::buildFromArray(DocumentNode *node, const toml::array &arr)
{
    std::vector<std::string> columns;
    std::set<std::string> seen;

    for (size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].is_table()) continue;
        const toml::table &tbl = *arr[i].as_table();
        for (auto it = tbl.begin(); it != tbl.end(); ++it) {
            std::string key(it->first.str());
            if (!it->second.is_table() && !it->second.is_array()) {
                if (!seen.count(key)) { seen.insert(key); columns.push_back(key); }
            }
        }
    }

    if (!columns.empty()) {
        node->columnNames = columns;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (!arr[i].is_table()) continue;
            const toml::table &tbl = *arr[i].as_table();
            std::vector<std::string> row;
            row.reserve(columns.size());
            for (auto &col : columns) {
                auto val = tbl[col];
                if (val) row.push_back(tomlValueToString(*val.node()));
                else row.push_back("");
            }
            node->rows.push_back(std::move(row));
        }
    }

    for (size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].is_table()) continue;
        const toml::table &tbl = *arr[i].as_table();
        bool hasNested = false;
        for (auto it = tbl.begin(); it != tbl.end(); ++it)
            if (it->second.is_table() || it->second.is_array()) { hasNested = true; break; }
        if (hasNested) {
            auto *child = node->addChild("[" + std::to_string(i) + "]");
            buildTree(child, tbl);
        }
    }
}

bool TomlEngine::parse(const std::string &data)
{
    try {
        m_rawText = data;
        toml::table tbl = toml::parse(data);
        m_root = std::make_unique<DocumentNode>("root");
        buildTree(m_root.get(), tbl);
        return true;
    } catch (const toml::parse_error &) {
        return false;
    }
}

toml::table TomlEngine::treeToToml(const DocumentNode *node) const
{
    toml::table tbl;

    if (node->columnNames.size() == 2 && node->columnNames[0] == "Key") {
        for (auto &row : node->rows) {
            const std::string &key = row[0];
            const std::string &val = row[1];
            if (val == "true") { tbl.insert(key, true); continue; }
            if (val == "false") { tbl.insert(key, false); continue; }
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

    for (auto *child : node->children) {
        std::string childKey = child->name;
        if (child->name.rfind("[[", 0) == 0) {
            childKey = child->name.substr(2, child->name.size() - 4);
            toml::array arr;
            if (!child->columnNames.empty()) {
                for (auto &row : child->rows) {
                    toml::table entry;
                    for (size_t c = 0; c < child->columnNames.size() && c < row.size(); ++c)
                        entry.insert(child->columnNames[c], row[c]);
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

std::string TomlEngine::serialize() const
{
    if (!m_root) return {};
    toml::table tbl = treeToToml(m_root.get());
    std::ostringstream ss;
    ss << toml::toml_formatter(tbl);
    return ss.str();
}

std::unique_ptr<TextFormatEngine> createTomlEngine()
{
    return std::make_unique<TomlEngine>();
}
