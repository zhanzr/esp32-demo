#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"

/* User LEDs on the ESP32-P4 CB V3.2 core board, both low active:
 * LED0 = green on GPIO 14, LED1 = blue on GPIO 13 (shared tricolor LED). */
#define LED0_GPIO       GPIO_NUM_14
#define LED1_GPIO       GPIO_NUM_13

static const char *TAG = "BLINK";

static temperature_sensor_handle_t s_temp_handle;

static void leds_init(void)
{
    gpio_config_t io = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED0_GPIO) | (1ULL << LED1_GPIO),
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(LED0_GPIO, 1);   /* low active: 1 = off */
    gpio_set_level(LED1_GPIO, 1);
}

static void tsens_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&cfg, &s_temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(s_temp_handle));
}

void app_main(void)
{
    leds_init();
    tsens_init();

    uint32_t freq_mhz = esp_clk_cpu_freq() / 1000000;
    float temp_c = 0.0f;
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(s_temp_handle, &temp_c));

    ESP_LOGI(TAG, "ESP32-P4 CB V3.2 blink_hello");
    ESP_LOGI(TAG, "CPU freq: %u MHz", freq_mhz);
    ESP_LOGI(TAG, "Die temperature: %.2f C", (double)temp_c);
    ESP_LOGI(TAG, "LED0 (green) on GPIO %d, LED1 (blue) on GPIO %d, low active",
             LED0_GPIO, LED1_GPIO);

    int state = 0;
    while (1) {
        gpio_set_level(LED0_GPIO, state);
        gpio_set_level(LED1_GPIO, !state);
        vTaskDelay(pdMS_TO_TICKS(500));
        state = !state;

        ESP_ERROR_CHECK(temperature_sensor_get_celsius(s_temp_handle, &temp_c));
        ESP_LOGI(TAG, "CPU freq: %u MHz, die temp: %.2f C",
                 freq_mhz, (double)temp_c);
    }
}
