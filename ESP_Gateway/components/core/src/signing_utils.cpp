#include "core/signing_utils.h"

#include <array>
#include <string>
#include <vector>

#include "esp_log.h"

#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"

namespace {
static const char* TAG = "signing_utils";

// TODO: echten Public Key einsetzen
static const char OTA_SIGN_PUBKEY_PEM[] =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAES65xQfSFNSWzSa5TCGNMdB1a9LpT\n"
"TKOFhUGaW0OLtfTLsm45rg0AC5p/RxPNBFLPfoK4Fqsh/yCBQrdA7IDlsA==\n"
"-----END PUBLIC KEY-----\n";

} // namespace

std::string toHexLower(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";

    std::string s;
    s.reserve(len * 2);

    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        s.push_back(hex[b >> 4]);
        s.push_back(hex[b & 0x0F]);
    }

    return s;
}

std::string toUpperAscii(const std::string& in) {
    std::string out = in;

    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }

    return out;
}

std::string buildCanonicalString(const std::string& method,
                                 const std::string& path,
                                 const std::string& timestamp,
                                 const std::string& nonce,
                                 const std::string& bodyRaw) {
    std::string canon;
    canon.reserve(method.size() + path.size() + timestamp.size() + nonce.size() + bodyRaw.size() + 4);

    canon += toUpperAscii(method);
    canon += '\n';
    canon += path;
    canon += '\n';
    canon += timestamp;
    canon += '\n';
    canon += nonce;
    canon += '\n';
    canon += bodyRaw;

    return canon;
}

std::string urlToPath(const std::string& urlOrPath) {
    if (urlOrPath.empty()) {
        return "/";
    }

    if (urlOrPath[0] == '/') {
        return urlOrPath;
    }

    const auto scheme = urlOrPath.find("://");
    if (scheme != std::string::npos) {
        const auto start = urlOrPath.find('/', scheme + 3);
        if (start == std::string::npos) {
            return "/";
        }
        return urlOrPath.substr(start);
    }

    return "/" + urlOrPath;
}

bool base64Decode(const std::string& in, std::vector<uint8_t>& out) {
    out.clear();

    if (in.empty()) {
        ESP_LOGW(TAG, "base64Decode: empty input");
        return false;
    }

    size_t olen = 0;
    int rc = mbedtls_base64_decode(nullptr, 0, &olen,
                                   reinterpret_cast<const unsigned char*>(in.data()),
                                   in.size());

    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || olen == 0) {
        ESP_LOGE(TAG, "base64Decode: size query failed rc=%d", rc);
        return false;
    }

    out.resize(olen);

    rc = mbedtls_base64_decode(out.data(), out.size(), &olen,
                               reinterpret_cast<const unsigned char*>(in.data()),
                               in.size());
    if (rc != 0) {
        ESP_LOGE(TAG, "base64Decode: decode failed rc=%d", rc);
        out.clear();
        return false;
    }

    out.resize(olen);
    return true;
}

bool verifyFirmwareSignature(const std::array<uint8_t, 32>& digest,
                             const std::string& signatureBase64Der) {
    if (signatureBase64Der.empty()) {
        ESP_LOGE(TAG, "verifyFirmwareSignature: empty signature");
        return false;
    }

    std::vector<uint8_t> sigDer;
    if (!base64Decode(signatureBase64Der, sigDer)) {
        ESP_LOGE(TAG, "verifyFirmwareSignature: signature base64 decode failed");
        return false;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    const int parseRc = mbedtls_pk_parse_public_key(
        &pk,
        reinterpret_cast<const unsigned char*>(OTA_SIGN_PUBKEY_PEM),
        sizeof(OTA_SIGN_PUBKEY_PEM));

    if (parseRc != 0) {
        ESP_LOGE(TAG, "verifyFirmwareSignature: public key parse failed -0x%04x", -parseRc);
        mbedtls_pk_free(&pk);
        return false;
    }

    const int verifyRc = mbedtls_pk_verify(
        &pk,
        MBEDTLS_MD_SHA256,
        digest.data(),
        digest.size(),
        sigDer.data(),
        sigDer.size());

    mbedtls_pk_free(&pk);

    if (verifyRc != 0) {
        ESP_LOGE(TAG, "verifyFirmwareSignature: invalid signature -0x%04x", -verifyRc);
        return false;
    }

    ESP_LOGI(TAG, "verifyFirmwareSignature: signature valid");
    return true;
}