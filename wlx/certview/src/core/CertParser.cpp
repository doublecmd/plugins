#include "CertParser.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <ctime>
#include <algorithm>

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

// MbedTLS headers
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/x509_crl.h>
#include <mbedtls/pkcs7.h>
#include <mbedtls/x509.h>
#include <mbedtls/asn1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha1.h>
#include <mbedtls/md5.h>
#include <mbedtls/oid.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>

namespace certview {

static std::string formatFingerprint(const unsigned char* hash, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0) ss << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)hash[i];
    }
    return ss.str();
}

static std::string formatTime(const mbedtls_x509_time& t) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
                  t.year, t.mon, t.day, t.hour, t.min, t.sec);
    return std::string(buf);
}

static time_t mbedTimeToTimeT(const mbedtls_x509_time& t) {
    struct tm tm_val;
    std::memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = t.year - 1900;
    tm_val.tm_mon = t.mon - 1;
    tm_val.tm_mday = t.day;
    tm_val.tm_hour = t.hour;
    tm_val.tm_min = t.min;
    tm_val.tm_sec = t.sec;
    return timegm(&tm_val);
}

static std::string nameToString(const mbedtls_x509_name* name) {
    char buf[1024];
    int ret = mbedtls_x509_dn_gets(buf, sizeof(buf), name);
    if (ret > 0) {
        return std::string(buf);
    }
    return "";
}

static std::string serialToString(const mbedtls_x509_buf* serial) {
    std::ostringstream ss;
    for (size_t i = 0; i < serial->len; ++i) {
        if (i > 0) ss << ":";
        ss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)serial->p[i];
    }
    return ss.str();
}

static void extractExtKeyUsage(const mbedtls_x509_sequence* ext_key_usage, std::vector<std::string>& out) {
    const mbedtls_x509_sequence* cur = ext_key_usage;
    while (cur != nullptr) {
        const char* desc = nullptr;
        mbedtls_oid_get_extended_key_usage(&cur->buf, &desc);
        if (desc) {
            out.push_back(desc);
        } else {
            char oid_str[128];
            mbedtls_oid_get_numeric_string(oid_str, sizeof(oid_str), &cur->buf);
            out.push_back(std::string("OID: ") + oid_str);
        }
        cur = cur->next;
    }
}

static void extractSan(const mbedtls_x509_sequence* san, std::vector<std::string>& out) {
    const mbedtls_x509_sequence* cur = san;
    while (cur != nullptr) {
        const mbedtls_x509_buf* buf = &cur->buf;
        int tag = buf->tag & 0x1F;
        std::string val(reinterpret_cast<const char*>(buf->p), buf->len);

        switch (tag) {
            case 1: out.push_back("email: " + val); break;
            case 2: out.push_back("DNS: " + val); break;
            case 6: out.push_back("URI: " + val); break;
            case 7: {
                if (buf->len == 4) {
                    char ip_buf[32];
                    std::snprintf(ip_buf, sizeof(ip_buf), "IP: %u.%u.%u.%u",
                                  buf->p[0], buf->p[1], buf->p[2], buf->p[3]);
                    out.push_back(ip_buf);
                } else {
                    out.push_back("IP: " + serialToString(buf));
                }
                break;
            }
            default:
                out.push_back("Other (" + std::to_string(tag) + "): " + val);
                break;
        }
        cur = cur->next;
    }
}

CertDocument CertParser::parseFile(const std::string& filePath, const std::string& password) {
    CertDocument doc;
    doc.filePath = filePath;

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        doc.overallStatus = CertStatus::Corrupted;
        doc.errorMessage = "Failed to open file: " + filePath;
        return doc;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    doc.fileSize = static_cast<size_t>(size);

    std::vector<unsigned char> buffer(doc.fileSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        doc.overallStatus = CertStatus::Corrupted;
        doc.errorMessage = "Failed to read file content";
        return doc;
    }

    return parseBuffer(buffer.data(), buffer.size(), filePath, password);
}

