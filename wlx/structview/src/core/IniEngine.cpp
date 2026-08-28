#include "core/DocumentModel.h"

#include <sstream>

/// INI engine: sections as tree nodes, keys as 2-column grid. Hand-rolled
/// order-preserving parser (replaces QSettings, which doesn't guarantee
/// key/section order across Qt versions/platforms the way this does).
class IniEngine : public TextFormatEngine {
public:
    bool parse(const std::string &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    std::string serialize() const override;
    std::string rawText() const override { return m_rawText; }
    std::string formatName() const override { return "INI"; }

private:
    std::unique_ptr<DocumentNode> m_root;
    std::string m_rawText;
};

namespace {
std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}

bool IniEngine::parse(const std::string &data)
{
    m_rawText = data;
    m_root = std::make_unique<DocumentNode>("root");

    DocumentNode *general = nullptr;
    DocumentNode *current = nullptr;

    std::istringstream in(data);
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;

        if (t.front() == '[' && t.back() == ']') {
            std::string section = t.substr(1, t.size() - 2);
            current = m_root->addChild(section);
            current->columnNames = {"Key", "Value"};
            any = true;
            continue;
        }

        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));

        if (!current) {
            if (!general) {
                general = m_root->addChild("General");
                general->columnNames = {"Key", "Value"};
                any = true;
            }
            general->rows.push_back({key, val});
        } else {
            current->rows.push_back({key, val});
        }
    }

    return any;
}

std::string IniEngine::serialize() const
{
    if (!m_root) return {};

    std::string result;
    for (auto *section : m_root->children) {
        if (section->name != "General") {
            result += "[" + section->name + "]\n";
        }
        for (auto &row : section->rows) {
            result += row[0] + "=" + row[1] + "\n";
        }
        result += "\n";
    }
    return result;
}

// Factory helpers -- called by TextFormatEngine::createForFile()
std::unique_ptr<TextFormatEngine> createJsonEngine();
std::unique_ptr<TextFormatEngine> createXmlEngine();
std::unique_ptr<TextFormatEngine> createCborEngine();
std::unique_ptr<TextFormatEngine> createYamlEngine();
std::unique_ptr<TextFormatEngine> createTomlEngine();

std::unique_ptr<TextFormatEngine> createIniEngine()
{
    return std::make_unique<IniEngine>();
}

namespace {
std::string extensionOf(const std::string &path) {
    auto slash = path.find_last_of('/');
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot + 1);
    for (auto &c : ext) c = (char)tolower((unsigned char)c);
    return ext;
}
}

std::unique_ptr<TextFormatEngine> TextFormatEngine::createForFile(const std::string &filepath)
{
    std::string ext = extensionOf(filepath);

    if (ext == "json") return createJsonEngine();
    if (ext == "xml" || ext == "svg" || ext == "xhtml" || ext == "plist") return createXmlEngine();
    if (ext == "cbor") return createCborEngine();
    if (ext == "ini" || ext == "cfg" || ext == "conf" || ext == "desktop" || ext == "inf") return createIniEngine();
    if (ext == "yaml" || ext == "yml") return createYamlEngine();
    if (ext == "toml") return createTomlEngine();

    return nullptr;
}
