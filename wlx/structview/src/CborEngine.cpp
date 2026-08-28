#include "TextFormatEngine.h"

#include <QCborValue>
#include <QCborMap>
#include <QCborArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

// Forward declare factory
std::unique_ptr<TextFormatEngine> createJsonEngine();

/// CBOR engine: binary JSON format.
///
/// Converts CBOR → JSON → delegates to JsonEngine's tree builder.
/// Serializes back via JSON → CBOR.
class CborEngine : public TextFormatEngine {
public:
    bool parse(const QByteArray &data) override;
    DocumentNode *rootNode() const override { return m_jsonEngine ? m_jsonEngine->rootNode() : nullptr; }
    QByteArray serialize() const override;
    QString rawText() const override { return m_rawText; }
    QString formatName() const override { return QStringLiteral("CBOR"); }

private:
    std::unique_ptr<TextFormatEngine> m_jsonEngine;
    QString m_rawText;
};

bool CborEngine::parse(const QByteArray &data)
{
    QCborParserError err;
    QCborValue cbor = QCborValue::fromCbor(data, &err);
    if (err.error != QCborError::NoError)
        return false;

    // Convert to JSON for tree building and text display
    QJsonValue jsonVal = cbor.toJsonValue();
    QJsonDocument doc;
    if (jsonVal.isObject())
        doc = QJsonDocument(jsonVal.toObject());
    else if (jsonVal.isArray())
        doc = QJsonDocument(jsonVal.toArray());
    else
        return false;

    m_rawText = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

    // Delegate to JsonEngine: parse the JSON bytes
    m_jsonEngine = createJsonEngine();
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);
    return m_jsonEngine->parse(jsonBytes);
}

QByteArray CborEngine::serialize() const
{
    if (!m_jsonEngine) return {};

    // Get JSON bytes from the JSON engine
    QByteArray jsonBytes = m_jsonEngine->serialize();

    // Convert JSON → CBOR
    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    QCborValue cbor;
    if (doc.isObject())
        cbor = QCborValue::fromJsonValue(QJsonValue(doc.object()));
    else if (doc.isArray())
        cbor = QCborValue::fromJsonValue(QJsonValue(doc.array()));
    else
        return {};

    return cbor.toCbor();
}

std::unique_ptr<TextFormatEngine> createCborEngine()
{
    return std::make_unique<CborEngine>();
}