CertDocument CertParser::parseBuffer(const unsigned char* buffer, size_t length, const std::string& filePath, const std::string& password) {
    CertDocument doc;
    doc.filePath = filePath;
    doc.fileSize = length;

    if (!buffer || length == 0) {
        doc.overallStatus = CertStatus::Corrupted;
        doc.errorMessage = "Empty buffer";
        return doc;
    }

    std::string text(reinterpret_cast<const char*>(buffer), length < 512 ? length : 512);

    // 1. SSH Public Key check
    if (text.rfind("ssh-rsa", 0) == 0 || text.rfind("ssh-ed25519", 0) == 0 ||
        text.rfind("ecdsa-sha2", 0) == 0 || text.rfind("sk-ssh", 0) == 0 ||
        text.rfind("sk-ecdsa", 0) == 0) {
        if (parseSshPub(buffer, length, doc)) {
            doc.fileType = FileType::SSHPublicKey;
            generateSummary(doc);
            return doc;
        }
    }

    // 2. GPG Trust Database (trustdb.gpg: 0x01 'g' 'p' 'g')
    if (length >= 16 && buffer[0] == 0x01 && buffer[1] == 'g' && buffer[2] == 'p' && buffer[3] == 'g') {
        if (parseGpgTrustDb(buffer, length, doc)) {
            doc.fileType = FileType::GPGTrustDatabase;
            generateSummary(doc);
            return doc;
        }
    }

    // 3. OpenPGP Armored key check
    if (text.find("-----BEGIN PGP PUBLIC KEY BLOCK-----") != std::string::npos ||
        text.find("-----BEGIN PGP PRIVATE KEY BLOCK-----") != std::string::npos ||
        text.find("-----BEGIN PGP ARMORED FILE-----") != std::string::npos) {
        if (parseOpenPgp(buffer, length, doc)) {
            doc.fileType = FileType::OpenPGPKey;
            generateSummary(doc);
            return doc;
        }
    }

    // 4. Private Key check (.key, PEM or GnuPG S-expression)
    if (text.find("-----BEGIN RSA PRIVATE KEY-----") != std::string::npos ||
        text.find("-----BEGIN PRIVATE KEY-----") != std::string::npos ||
        text.find("-----BEGIN ENCRYPTED PRIVATE KEY-----") != std::string::npos ||
        text.find("-----BEGIN EC PRIVATE KEY-----") != std::string::npos ||
        text.find("Key: (protected-private-key") != std::string::npos ||
        text.find("(protected-private-key") != std::string::npos ||
        text.find("(shadowed-private-key") != std::string::npos ||
        text.find("Token: ") != std::string::npos ||
        text.find("(private-key") != std::string::npos) {
        if (parsePrivateKey(buffer, length, doc)) {
            doc.fileType = FileType::PrivateKey;
            generateSummary(doc);
            return doc;
        }
    }

    // 3. CSR (Certificate Request) PEM check
    if (text.find("-----BEGIN CERTIFICATE REQUEST-----") != std::string::npos ||
        text.find("-----BEGIN NEW CERTIFICATE REQUEST-----") != std::string::npos) {
        if (parseCsr(buffer, length, doc)) {
            doc.fileType = FileType::CertificateRequest;
            generateSummary(doc);
            return doc;
        }
    }

    // 4. CRL (Certificate Revocation List) PEM check
    if (text.find("-----BEGIN X509 CRL-----") != std::string::npos) {
        if (parseCrl(buffer, length, doc)) {
            doc.fileType = FileType::RevocationList;
            generateSummary(doc);
            return doc;
        }
    }

    // 5. PKCS#7 PEM check
    if (text.find("-----BEGIN PKCS7-----") != std::string::npos) {
        if (parsePkcs7(buffer, length, doc)) {
            doc.fileType = FileType::PKCS7Bundle;
            generateSummary(doc);
            return doc;
        }
    }

    // 6. Keybox (.kbx) check
    if (length >= 32 && buffer[4] == 0x01 && buffer[5] == 0x01 &&
        std::memcmp(&buffer[8], "KBXf", 4) == 0) {
        if (parseKbx(buffer, length, doc)) {
            doc.fileType = FileType::KeyboxDatabase;
            generateSummary(doc);
            return doc;
        }
    }

    // 7. PKCS#12 container (.p12, .pfx)
    if (parsePkcs12(buffer, length, password, doc)) {
        doc.fileType = FileType::PKCS12Container;
        generateSummary(doc);
        return doc;
    }

    // 8. Try standard X.509 (PEM, DER, multi-cert trust bundles)
    if (parseX509(buffer, length, doc)) {
        doc.fileType = FileType::X509Certificate;
        generateSummary(doc);
        return doc;
    }

    // 9. Try DER CSR
    if (parseCsr(buffer, length, doc)) {
        doc.fileType = FileType::CertificateRequest;
        generateSummary(doc);
        return doc;
    }

    // 10. Try DER CRL
    if (parseCrl(buffer, length, doc)) {
        doc.fileType = FileType::RevocationList;
        generateSummary(doc);
        return doc;
    }

    // 11. Try DER PKCS#7
    if (parsePkcs7(buffer, length, doc)) {
        doc.fileType = FileType::PKCS7Bundle;
        generateSummary(doc);
        return doc;
    }

    // 12. Try Binary OpenPGP packets (.gpg)
    if ((buffer[0] & 0x80) != 0) {
        if (parseOpenPgp(buffer, length, doc)) {
            doc.fileType = FileType::OpenPGPKey;
            generateSummary(doc);
            return doc;
        }
    }

    doc.fileType = FileType::Unknown;
    doc.overallStatus = CertStatus::Corrupted;
    doc.errorMessage = "Could not parse file as any known certificate, keybox, or key format";
    return doc;
}

bool CertParser::parseX509(const unsigned char* buffer, size_t length, CertDocument& doc) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    int ret = -1;
    std::string text(reinterpret_cast<const char*>(buffer), length < 256 ? length : 256);
    if (text.find("-----BEGIN") != std::string::npos) {
        std::vector<unsigned char> nullTerminated(buffer, buffer + length);
        nullTerminated.push_back('\0');
        ret = mbedtls_x509_crt_parse(&crt, nullTerminated.data(), nullTerminated.size());
    } else {
        ret = mbedtls_x509_crt_parse_der(&crt, buffer, length);
    }

    if (ret != 0) {
        mbedtls_x509_crt_free(&crt);
        return false;
    }

    time_t now = time(nullptr);
    bool anyExpired = false;
    bool anyNotYet = false;

    mbedtls_x509_crt* cur = &crt;
    int idx = 1;
    while (cur != nullptr && cur->raw.len > 0) {
        CertEntry entry;
        entry.subject = nameToString(&cur->subject);
        entry.issuer = nameToString(&cur->issuer);
        entry.serialNumber = serialToString(&cur->serial);
        entry.version = "v" + std::to_string(cur->version);

        std::string commonName = entry.subject;
        size_t cnPos = commonName.find("CN=");
        if (cnPos != std::string::npos) {
            size_t endPos = commonName.find(',', cnPos);
            entry.displayName = commonName.substr(cnPos, endPos == std::string::npos ? std::string::npos : endPos - cnPos);
        } else {
            entry.displayName = "Cert #" + std::to_string(idx);
        }

        const char* sig_desc = nullptr;
        mbedtls_oid_get_sig_alg_desc(&cur->sig_oid, &sig_desc);
        entry.signatureAlgorithm = sig_desc ? sig_desc : "Unknown";

        const mbedtls_pk_context* pk = &cur->pk;
        entry.publicKeyBits = static_cast<int>(mbedtls_pk_get_bitlen(pk));
        entry.publicKeyAlgorithm = mbedtls_pk_get_name(pk);

        entry.validFrom = formatTime(cur->valid_from);
        entry.validTo = formatTime(cur->valid_to);

        time_t t_from = mbedTimeToTimeT(cur->valid_from);
        time_t t_to = mbedTimeToTimeT(cur->valid_to);

        if (now < t_from) {
            entry.isNotYetValid = true;
            anyNotYet = true;
        }
        if (now > t_to) {
            entry.isExpired = true;
            anyExpired = true;
        }
        entry.daysRemaining = static_cast<int>((t_to - now) / 86400);

        unsigned char sha256_hash[32];
        mbedtls_sha256(cur->raw.p, cur->raw.len, sha256_hash, 0);
        entry.sha256Fingerprint = formatFingerprint(sha256_hash, sizeof(sha256_hash));

        unsigned char sha1_hash[20];
        mbedtls_sha1(cur->raw.p, cur->raw.len, sha1_hash);
        entry.sha1Fingerprint = formatFingerprint(sha1_hash, sizeof(sha1_hash));

        static const struct { unsigned int flag; const char* name; } kKuMap[] = {
            { MBEDTLS_X509_KU_DIGITAL_SIGNATURE, "Digital Signature" },
            { MBEDTLS_X509_KU_NON_REPUDIATION,   "Non Repudiation" },
            { MBEDTLS_X509_KU_KEY_ENCIPHERMENT,  "Key Encipherment" },
            { MBEDTLS_X509_KU_DATA_ENCIPHERMENT, "Data Encipherment" },
            { MBEDTLS_X509_KU_KEY_AGREEMENT,     "Key Agreement" },
            { MBEDTLS_X509_KU_KEY_CERT_SIGN,     "Certificate Sign" },
            { MBEDTLS_X509_KU_CRL_SIGN,          "CRL Sign" },
            { MBEDTLS_X509_KU_ENCIPHER_ONLY,     "Encipher Only" },
            { MBEDTLS_X509_KU_DECIPHER_ONLY,     "Decipher Only" }
        };
        for (const auto& ku : kKuMap) {
            if (mbedtls_x509_crt_check_key_usage(cur, ku.flag) == 0) {
                entry.keyUsage.push_back(ku.name);
            }
        }
        extractExtKeyUsage(&cur->ext_key_usage, entry.extKeyUsage);
        extractSan(&cur->subject_alt_names, entry.subjectAltNames);

        char info_buf[4096];
        int info_len = mbedtls_x509_crt_info(info_buf, sizeof(info_buf), "", cur);
        if (info_len > 0) {
            entry.humanReadableText = std::string(info_buf, info_len);
        }

        // Export PEM
        unsigned char pemBuf[4096];
        size_t olen = 0;
        if (mbedtls_base64_encode(pemBuf, sizeof(pemBuf), &olen, cur->raw.p, cur->raw.len) == 0) {
            std::string pem = "-----BEGIN CERTIFICATE-----\n";
            for (size_t p = 0; p < olen; p += 64) {
                pem.append(reinterpret_cast<char*>(pemBuf + p), std::min<size_t>(64, olen - p));
                pem.push_back('\n');
            }
            pem += "-----END CERTIFICATE-----\n";
            entry.rawPem = pem;
        }

        doc.certificates.push_back(entry);
        cur = cur->next;
        idx++;
    }

    mbedtls_x509_crt_free(&crt);

    if (anyExpired) {
        doc.overallStatus = CertStatus::Expired;
        doc.statusText = "⚠ CERTIFICATE EXPIRED";
    } else if (anyNotYet) {
        doc.overallStatus = CertStatus::NotYetValid;
        doc.statusText = "⚠ NOT YET VALID";
    } else {
        doc.overallStatus = CertStatus::Valid;
        doc.statusText = "✓ CERTIFICATE VALID";
    }

    return true;
}

