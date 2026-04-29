/*
#include <cstring>
#include <string>

#include <unity.h>

#include "core/signing_utils.h"

void test_to_upper_ascii_basic() {
    TEST_ASSERT_EQUAL_STRING("GET", toUpperAscii("get").c_str());
    TEST_ASSERT_EQUAL_STRING("POST", toUpperAscii("Post").c_str());
    TEST_ASSERT_EQUAL_STRING("ABC123-_/.", toUpperAscii("abc123-_/." ).c_str());
}

void test_to_hex_lower_basic() {
    const uint8_t data[] = {0x00, 0x01, 0x0a, 0xab, 0xff};
    TEST_ASSERT_EQUAL_STRING("00010aabff", toHexLower(data, sizeof(data)).c_str());
}

void test_build_canonical_string_with_body() {
    const std::string got = buildCanonicalString(
        "post",
        "/api/sensor/push",
        "1700000000",
        "abcd-1234",
        R"({"temp":21.5})"
    );

    const std::string expected =
        "POST\n"
        "/api/sensor/push\n"
        "1700000000\n"
        "abcd-1234\n"
        R"({"temp":21.5})";

    TEST_ASSERT_EQUAL_STRING(expected.c_str(), got.c_str());
}

void test_build_canonical_string_with_empty_body() {
    const std::string got = buildCanonicalString(
        "get",
        "/api/ota/poll",
        "1700000001",
        "nonce-1",
        ""
    );

    const std::string expected =
        "GET\n"
        "/api/ota/poll\n"
        "1700000001\n"
        "nonce-1\n";

    TEST_ASSERT_EQUAL_STRING(expected.c_str(), got.c_str());
}

void test_url_to_path_keeps_existing_path() {
    TEST_ASSERT_EQUAL_STRING("/api/ota/poll", urlToPath("/api/ota/poll").c_str());
}

void test_url_to_path_extracts_path_from_https_url() {
    const std::string got = urlToPath("https://192.168.1.68:8443/api/ota/file/42");
    TEST_ASSERT_EQUAL_STRING("/api/ota/file/42", got.c_str());
}

void test_url_to_path_returns_root_if_url_has_no_path() {
    const std::string got = urlToPath("https://192.168.1.68:8443");
    TEST_ASSERT_EQUAL_STRING("/", got.c_str());
}

void test_url_to_path_prefixes_pathish_input() {
    const std::string got = urlToPath("api/ota/file/42");
    TEST_ASSERT_EQUAL_STRING("/api/ota/file/42", got.c_str());
}


void test_verify_empty_signature_returns_false()
{
    std::array<uint8_t, 32> digest{};
    const bool ok = verifyFirmwareSignature(digest, "");
    TEST_ASSERT_FALSE(ok);
}

void test_verify_invalid_base64_returns_false()
{
    std::array<uint8_t, 32> digest{};
    const bool ok = verifyFirmwareSignature(digest, "%%%not-base64%%%");
    TEST_ASSERT_FALSE(ok);
}

void test_verify_base64_garbage_returns_false()
{
    std::array<uint8_t, 32> digest{};
    const bool ok = verifyFirmwareSignature(digest, "AQIDBAUGBwgJCgsMDQ4PEA==");
    TEST_ASSERT_FALSE(ok);
}


void run_signing_utils_tests() {
    RUN_TEST(test_to_upper_ascii_basic);
    RUN_TEST(test_to_hex_lower_basic);
    RUN_TEST(test_build_canonical_string_with_body);
    RUN_TEST(test_build_canonical_string_with_empty_body);
    RUN_TEST(test_url_to_path_keeps_existing_path);
    RUN_TEST(test_url_to_path_extracts_path_from_https_url);
    RUN_TEST(test_url_to_path_returns_root_if_url_has_no_path);
    RUN_TEST(test_url_to_path_prefixes_pathish_input);
    // neue Tests
  //  RUN_TEST(test_verify_empty_signature_returns_false);
  //  RUN_TEST(test_verify_invalid_base64_returns_false);
  //  RUN_TEST(test_verify_base64_garbage_returns_false);
}
*/