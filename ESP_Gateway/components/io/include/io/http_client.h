#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct HmacHeaders {
    std::string deviceKey;
    std::string ts;
    std::string nonce;
    std::string sigHex;
};

using StreamHandler = std::function<bool(const uint8_t* data, size_t len, long long contentLength)>;

HmacHeaders makeHmacHeaders(const std::string& method,
                            const std::string& path,
                            const std::string& bodyRaw);

bool httpsPostJson(const std::string& path,
                   const std::string& json,
                   int* httpCodeOut = nullptr,
                   std::string* respOut = nullptr);

bool httpsGetAuth(const std::string& path,
                  int* httpCodeOut = nullptr,
                  std::string* respOut = nullptr);

bool httpsGetStream(const std::string& path,
                    StreamHandler handler,
                    int* httpCodeOut = nullptr);

void tlsProbe();
void tlsDiagnostics();