bool CertParser::parseCsr(const unsigned char* buffer, size_t length, CertDocument& doc) {
    mbedtls_x509_csr csr;
    mbedtls_x509_csr_init(&csr);

    int ret = -1;
    std::string text(reinterpret_cast<const char*>(buffer), length < 256 ? length : 256);
    if (text.find("-----BEGIN") != std::string::npos) {
        std::vector<unsigned char> nullTerminated(buffer, buffer + length);
        nullTerminated.push_back('\0');
        ret = mbedtls_x509_csr_parse(&csr, nullTerminated.data(), nullTerminated.size());
    } else {
        ret = mbedtls_x509_csr_parse_der(&csr, buffer, length);
    }

    if (ret != 0) {
        mbedtls_x509_csr_free(&csr);
        return false;
    }

    CertEntry entry;
    entry.displayName = "Certificate Signing Request (CSR)";
    entry.subject = nameToString(&csr.subject);
    entry.issuer = "(None - CSR)";
    entry.version = "v" + std::to_string(csr.version);

    const char* sig_desc = nullptr;
    mbedtls_oid_get_sig_alg_desc(&csr.sig_oid, &sig_desc);
    entry.signatureAlgorithm = sig_desc ? sig_desc : "Unknown";

    const mbedtls_pk_context* pk = &csr.pk;
    entry.publicKeyBits = static_cast<int>(mbedtls_pk_get_bitlen(pk));
    entry.publicKeyAlgorithm = mbedtls_pk_get_name(pk);
    entry.validFrom = "N/A (CSR)";
    entry.validTo = "N/A (CSR)";

    unsigned char sha256_hash[32];
    mbedtls_sha256(csr.raw.p, csr.raw.len, sha256_hash, 0);
    entry.sha256Fingerprint = formatFingerprint(sha256_hash, sizeof(sha256_hash));

    char info_buf[4096];
    int info_len = mbedtls_x509_csr_info(info_buf, sizeof(info_buf), "", &csr);
    if (info_len > 0) {
        entry.humanReadableText = std::string(info_buf, info_len);
    }

    mbedtls_x509_csr_free(&csr);

    doc.certificates.push_back(entry);
    doc.overallStatus = CertStatus::InfoOnly;
    doc.statusText = "📋 CERTIFICATE SIGNING REQUEST";
    return true;
}

bool CertParser::parseCrl(const unsigned char* buffer, size_t length, CertDocument& doc) {
    mbedtls_x509_crl crl;
    mbedtls_x509_crl_init(&crl);

    int ret = -1;
    std::string text(reinterpret_cast<const char*>(buffer), length < 256 ? length : 256);
    if (text.find("-----BEGIN") != std::string::npos) {
        std::vector<unsigned char> nullTerminated(buffer, buffer + length);
        nullTerminated.push_back('\0');
        ret = mbedtls_x509_crl_parse(&crl, nullTerminated.data(), nullTerminated.size());
    } else {
        ret = mbedtls_x509_crl_parse_der(&crl, buffer, length);
    }

    if (ret != 0) {
        mbedtls_x509_crl_free(&crl);
        return false;
    }

    CertEntry entry;
    entry.displayName = "Certificate Revocation List (CRL)";
    entry.issuer = nameToString(&crl.issuer);
    entry.subject = "(CRL Revocations)";
    entry.version = "v" + std::to_string(crl.version);
    entry.validFrom = formatTime(crl.this_update);
    entry.validTo = formatTime(crl.next_update);

    const char* sig_desc = nullptr;
    mbedtls_oid_get_sig_alg_desc(&crl.sig_oid, &sig_desc);
    entry.signatureAlgorithm = sig_desc ? sig_desc : "Unknown";

    char info_buf[8192];
    int info_len = mbedtls_x509_crl_info(info_buf, sizeof(info_buf), "", &crl);
    if (info_len > 0) {
        entry.humanReadableText = std::string(info_buf, info_len);
    }

    mbedtls_x509_crl_free(&crl);

    doc.certificates.push_back(entry);
    doc.overallStatus = CertStatus::InfoOnly;
    doc.statusText = "⛔ CERTIFICATE REVOCATION LIST";
    return true;
}

