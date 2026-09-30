#include <string.h>
#include "classifier.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "img_converters.h"

static const char *TAG = "class";

/* HSV color buckets (H 0-360, S/V 0-100) tuned on the OV5640. */
static const char *COLOR_NAMES[6] = { "red", "orange", "yellow", "green", "blue", "purple" };

#define CLASS_NONE  255

/* Minimum blob area as a fraction of the downscaled frame (keeps noise out). */
#define MIN_BLOB_FRAC 0.003f

/* Map a 16-bit RGB565 pixel to a color class index (0-5) or -1 if no match. */
static inline int rgb565_to_class(uint16_t px)
{
    uint8_t r5 = (uint8_t)(px >> 11) & 0x1F;
    uint8_t g6 = (uint8_t)(px >> 5)  & 0x3F;
    uint8_t b5 = (uint8_t)px         & 0x1F;
    int r = (r5 << 3) | (r5 >> 2);
    int g = (g6 << 2) | (g6 >> 1);
    int b = (b5 << 3) | (b5 >> 2);

    int max = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    int min = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    int d = max - min;
    if (d == 0) {
        return -1;   /* achromatic */
    }
    int v = (max * 100) / 255;
    int s = (d * 100) / max;
    if (s < 40 || v < 40) {
        return -1;   /* too dim / too grey */
    }

    int h;
    if (max == r) {
        h = 60 * (g - b) / d;
        if (h < 0) {
            h += 360;
        }
    } else if (max == g) {
        h = 60 * (b - r) / d + 120;
    } else {
        h = 60 * (r - g) / d + 240;
    }

    if (h < 15 || h >= 345)  return 0;   /* red     */
    if (h < 45)              return 1;   /* orange  */
    if (h < 75)              return 2;   /* yellow  */
    if (h < 160)             return 3;   /* green   */
    if (h < 260)             return 4;   /* blue    */
    return 5;                            /* purple  */
}

static const char *classify_shape(uint32_t area, int w, int h,
                                  float *fill_out, float *aspect_out)
{
    if (w <= 0 || h <= 0) {
        *fill_out = 0.0f;
        *aspect_out = 0.0f;
        return "unknown";
    }
    float fw = (float)w;
    float fh = (float)h;
    float fill = (float)area / (fw * fh);
    float aspect = fw / fh;
    *fill_out = fill;
    *aspect_out = aspect;

    /* Elongated region -> bar. Axis-aligned shape -> bbox fill ratio. */
    if (aspect > 2.4f || aspect < 0.42f) return "bar";
    if (fill >= 0.85f)                   return "rect";
    if (fill >= 0.68f)                   return "circle";
    if (fill >= 0.42f)                   return "triangle";
    return "unknown";
}

esp_err_t classify_rgb565(const uint16_t *rgb, int width, int height,
                          int downscale, classify_result_t *out)
{
    if (!rgb || !out || width <= 0 || height <= 0 || downscale < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    int sw = width / downscale;
    int sh = height / downscale;
    if (sw < 4 || sh < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *cls = heap_caps_malloc((size_t)sw * sh, MALLOC_CAP_SPIRAM);
    uint32_t *q = heap_caps_malloc((size_t)sw * sh * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!cls || !q) {
        heap_caps_free(cls);
        heap_caps_free(q);
        return ESP_ERR_NO_MEM;
    }

    /* 1) Build the class map (subsampled grid). */
    const uint16_t *row_src = rgb;
    for (int y = 0; y < sh; y++) {
        const uint16_t *p = row_src;
        for (int x = 0; x < sw; x++) {
            int c = rgb565_to_class(*p);
            cls[y * sw + x] = (uint8_t)((c >= 0) ? c : CLASS_NONE);
            p += downscale;
        }
        row_src += downscale * width;
    }

    /* 2) Connected components (BFS) per color; keep the largest blob per color. */
    out->count = 0;
    uint32_t best_area[6] = { 0 };
    uint32_t min_area = (uint32_t)(MIN_BLOB_FRAC * sw * sh);
    if (min_area < 8) {
        min_area = 8;
    }

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            uint8_t c = cls[y * sw + x];
            if (c >= 6) {
                continue;   /* already visited or none */
            }

            /* BFS flood fill from (x, y). */
            uint32_t head = 0, tail = 0;
            q[tail++] = (uint32_t)(y * sw + x);
            cls[y * sw + x] = CLASS_NONE;   /* mark visited */

            uint32_t area = 0;
            int minX = x, maxX = x, minY = y, maxY = y;
            int64_t sumX = 0, sumY = 0;

            while (head < tail) {
                uint32_t idx = q[head++];
                int yy = (int)(idx / sw);
                int xx = (int)(idx % sw);
                area++;
                sumX += xx;
                sumY += yy;
                if (xx < minX) minX = xx;
                if (xx > maxX) maxX = xx;
                if (yy < minY) minY = yy;
                if (yy > maxY) maxY = yy;

                if (xx > 0     && cls[idx - 1] == c) { cls[idx - 1] = CLASS_NONE; q[tail++] = idx - 1; }
                if (xx < sw - 1 && cls[idx + 1] == c) { cls[idx + 1] = CLASS_NONE; q[tail++] = idx + 1; }
                if (yy > 0     && cls[idx - sw] == c) { cls[idx - sw] = CLASS_NONE; q[tail++] = idx - sw; }
                if (yy < sh - 1 && cls[idx + sw] == c) { cls[idx + sw] = CLASS_NONE; q[tail++] = idx + sw; }
            }

            if (area < min_area || area <= best_area[c]) {
                continue;
            }
            best_area[c] = area;

            int bw = (maxX - minX + 1);
            int bh = (maxY - minY + 1);
            float fill, aspect;
            const char *shape = classify_shape(area, bw, bh, &fill, &aspect);

            classify_det_t *d = &out->dets[c];
            d->color = COLOR_NAMES[c];
            d->shape = shape;
            d->area  = area;
            d->x     = minX * downscale;
            d->y     = minY * downscale;
            d->w     = bw * downscale;
            d->h     = bh * downscale;
            d->cx    = (int)(sumX / area) * downscale;
            d->cy    = (int)(sumY / area) * downscale;
            d->fill  = fill;
            d->aspect = aspect;
        }
    }

    for (int i = 0; i < 6; i++) {
        if (best_area[i]) {
            out->dets[out->count++] = out->dets[i];
        }
    }

    heap_caps_free(cls);
    heap_caps_free(q);
    return ESP_OK;
}

esp_err_t classify_frame(const camera_fb_t *fb, int downscale,
                         classify_result_t *out)
{
    if (!fb || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fb->format == PIXFORMAT_RGB565) {
        return classify_rgb565((const uint16_t *)fb->buf, fb->width, fb->height,
                               downscale, out);
    }

    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "Unsupported format %d", fb->format);
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t rgb_len = (size_t)fb->width * fb->height * 2;
    uint8_t *rgb = heap_caps_malloc(rgb_len, MALLOC_CAP_SPIRAM);
    if (!rgb) {
        return ESP_ERR_NO_MEM;
    }
    if (!jpg2rgb565(fb->buf, fb->len, rgb, JPG_SCALE_NONE)) {
        ESP_LOGE(TAG, "JPEG decode failed");
        heap_caps_free(rgb);
        return ESP_FAIL;
    }
    esp_err_t ret = classify_rgb565((const uint16_t *)rgb, fb->width, fb->height,
                                    downscale, out);
    heap_caps_free(rgb);
    return ret;
}
