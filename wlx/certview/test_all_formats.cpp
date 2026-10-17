#include "CertParser.h"
#include <iostream>
#include <cassert>

int main() {
    using namespace certview;

    // 1. DER X.509
    {
        auto doc = CertParser::parseFile("testdata/test_rsa.cer");
        std::cout << "[1. DER .cer] count: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::X509Certificate);
        assert(doc.certificates.size() == 1);
    }

    // 2. PEM X.509
    {
        auto doc = CertParser::parseFile("testdata/test_rsa.pem");
        std::cout << "[2. PEM .pem] count: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::X509Certificate);
    }

    // 3. CSR
    {
        auto doc = CertParser::parseFile("testdata/test.csr");
        std::cout << "[3. CSR .csr] type: " << (int)doc.fileType << ", subject: " << doc.certificates[0].subject << std::endl;
        assert(doc.fileType == FileType::CertificateRequest);
    }

    // 4. PKCS#7
    {
        auto doc = CertParser::parseFile("testdata/test.p7b");
        std::cout << "[4. PKCS#7 .p7b] certs: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::PKCS7Bundle);
        assert(!doc.certificates.empty());
    }

    // 5. SSH Public Key
    {
        auto doc = CertParser::parseFile("testdata/test_ssh.pub");
        std::cout << "[5. SSH .pub] key: " << doc.certificates[0].displayName << ", fp: " << doc.certificates[0].sha256Fingerprint << std::endl;
        assert(doc.fileType == FileType::SSHPublicKey);
    }

    // 6. GnuPG Keybox (.kbx)
    {
        auto doc = CertParser::parseFile("testdata/test_pubring.kbx");
        std::cout << "[6. Keybox .kbx] entries: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::KeyboxDatabase);
        assert(!doc.certificates.empty());
        std::cout << "  First UID: " << doc.certificates[0].displayName << std::endl;
    }

    // 7. GPG Armored Key (.asc)
    {
        auto doc = CertParser::parseFile("testdata/test_gpg.asc");
        std::cout << "[7. GPG Armored .asc] entries: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::OpenPGPKey);
    }

    // 8. GPG Binary Key (.gpg)
    {
        auto doc = CertParser::parseFile("testdata/test_gpg.gpg");
        std::cout << "[8. GPG Binary .gpg] entries: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::OpenPGPKey);
    }

    std::cout << "\n>>> ALL 8 CRYPTO FORMAT TESTS PASSED SUCCESSFULLY! <<<\n" << std::endl;
    return 0;
}