bool CertParser::parsePkcs7(const unsigned char* buffer, size_t length, CertDocument& doc) {
    std::string text(reinterpret_cast<const char*>(buffer), length < 512 ? length : 512);
    std::vector<unsigned char> der;

    if (text.find("-----BEGIN PKCS7-----") != std::string::npos) {
        std::string fullText(reinterpret_cast<const char*>(buffer), length);
        size_t b64Start = fullText.find('\n');
        size_t b64End = fullText.rfind("-----END PKCS7-----");
        if (b64Start != std::string::npos && b64End != std::string::npos) {
            std::string b64 = fullText.substr(b64Start, b64End - b64Start);
            b64.erase(std::remove_if(b64.begin(), b64.end(), [](char c){ return c == '\r' || c == '\n' || c == ' '; }), b64.end());
            der.resize(b64.size());
            size_t derLen = 0;
            if (mbedtls_base64_decode(der.data(), der.size(), &derLen, reinterpret_cast<const unsigned char*>(b64.data()), b64.size()) == 0) {
                der.resize(derLen);
            }
        }
    } else {
        der.assign(buffer, buffer + length);
    }

    if (der.size() < 16) return false;

    // Scan for X.509 cert SET (tag 0xA0) in PKCS#7
    bool foundAny = false;
    for (size_t i = 0; i + 4 < der.size(); ++i) {
        if (der[i] == 0xA0) {
            const unsigned char* p = &der[i + 1];
            size_t setLen = 0;
            if (*p & 0x80) {
                int bytes = *p & 0x7F;
                p++;
                for (int b = 0; b < bytes && p < der.data() + der.size(); ++b) {
                    setLen = (setLen << 8) | *p++;
                }
            } else {
                setLen = *p++;
            }

            const unsigned char* endSet = p + setLen;
            if (endSet > der.data() + der.size()) endSet = der.data() + der.size();

            while (p + 4 < endSet && *p == 0x30) {
                const unsigned char* certStart = p;
                p++;
                size_t certLen = 0;
                if (*p & 0x80) {
                    int bytes = *p & 0x7F;
                    p++;
                    for (int b = 0; b < bytes && p < endSet; ++b) {
                        certLen = (certLen << 8) | *p++;
                    }
                } else {
                    certLen = *p++;
                }
                size_t totalCertBytes = (p - certStart) + certLen;
                if (certStart + totalCertBytes <= endSet) {
                    if (parseX509(certStart, totalCertBytes, doc)) {
                        foundAny = true;
                    }
                }
                p = certStart + totalCertBytes;
            }
            if (foundAny) break;
        }
    }

    if (foundAny) {
        doc.overallStatus = CertStatus::InfoOnly;
        doc.statusText = "📦 PKCS#7 CERTIFICATE BUNDLE";
        return true;
    }

    return false;
}

bool CertParser::parsePkcs12(const unsigned char* buffer, size_t length, const std::string&, CertDocument& doc) {
    if (length < 16 || buffer[0] != 0x30) return false;

    size_t offset = 1;
    if (buffer[offset] & 0x80) {
        offset += (buffer[offset] & 0x7F) + 1;
    } else {
        offset += 1;
    }

    if (offset + 3 > length) return false;

    if (buffer[offset] == 0x02 && buffer[offset+1] == 0x01 && buffer[offset+2] == 0x03) {
        doc.pkcs12Info.version = 3;
    } else {
        return false;
    }

    doc.overallStatus = CertStatus::EncryptedContainer;
    doc.statusText = "🔒 ENCRYPTED PKCS#12 ARCHIVE";
    doc.pkcs12Info.isPasswordProtected = true;

    std::ostringstream ss;
    ss << "Container: PKCS#12 v3 (PFX)\n";
    ss << "Status: Password Protected / Encrypted Safe Bags\n";
    ss << "Archive Size: " << length << " bytes\n";
    doc.pkcs12Info.summary = ss.str();

    return true;
}

bool CertParser::parseSshPub(const unsigned char* buffer, size_t length, CertDocument& doc) {
    std::string text(reinterpret_cast<const char*>(buffer), length);
    // Find newline if multiple keys
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream lineStream(line);
        std::string keyType, b64Key, comment;
        if (!(lineStream >> keyType >> b64Key)) continue;
        std::getline(lineStream, comment);
        // Trim leading space in comment
        if (!comment.empty() && comment[0] == ' ') comment.erase(0, 1);

        std::vector<unsigned char> rawKey(b64Key.size());
        size_t rawLen = 0;
        if (mbedtls_base64_decode(rawKey.data(), rawKey.size(), &rawLen,
                                 reinterpret_cast<const unsigned char*>(b64Key.data()), b64Key.size()) != 0) {
            continue;
        }

        CertEntry entry;
        entry.displayName = comment.empty() ? keyType : comment;
        entry.subject = comment.empty() ? "(No Comment)" : comment;
        entry.issuer = "SSH Public Key (" + keyType + ")";
        entry.publicKeyAlgorithm = keyType;
        entry.rawPem = line;

        // SHA-256 fingerprint (RFC 4716 base64)
        unsigned char sha256_hash[32];
        mbedtls_sha256(rawKey.data(), rawLen, sha256_hash, 0);
        unsigned char b64Fp[64];
        size_t b64FpLen = 0;
        mbedtls_base64_encode(b64Fp, sizeof(b64Fp), &b64FpLen, sha256_hash, sizeof(sha256_hash));
        std::string fpStr(reinterpret_cast<char*>(b64Fp), b64FpLen);
        while (!fpStr.empty() && fpStr.back() == '=') fpStr.pop_back();
        entry.sha256Fingerprint = "SHA256:" + fpStr;

        // MD5 fingerprint (legacy hex)
        unsigned char md5_hash[16];
        mbedtls_md5(rawKey.data(), rawLen, md5_hash);
        entry.sha1Fingerprint = "MD5:" + formatFingerprint(md5_hash, sizeof(md5_hash));

        entry.validFrom = "Permanent (No Expiry)";
        entry.validTo = "Permanent";

        std::ostringstream ss;
        ss << "Key Type:    " << keyType << "\n";
        ss << "Fingerprint: " << entry.sha256Fingerprint << "\n";
        ss << "MD5:         " << entry.sha1Fingerprint << "\n";
        if (!comment.empty()) ss << "Comment:     " << comment << "\n";
        entry.humanReadableText = ss.str();

        doc.certificates.push_back(entry);
    }

    if (doc.certificates.empty()) return false;

    doc.overallStatus = CertStatus::InfoOnly;
    doc.statusText = "🔑 SSH PUBLIC KEY";
    return true;
}

