#include "protocol.h"
#include "uart_comm.h"
#include "stm_ota.h"
#include "uart_comm.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define PROTOCOL_PING_INTERVAL_MS      5000U
#define PROTOCOL_STATUS_INTERVAL_MS    15000U
#define PROTOCOL_HEARTBEAT_TIMEOUT_MS  20000U
#define PROTOCOL_STATUS_STALE_MS       30000U

static uint32_t last_ping_ms = 0;
static uint32_t last_status_ms = 0;
static uint32_t last_pong_ms = 0;
static uint32_t last_status_rx_ms = 0;

static uint8_t esp_online = 0;
static uint8_t wifi_ok = 0;
static uint8_t time_ok = 0;
static uint8_t ota_idle = 0;

static uint8_t esp_offline_logged = 0;
static uint8_t status_stale_logged = 0;

static uint8_t server_ok = 0;
static uint8_t server_known = 0;

static void protocol_clear_status(void)
{
    wifi_ok = 0;
    time_ok = 0;
    ota_idle = 0;
}

static void protocol_log_status(void)
{
    uart_comm_log("PROTOCOL STATUS: ");

    uart_comm_log(wifi_ok ? "wifi=ok " : "wifi=down ");
    uart_comm_log(time_ok ? "time=ok " : "time=bad ");
    uart_comm_log(ota_idle ? "ota=idle " : "ota=busy ");

    if (!server_known)
        uart_comm_log("server=unknown\r\n");
    else
        uart_comm_log(server_ok ? "server=ok\r\n" : "server=err\r\n");
}

void protocol_init(void)
{
    last_ping_ms = 0;
    last_status_ms = 0;
    last_pong_ms = 0;
    last_status_rx_ms = 0;

    esp_online = 0;
    protocol_clear_status();

    esp_offline_logged = 0;
    status_stale_logged = 0;

    uart_comm_log("Protocol v1 init\r\n");
}

void protocol_handle_line(const char *line)   // Empfangen
{
    if (line == NULL)
    {
        return;
    }

    if (strcmp(line, "PONG") == 0)
    {
        last_pong_ms = HAL_GetTick();
        esp_offline_logged = 0;

        if (!esp_online)
        {
            esp_online = 1;
            uart_comm_log("PROTOCOL: ESP online\r\n");
        }

        uart_comm_log("PROTOCOL: PONG received\r\n");
        return;
    }

    if (strncmp(line, "STATUS:", 7) == 0)
    {
        last_status_rx_ms = HAL_GetTick();
        status_stale_logged = 0;

        wifi_ok  = (strstr(line, "WIFI_OK")  != NULL);
        time_ok  = (strstr(line, "TIME_OK")  != NULL);
        ota_idle = (strstr(line, "OTA_IDLE") != NULL);
        server_ok = (strstr(line, "SERVER_OK") != NULL);
        server_known = (strstr(line, "SERVER_OK") != NULL) ||
                        (strstr(line, "SERVER_ERR") != NULL);

        uart_comm_log("PROTOCOL: ");
        uart_comm_log(line);
        uart_comm_log("\r\n");

        protocol_log_status();
        return;
    }

    if (strncmp(line, "ERROR:", 6) == 0)
    {
        uart_comm_log("PROTOCOL ERROR: ");
        uart_comm_log(line);
        uart_comm_log("\r\n");
        return;
    }

    if (strcmp(line, "OTA_BEGIN") == 0)
    {
        uart_comm_log("PROTOCOL: OTA begin received\r\n");
        return;
    }

    if (strcmp(line, "OTA_DONE") == 0)
    {
        uart_comm_log("PROTOCOL: OTA done received\r\n");
        return;
    }

    if (strcmp(line, "OTA_FAIL") == 0)
    {
        uart_comm_log("PROTOCOL ERROR: OTA fail received\r\n");
        return;
    }

    if (strncmp(line, "OTA_PREPARE:", 12) == 0) {
        uint32_t size = 0;
        uint32_t crc32 = 0;

        if (sscanf(line, "OTA_PREPARE:%lu:%lx", &size, &crc32) == 2) {
            if (stm_ota_prepare(size, crc32) == 0) {
                uart_comm_send_esp("OTA_READY\n");
                uart_comm_log("STM OTA: prepared\r\n");
            } else {
                uart_comm_send_esp("OTA_FAIL:PREPARE\n");
                uart_comm_log("STM OTA: prepare failed\r\n");
            }
        } else {
            uart_comm_send_esp("OTA_FAIL:BAD_PREPARE\n");
            uart_comm_log("STM OTA: bad prepare command\r\n");
        }

        return;
    }

    if (strcmp(line, "OTA_DATA_BEGIN") == 0) {
        if (stm_ota_data_begin() == 0) {
            uart_comm_set_data_mode();
            uart_comm_send_esp("OTA_DATA_READY\n");
            uart_comm_log("STM OTA: data begin\r\n");
        } else {
            uart_comm_send_esp("OTA_FAIL:DATA_BEGIN\n");
            uart_comm_log("STM OTA: data begin failed\r\n");
        }

        return;
    }

    if (strcmp(line, "OTA_END") == 0) {
        uart_comm_set_control_mode();
        if (stm_ota_finish() == 0) {
            uart_comm_send_esp("OTA_OK\n");
            uart_comm_log("STM OTA: finished dummy OTA\r\n");
        } else {
            uart_comm_send_esp("OTA_FAIL:FINISH\n");
            uart_comm_log("STM OTA: finish failed\r\n");
        }

        return;
    }

    if (strcmp(line, "OTA_ABORT") == 0) {
        uart_comm_set_control_mode();
        stm_ota_abort();
        uart_comm_send_esp("OTA_ABORTED\n");
        uart_comm_log("STM OTA: aborted\r\n");
        return;
    }

    uart_comm_log("PROTOCOL WARN: unknown line: ");
    uart_comm_log(line);
    uart_comm_log("\r\n");
}

void protocol_process(void)     //  Senden
{
    uint32_t now = HAL_GetTick();

    if ((now - last_ping_ms) >= PROTOCOL_PING_INTERVAL_MS)
    {
        last_ping_ms = now;
        if (!uart_comm_is_data_mode()) {
            uart_comm_send_esp("PING\n");
        
        uart_comm_log("TX ESP: PING\r\n");
        }
    }

    if ((now - last_status_ms) >= PROTOCOL_STATUS_INTERVAL_MS)
    {
        last_status_ms = now;
       if (!uart_comm_is_data_mode()) {
            uart_comm_send_esp("STATUS?\n");
            uart_comm_log("TX ESP: STATUS?\r\n");
        }
        
    }

    if (esp_online && (now - last_pong_ms) > PROTOCOL_HEARTBEAT_TIMEOUT_MS)
    {
        esp_online = 0;
        protocol_clear_status();

        if (!esp_offline_logged)
        {
            uart_comm_log("PROTOCOL WARN: ESP offline / heartbeat timeout\r\n");
            esp_offline_logged = 1;
        }
    }

    if (last_status_rx_ms != 0 &&
        (now - last_status_rx_ms) > PROTOCOL_STATUS_STALE_MS)
    {
        protocol_clear_status();

        if (!status_stale_logged)
        {
            uart_comm_log("PROTOCOL WARN: ESP status stale\r\n");
            status_stale_logged = 1;
        }
    }
}