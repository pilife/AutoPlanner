#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <random>
#include <spdlog/spdlog.h>

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>
#define HAS_BLOB_STORAGE 1
#else
#define HAS_BLOB_STORAGE 0
#endif

namespace blob_storage {

#if HAS_BLOB_STORAGE

inline std::string base64Encode(const unsigned char* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

inline std::vector<unsigned char> base64Decode(const std::string& input) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(input.c_str(), static_cast<int>(input.size()));
    bmem = BIO_push(b64, bmem);
    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);
    std::vector<unsigned char> output(input.size());
    int len = BIO_read(bmem, output.data(), static_cast<int>(output.size()));
    BIO_free_all(bmem);
    output.resize(len > 0 ? len : 0);
    return output;
}

#endif // HAS_BLOB_STORAGE

inline std::string urlEncode(const std::string& value) {
    std::ostringstream encoded;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::uppercase << std::hex
                    << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return encoded.str();
}

inline std::string utcTime(int offsetMinutes = 0) {
    auto t = std::time(nullptr) + offsetMinutes * 60;
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

inline std::string generateUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::ostringstream oss;
    for (int i = 0; i < 16; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return oss.str();
}

inline std::string getExtension(const std::string& filename) {
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) return "bin";
    return filename.substr(pos + 1);
}

inline std::string generateBlobSasUrl(
    const std::string& accountName,
    const std::string& accountKey,
    const std::string& containerName,
    const std::string& blobPath,
    const std::string& permissions,
    int expiryMinutes)
{
#if !HAS_BLOB_STORAGE
    (void)accountName; (void)accountKey; (void)containerName;
    (void)blobPath; (void)permissions; (void)expiryMinutes;
    spdlog::error("[BLOB] OpenSSL not available, cannot generate SAS");
    return "";
#else
    std::string version = "2022-11-02";
    std::string start = utcTime(-5);
    std::string expiry = utcTime(expiryMinutes);
    std::string resource = "b";
    std::string canonicalResource = "/blob/" + accountName + "/" + containerName + "/" + blobPath;

    std::string stringToSign =
        permissions + "\n" +
        start + "\n" +
        expiry + "\n" +
        canonicalResource + "\n" +
        "\n" +    // signedIdentifier
        "\n" +    // signedIP
        "https\n" +
        version + "\n" +
        resource + "\n" +
        "\n" +    // signedSnapshotTime
        "\n" +    // signedEncryptionScope
        "\n" +    // rscc
        "\n" +    // rscd
        "\n" +    // rsce
        "\n" +    // rscl
        "";       // rsct

    auto keyBytes = base64Decode(accountKey);
    unsigned char hmacResult[32];
    unsigned int hmacLen = 0;
    HMAC(EVP_sha256(), keyBytes.data(), static_cast<int>(keyBytes.size()),
         reinterpret_cast<const unsigned char*>(stringToSign.c_str()),
         stringToSign.size(), hmacResult, &hmacLen);

    std::string signature = base64Encode(hmacResult, hmacLen);

    std::string sasToken =
        "sv=" + version +
        "&sp=" + permissions +
        "&st=" + urlEncode(start) +
        "&se=" + urlEncode(expiry) +
        "&spr=https" +
        "&sr=" + resource +
        "&sig=" + urlEncode(signature);

    std::string url = "https://" + accountName + ".blob.core.windows.net/" +
                      containerName + "/" + blobPath + "?" + sasToken;

    spdlog::debug("[BLOB] Generated SAS: container={} blob={} perms={} expiry={}",
                  containerName, blobPath, permissions, expiry);
    return url;
#endif
}

} // namespace blob_storage