bool CertParser::parseKbx(const unsigned char* buffer, size_t length, CertDocument& doc) {
    if (length < 32 || std::memcmp(&buffer[8], "KBXf", 4) != 0) return false;

    size_t offset = 0;
    int recordIdx = 0;

    while (offset + 5 <= length) {
        uint32_t recLen = (buffer[offset] << 24) | (buffer[offset+1] << 16) | (buffer[offset+2] << 8) | buffer[offset+3];
        uint8_t recType = buffer[offset+4];

        if (recLen < 5 || offset + recLen > length) break;

        if (recType == 0x02) { // OpenPGP Key Block
            // Extract fingerprints & user IDs according to GnuPG doc/KEYBOX
            // Offset 0x08: flags (2 bytes)
            // Offset 0x0C: total key block offset (4 bytes), size (4 bytes)
            // Offset 0x10: num_fprs (2 bytes), fpr_len (2 bytes)
            if (offset + 0x14 <= length) {
                uint16_t numFprs = (buffer[offset + 0x10] << 8) | buffer[offset + 0x11];
                uint16_t fprLen = (buffer[offset + 0x12] << 8) | buffer[offset + 0x13];
                size_t fprOffset = offset + 0x14;

                std::string primaryFpr;
                if (numFprs > 0 && fprOffset + fprLen <= length) {
                    std::ostringstream fss;
                    for (size_t f = 0; f < fprLen; ++f) {
                        fss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)buffer[fprOffset + f];
                    }
                    primaryFpr = fss.str();
                }

                // User IDs count & offset
                size_t uidInfoOffset = fprOffset + (numFprs * fprLen);
                std::string primaryUid = "OpenPGP Key";
                if (uidInfoOffset + 4 <= offset + recLen) {
                    uint16_t numUids = (buffer[uidInfoOffset] << 8) | buffer[uidInfoOffset + 1];
                    size_t uidEntryOffset = uidInfoOffset + 4; // uid entries array
                    for (int u = 0; u < numUids && uidEntryOffset + 8 <= offset + recLen; ++u) {
                        uint32_t uOff = (buffer[uidEntryOffset] << 24) | (buffer[uidEntryOffset+1] << 16) |
                                        (buffer[uidEntryOffset+2] << 8) | buffer[uidEntryOffset+3];
                        uint32_t uLen = (buffer[uidEntryOffset+4] << 24) | (buffer[uidEntryOffset+5] << 16) |
                                        (buffer[uidEntryOffset+6] << 8) | buffer[uidEntryOffset+7];
                        if (offset + uOff + uLen <= length) {
                            primaryUid = std::string(reinterpret_cast<const char*>(&buffer[offset + uOff]), uLen);
                            break;
                        }
                        uidEntryOffset += 12; // Each UID entry is 12 bytes
                    }
                }

                CertEntry entry;
                entry.displayName = primaryUid;
                entry.subject = primaryUid;
                entry.issuer = "GnuPG Keybox (OpenPGP)";
                entry.publicKeyAlgorithm = "OpenPGP Key";
                entry.sha256Fingerprint = primaryFpr;
                entry.serialNumber = primaryFpr.substr(primaryFpr.size() > 16 ? primaryFpr.size() - 16 : 0);
                entry.validFrom = "Active";
                entry.validTo = "N/A";

                std::ostringstream ss;
                ss << "User ID:     " << primaryUid << "\n";
                ss << "Fingerprint: " << primaryFpr << "\n";
                ss << "Key ID:      " << entry.serialNumber << "\n";
                entry.humanReadableText = ss.str();

                doc.certificates.push_back(entry);
            }
        } else if (recType == 0x03) { // X.509 Record in Keybox
            // Embedded raw DER X.509 cert
            // Read cert offset & length from blob header
            if (offset + 0x10 <= length) {
                uint32_t certOff = (buffer[offset + 0x08] << 24) | (buffer[offset + 0x09] << 16) |
                                   (buffer[offset + 0x0A] << 8) | buffer[offset + 0x0B];
                uint32_t certLen = (buffer[offset + 0x0C] << 24) | (buffer[offset + 0x0D] << 16) |
                                   (buffer[offset + 0x0E] << 8) | buffer[offset + 0x0F];
                if (offset + certOff + certLen <= length) {
                    parseX509(&buffer[offset + certOff], certLen, doc);
                }
            }
        }

        offset += recLen;
        recordIdx++;
    }

    if (doc.certificates.empty()) return false;

    doc.overallStatus = CertStatus::InfoOnly;
    doc.statusText = "📦 GNUPG KEYBOX DATABASE";
    return true;
}

