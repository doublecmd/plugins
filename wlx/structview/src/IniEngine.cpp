#include "TextFormatEngine.h"

#include <QSettings>
#include <QTemporaryFile>
#include <QFileInfo>

/// INI engine: sections as tree nodes, keys as 2-column grid.
class IniEngine : public TextFormatEngine {
public:
    bool parse(const QByteArray &data) override;
    DocumentNode *rootNode() const override { return m_root.get(); }
    QByteArray serialize() const override;
    QString rawText() const override { return m_rawText; }
    QString formatName() const override { return QStringLiteral("INI"); }

private:
    std::unique_ptr<DocumentNode> m_root;
    QString m_rawText;
    QString m_filepath;
};

bool IniEngine::parse(const QByteArray &data)
{
    m_rawText = QString::fromUtf8(data);

    // Write to temp file for QSettings to parse
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    if (!tmp.open()) return false;
    tmp.write(data);
    tmp.flush();

    QSettings ini(tmp.fileName(), QSettings::IniFormat);
    if (ini.status() != QSettings::NoError)
        return false;

    m_root = std::make_unique<DocumentNode>(QStringLiteral("root"));

    // Get all groups (sections)
    QStringList groups = ini.childGroups();

    // General section (keys not in any group)
    QStringList generalKeys = ini.childKeys();
    if (!generalKeys.isEmpty()) {
        auto *general = m_root->addChild(QStringLiteral("General"));
        general->columnNames = {QStringLiteral("Key"), QStringLiteral("Value")};
        for (const auto &key : generalKeys) {
            general->rows.append({QVariant(key), ini.value(key)});
        }
    }

    // Named sections
    for (const auto &group : groups) {
        auto *section = m_root->addChild(group);
        section->columnNames = {QStringLiteral("Key"), QStringLiteral("Value")};

        ini.beginGroup(group);
        QStringList keys = ini.childKeys();
        for (const auto &key : keys) {
            section->rows.append({QVariant(key), ini.value(key)});
        }
        ini.endGroup();
    }

    return !m_root->children.isEmpty();
}

QByteArray IniEngine::serialize() const
{
    if (!m_root) return {};

    QByteArray result;
    for (const auto *section : m_root->children) {
        if (section->name != QStringLiteral("General")) {
            result.append('[');
            result.append(section->name.toUtf8());
            result.append("]\n");
        }
        for (const auto &row : section->rows) {
            result.append(row[0].toString().toUtf8());
            result.append('=');
            result.append(row[1].toString().toUtf8());
            result.append('\n');
        }
        result.append('\n');
    }
    return result;
}

// Factory helpers — called by TextFormatEngine::createForFile()
std::unique_ptr<TextFormatEngine> createJsonEngine();
std::unique_ptr<TextFormatEngine> createXmlEngine();
std::unique_ptr<TextFormatEngine> createCborEngine();
std::unique_ptr<TextFormatEngine> createYamlEngine();
std::unique_ptr<TextFormatEngine> createTomlEngine();

std::unique_ptr<TextFormatEngine> createIniEngine()
{
    return std::make_unique<IniEngine>();
}

std::unique_ptr<TextFormatEngine> TextFormatEngine::createForFile(const QString &filepath)
{
    QFileInfo fi(filepath);
    QString ext = fi.suffix().toLower();

    if (ext == QStringLiteral("json"))
        return createJsonEngine();
    if (ext == QStringLiteral("xml") || ext == QStringLiteral("svg")
        || ext == QStringLiteral("xhtml") || ext == QStringLiteral("plist"))
        return createXmlEngine();
    if (ext == QStringLiteral("cbor"))
        return createCborEngine();
    if (ext == QStringLiteral("ini") || ext == QStringLiteral("cfg")
        || ext == QStringLiteral("conf") || ext == QStringLiteral("desktop")
        || ext == QStringLiteral("inf"))
        return createIniEngine();
    if (ext == QStringLiteral("yaml") || ext == QStringLiteral("yml"))
        return createYamlEngine();
    if (ext == QStringLiteral("toml"))
        return createTomlEngine();

    return nullptr;
}
