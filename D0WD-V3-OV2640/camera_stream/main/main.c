#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_camera.h"
#include "driver/ledc.h"
#include "hal/gpio_types.h"
#include "lwip/ip_addr.h"

#include "wifi_config.h"
#include "app_httpd.h"

#ifndef DEFAULT_AP
#error "wifi_config.h is missing DEFAULT_AP. Copy wifi_config.h.example to main/wifi_config.h and fill in your credentials."
#endif

static const char *TAG = "camera_stream";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;

/* Board wiring: ESP32-CAM module pin map (module datasheet) */
#define CAM_PIN_PWDN   32
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   0
#define CAM_PIN_SIOD   26
#define CAM_PIN_SIOC   27
#define CAM_PIN_D7     35
#define CAM_PIN_D6     34
#define CAM_PIN_D5     39
#define CAM_PIN_D4     36
#define CAM_PIN_D3     21
#define CAM_PIN_D2     19
#define CAM_PIN_D1     18
#define CAM_PIN_D0     5
#define CAM_PIN_VSYNC  25
#define CAM_PIN_HREF   23
#define CAM_PIN_PCLK   22

/* Flash LED on GPIO 4 (SD DATA1 / flash light). LEDC timer 1 / channel 1:
 * timer 0 / channel 0 belong to the camera XCLK. */
#define LED_GPIO        GPIO_NUM_4
#define LED_PWM_FREQ_HZ 5000
#define LED_PWM_TIMER   LEDC_TIMER_1
#define LED_PWM_CHANNEL LEDC_CHANNEL_1

void camera_led_set_percent(int percent)
{
    int duty = ((1 << LEDC_TIMER_10_BIT) - 1) * percent / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL);
}

static void led_pwm_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LED_PWM_TIMER,
        .freq_hz         = LED_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t ch = {
        .gpio_num   = LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LED_PWM_CHANNEL,
        .timer_sel  = LED_PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "AP rssi=%d phy=%s ch=%d",
                     ap_info.rssi,
                     ap_info.phy_11n ? "11n" : (ap_info.phy_11g ? "11g" : "11b"),
                     ap_info.primary);
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, DEFAULT_AP, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, DEFAULT_PASSWD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    /* Modem-sleep power save (default WIFI_PS_MIN_MODEM) wakes the radio
     * periodically and stalls MJPEG TX for hundreds of ms at a time (seen as
     * send_max spikes and FPS collapses to 1-3). Disable it for a steady stream. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\"...", DEFAULT_AP);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR(&ip.ip));
        }
    }
}

static camera_config_t s_camera_config;

static void camera_init(void)
{
    s_camera_config = (camera_config_t){
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href  = CAM_PIN_HREF,
        .pin_pclk  = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&s_camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%X (%s)", err, esp_err_to_name(err));
        return;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        ESP_LOGI(TAG, "Sensor: pid=0x%X ver=0x%X slv=0x%02X", s->id.PID, s->id.VER, s->slv_addr);
        ESP_LOGI(TAG, "Sensor defaults: aec=%d aec2(night)=%d agc=%d aec_value=%d",
                 (int)s->status.aec, (int)s->status.aec2, (int)s->status.agc,
                 (int)s->status.aec_value);
    }

    /* Warm up the JPEG pipeline: the first grab after init often has no SOI
     * marker (cam_hal NO-SOI). Discard one frame so clients get clean data. */
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
        ESP_LOGI(TAG, "Warmup frame captured");
    }

    ESP_LOGI(TAG, "Camera init OK");
}

/* Reinit the camera with a new frame size / pixel format. cam_hal allocates
 * its DMA frame buffer once at init (JPEG: w*h/5, RGB565: w*h*2), so a plain
 * s->set_framesize() overflows the buffer at larger resolutions (FB-OVF).
 * Reconfiguring deinit+init reallocates it for the new size. */
esp_err_t camera_reconfigure(framesize_t frame_size, pixformat_t pixel_format)
{
    s_camera_config.frame_size = frame_size;
    s_camera_config.pixel_format = pixel_format;
    return esp_camera_reconfigure(&s_camera_config);
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32 D0WD-V3-OV2640 Stream Server");
    ESP_LOGI(TAG, "  XCLK=GPIO0 (20 MHz)  ANT=PCB");
    ESP_LOGI(TAG, "========================================");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, retrying...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    wifi_init_sta();
    camera_init();
    led_pwm_init();

    ESP_ERROR_CHECK(start_camera_server());

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            ESP_LOGI(TAG, "Camera ready! Open http://" IPSTR " in a browser", IP2STR(&ip.ip));
        }
    }
}
