#include "protocol.h"
#include "uart_comm.h"

#include <string.h>
#include <stdint.h>

static uint32_t last_ping_ms = 0;
static uint32_t last_status_ms = 0;
static uint32_t last_pong_ms = 0;
static uint32_t last_status_rx_ms = 0;

static uint8_t esp_online = 0;
static uint8_t wifi_ok = 0;
static uint8_t time_ok = 0;
static uint8_t ota_idle = 0;

void protocol_init(void)
{
    uart_comm_log("Protocol v1 init\r\n");
}

void protocol_handle_line(const char *line)
{
    if (strcmp(line, "PONG") == 0)
    {
        last_pong_ms = HAL_GetTick();

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

        wifi_ok  = (strstr(line, "WIFI_OK")  != NULL);
        time_ok  = (strstr(line, "TIME_OK")  != NULL);
        ota_idle = (strstr(line, "OTA_IDLE") != NULL);

        uart_comm_log("PROTOCOL: ");
        uart_comm_log(line);
        uart_comm_log("\r\n");

        if (wifi_ok)
            uart_comm_log("PROTOCOL: WIFI ok\r\n");
        else
            uart_comm_log("PROTOCOL WARN: WIFI not ok\r\n");

        if (time_ok)
            uart_comm_log("PROTOCOL: TIME ok\r\n");
        else
            uart_comm_log("PROTOCOL WARN: TIME not ok\r\n");

        if (ota_idle)
            uart_comm_log("PROTOCOL: OTA idle\r\n");
        else
            uart_comm_log("PROTOCOL WARN: OTA not idle\r\n");

        return;
    }

    if (strncmp(line, "ERROR:", 6) == 0)
    {
        uart_comm_log("PROTOCOL ERROR: ");
        uart_comm_log(line);
        uart_comm_log("\r\n");
        return;
    }

    uart_comm_log("PROTOCOL: unknown line\r\n");
}

void protocol_process(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - last_ping_ms) >= 5000)
    {
        last_ping_ms = now;
        uart_comm_send_esp("PING\n");
        uart_comm_log("TX ESP: PING\r\n");
    }

    if ((now - last_status_ms) >= 15000)
    {
        last_status_ms = now;
        uart_comm_send_esp("STATUS?\n");
        uart_comm_log("TX ESP: STATUS?\r\n");
    }

    if (esp_online && (now - last_pong_ms) > 12000)
    {
        esp_online = 0;
        wifi_ok = 0;
        time_ok = 0;
        ota_idle = 0;

        uart_comm_log("PROTOCOL WARN: ESP offline / heartbeat timeout\r\n");
    }

    if (last_status_rx_ms != 0 && (now - last_status_rx_ms) > 30000)
    {
        wifi_ok = 0;
        time_ok = 0;
        ota_idle = 0;

        uart_comm_log("PROTOCOL WARN: ESP status stale\r\n");

        last_status_rx_ms = now;
    }
}