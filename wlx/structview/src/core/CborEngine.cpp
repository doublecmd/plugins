#include "core/DocumentModel.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::unique_ptr<TextFormatEngine> createJsonEngine();

/// CBOR engine: binary JSON format. nlohmann::json has native CBOR
/// encode/decode (to_cbor/from_cbor), so this just bridges CBOR bytes to
/// JSON text and delegates tree building to JsonEngine, exactly like the
/// original QCborValue-via-QJsonValue implementation did.
class CborEngine : public TextFormatEngine {
public:
    bool parse(const std::string &data) override;
    DocumentNode *rootNode() const override { return m_jsonEngine ? m_jsonEngine->rootNode() : nullptr; }
    std::string serialize() const override;
    std::string rawText() const override { return m_rawText; }
    std::string formatName() const override { return "CBOR"; }

private:
    std::unique_ptr<TextFormatEngine> m_jsonEngine;
    std::string m_rawText;
};

bool CborEngine::parse(const std::string &data)
{
    json doc;
    try {
        std::vector<uint8_t> bytes(data.begin(), data.end());
        doc = json::from_cbor(bytes);
    } catch (const json::exception &) {
        return false;
    }
    if (!doc.is_object() && !doc.is_array()) return false;

    m_rawText = doc.dump(2);
    m_jsonEngine = createJsonEngine();
    return m_jsonEngine->parse(doc.dump());
}

std::string CborEngine::serialize() const
{
    if (!m_jsonEngine) return {};
    std::string jsonBytes = m_jsonEngine->serialize();
    try {
        json doc = json::parse(jsonBytes);
        std::vector<uint8_t> cbor = json::to_cbor(doc);
        return std::string(cbor.begin(), cbor.end());
    } catch (const json::exception &) {
        return {};
    }
}

std::unique_ptr<TextFormatEngine> createCborEngine()
{
    return std::make_unique<CborEngine>();
}
