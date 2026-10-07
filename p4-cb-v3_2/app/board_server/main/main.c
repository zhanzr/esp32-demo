/* e_server for the ESP32-P4 CB V3.2.
 *
 * Embedded web demo ported from f4-demo/fire-f429/e_server:
 *   - serves the bundled single-page app (web_assets.h, gzip)
 *   - /api/leds  -> the two user LEDs (GPIO 14 green, GPIO 13 blue, low active)
 *   - /api/adc   -> internal die temperature sensor
 *   - /stream    -> OV5647 MJPEG (HW JPEG encoder), previewed on the
 *                   MIPI-DSI LCD simultaneously (PPA preview task)
 *   - /api/info  -> arch + LAN IP (public IP / geo / weather = null)
 *
 * Camera pipeline from the vendor 17_mipicamera / 18_wifi-camera examples;
 * WiFi via the on-board ESP32-C6 (esp_hosted/SDIO). */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "led.h"
#include "myiic.h"
#include "lcd.h"
#include "wifi_config.h"
#include "mipi_cam.h"
#include "stream_jpeg.h"
#include "web_server.h"
static const char *TAG = "e_server";

static SemaphoreHandle_t s_wifi_sem;
static int s_retry = 0;
#define WIFI_MAX_RETRY 10

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGW(TAG, "WiFi reconnect (%d/%d)...", s_retry, WIFI_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "WiFi connect failed, retries exhausted");
            xSemaphoreGive(s_wifi_sem);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        xSemaphoreGive(s_wifi_sem);
    }
}

static esp_err_t wifi_sta_init(void)
{
    s_wifi_sem = xSemaphoreCreateBinary();
    if (!s_wifi_sem) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_loop_create_default();   /* may already exist (web_server_init) */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: 0x%x (check SDIO pins / C6 slave firmware)", ret);
        return ret;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = DEFAULT_AP,
            .password = DEFAULT_PASSWD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for WiFi (SSID: %s)...", DEFAULT_AP);
    xSemaphoreTake(s_wifi_sem, portMAX_DELAY);
    return (s_retry < WIFI_MAX_RETRY) ? ESP_OK : ESP_FAIL;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    led_init();
    myiic_init();
    lcd_init();
    lcd_show_string(30, 50,  240, 16, 16, "ESP32-P4 CB V3.2", RED);
    lcd_show_string(30, 70,  240, 16, 16, "e_server", RED);
    lcd_show_string(30, 90,  240, 16, 16, "OV5647 + WiFi", RED);

    ESP_LOGI(TAG, "e_server: starting (JPEG encoder, WiFi, camera, HTTP)");
    ESP_ERROR_CHECK(stream_jpeg_init());

    web_server_init();   /* catch the LAN IP event before WiFi comes up */
    ret = wifi_sta_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed - continuing without network services");
    }

    mipi_cam_init();          /* LCD preview + MJPEG feed */
    ESP_ERROR_CHECK(web_server_start());

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            ESP_LOGI(TAG, "e_server ready: http://" IPSTR "/", IP2STR(&ip.ip));
            lcd_show_string(30, 110, 240, 16, 16, "IP:", BLUE);
        }
    }

    /* app_main returns: the camera/preview task, HTTP server and WiFi
     * (esp_hosted) tasks keep running. The LEDs are web-controlled only. */
}
