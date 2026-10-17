#include "CertParser.h"
#include <iostream>
#include <cassert>

int main() {
    using namespace certview;

    // Test 1: DER certificate (.cer)
    {
        CertDocument doc = CertParser::parseFile("testdata/test_rsa.cer");
        std::cout << "[Test 1: DER .cer] status: " << (int)doc.overallStatus 
                  << ", certs: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::X509Certificate);
        assert(doc.certificates.size() == 1);
        assert(doc.certificates[0].subject.find("example.com") != std::string::npos);
        assert(!doc.certificates[0].sha256Fingerprint.empty());
        assert(doc.certificates[0].subjectAltNames.size() == 3);
        std::cout << "  Subject: " << doc.certificates[0].subject << std::endl;
        std::cout << "  SANs count: " << doc.certificates[0].subjectAltNames.size() << std::endl;
    }

    // Test 2: PEM certificate (.pem)
    {
        CertDocument doc = CertParser::parseFile("testdata/test_rsa.pem");
        std::cout << "[Test 2: PEM .pem] status: " << (int)doc.overallStatus 
                  << ", certs: " << doc.certificates.size() << std::endl;
        assert(doc.fileType == FileType::X509Certificate);
        assert(doc.certificates.size() == 1);
        assert(doc.certificates[0].subject.find("example.com") != std::string::npos);
        std::cout << "  SHA-256: " << doc.certificates[0].sha256Fingerprint << std::endl;
    }

    // Test 3: PKCS#12 container (.p12)
    {
        CertDocument doc = CertParser::parseFile("testdata/test_protected.p12");
        std::cout << "[Test 3: PKCS#12 .p12] status: " << (int)doc.overallStatus 
                  << ", type: " << (int)doc.fileType << std::endl;
        assert(doc.fileType == FileType::PKCS12Container);
        assert(doc.overallStatus == CertStatus::EncryptedContainer);
        std::cout << "  Outer summary:\n" << doc.fullTextSummary << std::endl;
    }

    std::cout << "All core tests passed successfully!" << std::endl;
    return 0;
}
