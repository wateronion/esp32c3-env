#pragma once

#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_PORT_NUM      UART_NUM_1
#define UART_BAUD_RATE     115200
#define UART_TX_GPIO       GPIO_NUM_6
#define UART_RX_GPIO       GPIO_NUM_7
#define UART_BUF_SIZE      1024

void uart_init(void);
void uart_send_data(const char *data, size_t len);
int uart_receive_data(char *buf, size_t max_len, TickType_t timeout);
void uart_send_string(const char *str);
