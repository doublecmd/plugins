#include "TextFormatEngine.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QSet>
#include <QMap>

/// XML engine: builds a document tree from the DOM.
///
/// - Each element → tree node
/// - Repeating children with same tag → tabular grid on parent (attributes as @columns)
/// - Non-repeating children with text → 2-column Name | Value grid
class XmlEngine : public TextFormatEngine {
public:
    bool parse(const QByteArray &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    QByteArray serialize() const override;
    QString rawText() const override { return m_rawText; }
    QString formatName() const override { return QStringLiteral("XML"); }

private:
    void buildTree(DocumentNode *node, const QDomElement &elem);
    QDomElement treeToXml(QDomDocument &doc, const DocumentNode *node) const;

    std::unique_ptr<DocumentNode> m_root;
    QString m_rawText;
    QDomDocument m_originalDoc;
};

void XmlEngine::buildTree(DocumentNode *node, const QDomElement &elem)
{
    // Count child element tag frequencies
    QMap<QString, int> tagCounts;
    QMap<QString, QList<QDomElement>> tagElements;
    QDomNodeList children = elem.childNodes();

    for (int i = 0; i < children.count(); ++i) {
        QDomNode child = children.at(i);
        if (child.isElement()) {
            QString tag = child.toElement().tagName();
            tagCounts[tag]++;
            tagElements[tag].append(child.toElement());
        }
    }

    // Find repeating tags (tabular data)
    QString tabularTag;
    int maxCount = 0;
    for (auto it = tagCounts.begin(); it != tagCounts.end(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            tabularTag = it.key();
        }
    }

    if (maxCount > 1) {
        // Repeating elements → tabular grid
        QStringList columns;
        QSet<QString> seen;
        const auto &elems = tagElements[tabularTag];

        for (const auto &rowEl : elems) {
            // Attributes (prefixed with @)
            QDomNamedNodeMap attrs = rowEl.attributes();
            for (int a = 0; a < attrs.count(); ++a) {
                QString attrName = QStringLiteral("@") + attrs.item(a).toAttr().name();
                if (!seen.contains(attrName)) {
                    seen.insert(attrName);
                    columns.append(attrName);
                }
            }
            // Child element text values
            QDomNodeList rowChildren = rowEl.childNodes();
            for (int j = 0; j < rowChildren.count(); ++j) {
                if (rowChildren.at(j).isElement()) {
                    QString tag = rowChildren.at(j).toElement().tagName();
                    if (!seen.contains(tag)) {
                        seen.insert(tag);
                        columns.append(tag);
                    }
                }
            }
        }

        node->columnNames = columns;
        for (const auto &rowEl : elems) {
            QVector<QVariant> row;
            row.reserve(columns.size());
            for (const auto &col : columns) {
                if (col.startsWith('@')) {
                    row.append(QVariant(rowEl.attribute(col.mid(1))));
                } else {
                    QDomElement sub = rowEl.firstChildElement(col);
                    row.append(QVariant(sub.isNull() ? QString() : sub.text()));
                }
            }
            node->rows.append(std::move(row));
        }

        // Also create child nodes for each repeating element (for deep navigation)
        for (int i = 0; i < elems.size(); ++i) {
            const auto &rowEl = elems[i];
            // Check if element has nested elements (not just text children)
            bool hasNestedElements = false;
            QDomNodeList rowChildren = rowEl.childNodes();
            for (int j = 0; j < rowChildren.count(); ++j) {
                if (rowChildren.at(j).isElement()) {
                    QDomElement childEl = rowChildren.at(j).toElement();
                    // Check if the child itself has children (deep nesting)
                    if (childEl.hasChildNodes() && !childEl.firstChildElement().isNull()) {
                        hasNestedElements = true;
                        break;
                    }
                }
            }
            if (hasNestedElements) {
                auto *child = node->addChild(
                    QStringLiteral("%1[%2]").arg(tabularTag).arg(i));
                buildTree(child, rowEl);
            }
        }

        // Non-repeating elements → also add as child nodes
        for (auto it = tagElements.begin(); it != tagElements.end(); ++it) {
            if (it.key() == tabularTag) continue;
            for (const auto &el : it.value()) {
                auto *child = node->addChild(el.tagName());
                buildTree(child, el);
            }
        }
    } else {
        // No repeating elements → either leaf text or mixed content
        // Collect all child elements as Key | Value pairs or child nodes
        QStringList scalarNames;
        QStringList scalarValues;

        for (int i = 0; i < children.count(); ++i) {
            QDomNode child = children.at(i);
            if (!child.isElement()) continue;

            QDomElement childEl = child.toElement();

            // If element has child elements itself, make it a tree node
            if (!childEl.firstChildElement().isNull()) {
                auto *childNode = node->addChild(childEl.tagName());
                buildTree(childNode, childEl);
            } else {
                // Leaf element → grid row
                scalarNames.append(childEl.tagName());
                scalarValues.append(childEl.text());
            }
        }

        // Add attributes to grid too
        QDomNamedNodeMap attrs = elem.attributes();
        for (int a = 0; a < attrs.count(); ++a) {
            scalarNames.prepend(QStringLiteral("@") + attrs.item(a).toAttr().name());
            scalarValues.prepend(attrs.item(a).toAttr().value());
        }

        if (!scalarNames.isEmpty()) {
            node->columnNames = {QStringLiteral("Name"), QStringLiteral("Value")};
            for (int i = 0; i < scalarNames.size(); ++i) {
                node->rows.append({QVariant(scalarNames[i]), QVariant(scalarValues[i])});
            }
        }
    }
}

bool XmlEngine::parse(const QByteArray &data)
{
    QString errorMsg;
    int errorLine, errorCol;
    if (!m_originalDoc.setContent(data, &errorMsg, &errorLine, &errorCol))
        return false;

    m_rawText = m_originalDoc.toString(2);

    QDomElement rootEl = m_originalDoc.documentElement();
    if (rootEl.isNull()) return false;

    m_root = std::make_unique<DocumentNode>(rootEl.tagName());
    buildTree(m_root.get(), rootEl);

    return true;
}

QDomElement XmlEngine::treeToXml(QDomDocument &doc, const DocumentNode *node) const
{
    QDomElement elem = doc.createElement(node->name.contains('[')
        ? node->name.section('[', 0, 0) : node->name);

    // Grid data: Name | Value → child elements or attributes
    if (node->columnNames.size() == 2
        && node->columnNames[0] == QStringLiteral("Name")) {
        for (const auto &row : node->rows) {
            QString name = row[0].toString();
            QString value = row[1].toString();
            if (name.startsWith('@')) {
                elem.setAttribute(name.mid(1), value);
            } else {
                QDomElement child = doc.createElement(name);
                child.appendChild(doc.createTextNode(value));
                elem.appendChild(child);
            }
        }
    }
    // Tabular grid → repeating child elements
    else if (!node->columnNames.isEmpty()) {
        // Determine the tag name from the first column or node context
        QString rowTag = node->name;
        for (const auto &row : node->rows) {
            QDomElement rowElem = doc.createElement(rowTag);
            for (int c = 0; c < node->columnNames.size() && c < row.size(); ++c) {
                QString col = node->columnNames[c];
                QString val = row[c].toString();
                if (col.startsWith('@')) {
                    if (!val.isEmpty())
                        rowElem.setAttribute(col.mid(1), val);
                } else {
                    QDomElement child = doc.createElement(col);
                    child.appendChild(doc.createTextNode(val));
                    rowElem.appendChild(child);
                }
            }
            elem.appendChild(rowElem);
        }
    }

    // Child nodes
    for (const auto *child : node->children) {
        elem.appendChild(treeToXml(doc, child));
    }

    return elem;
}

QByteArray XmlEngine::serialize() const
{
    if (!m_root) return {};

    QDomDocument doc;
    QDomProcessingInstruction pi = doc.createProcessingInstruction(
        QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\""));
    doc.appendChild(pi);
    doc.appendChild(treeToXml(doc, m_root.get()));

    return doc.toByteArray(2);
}

std::unique_ptr<TextFormatEngine> createXmlEngine()
{
    return std::make_unique<XmlEngine>();
}
