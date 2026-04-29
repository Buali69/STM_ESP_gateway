#include "app.h"
#include "uart_comm.h"
#include "protocol.h"

void app_main(void)
{
    uart_comm_init();
    protocol_init();

    while (1)
    {
        uart_comm_process();
        protocol_process();
        HAL_Delay(1);
    }
}