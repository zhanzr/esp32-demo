/* e_server embedded backend for the ESP32-P4 CB V3.2.
 *
 * Same API contract as the F429/F769 e_server reference backend
 * (f4-demo/fire-f429/e_server/server.c), with the hardware side adapted:
 *
 *   GET  /                gzip page (web_assets.h, Content-Encoding: gzip)
 *   GET  /api/leds        {"leds":[led0,led1]}  - GPIO 14 / GPIO 13, low active
 *   POST /api/leds        body {"leds":[led0,led1]} -> applied, echoed back
 *   GET  /api/adc         {"temp_c":..,"ts":..}  - internal die temperature
 *   GET  /api/camera      {"source":"ov5647","ready":1,"w":..,"h":..,"frames":..,"ts":..}
 *   GET  /api/camera/control?rot=&quality=  -> {"rot":..,"quality":..}
 *   GET  /api/info        {"arch":"esp32p4","lan_ip":..,"public_ip":null,"geo":null,"weather":null,"ts":..}
 *   GET  /stream          MJPEG multipart stream (HW JPEG encoder)
 *   GET  /capture         latest single JPEG frame
 *   GET  /public/<name>   embedded images (web_assets.h table)
 *
 * The /api/info external lookups (public IP / geo / weather) are left null:
 * the P4 stack has no HTTPS client wired up yet, and the page renders N/A.
 */

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "led.h"
#include "mipi_cam.h"
#include "stream_jpeg.h"
#include "web_assets.h"
#include "web_server.h"

static const char *TAG = "web_server";

#define MAIN_SERVER_PORT 80

/* LED0 = green GPIO 14, LED1 = blue GPIO 13, both low active */
#define LED0_GPIO  GPIO_NUM_14
#define LED1_GPIO  GPIO_NUM_13

static int s_leds[2];
static char s_lan_ip[16] = "0.0.0.0";   /* updated on IP_EVENT_STA_GOT_IP */
static temperature_sensor_handle_t s_temp_handle;

static void lan_ip_event_handler(void *arg, esp_event_base_t base,
                                 int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_lan_ip, sizeof(s_lan_ip), IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

static void leds_apply(void)
{
    /* low active: level = !state */
    gpio_set_level(LED0_GPIO, s_leds[0] ? 0 : 1);
    gpio_set_level(LED1_GPIO, s_leds[1] ? 0 : 1);
}

static esp_err_t handler_leds(httpd_req_t *req)
{
    char body[128] = { 0 };
    if (req->method == HTTP_POST) {
        size_t rx = req->content_len;
        if (rx > sizeof(body) - 1) rx = sizeof(body) - 1;
        if (rx && httpd_req_recv(req, body, rx) == rx) {
            const char *b = strchr(body, '[');
            if (b) {
                int a[2] = { 0, 0 };
                int got = sscanf(b + 1, "%d,%d", &a[0], &a[1]);
                if (got >= 1) s_leds[0] = a[0] ? 1 : 0;
                if (got >= 2) s_leds[1] = a[1] ? 1 : 0;
                leds_apply();
            }
        }
    }

    char resp[64];
    int n = snprintf(resp, sizeof(resp), "{\"leds\":[%d,%d]}", s_leds[0], s_leds[1]);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, n);
}

static esp_err_t handler_adc(httpd_req_t *req)
{
    float temp_c = 0.0f;
    temperature_sensor_get_celsius(s_temp_handle, &temp_c);

    char resp[96];
    int n = snprintf(resp, sizeof(resp),
                     "{\"temp_c\":%.1f,\"ts\":%lld}",
                     (double)temp_c, (long long)(esp_timer_get_time() / 1000000));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, n);
}

static esp_err_t handler_camera(httpd_req_t *req)
{
    uint32_t frames = stream_jpeg_frames();
    char resp[160];
    int n = snprintf(resp, sizeof(resp),
                     "{\"source\":\"ov5647\",\"ready\":%d,\"w\":%lu,\"h\":%lu,"
                     "\"frames\":%lu,\"rot\":%d,\"quality\":%d,\"ts\":%lld}",
                     frames > 0 ? 1 : 0,
                     (unsigned long)stream_jpeg_width(),
                     (unsigned long)stream_jpeg_height(),
                     (unsigned long)frames,
                     mipi_cam_get_rotation(),
                     stream_jpeg_get_quality(),
                     (long long)(esp_timer_get_time() / 1000000));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, n);
}

