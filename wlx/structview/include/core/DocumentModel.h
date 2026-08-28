#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

/// Toolkit-neutral document tree + format-engine interface, extracted out
/// of what used to be a Qt-only (QString/QVariant/QVector) TextFormatEngine.h.
/// No Qt, no GTK -- the UI layer (src/qt6/, src/gtk3/) owns the tree/grid
/// widgets and just walks this tree. Every cell value is stored pre-
/// stringified (mirrors the original: every engine immediately converted
/// scalars to display strings via a valueToString()-style helper before
/// putting them in a QVariant), so rows are plain strings, not a variant.

/// A node in the document tree. Owns its children (matches the original's
/// QList<DocumentNode*> + qDeleteAll(children) ownership).
class DocumentNode {
public:
    explicit DocumentNode(const std::string &name, DocumentNode *parent = nullptr)
        : name(name), parent(parent) {}

    ~DocumentNode() { for (auto *c : children) delete c; }

    DocumentNode(const DocumentNode &) = delete;
    DocumentNode &operator=(const DocumentNode &) = delete;

    std::string name;
    DocumentNode *parent = nullptr;
    std::vector<DocumentNode *> children;

    std::vector<std::string> columnNames;
    std::vector<std::vector<std::string>> rows;

    bool editable = true;

    DocumentNode *addChild(const std::string &childName) {
        auto *child = new DocumentNode(childName, this);
        children.push_back(child);
        return child;
    }

    void removeChild(int index) {
        if (index >= 0 && index < (int)children.size()) {
            delete children[index];
            children.erase(children.begin() + index);
        }
    }

    int childIndex() const {
        if (!parent) return -1;
        auto it = std::find(parent->children.begin(), parent->children.end(), this);
        return it == parent->children.end() ? -1 : (int)(it - parent->children.begin());
    }

    bool isLeaf() const { return children.empty() && rows.empty(); }
    bool isContainer() const { return !children.empty(); }
};

/// Abstract base class for structured text format engines.
class TextFormatEngine {
public:
    virtual ~TextFormatEngine() = default;

    virtual bool parse(const std::string &data) = 0;
    virtual DocumentNode *rootNode() const = 0;
    virtual std::string serialize() const = 0;
    virtual std::string rawText() const = 0;
    virtual std::string formatName() const = 0;

    /// Factory: detect format from file extension and return the right engine.
    static std::unique_ptr<TextFormatEngine> createForFile(const std::string &filepath);
};
