/* Hardware-JPEG MJPEG stream module.
 * Adapted from the vendor 18_wifi-camera example's wifi_stream.c (WiFi and
 * its Kconfig-based credentials removed; the HTTP server lives in
 * web_server.c). */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "driver/jpeg_encode.h"
#include "stream_jpeg.h"

static const char *TAG = "stream_jpeg";

#define MJPEG_BOUNDARY       "mjpegboundary"
#define MJPEG_CONTENT_TYPE   "multipart/x-mixed-replace;boundary=" MJPEG_BOUNDARY

#define JPEG_QUALITY_DEFAULT 40              /* 1-100 */
#define JPEG_BUF_MAX_SIZE    (1024 * 1024)   /* up to 1 MB JPEG per frame */

static SemaphoreHandle_t s_frame_mutex = NULL;   /* guards s_jpeg_* */
static SemaphoreHandle_t s_frame_ready = NULL;   /* new-frame signal */

static uint8_t *s_jpeg_buf = NULL;
static size_t   s_jpeg_len = 0;
static uint32_t s_frame_seq = 0;
static uint32_t s_w = 0, s_h = 0;
static volatile int s_quality = JPEG_QUALITY_DEFAULT;

static jpeg_encoder_handle_t s_jpeg_enc = NULL;

esp_err_t stream_jpeg_init(void)
{
    jpeg_encode_engine_cfg_t enc_cfg = {
        .timeout_ms = 70,
    };
    esp_err_t ret = jpeg_new_encoder_engine(&enc_cfg, &s_jpeg_enc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "jpeg_new_encoder_engine failed: 0x%x", ret);
        return ret;
    }

    s_jpeg_buf = heap_caps_aligned_alloc(64, JPEG_BUF_MAX_SIZE,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_jpeg_buf) {
        ESP_LOGE(TAG, "JPEG buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    s_frame_mutex = xSemaphoreCreateMutex();
    s_frame_ready = xSemaphoreCreateBinary();
    if (!s_frame_mutex || !s_frame_ready) return ESP_ERR_NO_MEM;

    return ESP_OK;
}

esp_err_t stream_jpeg_push_frame(const uint8_t *rgb565, uint32_t w, uint32_t h)
{
    if (!s_jpeg_buf || !s_frame_mutex) return ESP_FAIL;
    if (!rgb565 || !w || !h) return ESP_ERR_INVALID_ARG;

    /* non-blocking: skip this frame while the previous one is in flight */
    if (xSemaphoreTake(s_frame_mutex, 0) != pdTRUE) return ESP_OK;

    jpeg_encode_cfg_t cfg = {
        .src_type      = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample    = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = s_quality,
        .width         = w,
        .height        = h,
    };

    uint32_t out_len = 0;
    esp_err_t ret = jpeg_encoder_process(s_jpeg_enc, &cfg, rgb565,
                                         w * h * 2, s_jpeg_buf,
                                         JPEG_BUF_MAX_SIZE, &out_len);
    if (ret == ESP_OK) {
        s_jpeg_len = out_len;
        s_w = w;
        s_h = h;
        s_frame_seq++;
        xSemaphoreGive(s_frame_ready);
    } else {
        ESP_LOGW(TAG, "jpeg encode failed: 0x%x", ret);
    }

    xSemaphoreGive(s_frame_mutex);
    return ret;
}

esp_err_t stream_jpeg_http_stream(httpd_req_t *req)
{
    char part_hdr[128];

    esp_err_t ret = httpd_resp_set_type(req, MJPEG_CONTENT_TYPE);
    if (ret != ESP_OK) return ret;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    ESP_LOGI(TAG, "stream client connected");

    while (1) {
        if (xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;   /* no new frame within 200 ms: keep waiting */
        }
        if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(50)) != pdTRUE) continue;

        size_t len = s_jpeg_len;
        int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
                               "--" MJPEG_BOUNDARY "\r\n"
                               "Content-Type: image/jpeg\r\n"
                               "Content-Length: %u\r\n\r\n",
                               (unsigned)len);

        ret = httpd_resp_send_chunk(req, part_hdr, hdr_len);
        if (ret == ESP_OK) {
            ret = httpd_resp_send_chunk(req, (const char *)s_jpeg_buf, len);
        }
        xSemaphoreGive(s_frame_mutex);
        if (ret != ESP_OK) break;

        ret = httpd_resp_send_chunk(req, "\r\n", 2);
        if (ret != ESP_OK) break;
    }

    ESP_LOGI(TAG, "stream client disconnected");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t stream_jpeg_http_capture(httpd_req_t *req)
{
    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (s_jpeg_len == 0) {
        xSemaphoreGive(s_frame_mutex);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t ret = httpd_resp_send(req, (const char *)s_jpeg_buf, s_jpeg_len);
    xSemaphoreGive(s_frame_mutex);
    return ret;
}

uint32_t stream_jpeg_frames(void) { return s_frame_seq; }
uint32_t stream_jpeg_width(void)  { return s_w; }
uint32_t stream_jpeg_height(void) { return s_h; }

void stream_jpeg_set_quality(int quality)
{
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    s_quality = quality;
}

int stream_jpeg_get_quality(void) { return s_quality; }
