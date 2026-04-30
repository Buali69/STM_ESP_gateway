#include "uart_comm.h"
#include "protocol.h"
#include <string.h>

extern UART_HandleTypeDef huart1; // ESP
extern UART_HandleTypeDef huart2; // Log

#define RX_RING_SIZE 128
#define LINE_SIZE    80

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
        ring_put(rx_byte);
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