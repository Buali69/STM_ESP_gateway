#include <array>
#include <string>

#include "unity.h"
#include "core/signing_utils.h"

namespace {

// Diesen Digest durch euren echten Test-Digest ersetzen
static const std::array<uint8_t, 32> kTestDigest = {
    0x19, 0x25, 0x06, 0x62, 0xb1, 0x7d, 0x1c, 0x4a,
    0x7a, 0x33, 0x91, 0x80, 0x5b, 0x73, 0x12, 0x81,
    0x91, 0x4f, 0xc3, 0x4c, 0x2a, 0x8e, 0x5d, 0x7e,
    0x44, 0x11, 0x8a, 0x6d, 0x0c, 0x55, 0x92, 0xaf
};

// Diese Signatur durch eure echte Base64-DER-Signatur ersetzen
static const char* kValidSignatureBase64 =
    "REPLACE_WITH_REAL_BASE64_DER_SIGNATURE";

std::string makeCorruptedSignature(const char* sig)
{
    std::string out = sig ? sig : "";
    if (!out.empty()) {
        char& c = out.back();
        c = (c == 'A') ? 'B' : 'A';
    }
    return out;
}

void test_verifyFirmwareSignature_valid_returns_true()
{
    const bool ok = verifyFirmwareSignature(kTestDigest, kValidSignatureBase64);
    TEST_ASSERT_TRUE(ok);
}

void test_verifyFirmwareSignature_invalid_returns_false()
{
    const std::string badSig = makeCorruptedSignature(kValidSignatureBase64);
    const bool ok = verifyFirmwareSignature(kTestDigest, badSig);
    TEST_ASSERT_FALSE(ok);
}

void test_verifyFirmwareSignature_emptySignature_returns_false()
{
    const bool ok = verifyFirmwareSignature(kTestDigest, "");
    TEST_ASSERT_FALSE(ok);
}

} // namespace

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_verifyFirmwareSignature_valid_returns_true);
    RUN_TEST(test_verifyFirmwareSignature_invalid_returns_false);
    RUN_TEST(test_verifyFirmwareSignature_emptySignature_returns_false);

    return UNITY_END();
}