#include "io/http_client.h"
#include "io/config.h"
#include "io/crypto_helpers.h"
#include "core/signing_utils.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
//#include "io/ota_mgr.h"

#include "esp_system.h"


static const char* TAG = "HTTP_TLS";

struct HttpResponseBuffer {
    std::string body;
};

namespace {

static esp_http_client_config_t makeBaseConfig(const std::string& path) {
    esp_http_client_config_t cfg = {};
    cfg.host = SERVER_HOST;
    cfg.path = path.c_str();
    cfg.port = SERVER_HTTPS_PORT;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg.skip_cert_common_name_check = true;
    cfg.timeout_ms = 30000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 2048;
    //cfg.cert_pem = TLS_CA_CERT_PEM;
    //cfg.cert_pem = TLS_INTERMEDIATE_CERT_PEM;
    cfg.cert_pem = TLS_CHAIN_CERT_PEM;
    return cfg;
}

static bool applyAuthHeaders(esp_http_client_handle_t client,
                             const std::string& method,
                             const std::string& path,
                             const std::string& bodyRaw) {
    const HmacHeaders h = makeHmacHeaders(method, path, bodyRaw);

    if (esp_http_client_set_header(client, "x-device-key", h.deviceKey.c_str()) != ESP_OK) return false;
    if (esp_http_client_set_header(client, "x-timestamp",  h.ts.c_str())        != ESP_OK) return false;
    if (esp_http_client_set_header(client, "x-nonce",      h.nonce.c_str())     != ESP_OK) return false;
    if (esp_http_client_set_header(client, "x-signature",  h.sigHex.c_str())    != ESP_OK) return false;

    return true;
}

} // namespace

static esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
    auto* buf = static_cast<HttpResponseBuffer*>(evt->user_data);
    if (!buf) {
        return ESP_OK;
    }

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data && evt->data_len > 0) {
                buf->body.append(static_cast<const char*>(evt->data), evt->data_len);
            }
            break;
        default:
            break;
    }

    return ESP_OK;
}

static const std::vector<uint8_t>& deviceSecretBytes() {
    static std::vector<uint8_t> key;
    static bool inited = false;

    if (!inited) {
        key = b64urlDecode(std::string(DEVICE_SECRET_B64URL));
        inited = true;
    }

    return key;
}

static std::string makeNonce() {
    char buf[37];

    const uint32_t r0 = esp_random();
    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();
    const uint32_t r3 = esp_random();

    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%08x",
              static_cast<unsigned int>(r0),
              static_cast<unsigned int>(static_cast<uint16_t>(r1)),
              static_cast<unsigned int>(static_cast<uint16_t>(r1 >> 16)),
              static_cast<unsigned int>(static_cast<uint16_t>(r2)),
              static_cast<unsigned int>(r3));

    return std::string(buf);
}

HmacHeaders makeHmacHeaders(const std::string& method,
                            const std::string& path,
                            const std::string& bodyRaw) {
    const auto& key = deviceSecretBytes();
    const std::string ts = std::to_string(static_cast<unsigned long>(time(nullptr)));
    const std::string nonce = makeNonce();

    const std::string canon = buildCanonicalString(method, path, ts, nonce, bodyRaw);

    const std::array<uint8_t, 32> mac = hmacSha256(
        key.data(),
        key.size(),
        reinterpret_cast<const uint8_t*>(canon.c_str()),
        canon.size());

    HmacHeaders h;
    h.deviceKey = DEVICE_KEY_ID;
    h.ts = ts;
    h.nonce = nonce;
    h.sigHex = toHexLower(mac.data(), mac.size());
    return h;
}

bool httpsGet(const std::string& path, int* httpCodeOut, std::string* respOut) {
    if (httpCodeOut) *httpCodeOut = 0;
    if (respOut) respOut->clear();

    HttpResponseBuffer buf;

    const std::string url =
        std::string("https://") + SERVER_HOST + ":" +
        std::to_string(SERVER_HTTPS_PORT) + path;

    ESP_LOGI(TAG, "GET %s", url.c_str());

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    //cfg.cert_pem = TLS_CA_CERT_PEM;
    //cfg.cert_pem = TLS_INTERMEDIATE_CERT_PEM;
    cfg.cert_pem = TLS_CHAIN_CERT_PEM;
    cfg.event_handler = httpEventHandler;
    cfg.user_data = &buf;
    cfg.timeout_ms = 10000;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    const auto h = makeHmacHeaders("GET", path, "");

    esp_http_client_set_header(client, "x-device-key", h.deviceKey.c_str());
    esp_http_client_set_header(client, "x-timestamp",  h.ts.c_str());
    esp_http_client_set_header(client, "x-nonce",      h.nonce.c_str());
    esp_http_client_set_header(client, "x-signature",  h.sigHex.c_str());

    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    const int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "http status=%d", status);

    if (httpCodeOut) *httpCodeOut = status;
    if (respOut) *respOut = buf.body;

    esp_http_client_cleanup(client);
    return true;
}

