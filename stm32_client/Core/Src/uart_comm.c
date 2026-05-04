#include "uart_comm.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include "stm_ota.h"

extern UART_HandleTypeDef huart1; // ESP
extern UART_HandleTypeDef huart2; // Log

static void uart_comm_handle_data_byte(uint8_t b);

#define RX_RING_SIZE 128
#define LINE_SIZE    80

#define STM_FW_FRAME_MAGIC 0x53544D31u
#define STM_FW_FRAME_HEADER_SIZE 16u
#define STM_FW_FRAME_MAX_PAYLOAD 1024u

static uint8_t fw_frame_buf[STM_FW_FRAME_HEADER_SIZE + STM_FW_FRAME_MAX_PAYLOAD];
static uint16_t fw_frame_pos = 0;
static uint16_t fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;

static uint8_t rx_byte;

static uint8_t ring[RX_RING_SIZE];
static volatile uint16_t ring_head = 0;
static volatile uint16_t ring_tail = 0;
static volatile uint32_t ring_overflow_count = 0;

static char line_buf[LINE_SIZE];
static uint16_t line_index = 0;

typedef enum {
    UART_MODE_CONTROL = 0,
    UART_MODE_DATA
} uart_mode_t;

static volatile uart_mode_t uart_mode = UART_MODE_CONTROL;

static uint32_t get_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t get_u16_le(const uint8_t *p)
{
    return ((uint16_t)p[0]) |
           ((uint16_t)p[1] << 8);
}

static void uart_comm_start_rx(void)
{
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

static void ring_put(uint8_t b)
{
    uint16_t next = (uint16_t)((ring_head + 1) % RX_RING_SIZE);

    if (next == ring_tail)
    {
        ring_overflow_count++;
        return;
    }

    ring[ring_head] = b;
    ring_head = next;
}

static int ring_get(uint8_t *b)
{
    if (ring_tail == ring_head)
    {
        return 0;
    }

    *b = ring[ring_tail];
    ring_tail = (uint16_t)((ring_tail + 1) % RX_RING_SIZE);
    return 1;
}

void uart_comm_log(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

void uart_comm_send_esp(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
}

void uart_comm_init(void)
{
    ring_head = 0;
    ring_tail = 0;
    line_index = 0;

    uart_comm_log("\r\nSTM32 UART COMM START\r\n");
    uart_comm_log("USART1: ESP\r\n");
    uart_comm_log("USART2: LOG\r\n");

    uart_comm_start_rx();
    uart_comm_log("USART1 RX armed\r\n");
}

void uart_comm_rx_callback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (uart_comm_is_data_mode()) {
            uart_comm_handle_data_byte(rx_byte);
        } else {
            ring_put(rx_byte);
        }

        uart_comm_start_rx();
    }
}
void uart_comm_process(void)
{
    uint8_t b;

    while (ring_get(&b))
    {
        if (b == '\0' || b == '\r')
        {
            continue;
        }

        if (b == '\n')
        {
            line_buf[line_index] = '\0';

            uart_comm_log("RX LINE: ");
            uart_comm_log(line_buf);
            uart_comm_log("\r\n");

            protocol_handle_line(line_buf);

            line_index = 0;
            continue;
        }

        if (line_index < LINE_SIZE - 1)
        {
            line_buf[line_index++] = (char)b;
        }
        else
        {
            line_index = 0;
            uart_comm_log("RX LINE overflow, cleared\r\n");
        }
    }

    if (ring_overflow_count > 0)
    {
        ring_overflow_count = 0;
        uart_comm_log("RX ring overflow\r\n");
    }
}

void uart_comm_set_control_mode(void)
{
    uart_mode = UART_MODE_CONTROL;
}

void uart_comm_set_data_mode(void)
{
    uart_mode = UART_MODE_DATA;
}

int uart_comm_is_data_mode(void)
{
    return uart_mode == UART_MODE_DATA;
}

