#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

std::string toHexLower(const uint8_t* data, size_t len);
std::string toUpperAscii(const std::string& in);

std::string buildCanonicalString(const std::string& method,
                                 const std::string& path,
                                 const std::string& timestamp,
                                 const std::string& nonce,
                                 const std::string& bodyRaw);

std::string urlToPath(const std::string& urlOrPath);

bool base64Decode(const std::string& in, std::vector<uint8_t>& out);

bool verifyFirmwareSignature(const std::array<uint8_t, 32>& digest,
                             const std::string& signatureBase64Der);