bool CertParser::parseOpenPgp(const unsigned char* buffer, size_t length, CertDocument& doc) {
    std::string text(reinterpret_cast<const char*>(buffer), length < 1024 ? length : 1024);
    std::vector<unsigned char> binaryData;

    if (text.find("-----BEGIN PGP") != std::string::npos) {
        std::string fullText(reinterpret_cast<const char*>(buffer), length);
        size_t b64Start = fullText.find("\n\n");
        if (b64Start == std::string::npos) b64Start = fullText.find("\r\n\r\n");
        size_t b64End = fullText.rfind("-----END PGP");

        if (b64Start != std::string::npos && b64End != std::string::npos) {
            std::string b64 = fullText.substr(b64Start, b64End - b64Start);
            size_t eqPos = b64.rfind("\n=");
            if (eqPos != std::string::npos) {
                b64 = b64.substr(0, eqPos);
            }
            b64.erase(std::remove_if(b64.begin(), b64.end(), [](char c){ return c == '\r' || c == '\n' || c == ' '; }), b64.end());

            binaryData.resize(b64.size());
            size_t olen = 0;
            if (mbedtls_base64_decode(binaryData.data(), binaryData.size(), &olen,
                                     reinterpret_cast<const unsigned char*>(b64.data()), b64.size()) == 0) {
                binaryData.resize(olen);
            }
        }
    } else {
        binaryData.assign(buffer, buffer + length);
    }

    if (binaryData.size() < 6) return false;

    // Parse OpenPGP RFC 4880 packets
    size_t offset = 0;
    CertEntry currentEntry;
    bool foundAny = false;

    while (offset < binaryData.size()) {
        uint8_t tagByte = binaryData[offset++];
        if (!(tagByte & 0x80)) break; // Not an OpenPGP packet

        uint8_t packetTag = 0;
        size_t packetLen = 0;

        if (tagByte & 0x40) { // New format packet header
            packetTag = tagByte & 0x3F;
            if (offset >= binaryData.size()) break;
            uint8_t lenByte1 = binaryData[offset++];
            if (lenByte1 < 192) {
                packetLen = lenByte1;
            } else if (lenByte1 < 224) {
                if (offset >= binaryData.size()) break;
                packetLen = ((lenByte1 - 192) << 8) + binaryData[offset++] + 192;
            } else if (lenByte1 == 255) {
                if (offset + 4 > binaryData.size()) break;
                packetLen = (binaryData[offset] << 24) | (binaryData[offset+1] << 16) |
                            (binaryData[offset+2] << 8) | binaryData[offset+3];
                offset += 4;
            } else {
                break; // Partial body length not handled
            }
        } else { // Old format packet header
            packetTag = (tagByte >> 2) & 0x0F;
            uint8_t lenType = tagByte & 0x03;
            if (lenType == 0) {
                if (offset >= binaryData.size()) break;
                packetLen = binaryData[offset++];
            } else if (lenType == 1) {
                if (offset + 2 > binaryData.size()) break;
                packetLen = (binaryData[offset] << 8) | binaryData[offset+1];
                offset += 2;
            } else if (lenType == 2) {
                if (offset + 4 > binaryData.size()) break;
                packetLen = (binaryData[offset] << 24) | (binaryData[offset+1] << 16) |
                            (binaryData[offset+2] << 8) | binaryData[offset+3];
                offset += 4;
            } else {
                packetLen = binaryData.size() - offset; // Indeterminate
            }
        }

        if (offset + packetLen > binaryData.size()) break;

        const unsigned char* pktData = &binaryData[offset];

        if (packetTag == 6 || packetTag == 14) { // Public Key (6) or Public Subkey (14)
            if (!currentEntry.displayName.empty()) {
                doc.certificates.push_back(currentEntry);
                currentEntry = CertEntry();
            }

            if (packetLen >= 6) {
                uint8_t version = pktData[0];
                uint32_t creationTime = (pktData[1] << 24) | (pktData[2] << 16) | (pktData[3] << 8) | pktData[4];
                uint8_t pkAlgo = pktData[5];

                time_t t_creat = creationTime;
                char timeBuf[64];
                struct tm* gmt = gmtime(&t_creat);
                if (gmt) strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S UTC", gmt);
                else strncpy(timeBuf, "Unknown", sizeof(timeBuf));

                currentEntry.validFrom = timeBuf;
                currentEntry.validTo = "N/A (OpenPGP)";

                // Compute OpenPGP v4 Fingerprint (SHA-1 over: 0x99, len_hi, len_lo, pktData)
                if (version == 4) {
                    unsigned char header[3];
                    header[0] = 0x99;
                    header[1] = (packetLen >> 8) & 0xFF;
                    header[2] = packetLen & 0xFF;

                    mbedtls_sha1_context sha1_ctx;
                    mbedtls_sha1_init(&sha1_ctx);
                    mbedtls_sha1_starts(&sha1_ctx);
                    mbedtls_sha1_update(&sha1_ctx, header, 3);
                    mbedtls_sha1_update(&sha1_ctx, pktData, packetLen);
                    unsigned char fpr[20];
                    mbedtls_sha1_finish(&sha1_ctx, fpr);
                    mbedtls_sha1_free(&sha1_ctx);

                    std::ostringstream ss;
                    for (int f = 0; f < 20; ++f) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)fpr[f];
                    }
                    currentEntry.sha256Fingerprint = ss.str();
                    currentEntry.serialNumber = currentEntry.sha256Fingerprint.substr(24);
                }

                std::string algoName = "Unknown (" + std::to_string(pkAlgo) + ")";
                if (pkAlgo == 1) algoName = "RSA";
                else if (pkAlgo == 17) algoName = "DSA";
                else if (pkAlgo == 18) algoName = "ECDH";
                else if (pkAlgo == 19) algoName = "ECDSA";
                else if (pkAlgo == 22) algoName = "Ed25519";

                currentEntry.publicKeyAlgorithm = algoName;
                currentEntry.issuer = packetTag == 6 ? "OpenPGP Primary Key" : "OpenPGP Subkey";
                foundAny = true;
            }
        } else if (packetTag == 13) { // User ID Packet
            std::string uid(reinterpret_cast<const char*>(pktData), packetLen);
            if (currentEntry.displayName.empty()) {
                currentEntry.displayName = uid;
                currentEntry.subject = uid;
            }
            currentEntry.subjectAltNames.push_back("UID: " + uid);
        }

        offset += packetLen;
    }

    if (!currentEntry.displayName.empty() || foundAny) {
        if (currentEntry.displayName.empty()) currentEntry.displayName = "OpenPGP Key";
        std::ostringstream ss;
        ss << "User ID:     " << currentEntry.displayName << "\n";
        ss << "Algorithm:   " << currentEntry.publicKeyAlgorithm << "\n";
        ss << "Fingerprint: " << currentEntry.sha256Fingerprint << "\n";
        ss << "Key ID:      " << currentEntry.serialNumber << "\n";
        ss << "Created:     " << currentEntry.validFrom << "\n";
        currentEntry.humanReadableText = ss.str();
        doc.certificates.push_back(currentEntry);
    }

    if (!doc.certificates.empty()) {
        doc.overallStatus = CertStatus::InfoOnly;
        doc.statusText = "🛡 OPENPGP PUBLIC KEY";
        return true;
    }

    return false;
}

