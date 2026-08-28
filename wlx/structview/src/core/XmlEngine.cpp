#include "core/DocumentModel.h"

#include <tinyxml2.h>
#include <map>
#include <set>
#include <sstream>

using namespace tinyxml2;

/// XML engine: builds a document tree from the DOM (direct port of the
/// original QDomDocument-based engine onto tinyxml2).
class XmlEngine : public TextFormatEngine {
public:
    bool parse(const std::string &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    std::string serialize() const override;
    std::string rawText() const override { return m_rawText; }
    std::string formatName() const override { return "XML"; }

private:
    void buildTree(DocumentNode *node, const XMLElement *elem);
    XMLElement *treeToXml(XMLDocument &doc, const DocumentNode *node) const;

    std::unique_ptr<DocumentNode> m_root;
    std::string m_rawText;
};

namespace {
std::string elemText(const XMLElement *el) {
    const char *t = el->GetText();
    return t ? t : "";
}
std::string firstColSegment(const std::string &name) {
    auto pos = name.find('[');
    return pos == std::string::npos ? name : name.substr(0, pos);
}
}

void XmlEngine::buildTree(DocumentNode *node, const XMLElement *elem)
{
    std::map<std::string, int> tagCounts;
    std::map<std::string, std::vector<const XMLElement *>> tagElements;
    std::vector<std::string> tagOrder;

    for (const XMLElement *c = elem->FirstChildElement(); c; c = c->NextSiblingElement()) {
        std::string tag = c->Name();
        if (!tagCounts.count(tag)) tagOrder.push_back(tag);
        tagCounts[tag]++;
        tagElements[tag].push_back(c);
    }

    std::string tabularTag;
    int maxCount = 0;
    for (auto &kv : tagCounts) {
        if (kv.second > maxCount) { maxCount = kv.second; tabularTag = kv.first; }
    }

    if (maxCount > 1) {
        std::vector<std::string> columns;
        std::set<std::string> seen;
        auto &elems = tagElements[tabularTag];

        for (auto *rowEl : elems) {
            for (const XMLAttribute *a = rowEl->FirstAttribute(); a; a = a->Next()) {
                std::string attrName = "@" + std::string(a->Name());
                if (!seen.count(attrName)) { seen.insert(attrName); columns.push_back(attrName); }
            }
            for (const XMLElement *c = rowEl->FirstChildElement(); c; c = c->NextSiblingElement()) {
                std::string tag = c->Name();
                if (!seen.count(tag)) { seen.insert(tag); columns.push_back(tag); }
            }
        }

        node->columnNames = columns;
        for (auto *rowEl : elems) {
            std::vector<std::string> row;
            row.reserve(columns.size());
            for (auto &col : columns) {
                if (!col.empty() && col[0] == '@') {
                    const char *v = rowEl->Attribute(col.c_str() + 1);
                    row.push_back(v ? v : "");
                } else {
                    const XMLElement *sub = rowEl->FirstChildElement(col.c_str());
                    row.push_back(sub ? elemText(sub) : "");
                }
            }
            node->rows.push_back(std::move(row));
        }

        for (size_t i = 0; i < elems.size(); ++i) {
            const XMLElement *rowEl = elems[i];
            bool hasNestedElements = false;
            for (const XMLElement *c = rowEl->FirstChildElement(); c; c = c->NextSiblingElement()) {
                if (c->FirstChildElement()) { hasNestedElements = true; break; }
            }
            if (hasNestedElements) {
                auto *child = node->addChild(tabularTag + "[" + std::to_string(i) + "]");
                buildTree(child, rowEl);
            }
        }

        for (auto &tag : tagOrder) {
            if (tag == tabularTag) continue;
            for (auto *el : tagElements[tag]) {
                auto *child = node->addChild(el->Name());
                buildTree(child, el);
            }
        }
    } else {
        std::vector<std::string> scalarNames, scalarValues;

        for (const XMLElement *c = elem->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (c->FirstChildElement()) {
                auto *childNode = node->addChild(c->Name());
                buildTree(childNode, c);
            } else {
                scalarNames.push_back(c->Name());
                scalarValues.push_back(elemText(c));
            }
        }

        // Attributes prepended (matches original: attrs.prepend()).
        std::vector<std::pair<std::string, std::string>> attrs;
        for (const XMLAttribute *a = elem->FirstAttribute(); a; a = a->Next())
            attrs.push_back({"@" + std::string(a->Name()), a->Value() ? a->Value() : ""});
        for (auto it = attrs.rbegin(); it != attrs.rend(); ++it) {
            scalarNames.insert(scalarNames.begin(), it->first);
            scalarValues.insert(scalarValues.begin(), it->second);
        }

        if (!scalarNames.empty()) {
            node->columnNames = {"Name", "Value"};
            for (size_t i = 0; i < scalarNames.size(); ++i)
                node->rows.push_back({scalarNames[i], scalarValues[i]});
        }
    }
}

bool XmlEngine::parse(const std::string &data)
{
    auto doc = std::make_unique<XMLDocument>();
    if (doc->Parse(data.c_str(), data.size()) != XML_SUCCESS) return false;

    XMLPrinter printer;
    doc->Print(&printer);
    m_rawText = printer.CStr();

    const XMLElement *rootEl = doc->RootElement();
    if (!rootEl) return false;

    m_root = std::make_unique<DocumentNode>(rootEl->Name());
    buildTree(m_root.get(), rootEl);
    return true;
}

XMLElement *XmlEngine::treeToXml(XMLDocument &doc, const DocumentNode *node) const
{
    XMLElement *elem = doc.NewElement(firstColSegment(node->name).c_str());

    if (node->columnNames.size() == 2 && node->columnNames[0] == "Name") {
        for (auto &row : node->rows) {
            const std::string &name = row[0];
            const std::string &value = row[1];
            if (!name.empty() && name[0] == '@') {
                elem->SetAttribute(name.c_str() + 1, value.c_str());
            } else {
                XMLElement *child = doc.NewElement(name.c_str());
                child->SetText(value.c_str());
                elem->InsertEndChild(child);
            }
        }
    } else if (!node->columnNames.empty()) {
        std::string rowTag = node->name;
        for (auto &row : node->rows) {
            XMLElement *rowElem = doc.NewElement(rowTag.c_str());
            for (size_t c = 0; c < node->columnNames.size() && c < row.size(); ++c) {
                const std::string &col = node->columnNames[c];
                const std::string &val = row[c];
                if (!col.empty() && col[0] == '@') {
                    if (!val.empty()) rowElem->SetAttribute(col.c_str() + 1, val.c_str());
                } else {
                    XMLElement *child = doc.NewElement(col.c_str());
                    child->SetText(val.c_str());
                    rowElem->InsertEndChild(child);
                }
            }
            elem->InsertEndChild(rowElem);
        }
    }

    for (auto *child : node->children)
        elem->InsertEndChild(treeToXml(doc, child));

    return elem;
}

std::string XmlEngine::serialize() const
{
    if (!m_root) return {};

    XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());
    doc.InsertEndChild(treeToXml(doc, m_root.get()));

    XMLPrinter printer;
    doc.Print(&printer);
    return printer.CStr();
}

std::unique_ptr<TextFormatEngine> createXmlEngine()
{
    return std::make_unique<XmlEngine>();
}
