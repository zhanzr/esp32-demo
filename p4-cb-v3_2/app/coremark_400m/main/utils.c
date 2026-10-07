#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "custom_def.h"
#include "utils.h"

#define BUF_SIZE 512

void uart_printf(const char *fmt, ...)
{
    char buf[BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s", buf);
}

uint32_t HAL_GetTick(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void HAL_Delay(uint32_t t)
{
    vTaskDelay(pdMS_TO_TICKS(t));
}