bool CertParser::parseGpgTrustDb(const unsigned char* buffer, size_t length, CertDocument& doc) {
    if (length < 12) return false;
    if (buffer[0] != 0x01 || buffer[1] != 'g' || buffer[2] != 'p' || buffer[3] != 'g') {
        return false;
    }

    uint8_t version = buffer[4];
    uint8_t marginals = buffer[5];
    uint8_t completes = buffer[6];
    uint8_t maxLevel = buffer[7];

    uint32_t createdTime = 0;
    if (length >= 16) {
        createdTime = (static_cast<uint32_t>(buffer[12]) << 24) |
                      (static_cast<uint32_t>(buffer[13]) << 16) |
                      (static_cast<uint32_t>(buffer[14]) << 8)  |
                      static_cast<uint32_t>(buffer[15]);
    }

    // Trustdb record size is 12 bytes in version 3, 40 bytes in version 2
    size_t recordSize = (version == 3) ? 12 : ((version == 2) ? 40 : 12);
    size_t totalRecords = length / recordSize;

    CertEntry entry;
    entry.displayName = "GnuPG Trust Database (v" + std::to_string(version) + ")";
    entry.subject = "GnuPG Web-of-Trust Database";
    entry.issuer = "GnuPG internal database";
    entry.version = "v" + std::to_string(version);

    if (createdTime > 0) {
        std::time_t t = static_cast<std::time_t>(createdTime);
        std::tm* tm_val = std::gmtime(&t);
        if (tm_val) {
            char tbuf[64];
            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S UTC", tm_val);
            entry.validFrom = tbuf;
        }
    }

    entry.subjectAltNames.push_back("Record Size: " + std::to_string(recordSize) + " bytes");
    entry.subjectAltNames.push_back("Total Records: " + std::to_string(totalRecords));
    entry.subjectAltNames.push_back("Marginal Trust Threshold: " + std::to_string(marginals));
    entry.subjectAltNames.push_back("Complete Trust Threshold: " + std::to_string(completes));
    entry.subjectAltNames.push_back("Max Cert Depth / Level: " + std::to_string(maxLevel));

    std::ostringstream ss;
    ss << "GnuPG Trust Database (trustdb.gpg)\n";
    ss << "Version:          " << (int)version << "\n";
    ss << "Record Size:      " << recordSize << " bytes\n";
    ss << "Total Records:    " << totalRecords << "\n";
    ss << "Marginals Needed: " << (int)marginals << "\n";
    ss << "Completes Needed: " << (int)completes << "\n";
    ss << "Max Trust Depth:  " << (int)maxLevel << "\n";
    if (!entry.validFrom.empty()) {
        ss << "Created:          " << entry.validFrom << "\n";
    }
    entry.humanReadableText = ss.str();

    doc.certificates.push_back(entry);
    doc.overallStatus = CertStatus::InfoOnly;
    doc.statusText = "🔒 GNUPG TRUST DATABASE";
    return true;
}

