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
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "driver/gpio.h"

#include "wifi_config.h"

#ifndef DEFAULT_AP
#error "wifi_config.h is missing DEFAULT_AP. Copy wifi_config.h.example to main/wifi_config.h and fill in your credentials."
#endif

/* LED0 = green leg of the tricolor LED, low active.
 * WiFi itself runs on the ESP32-C6 co-module (esp_hosted over SDIO). */
#define BLINK_GPIO      GPIO_NUM_14
#define MAX_AP_COUNT    20

static const char *TAG = "wifi_con";

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static const char* auth_mode_str(wifi_auth_mode_t mode)
{
    switch (mode) {
        case WIFI_AUTH_OPEN:         return "Open";
        case WIFI_AUTH_WEP:          return "WEP";
        case WIFI_AUTH_WPA_PSK:      return "WPA";
        case WIFI_AUTH_WPA2_PSK:     return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:     return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:return "WPA2/WPA3";
        default:                     return "?";
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* evt = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGW(TAG, "Disconnected (reason: %d)", evt->reason);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t scan_networks(void)
{
    ESP_LOGI(TAG, "--- Scanning for available APs ---");

    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Get AP count failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_ap_record_t ap_records[MAX_AP_COUNT];
    uint16_t record_count = MIN(ap_count, MAX_AP_COUNT);
    err = esp_wifi_scan_get_ap_records(&record_count, ap_records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Get AP records failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "  Found %d AP(s)", record_count);
    for (int i = 0; i < record_count; i++) {
        ESP_LOGI(TAG, "  [%02d] %-32s  %4d dBm  ch%2d  %s",
                 i, ap_records[i].ssid, ap_records[i].rssi,
                 ap_records[i].primary, auth_mode_str(ap_records[i].authmode));
    }

    return ESP_OK;
}

static esp_err_t connect_with_retry(const char *auth_name, wifi_auth_mode_t auth,
                                    uint32_t timeout_ms)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = auth,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, DEFAULT_AP, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, DEFAULT_PASSWD, sizeof(wifi_config.sta.password));

    ESP_LOGI(TAG, "  Connecting (%s, %u ms)...", auth_name, timeout_ms);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  Set config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_connect();
    if ( err != ESP_OK) {
        ESP_LOGE(TAG, "  Connect failed: %s", esp_err_to_name(err));
        return err;
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "  LINK_UP");
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "  FAILED");
    } else {
        ESP_LOGI(TAG, "  TIMEOUT");
    }
    return ESP_FAIL;
}

static void print_ip_address(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "  GW: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG, "  NM: " IPSTR, IP2STR(&ip_info.netmask));
        }
    }
}

void app_main(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 1);   /* low active: off */

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-P4 CB V3.2");
    ESP_LOGI(TAG, "  WiFi Connection Test");
    ESP_LOGI(TAG, "  (WiFi via ESP32-C6, esp_hosted/SDIO)");
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

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Check the SDIO link and that the C6 runs the"
                      " esp-hosted slave firmware.");
        return;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    scan_networks();

    ESP_LOGI(TAG, "--- Connecting to \"%s\" (WPA2, 30s timeout) ---", DEFAULT_AP);
    int conn = connect_with_retry("WPA2", WIFI_AUTH_WPA2_PSK, 30000);
    if (conn == ESP_OK) {
        ESP_LOGI(TAG, "Connected successfully");
        print_ip_address();
    } else {
        ESP_LOGI(TAG, "WPA2 failed. Trying WPA/WPA2 mixed mode...");
        conn = connect_with_retry("WPA/WPA2", WIFI_AUTH_WPA_WPA2_PSK, 20000);
        if (conn == ESP_OK) {
            ESP_LOGI(TAG, "Connected successfully (WPA/WPA2 mixed)");
            print_ip_address();
        } else {
            ESP_LOGW(TAG, "Connection failed. Check credentials.");
        }
    }

    ESP_LOGI(TAG, "--- Entering main loop ---");
    uint32_t loop_count = 0;

    while (1) {
        if (loop_count % 60 == 0) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                ESP_LOGI(TAG, "--- WiFi status: UP (\"%s\", RSSI: %d dBm) ---",
                         ap_info.ssid, ap_info.rssi);
                print_ip_address();
            } else {
                ESP_LOGI(TAG, "--- WiFi status: DOWN ---");
            }
        }

        gpio_set_level(BLINK_GPIO, loop_count & 1);
        loop_count++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
