/* MIPI CSI (OV5647) capture task: LCD preview + MJPEG stream feed.
 * Adapted from the vendor 17_mipicamera / 18_wifi-camera examples for the
 * ESP32-P4 CB V3.2 + 3.5" ST7796U DSI panel. Only the ST7796U (id 0x7796)
 * path is kept; other vendor panels were dropped. */

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ppa.h"
#include "linux/videodev2.h"
#include "mipi_cam.h"
#include "mipi_lcd.h"
#include "myiic.h"
#include "app_video.h"
#include "stream_jpeg.h"
#include "web_server.h"

static const char *mipi_cam_tag = "mipi_cam";

#define FPS_FUNC_ON   1
#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

static int s_rotation = 0;   /* 0 / 90 / 180 / 270 (sensor vflip+hmirror) */

static void lcd_cam_task(void *arg);

/* 3.5" ST7796U panel: 320x480 portrait; the preview crops a 320x480 portrait
 * window from the 1024x600 camera frame (vendor numbers) and shows it 1:1. */
#define CROP_W   320
#define CROP_H   480
#define CROP_X   352
#define CROP_Y   60

esp_err_t mipi_cam_init(void)
{
    esp_err_t ret;
    mipi_dev_bsp_enable_dsi_phy_power();

    ret = app_video_main(myiic_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(mipi_cam_tag, "video main init failed with error 0x%x", ret);
        return ESP_FAIL;
    }

    int video_cam_fd0 = app_video_open(0);
    if (video_cam_fd0 < 0) {
        ESP_LOGE(mipi_cam_tag, "video cam open failed");
        return ESP_FAIL;
    }

    ret = app_video_init(video_cam_fd0, APP_VIDEO_FMT_RGB565);
    if (ret != ESP_OK) {
        ESP_LOGE(mipi_cam_tag, "Video cam init failed with error 0x%x", ret);
        return ESP_FAIL;
    }

    xTaskCreatePinnedToCore(lcd_cam_task, "lcd cam display", 4096,
                            &video_cam_fd0, 4, NULL, 0);
    return ESP_OK;
}

static void lcd_cam_task(void *arg)
{
    int video_fd = *((int *)arg);

    struct v4l2_buffer buf;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_format format = { 0 };
    format.type = type;

    void *lcd_buffer[2];
    void *draw_buffer = NULL;
    size_t data_cache_line_size = 0;

    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(lcddev.lcd_panel_handle,
                                                       2, &lcd_buffer[0], &lcd_buffer[1]));
    draw_buffer = lcd_buffer[1];
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    void *camera_outbuf[2];
    ESP_ERROR_CHECK(camera_set_bufs(video_fd, 2, NULL));
    ESP_ERROR_CHECK(camera_get_bufs(2, &camera_outbuf[0]));

    ppa_client_handle_t ppa_srm_handle = NULL;
    ppa_client_config_t ppa_srm_config = {
        .oper_type             = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &ppa_srm_handle));
    ESP_ERROR_CHECK(camera_stream_start(video_fd));

#if FPS_FUNC_ON == 1
    int fps_count = 0;
    int64_t start_time = esp_timer_get_time();
#endif

    if (ioctl(video_fd, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(mipi_cam_tag, "get fmt failed");
    }
    ESP_LOGI(mipi_cam_tag, "camera %ldx%ld -> preview %dx%d, stream full frame",
             (long)format.fmt.pix.width, (long)format.fmt.pix.height, CROP_W, CROP_H);

    while (1) {
        draw_buffer = lcd_buffer[0] == draw_buffer ? lcd_buffer[1] : lcd_buffer[0];

#if FPS_FUNC_ON == 1
        fps_count++;
        if (fps_count == 50) {
            int64_t end_time = esp_timer_get_time();
            float fps = 1000000.0f / ((end_time - start_time) / 50.0f);
            ESP_LOGI(mipi_cam_tag, "fps: %.1f @ http://%s",
                     (double)fps, web_server_lan_ip());
            start_time = end_time;
            fps_count = 0;
        }
#endif
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(video_fd, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGE(mipi_cam_tag, "failed to receive video frame");
            break;
        }

        if (mipidev.id == 0x7796) {
            ppa_srm_oper_config_t oper_config = {
                .in.buffer          = camera_outbuf[buf.index],
                .in.pic_w           = format.fmt.pix.width,
                .in.pic_h           = format.fmt.pix.height,
                .in.block_w         = CROP_W,
                .in.block_h         = CROP_H,
                .in.block_offset_x  = CROP_X,
                .in.block_offset_y  = CROP_Y,
                .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

                .out.buffer         = draw_buffer,
                .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8,
                                                  data_cache_line_size),
                .out.pic_w          = lcddev.width,
                .out.pic_h          = lcddev.height,
                .out.block_offset_x = 0,
                .out.block_offset_y = 0,
                .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
                .rotation_angle     = PPA_SRM_ROTATION_ANGLE_0,
                .scale_x            = 1.0f,
                .scale_y            = 1.0f,
                .mirror_x           = false,
                .mode               = PPA_TRANS_MODE_BLOCKING,
            };
            ppa_do_scale_rotate_mirror(ppa_srm_handle, &oper_config);

            /* feed the MJPEG stream with the full camera frame (HW JPEG) */
            stream_jpeg_push_frame(camera_outbuf[buf.index],
                                   format.fmt.pix.width, format.fmt.pix.height);
        }

        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(mipi_cam_tag, "failed to free video frame");
        }
    }
}

int mipi_cam_get_rotation(void)
{
    return s_rotation;
}

void mipi_cam_set_rotation(int deg)
{
    if (deg != 90 && deg != 180 && deg != 270) deg = 0;
    s_rotation = deg;
    /* applied by app_video via the sensor controls on the next frame */
    app_video_set_rotation(deg);
}
