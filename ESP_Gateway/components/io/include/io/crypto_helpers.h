#pragma once
#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

struct Sha256Stream {
  Sha256Stream();
  bool begin();
  bool update(const uint8_t* p, size_t n);
  bool finish(uint8_t out[32]);
  ~Sha256Stream();

private:
  void* _ctx;         // opaque (mbedtls_md_context_t*)
  const void* _info;  // opaque (mbedtls_md_info_t*)
};

std::string b64urlEncode(const uint8_t* data, size_t len);
std::vector<uint8_t> b64urlDecode(const std::string& inB64url);

std::array<uint8_t,32> sha256(const uint8_t* d, size_t n);
std::array<uint8_t,32> sha256(const std::vector<uint8_t>& v);
std::array<uint8_t,32> sha256(const std::string& s);

std::string b64urlOfSha256(const std::string& body);

std::array<uint8_t,32> hmacSha256(const uint8_t* key, size_t keyLen,
                                  const uint8_t* msg, size_t msgLen);

bool bytesEqualCT(const uint8_t* a, const uint8_t* b, size_t n);

void tlsProbeBoth(void);