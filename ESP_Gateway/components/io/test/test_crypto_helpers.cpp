#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "unity.h"
}

#include "io/crypto_helpers.h"

namespace {

static std::string toHex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);

    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        out.push_back(hex[(b >> 4) & 0x0F]);
        out.push_back(hex[b & 0x0F]);
    }
    return out;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("b64urlDecode decodes AQID to 01 02 03", "[io][crypto]") {
    const std::vector<uint8_t> out = b64urlDecode("AQID");

    TEST_ASSERT_EQUAL_UINT32(3, out.size());
    TEST_ASSERT_EQUAL_HEX8(0x01, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[2]);
}

TEST_CASE("sha256 of abc matches known digest", "[io][crypto]") {
    const auto dig = sha256(std::string("abc"));
    const std::string hex = toHex(dig.data(), dig.size());

    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        hex.c_str()
    );
}

TEST_CASE("b64urlOfSha256 of abc matches known value", "[io][crypto]") {
    const std::string got = b64urlOfSha256("abc");

    TEST_ASSERT_EQUAL_STRING(
        "ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0",
        got.c_str()
    );
}

TEST_CASE("hmacSha256 matches RFC4231 test vector 1", "[io][crypto]") {
    const uint8_t key[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    const char* msg = "Hi There";

    const auto mac = hmacSha256(
        key, sizeof(key),
        reinterpret_cast<const uint8_t*>(msg), std::strlen(msg)
    );

    const std::string hex = toHex(mac.data(), mac.size());

    TEST_ASSERT_EQUAL_STRING(
        "b0344c61d8db38535ca8afceaf0bf12b"
        "881dc200c9833da726e9376c2e32cff7",
        hex.c_str()
    );
}

TEST_CASE("bytesEqualCT returns true for equal buffers", "[io][crypto]") {
    const uint8_t a[4] = {1, 2, 3, 4};
    const uint8_t b[4] = {1, 2, 3, 4};

    TEST_ASSERT_TRUE(bytesEqualCT(a, b, sizeof(a)));
}

TEST_CASE("bytesEqualCT returns false for different buffers", "[io][crypto]") {
    const uint8_t a[4] = {1, 2, 3, 4};
    const uint8_t b[4] = {1, 2, 3, 5};

    TEST_ASSERT_FALSE(bytesEqualCT(a, b, sizeof(a)));
}

TEST_CASE("Sha256Stream matches one shot sha256", "[io][crypto]") {
    const std::string p1 = "abc";
    const std::string p2 = "def";
    const std::string all = p1 + p2;

    Sha256Stream s;
    TEST_ASSERT_TRUE(s.begin());
    TEST_ASSERT_TRUE(s.update(reinterpret_cast<const uint8_t*>(p1.data()), p1.size()));
    TEST_ASSERT_TRUE(s.update(reinterpret_cast<const uint8_t*>(p2.data()), p2.size()));

    std::array<uint8_t, 32> streamed{};
    TEST_ASSERT_TRUE(s.finish(streamed.data()));

    const auto oneshot = sha256(all);

    TEST_ASSERT_EQUAL_MEMORY(oneshot.data(), streamed.data(), oneshot.size());
}