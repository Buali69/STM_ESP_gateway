#include "io/crypto_helpers.h"

// mbedTLS
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

Sha256Stream::Sha256Stream() : _ctx(nullptr), _info(nullptr) {
    _ctx = new mbedtls_md_context_t;
    mbedtls_md_init(static_cast<mbedtls_md_context_t*>(_ctx));
}

bool Sha256Stream::begin() {
    _info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!_info) {
        return false;
    }

    if (mbedtls_md_setup(static_cast<mbedtls_md_context_t*>(_ctx),
                         static_cast<const mbedtls_md_info_t*>(_info),
                         0) != 0) {
        return false;
    }

    return mbedtls_md_starts(static_cast<mbedtls_md_context_t*>(_ctx)) == 0;
}

bool Sha256Stream::update(const uint8_t* p, size_t n) {
    return mbedtls_md_update(static_cast<mbedtls_md_context_t*>(_ctx), p, n) == 0;
}

bool Sha256Stream::finish(uint8_t out[32]) {
    return mbedtls_md_finish(static_cast<mbedtls_md_context_t*>(_ctx), out) == 0;
}

Sha256Stream::~Sha256Stream() {
    if (_ctx) {
        mbedtls_md_free(static_cast<mbedtls_md_context_t*>(_ctx));
        delete static_cast<mbedtls_md_context_t*>(_ctx);
        _ctx = nullptr;
    }
}

std::string b64urlEncode(const uint8_t* data, size_t len) {
    size_t outLen = 0;
    (void)mbedtls_base64_encode(nullptr, 0, &outLen, data, len);

    std::vector<unsigned char> out(outLen + 4, 0);
    size_t written = 0;

    if (mbedtls_base64_encode(out.data(), out.size(), &written, data, len) != 0) {
        return {};
    }

    std::string s(reinterpret_cast<const char*>(out.data()), written);

    for (char& c : s) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }

    while (!s.empty() && s.back() == '=') {
        s.pop_back();
    }

    return s;
}

std::vector<uint8_t> b64urlDecode(const std::string& inB64url) {
    std::string s = inB64url;

    for (char& c : s) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }

    while ((s.length() % 4) != 0) {
        s += '=';
    }

    size_t outLen = 0;
    (void)mbedtls_base64_decode(
        nullptr,
        0,
        &outLen,
        reinterpret_cast<const unsigned char*>(s.c_str()),
        s.length());

    std::vector<uint8_t> out(outLen + 4);
    size_t written = 0;

    const int rc = mbedtls_base64_decode(
        out.data(),
        out.size(),
        &written,
        reinterpret_cast<const unsigned char*>(s.c_str()),
        s.length());

    if (rc != 0) {
        out.clear();
        return out;
    }

    out.resize(written);
    return out;
}

std::array<uint8_t, 32> sha256(const uint8_t* d, size_t n) {
    std::array<uint8_t, 32> out{};

    Sha256Stream s;
    if (!s.begin()) {
        return out;
    }

    if (n > 0 && d != nullptr) {
        (void)s.update(d, n);
    }

    (void)s.finish(out.data());
    return out;
}

std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& v) {
    if (v.empty()) {
        return sha256(nullptr, 0);
    }
    return sha256(v.data(), v.size());
}

std::array<uint8_t, 32> sha256(const std::string& s) {
    return sha256(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

std::string b64urlOfSha256(const std::string& body) {
    const auto dig = sha256(reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
    return b64urlEncode(dig.data(), dig.size());
}

std::array<uint8_t, 32> hmacSha256(const uint8_t* key, size_t keyLen,
                                   const uint8_t* msg, size_t msgLen) {
    std::array<uint8_t, 32> mac{};

    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) {
        return mac;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    if (mbedtls_md_setup(&ctx, md, 1) != 0) {
        mbedtls_md_free(&ctx);
        return mac;
    }

    if (mbedtls_md_hmac_starts(&ctx, key, keyLen) != 0) {
        mbedtls_md_free(&ctx);
        return mac;
    }

    if (msgLen > 0 && msg != nullptr) {
        if (mbedtls_md_hmac_update(&ctx, msg, msgLen) != 0) {
            mbedtls_md_free(&ctx);
            return mac;
        }
    }

    if (mbedtls_md_hmac_finish(&ctx, mac.data()) != 0) {
        mbedtls_md_free(&ctx);
        return {};
    }

    mbedtls_md_free(&ctx);
    return mac;
}

bool bytesEqualCT(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}