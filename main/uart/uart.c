#include "uart.h"

static QueueHandle_t uart_queue;

void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 10, &uart_queue, 0);
}

void uart_send_data(const char *data, size_t len)
{
    uart_write_bytes(UART_PORT_NUM, data, len);
}

void uart_send_string(const char *str)
{
    uart_write_bytes(UART_PORT_NUM, str, strlen(str));
}

int uart_receive_data(char *buf, size_t max_len, TickType_t timeout)
{
    return uart_read_bytes(UART_PORT_NUM, buf, max_len, timeout);
}
