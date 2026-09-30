#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "hal/gpio_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#define LED_GPIO        GPIO_NUM_4  /* flash LED (camera flash on GPIO 4) */

#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_IDX  LEDC_TIMER_0
#define LEDC_CH         LEDC_CHANNEL_0
#define LEDC_FREQ_HZ    5000
#define LEDC_RESOLUTION LEDC_TIMER_10_BIT
#define LED_ON_PERCENT  5

/* ADC2 inputs exposed on the header (the ADC1 pins 36/37/39/34/35 are camera
 * data lines). ADC2 cannot be used while Wi-Fi is active; blink_hello never
 * enables it. GPIO 4 (ADC2_CH0) is skipped because it drives the LED. */
static const gpio_num_t s_adc_pins[] = {
    GPIO_NUM_2, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15,
};

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;
static const char *s_cali_desc = "unavailable (raw counts only)";
static adc_channel_t s_adc_chan[sizeof(s_adc_pins) / sizeof(s_adc_pins[0])];

static const char *TAG = "BLINK";

static void led_pwm_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num       = LEDC_TIMER_IDX,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t ch = {
        .gpio_num   = LED_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH,
        .timer_sel  = LEDC_TIMER_IDX,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void led_set_percent(int percent)
{
    int duty = ((1 << LEDC_RESOLUTION) - 1) * percent / 100;
    ledc_set_duty(LEDC_MODE, LEDC_CH, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CH);
}

static void adc_init(void)
{
    const adc_oneshot_unit_init_cfg_t init = {
        .unit_id = ADC_UNIT_2,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init, &s_adc));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    for (size_t i = 0; i < sizeof(s_adc_pins) / sizeof(s_adc_pins[0]); i++) {
        adc_unit_t unit;
        ESP_ERROR_CHECK(adc_oneshot_io_to_channel(s_adc_pins[i], &unit, &s_adc_chan[i]));
        ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_adc_chan[i], &chan_cfg));
    }

#if defined(ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED)
    adc_cali_line_fitting_efuse_val_t efuse_val;
    esp_err_t chk = adc_cali_scheme_line_fitting_check_efuse(&efuse_val);
    const adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id      = ADC_UNIT_2,
        .atten        = ADC_ATTEN_DB_12,
        .bitwidth     = ADC_BITWIDTH_12,
        .default_vref = 1100,  /* used only when eFuse Vref/TP is not burnt */
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_ok = true;
        s_cali_desc = (chk == ESP_OK &&
                       efuse_val == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_VREF)
                          ? "line fitting (eFuse Vref)"
                          : "line fitting (nominal 1100 mV Vref)";
    }
#endif
}

static void adc_log_readings(void)
{
    char line[256];
    int n = snprintf(line, sizeof(line), "ADC2:");
    for (size_t i = 0; i < sizeof(s_adc_pins) / sizeof(s_adc_pins[0]); i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, s_adc_chan[i], &raw) != ESP_OK) {
            continue;
        }
        if (s_cali_ok) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
            n += snprintf(line + n, sizeof(line) - n, " IO%d=%d (%d mV)",
                          s_adc_pins[i], raw, mv);
        } else {
            n += snprintf(line + n, sizeof(line) - n, " IO%d=%d", s_adc_pins[i], raw);
        }
    }
    ESP_LOGI(TAG, "%s", line);
}

void app_main(void)
{
    led_pwm_init();
    adc_init();

    ESP_LOGI(TAG, "ESP32 D0WD-V3-OV2640 blink_hello");
    ESP_LOGI(TAG, "LED on GPIO %d, hardware PWM @ %d Hz, %d%% intensity when on",
             LED_GPIO, LEDC_FREQ_HZ, LED_ON_PERCENT);
    ESP_LOGI(TAG, "ADC calibration: %s", s_cali_desc);

    uint32_t tick = 0;

    while (1) {
        led_set_percent(((tick / 16) & 1) ? LED_ON_PERCENT : 0);
        if ((tick % 32) == 0) {
            adc_log_readings();
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
