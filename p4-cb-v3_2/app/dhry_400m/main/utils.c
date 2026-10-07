#include <stdint.h>
#include "esp_timer.h"
#include "utils.h"

uint32_t HAL_GetTick(void) {
    return esp_timer_get_time() / 1000;
}

void HAL_Delay(uint32_t t) {
    uint32_t end = HAL_GetTick() + t;
    while (HAL_GetTick() < end) {
        __asm__ volatile ("nop");
    }
}
