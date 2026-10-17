#pragma once

#include <string>
#include <vector>
#include <utility>

namespace certview {

enum class CertStatus {
    Valid,
    Expired,
    NotYetValid,
    Revoked,
    Corrupted,
    EncryptedContainer,
    InfoOnly
};

enum class FileType {
    X509Certificate,       // .cer, .crt, .pem, .der, trust bundles
    CertificateRequest,    // .csr, .req
    RevocationList,        // .crl
    PKCS7Bundle,           // .p7b, .p7c, .spc
    PKCS12Container,       // .p12, .pfx
    OpenPGPKey,            // .asc, .gpg, .pgp
    GPGTrustDatabase,      // trustdb.gpg
    PrivateKey,            // .key (PEM, DER, or GnuPG private-keys-v1.d)
    KeyboxDatabase,        // .kbx
    SSHPublicKey,          // .pub
    Unknown
};

struct CertEntry {
    std::string displayName;
    std::string subject;
    std::string issuer;
    std::string serialNumber;
    std::string version;
    std::string signatureAlgorithm;
    std::string publicKeyAlgorithm;
    int publicKeyBits = 0;
    
    std::string validFrom;
    std::string validTo;
    bool isExpired = false;
    bool isNotYetValid = false;
    int daysRemaining = 0;

    std::string sha256Fingerprint;
    std::string sha1Fingerprint;

    std::vector<std::string> subjectAltNames;
    std::vector<std::string> keyUsage;
    std::vector<std::string> extKeyUsage;
    std::vector<std::pair<std::string, std::string>> otherExtensions;

    std::string rawPem;
    std::string humanReadableText;
};

struct Pkcs12Metadata {
    int version = 3;
    bool isPasswordProtected = true;
    bool emptyPasswordSucceeded = false;
    std::string macAlgorithm;
    std::string macSaltHex;
    int macIterations = 0;
    std::vector<std::string> bagEncryptions;
    std::string summary;
};

struct CertDocument {
    FileType fileType = FileType::Unknown;
    CertStatus overallStatus = CertStatus::Valid;
    std::string statusText;
    std::string filePath;
    size_t fileSize = 0;
    std::string errorMessage;

    // Decoded entities (X.509 certs, PGP keys, CRLs, CSRs)
    std::vector<CertEntry> certificates;

    // Metadata for PKCS#12
    Pkcs12Metadata pkcs12Info;

    // Full text summary for clipboard copying
    std::string fullTextSummary;
};

} // namespace certview
