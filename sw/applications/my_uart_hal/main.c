#include <stdio.h>
#include <stdlib.h>
#include "core_v_mini_mcu.h"
#include "uart.h"
#include "x-heep.h"

int main(void)
{
    uart_t uart = {
        .base_addr   = mmio_region_from_addr(UART_START_ADDRESS),
        .baudrate    = UART_BAUDRATE,
        .clk_freq_hz = REFERENCE_CLOCK_Hz,
        .nco         = UART_NCO,
    };
    uart_init(&uart);

    char *msg = "UART HAL test passed!\r\n";
    uart_write(&uart, (uint8_t *)msg, 22);

    return EXIT_SUCCESS;
}