bool CertParser::parsePrivateKey(const unsigned char* buffer, size_t length, CertDocument& doc) {
    std::string text(reinterpret_cast<const char*>(buffer), length);

    // 1. Check for GnuPG private key S-expression format (e.g. ~/.gnupg/private-keys-v1.d/*.key)
    if (text.find("(protected-private-key") != std::string::npos ||
        text.find("(shadowed-private-key") != std::string::npos ||
        text.find("(private-key") != std::string::npos) {

        CertEntry entry;
        entry.displayName = "GnuPG Private Key";
        entry.subject = "GnuPG Private Key (S-expression)";

        bool isShadowed = (text.find("(shadowed-private-key") != std::string::npos);
        bool isProtected = (text.find("(protected-private-key") != std::string::npos);

        if (isShadowed) {
            entry.issuer = "Smartcard / Hardware Token Shadowed Key";
        } else if (isProtected) {
            entry.issuer = "Passphrase-Protected Private Key";
        } else {
            entry.issuer = "Unencrypted Private Key";
        }

        // Parse "Created: YYYYMMDDTHHMMSS" header if present
        size_t cPos = text.find("Created:");
        if (cPos != std::string::npos) {
            size_t lineEnd = text.find('\n', cPos);
            std::string cLine = text.substr(cPos + 8, lineEnd != std::string::npos ? lineEnd - (cPos + 8) : std::string::npos);
            cLine.erase(std::remove_if(cLine.begin(), cLine.end(), [](char c){ return c == ' ' || c == '\r'; }), cLine.end());
            if (cLine.size() >= 15 && cLine[8] == 'T') {
                std::string formattedDate = cLine.substr(0, 4) + "-" + cLine.substr(4, 2) + "-" + cLine.substr(6, 2) +
                                            " " + cLine.substr(9, 2) + ":" + cLine.substr(11, 2) + ":" + cLine.substr(13, 2) + " UTC";
                entry.validFrom = formattedDate;
            } else {
                entry.validFrom = cLine;
            }
        }

        // Parse Token information if smartcard-shadowed
        size_t tokPos = text.find("Token:");
        if (tokPos != std::string::npos) {
            size_t tokEnd = text.find('\n', tokPos);
            std::string tokLine = text.substr(tokPos + 6, tokEnd != std::string::npos ? tokEnd - (tokPos + 6) : std::string::npos);
            while (!tokLine.empty() && (tokLine.front() == ' ' || tokLine.front() == '\t')) tokLine.erase(0, 1);
            while (!tokLine.empty() && (tokLine.back() == '\r' || tokLine.back() == ' ')) tokLine.pop_back();
            entry.subjectAltNames.push_back("Smartcard Token: " + tokLine);
        }

        // Detect Algorithm and curve
        std::string algo = "Unknown";
        if (text.find("(rsa") != std::string::npos) algo = "RSA";
        else if (text.find("(dsa") != std::string::npos) algo = "DSA";
        else if (text.find("(elg") != std::string::npos) algo = "Elgamal";
        else if (text.find("(ecc") != std::string::npos || text.find("(ecdsa") != std::string::npos || text.find("(eddsa") != std::string::npos) {
            algo = "ECC";
            size_t curvePos = text.find("(curve ");
            if (curvePos != std::string::npos) {
                size_t curveEnd = text.find(')', curvePos);
                if (curveEnd != std::string::npos) {
                    std::string curve = text.substr(curvePos + 7, curveEnd - (curvePos + 7));
                    algo += " (" + curve + ")";
                }
            }
        }
        entry.publicKeyAlgorithm = algo;

        // Try extracting keygrip from filename if available in doc.filePath
        if (!doc.filePath.empty()) {
            size_t slashPos = doc.filePath.find_last_of("/\\");
            std::string filename = (slashPos != std::string::npos) ? doc.filePath.substr(slashPos + 1) : doc.filePath;
            if (filename.size() >= 40 && filename.find(".key") != std::string::npos) {
                entry.serialNumber = filename.substr(0, 40);
                entry.subjectAltNames.push_back("Keygrip: " + entry.serialNumber);
                entry.displayName = "Keygrip: " + entry.serialNumber.substr(0, 16) + "...";
            }
        }

        std::ostringstream ss;
        ss << "GnuPG Private Key S-Expression\n";
        ss << "Algorithm:   " << entry.publicKeyAlgorithm << "\n";
        ss << "Protection:  " << entry.issuer << "\n";
        if (!entry.validFrom.empty()) ss << "Created:     " << entry.validFrom << "\n";
        if (!entry.serialNumber.empty()) ss << "Keygrip:     " << entry.serialNumber << "\n";
        for (const auto& san : entry.subjectAltNames) {
            ss << san << "\n";
        }
        entry.humanReadableText = ss.str();
        entry.rawPem = text;

        doc.certificates.push_back(entry);
        doc.overallStatus = isProtected ? CertStatus::EncryptedContainer : CertStatus::InfoOnly;
        doc.statusText = isShadowed ? "🔑 GPG TOKEN KEY" : (isProtected ? "🔑 GPG PRIVATE KEY (ENCRYPTED)" : "🔑 GPG PRIVATE KEY");
        return true;
    }

    // 2. Standard PEM / DER Private Key using mbedtls_pk
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    std::vector<unsigned char> nullTerminated(buffer, buffer + length);
    nullTerminated.push_back('\0');

    int ret = mbedtls_pk_parse_key(&pk, nullTerminated.data(), nullTerminated.size(), nullptr, 0, nullptr, nullptr);
    if (ret != 0) {
        // Try as raw DER
        ret = mbedtls_pk_parse_key(&pk, buffer, length, nullptr, 0, nullptr, nullptr);
    }

    if (ret == 0) {
        CertEntry entry;
        entry.displayName = "Private Key (" + std::string(mbedtls_pk_get_name(&pk)) + ")";
        entry.subject = "Private Key";
        entry.issuer = "PKCS#1 / PKCS#8 Key";
        entry.publicKeyAlgorithm = mbedtls_pk_get_name(&pk);
        entry.publicKeyBits = static_cast<int>(mbedtls_pk_get_bitlen(&pk));

        std::ostringstream ss;
        ss << "Standard Private Key\n";
        ss << "Algorithm: " << entry.publicKeyAlgorithm << "\n";
        ss << "Bit Length: " << entry.publicKeyBits << " bits\n";
        entry.humanReadableText = ss.str();
        entry.rawPem = text;

        mbedtls_pk_free(&pk);

        doc.certificates.push_back(entry);
        doc.overallStatus = CertStatus::InfoOnly;
        doc.statusText = "🔑 PRIVATE KEY";
        return true;
    }

    mbedtls_pk_free(&pk);

    // If parsing failed with password error or header indicated encrypted private key
    if (text.find("-----BEGIN ENCRYPTED PRIVATE KEY-----") != std::string::npos ||
        text.find("Proc-Type: 4,ENCRYPTED") != std::string::npos) {
        CertEntry entry;
        entry.displayName = "Encrypted Private Key";
        entry.subject = "PKCS#8 / OpenSSL Encrypted Private Key";
        entry.issuer = "Password-Protected Private Key";
        entry.humanReadableText = "Encrypted Private Key (PKCS#8 / OpenSSL)\nEnter password to decrypt";
        entry.rawPem = text;

        doc.certificates.push_back(entry);
        doc.overallStatus = CertStatus::EncryptedContainer;
        doc.statusText = "🔑 ENCRYPTED PRIVATE KEY";
        return true;
    }

    return false;
}

void CertParser::generateSummary(CertDocument& doc) {
    std::ostringstream ss;
    if (doc.fileType == FileType::PKCS12Container) {
        ss << doc.pkcs12Info.summary << "\n";
        ss << "To decrypt and inspect certificates:\n";
        ss << "  openssl pkcs12 -in \"" << doc.filePath << "\" -nokeys\n";
        doc.fullTextSummary = ss.str();
        return;
    }

    for (size_t i = 0; i < doc.certificates.size(); ++i) {
        const auto& c = doc.certificates[i];
        if (doc.certificates.size() > 1) {
            ss << "=== Entry [" << (i + 1) << " of " << doc.certificates.size() << "] ===\n";
        }
        ss << "Name:    " << c.displayName << "\n";
        ss << "Subject: " << c.subject << "\n";
        ss << "Type:    " << c.issuer << "\n";
        if (!c.validFrom.empty()) {
            ss << "Valid:   " << c.validFrom << "  -->  " << c.validTo;
            if (c.isExpired) ss << " [EXPIRED]\n";
            else if (c.isNotYetValid) ss << " [NOT YET VALID]\n";
            else if (c.daysRemaining > 0) ss << " (" << c.daysRemaining << " days remaining)\n";
            else ss << "\n";
        }
        if (!c.serialNumber.empty()) ss << "ID / Serial:   " << c.serialNumber << "\n";
        if (!c.publicKeyAlgorithm.empty()) ss << "Algorithm:     " << c.publicKeyAlgorithm;
        if (c.publicKeyBits > 0) ss << " (" << c.publicKeyBits << " bits)\n";
        else ss << "\n";
        if (!c.sha256Fingerprint.empty()) ss << "Fingerprint:   " << c.sha256Fingerprint << "\n";
        if (!c.sha1Fingerprint.empty()) ss << "Secondary FP:  " << c.sha1Fingerprint << "\n";

        if (!c.subjectAltNames.empty()) {
            ss << "Attributes / SANs / Identities:\n";
            for (const auto& san : c.subjectAltNames) {
                ss << "  • " << san << "\n";
            }
        }
        if (!c.keyUsage.empty()) {
            ss << "Key Usage:\n";
            for (const auto& ku : c.keyUsage) {
                ss << "  • " << ku << "\n";
            }
        }
        if (!c.extKeyUsage.empty()) {
            ss << "Extended Key Usage:\n";
            for (const auto& eku : c.extKeyUsage) {
                ss << "  • " << eku << "\n";
            }
        }
        ss << "\n";
    }

    doc.fullTextSummary = ss.str();
}

} // namespace certview