bool httpsPostJson(const std::string& path,
                   const std::string& json,
                   int* httpCodeOut,
                   std::string* respOut) {
    if (httpCodeOut) *httpCodeOut = 0;
    if (respOut) respOut->clear();

    HttpResponseBuffer buf;

    const std::string url =
        std::string("https://") + SERVER_HOST + ":" +
        std::to_string(SERVER_HTTPS_PORT) + path;

    ESP_LOGI(TAG, "POST %s", url.c_str());
    ESP_LOGI(TAG, "json len=%u", static_cast<unsigned>(json.size()));

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    //cfg.cert_pem = TLS_CA_CERT_PEM;
    //cfg.cert_pem = TLS_INTERMEDIATE_CERT_PEM;
    cfg.cert_pem = TLS_CHAIN_CERT_PEM;
    cfg.event_handler = httpEventHandler;
    cfg.user_data = &buf;
    cfg.timeout_ms = 10000;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg.skip_cert_common_name_check = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json.c_str(), json.size());

    const auto h = makeHmacHeaders("POST", path, json);

    esp_http_client_set_header(client, "x-device-key", h.deviceKey.c_str());
    esp_http_client_set_header(client, "x-timestamp",  h.ts.c_str());
    esp_http_client_set_header(client, "x-nonce",      h.nonce.c_str());
    esp_http_client_set_header(client, "x-signature",  h.sigHex.c_str());

    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "POST failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    const int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "http status=%d", status);

    if (httpCodeOut) *httpCodeOut = status;
    if (respOut) *respOut = buf.body;

    esp_http_client_cleanup(client);
    return true;
}

void tlsProbe(void) {
    int code = 0;
    std::string body;

    const bool ok = httpsGet("/", &code, &body);

    ESP_LOGI(TAG, "tlsProbe ok=%d http=%d body_len=%u",
         ok ? 1 : 0,
         code,
         (unsigned)body.size());

 //   otaMgrSetServerStatus(ok);
}

bool httpsGetStream(const std::string& path,
                    StreamHandler handler,
                    int* httpCodeOut)
{
    
    auto cfg = makeBaseConfig(path);
    cfg.method = HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    if (!applyAuthHeaders(client, "GET", path, "")) {
        esp_http_client_cleanup(client);
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    int64_t contentLen = esp_http_client_fetch_headers(client);

    if (httpCodeOut)
        *httpCodeOut = esp_http_client_get_status_code(client);

    uint8_t buf[4096];

    while (true) {
        int r = esp_http_client_read(client, (char*)buf, sizeof(buf));
        if (r <= 0) break;

        if (!handler(buf, r, contentLen)) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return true;
}

bool httpsGetAuth(const std::string& path,
                  int* httpCodeOut,
                  std::string* respOut) {
    if (httpCodeOut) *httpCodeOut = 0;
    if (respOut) respOut->clear();

    auto cfg = makeBaseConfig(path);
    cfg.method = HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "httpsGetAuth: esp_http_client_init failed");
        return false;
    }

    bool ok = false;

    do {
        ESP_LOGI(TAG, "httpsGetAuth: GET https://%s:%d%s",
                 SERVER_HOST, SERVER_HTTPS_PORT, path.c_str());

        if (!applyAuthHeaders(client, "GET", path, "")) {
            ESP_LOGE(TAG, "httpsGetAuth: applyAuthHeaders failed");
            break;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "httpsGetAuth: open failed: %s", esp_err_to_name(err));
            break;
        }

        int64_t contentLen = esp_http_client_fetch_headers(client);
        int code = esp_http_client_get_status_code(client);

        if (httpCodeOut) *httpCodeOut = code;

        ESP_LOGI(TAG, "httpsGetAuth: http=%d contentLen=%lld",
                 code, (long long)contentLen);

        if (respOut) respOut->clear();

        char buf[1024];
        while (true) {
            const int n = esp_http_client_read(client, buf, sizeof(buf));
            if (n < 0) {
                ESP_LOGE(TAG, "httpsGetAuth: read error");
                break;
            }
            if (n == 0) {
                ok = (code > 0);
                break;
            }
            if (respOut) {
                respOut->append(buf, static_cast<size_t>(n));
            }
        }

        if (!ok) {
            break;
        }

        if (respOut) {
            ESP_LOGI(TAG, "httpsGetAuth: body size=%u",
                     (unsigned)respOut->size());

            ESP_LOGI(TAG, "httpsGetAuth: body first bytes hex=%02x %02x %02x %02x",
                     respOut->size() > 0 ? (unsigned char)(*respOut)[0] : 0,
                     respOut->size() > 1 ? (unsigned char)(*respOut)[1] : 0,
                     respOut->size() > 2 ? (unsigned char)(*respOut)[2] : 0,
                     respOut->size() > 3 ? (unsigned char)(*respOut)[3] : 0);

           // ESP_LOGI(TAG, "httpsGetAuth: body=%s", respOut->c_str());
        }

    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

void tlsDiagnostics() {
    ESP_LOGI(TAG,
             "!!!!!!!! TLS PRECHECK MARKER heap=%u min=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    tlsProbe();
}