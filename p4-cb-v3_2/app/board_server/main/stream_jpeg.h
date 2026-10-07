#ifndef __STREAM_JPEG_H
#define __STREAM_JPEG_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

/* Hardware-JPEG MJPEG stream module (P4 JPEG encoder).
 *
 * The camera task calls stream_jpeg_push_frame() per captured RGB565 frame;
 * the latest encoded JPEG is served by the /stream (MJPEG multipart) and
 * /capture (single frame) HTTP handlers. Frame hand-off is non-blocking:
 * while a client is being served the next frame is skipped, so the camera
 * loop is never stalled by the network.
 */

esp_err_t stream_jpeg_init(void);

/* Encode one RGB565 frame and publish it as the latest JPEG. */
esp_err_t stream_jpeg_push_frame(const uint8_t *rgb565, uint32_t w, uint32_t h);

/* MJPEG multipart handler (register on GET /stream). */
esp_err_t stream_jpeg_http_stream(httpd_req_t *req);

/* Single latest-JPEG handler (register on GET /capture). */
esp_err_t stream_jpeg_http_capture(httpd_req_t *req);

/* Stats for /api/camera. */
uint32_t stream_jpeg_frames(void);
uint32_t stream_jpeg_width(void);
uint32_t stream_jpeg_height(void);
void     stream_jpeg_set_quality(int quality);
int      stream_jpeg_get_quality(void);

#endif
