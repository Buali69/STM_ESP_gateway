#ifndef UART_COMM_H
#define UART_COMM_H

#include "main.h"
#include <stdint.h>
#include <stddef.h>

void uart_comm_init(void);
void uart_comm_process(void);
void uart_comm_rx_callback(UART_HandleTypeDef *huart);

void uart_comm_send_esp(const char *msg);
void uart_comm_log(const char *msg);

#endif