#include "io/wifi_mgr.h"
#include "io/config.h"

#include <string>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"

static const char* TAG = "WIFI_MGR";

static EventGroupHandle_t s_wifiEventGroup = nullptr;
static bool s_wifiConnected = false;
static esp_netif_t* s_staNetif = nullptr;

static constexpr int WIFI_CONNECTED_BIT = BIT0;

static void wifiEventHandler(void*,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
            ESP_LOGI(TAG, "STA start -> connecting");
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_wifiConnected = false;
            if (s_wifiEventGroup) {
                xEventGroupClearBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);
            }
            ESP_LOGW(TAG, "STA disconnected -> reconnecting");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            auto* ev = static_cast<ip_event_got_ip_t*>(event_data);
            s_wifiConnected = true;
            if (s_wifiEventGroup) {
                xEventGroupSetBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);
            }

            ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&ev->ip_info.ip));
        }
    }
}

void wifiInit() {
    static bool initialized = false;
    if (initialized) {
        ESP_LOGI(TAG, "wifi already initialized");
        return;
    }
    initialized = true;

    s_wifiEventGroup = xEventGroupCreate();
    if (!s_wifiEventGroup) {
        ESP_LOGE(TAG, "failed to create wifi event group");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_staNetif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr));

#ifdef USE_STATIC_IP
    {
        esp_netif_dhcpc_stop(s_staNetif);

        esp_netif_ip_info_t ip_info{};
        ip4addr_aton(LOCAL_IP, &ip_info.ip);
        ip4addr_aton(GATEWAY, &ip_info.gw);
        ip4addr_aton(SUBNET, &ip_info.netmask);

        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_staNetif, &ip_info));

        esp_netif_dns_info_t dns{};
        ip4addr_aton(DNS1, &dns.ip.u_addr.ip4);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(s_staNetif, ESP_NETIF_DNS_MAIN, &dns));

        ip4addr_aton(DNS2, &dns.ip.u_addr.ip4);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(s_staNetif, ESP_NETIF_DNS_BACKUP, &dns));
    }
#endif

    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
                 WIFI_SSID,
                 sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
                 WIFI_PASS,
                 sizeof(wifi_config.sta.password) - 1);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to '%s' ...", WIFI_SSID);
}

bool wifiIsConnected() {
    return s_wifiConnected;
}

std::string wifiLocalIpStr() {
    if (!s_staNetif) {
        return "0.0.0.0";
    }

    esp_netif_ip_info_t ip_info{};
    if (esp_netif_get_ip_info(s_staNetif, &ip_info) != ESP_OK) {
        return "0.0.0.0";
    }

    char buf[16];
    std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip_info.ip));
    return std::string(buf);
}