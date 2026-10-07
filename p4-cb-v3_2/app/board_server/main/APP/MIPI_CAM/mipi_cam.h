#ifndef __MIPI_CAM_H
#define __MIPI_CAM_H

#include "esp_err.h"

/* Init camera + start the preview task: each captured frame is
 *  - PPA-scaled/cropped into the MIPI-DSI LCD frame buffer (live preview), and
 *  - pushed to the HW-JPEG MJPEG stream (stream_jpeg_push_frame).
 * Call after lcd_init() and stream_jpeg_init(). */
esp_err_t mipi_cam_init(void);

/* Rotation via sensor vflip/hmirror: 0 / 90 / 180 / 270. */
int      mipi_cam_get_rotation(void);
void     mipi_cam_set_rotation(int deg);

#endif
