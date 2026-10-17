#pragma once

#include "CertModel.h"
#include <string>

namespace certview {

class CertParser {
public:
    static CertDocument parseFile(const std::string& filePath, const std::string& password = "");
    static CertDocument parseBuffer(const unsigned char* buffer, size_t length, const std::string& filePath = "", const std::string& password = "");

private:
    static bool parseX509(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parseCsr(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parseCrl(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parsePkcs7(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parsePkcs12(const unsigned char* buffer, size_t length, const std::string& password, CertDocument& doc);
    static bool parseOpenPgp(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parseGpgTrustDb(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parsePrivateKey(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parseKbx(const unsigned char* buffer, size_t length, CertDocument& doc);
    static bool parseSshPub(const unsigned char* buffer, size_t length, CertDocument& doc);

    static void generateSummary(CertDocument& doc);
};

} // namespace certview
