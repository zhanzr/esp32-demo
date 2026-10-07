#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "esp_private/esp_clk.h"
#include "esp_task_wdt.h"
#include "utils.h"
#include "custom_def.h"
#include "core_portme.h"

int coremark_main(void);

static void suspend_task_wdt(void)
{
    esp_task_wdt_deinit();
}

static void resume_task_wdt(void)
{
    esp_task_wdt_config_t cfg = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1U << portNUM_PROCESSORS) - 1,
        .trigger_panic = false,
    };
    esp_task_wdt_init(&cfg);
}

void app_main(void)
{
    const uint32_t cpu_hz = esp_clk_cpu_freq();

    while (1) {
        PRINTF("\n--- CoreMark run on ESP32-P4 CB V3.2 @ %u Hz ---\n", cpu_hz);
        suspend_task_wdt();
        coremark_main();
        resume_task_wdt();
        PRINTF("--- CoreMark complete. %u %s ---\n", cpu_hz, COMPILER_NAME);
        for (int i = 0; i < 10; i++) {
            HAL_Delay(1000);
        }
    }
}
