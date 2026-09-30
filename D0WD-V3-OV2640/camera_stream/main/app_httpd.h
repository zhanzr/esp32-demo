#pragma once
#include "esp_err.h"
#include "esp_camera.h"

esp_err_t start_camera_server(void);

/* Reinit the camera with a new frame size / pixel format (reallocates the
 * cam_hal DMA frame buffer; see main.c). */
esp_err_t camera_reconfigure(framesize_t frame_size, pixformat_t pixel_format);

/* Set the on-board flash LED (GPIO 4) intensity via LEDC PWM, 0-100%. */
void camera_led_set_percent(int percent);