static uint32_t crc32_le_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    while (len--) {
        crc ^= (uint32_t)(*data++);

        for (uint32_t i = 0; i < 8; i++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static void uart_comm_handle_data_byte(uint8_t b)
{
    if (fw_frame_pos >= sizeof(fw_frame_buf)) {
        fw_frame_pos = 0;
        fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
        uart_comm_send_esp("NACK:0:OVERSIZE\n");
        return;
    }

    fw_frame_buf[fw_frame_pos++] = b;

    if (fw_frame_pos == STM_FW_FRAME_HEADER_SIZE) {
        uint32_t magic = get_u32_le(&fw_frame_buf[0]);
        uint16_t len = get_u16_le(&fw_frame_buf[8]);

        if (magic != STM_FW_FRAME_MAGIC) {
            fw_frame_pos = 0;
            fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
            uart_comm_send_esp("NACK:0:BAD_MAGIC\n");
            return;
        }

        if (len > STM_FW_FRAME_MAX_PAYLOAD) {
            fw_frame_pos = 0;
            fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
            uart_comm_send_esp("NACK:0:BAD_LEN\n");
            return;
        }

        fw_frame_expected = STM_FW_FRAME_HEADER_SIZE + len;
    }

    if (fw_frame_pos == fw_frame_expected) {
        uint32_t seq = get_u32_le(&fw_frame_buf[4]);
        uint16_t len = get_u16_le(&fw_frame_buf[8]);
        
        if (len == 0) {
            uart_comm_log("STM OTA: DATA_END received\r\n");

            if (stm_ota_get_received_size() != stm_ota_get_expected_size()) {
                uart_comm_send_esp("DATA_END_FAIL:SIZE\n");
                fw_frame_pos = 0;
                fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
                return;
            }

            fw_frame_pos = 0;
            fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;

            uint32_t final_crc = stm_ota_get_final_crc32();
            uint32_t expected_crc = stm_ota_get_expected_crc32();

            if (final_crc != expected_crc) {
                uart_comm_send_esp("DATA_END_FAIL:CRC\n");
                fw_frame_pos = 0;
                fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
                return;
            }
            
            uart_comm_set_control_mode();
            uart_comm_send_esp("DATA_END_OK\n");
            return;
        }

        uint32_t expected_crc = get_u32_le(&fw_frame_buf[12]);
        uint32_t actual_crc = crc32_le_update(UINT32_MAX, &fw_frame_buf[16], len) ^ UINT32_MAX;

        if (actual_crc != expected_crc) {
            char logbuf[96];
            snprintf(logbuf, sizeof(logbuf),
                    "STM OTA CRC FAIL seq=%lu exp=%08lx act=%08lx len=%u\r\n",
                    (unsigned long)seq,
                    (unsigned long)expected_crc,
                    (unsigned long)actual_crc,
                    (unsigned int)len);
            uart_comm_log(logbuf);

            char nack[40];
            snprintf(nack, sizeof(nack), "NACK:%lu:CRC\n", (unsigned long)seq);
            uart_comm_send_esp(nack);

            fw_frame_pos = 0;
            fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
            uart_comm_set_control_mode();
            stm_ota_abort();
            return;
        }

        // Chunk-CRC war korrekt → jetzt Gesamt-CRC weiterführen
        stm_ota_crc_update(&fw_frame_buf[16], len);

        uint32_t received = stm_ota_get_received_size();
        uint32_t expected = stm_ota_get_expected_size();

        char sizelog[96];
        snprintf(sizelog, sizeof(sizelog),
                "STM OTA SIZE CHECK seq=%lu len=%u received=%lu expected=%lu\r\n",
                (unsigned long)seq,
                (unsigned int)len,
                (unsigned long)received,
                (unsigned long)expected);
        uart_comm_log(sizelog);

        if ((received + len) > expected) {
            char nack[32];

            fw_frame_pos = 0;
            fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;

            snprintf(nack, sizeof(nack),
                     "NACK:%lu:SIZE\n",
                     (unsigned long)seq);
            uart_comm_send_esp(nack);
            return;
        }

        if (stm_ota_add_received((uint32_t)len) != 0) {
            char nack[32];

            fw_frame_pos = 0;
            fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;

            snprintf(nack, sizeof(nack),
                     "NACK:%lu:SIZE\n",
                     (unsigned long)seq);
            uart_comm_send_esp(nack);
            return;
        }

        char ack[32];
        char logbuf[96];

        snprintf(logbuf, sizeof(logbuf),
                 "STM OTA: chunk seq=%lu len=%u total=%lu/%lu\r\n",
                 (unsigned long)seq,
                 (unsigned int)len,
                 (unsigned long)stm_ota_get_received_size(),
                 (unsigned long)stm_ota_get_expected_size());
        uart_comm_log(logbuf);

        snprintf(ack, sizeof(ack),
                 "ACK:%lu\n",
                 (unsigned long)seq);
        uart_comm_send_esp(ack);

        fw_frame_pos = 0;
        fw_frame_expected = STM_FW_FRAME_HEADER_SIZE;
    }
}