static esp_err_t handler_camera_control(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) {
        httpd_resp_sendstr(req, "{\"ok\":0}");
        return ESP_OK;
    }
    char query[128] = { 0 };
    if (qlen > sizeof(query) - 1) qlen = sizeof(query) - 1;
    httpd_req_get_url_query_str(req, query, qlen + 1);

    char val[32];
    if (httpd_query_key_value(query, "quality", val, sizeof(val)) == ESP_OK) {
        stream_jpeg_set_quality(atoi(val));
    }
    if (httpd_query_key_value(query, "rot", val, sizeof(val)) == ESP_OK) {
        mipi_cam_set_rotation(atoi(val));
    }

    char resp[96];
    int n = snprintf(resp, sizeof(resp), "{\"rot\":%d,\"quality\":%d}",
                     mipi_cam_get_rotation(), stream_jpeg_get_quality());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, n);
}

static esp_err_t handler_info(httpd_req_t *req)
{
    char resp[192];
    int n = snprintf(resp, sizeof(resp),
                     "{\"arch\":\"esp32p4\",\"lan_ip\":\"%s\","
                     "\"public_ip\":null,\"geo\":null,\"weather\":null,\"ts\":%lld}",
                     s_lan_ip, (long long)(esp_timer_get_time() / 1000000));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, n);
}

static esp_err_t handler_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz, index_html_gz_len);
}

static esp_err_t handler_favicon(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_sendstr(req, "");
}

static esp_err_t handler_public(httpd_req_t *req)
{
    const char *path = req->uri;
    for (unsigned i = 0; i < embedded_files_count; i++) {
        if (strcmp(path, embedded_files[i].path) == 0) {
            httpd_resp_set_type(req, embedded_files[i].ctype);
            return httpd_resp_send(req, (const char *)embedded_files[i].data,
                                   embedded_files[i].len);
        }
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t handler_stream(httpd_req_t *req)
{
    return stream_jpeg_http_stream(req);
}

static esp_err_t handler_capture(httpd_req_t *req)
{
    return stream_jpeg_http_capture(req);
}

static esp_err_t register_uri(httpd_handle_t server, const char *uri,
                              httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t u = { .uri = uri, .method = method, .handler = handler, .user_ctx = NULL };
    return httpd_register_uri_handler(server, &u);
}

void web_server_init(void)
{
    /* tolerate an existing default loop (main may have created it) */
    esp_event_loop_create_default();
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               lan_ip_event_handler, NULL);
}

const char *web_server_lan_ip(void)
{
    return s_lan_ip;
}

esp_err_t web_server_start(void)
{
    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_cfg, &s_temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(s_temp_handle));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 6;
    cfg.max_uri_handlers = 16;   /* default is 8; this app registers 12 routes */
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    cfg.uri_match_fn = httpd_uri_match_wildcard;   /* for the /public prefix route */

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    register_uri(server, "/", HTTP_GET, handler_index);
    register_uri(server, "/index.html", HTTP_GET, handler_index);
    register_uri(server, "/favicon.ico", HTTP_GET, handler_favicon);
    register_uri(server, "/api/leds", HTTP_GET, handler_leds);
    register_uri(server, "/api/leds", HTTP_POST, handler_leds);
    register_uri(server, "/api/adc", HTTP_GET, handler_adc);
    register_uri(server, "/api/camera", HTTP_GET, handler_camera);
    register_uri(server, "/api/camera/control", HTTP_GET, handler_camera_control);
    register_uri(server, "/api/info", HTTP_GET, handler_info);
    register_uri(server, "/stream", HTTP_GET, handler_stream);
    register_uri(server, "/capture", HTTP_GET, handler_capture);
    register_uri(server, "/public/*", HTTP_GET, handler_public);

    ESP_LOGI(TAG, "e_server ready on port %d", MAIN_SERVER_PORT);
    return ESP_OK;
}
