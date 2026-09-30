#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include <stdint.h>
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One detected object: largest blob of a given color, with its shape. */
typedef struct {
    const char *color;   /* "red", "orange", ... */
    const char *shape;   /* "circle", "triangle", "rect", "bar", "unknown" */
    uint32_t area;       /* blob pixel count (downscaled space, *ds^2 to map) */
    int x, y, w, h;      /* bounding box in full-frame pixels */
    int cx, cy;          /* centroid in full-frame pixels */
    float fill;          /* area / (bbox_w*bbox_h) -> shape discriminator */
    float aspect;        /* bbox_w / bbox_h */
} classify_det_t;

typedef struct {
    int count;
    classify_det_t dets[6];   /* one (largest) blob per color class */
} classify_result_t;

/* Analyze an RGB565 frame. downscale >= 1 speeds up blob search (pixel grid
 * subsampling); returned bbox/centroid are scaled back to full-frame coords.
 * Result holds at most one object per color (the largest blob of that color).
 */
esp_err_t classify_rgb565(const uint16_t *rgb, int width, int height,
                          int downscale, classify_result_t *out);

/* Analyze a camera frame: decodes JPEG (or passes through RGB565) first. */
esp_err_t classify_frame(const camera_fb_t *fb, int downscale,
                         classify_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CLASSIFIER_H */
