#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "img_converters.h"
#include "app_httpd.h"
#include "classifier.h"

static const char *TAG = "httpd";

/* The MJPEG stream loops forever inside the httpd handler, and esp_http_server
 * processes all connections in a single task. So the stream must run on its
 * own httpd instance (port 81) or /status and /control stall while streaming.
 */
#define MAIN_SERVER_PORT   80
#define STREAM_SERVER_PORT 81

#define PART_BOUNDARY   "123456789000000000000987654321"
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY
#define STREAM_BOUNDARY "\r\n--" PART_BOUNDARY "\r\n"
#define STREAM_PART     "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

static volatile int g_streaming = 0;
static float g_fps = 0.0f;
static int g_quality = 12;
static int g_rot = 0;
static framesize_t g_framesize = FRAMESIZE_QVGA;
static pixformat_t g_pixfmt = PIXFORMAT_JPEG;
static volatile int g_detect = 0;   /* draw boxes + classify on the stream */
static int g_led = 0;               /* flash LED intensity, percent */

static const struct {
    const char *name;
    framesize_t size;
} g_res_map[] = {
    { "QQVGA", FRAMESIZE_QQVGA },
    { "QVGA",  FRAMESIZE_QVGA  },
    { "VGA",   FRAMESIZE_VGA   },
    { "SVGA",  FRAMESIZE_SVGA  },
    { "XGA",   FRAMESIZE_XGA   },
    { "HD",    FRAMESIZE_HD    },
    { "SXGA",  FRAMESIZE_SXGA  },
    { "UXGA",  FRAMESIZE_UXGA  },
    { NULL, 0 },   /* OV2640 maxes out at UXGA (QXGA/5MP are OV5640-only) */
};

/* -------------------------------------------------------------------------
 * /  -> control page
 * ---------------------------------------------------------------------- */
static const char g_index_html[] =
"<!DOCTYPE html>\n"
"<html><head><meta charset=\"utf-8\">\n"
"<title>OV2640 Stream</title>\n"
"<style>\n"
"body{font-family:Segoe UI,Arial,sans-serif;background:#121212;color:#eee;margin:16px}\n"
".card{background:#1e1e1e;border:1px solid #333;border-radius:8px;padding:12px;margin-bottom:12px}\n"
"select,button,input{background:#2a2a2a;color:#eee;border:1px solid #555;border-radius:4px;padding:6px 8px;margin:4px}\n"
"button{cursor:pointer}\n"
"h2,h3{margin:4px 0}\n"
"#view{width:100%;max-width:960px;background:#000;border:1px solid #333;border-radius:8px;min-height:240px}\n"
"#view img{width:100%;display:block}\n"
"#stream{display:none}\n"
"#still{display:none}\n"
"</style></head><body>\n"
"<h2>ESP32 D0WD-V3-OV2640</h2>\n"
"<div class=\"card\">\n"
"<label>Mode:\n"
"<select id=\"mode\"><option value=\"stream\" selected>Stream</option><option value=\"static\">Static</option></select>\n"
"</label>\n"
"<button id=\"btnStart\">Start</button>\n"
"<button id=\"btnStop\" style=\"display:none\">Stop</button>\n"
"<button id=\"btnGrab\" style=\"display:none\">Grab</button>\n"
"<button id=\"btnClass\">Classify</button>\n"
"<label><input type=\"checkbox\" id=\"det\">Draw boxes</label>\n"
"<span>FPS: <b id=\"fps\">-</b></span>\n"
"<span>Status: <b id=\"stat\">idle</b></span>\n"
"</div>\n"
"<div class=\"card\">\n"
"<b>Detection:</b> <span id=\"detres\">-</span>\n"
"</div>\n"
"<div class=\"card\">\n"
"<label>Resolution:\n"
"<select id=\"res\">\n"
"<option value=\"QQVGA\">QQVGA 160x120</option>\n"
"<option value=\"QVGA\" selected>QVGA 320x240</option>\n"
"<option value=\"VGA\">VGA 640x480</option>\n"
"</select></label>\n"
"<label>Rotation:\n"
"<select id=\"rot\"><option value=\"0\" selected>0 deg</option><option value=\"90\">90 deg</option><option value=\"180\">180 deg</option><option value=\"270\">270 deg</option></select>\n"
"</label>\n"
"<label>Quality: <input type=\"range\" id=\"q\" min=\"4\" max=\"63\" value=\"12\"> <b id=\"qval\">12</b></label>\n"
"<label>Format:\n"
"<select id=\"pix\"><option value=\"JPEG\" selected>JPEG</option><option value=\"RGB565\">RGB565</option></select>\n"
"</label>\n"
"<label>LED: <input type=\"range\" id=\"led\" min=\"0\" max=\"100\" value=\"0\"> <b id=\"ledval\">0%</b></label>\n"
"<button id=\"btnApply\">Apply</button>\n"
"</div>\n"
"<div id=\"view\"><img id=\"stream\"><img id=\"still\"></div>\n"
"<script>\n"
"var m=document.getElementById('mode'),s=document.getElementById('stream'),\n"
"    st=document.getElementById('still'),bs=document.getElementById('btnStart'),\n"
"    bp=document.getElementById('btnStop'),bg=document.getElementById('btnGrab'),\n"
"    f=document.getElementById('fps'),t=document.getElementById('stat'),\n"
"    r=document.getElementById('res'),q=document.getElementById('q'),\n"
"    qv=document.getElementById('qval'),p=document.getElementById('pix'),\n"
"    rot=document.getElementById('rot'),det=document.getElementById('det'),\n"
"    dr=document.getElementById('detres'),bc=document.getElementById('btnClass'),\n"
"    led=document.getElementById('led'),ledv=document.getElementById('ledval');\n"
"var streaming=false;\n"
"var STREAM_URL='http://'+location.hostname+':81/stream';\n"
"function show(el){el.style.display='inline-block';}\n"
"function hide(el){el.style.display='none';}\n"
"function showimg(el){el.style.display='block';}\n"
"function hideimg(el){el.style.display='none';}\n"
"function refreshButtons(){\n"
"  if(m.value!=='stream'){hide(bs);hide(bp);show(bg);return;}\n"
"  hide(bg);\n"
"  if(streaming){show(bp);hide(bs);}else{show(bs);hide(bp);}\n"
"}\n"
"function startStream(){\n"
"  streaming=true;t.textContent='starting';showimg(s);hideimg(st);s.src=STREAM_URL;refreshButtons();\n"
"}\n"
"function stopStream(){streaming=false;s.removeAttribute('src');t.textContent='stopped';refreshButtons();}\n"
"function grab(){st.src='/capture?ts='+Date.now();t.textContent='grabbed';}\n"
"function setMode(){\n"
"  if(m.value==='stream'){hideimg(st);showimg(s);}\n"
"  else{hideimg(s);showimg(st);stopStream();}\n"
"  refreshButtons();\n"
"}\n"
"function applyCfg(){\n"
"  fetch('/control?res='+encodeURIComponent(r.value)+'&quality='+q.value+'&pixfmt='+encodeURIComponent(p.value)+'&rot='+encodeURIComponent(rot.value)+'&detect='+(det.checked?1:0))\n"
"    .then(function(){t.textContent='applied';});\n"
"  if(streaming){stopStream();startStream();}\n"
"}\n"
"function classify(){\n"
"  bc.textContent='Classifying...';t.textContent='classifying';\n"
"  fetch('/classify?ts='+Date.now()).then(function(x){return x.json();}).then(function(j){\n"
"    if(!j.dets||!j.dets.length){dr.textContent='nothing detected';t.textContent='classify done';}\n"
"    else{\n"
"      var parts=[];\n"
"      for(var i=0;i<j.dets.length;i++){var d=j.dets[i];parts.push(d.color+' '+d.shape);}\n"
"      dr.textContent=parts.join(', ');t.textContent='classify done';\n"
"    }\n"
"  }).catch(function(){dr.textContent='classify error';}).then(function(){bc.textContent='Classify';});\n"
"}\n"
"bs.onclick=function(){if(m.value!=='stream'){m.value='stream';setMode();}startStream();};\n"
"bp.onclick=function(){stopStream();};\n"
"bg.onclick=grab;\n"
"bc.onclick=classify;\n"
"det.onchange=function(){applyCfg();};\n"
"q.oninput=function(){qv.textContent=q.value;};\n"
"led.oninput=function(){ledv.textContent=led.value+'%';};\n"
"led.onchange=function(){fetch('/control?led='+led.value);};\n"
"m.onchange=setMode;\n"
"rot.onchange=function(){applyCfg();};\n"
"setMode();\n"
"setInterval(function(){\n"
"  fetch('/status').then(function(x){return x.json();}).then(function(j){\n"
"    if(j.streaming){f.textContent=j.fps.toFixed(1);t.textContent='streaming '+j.w+'x'+j.h+' '+(j.format===0?'RGB565':'JPEG')+(j.detect?' [detect]':'');}\n"
"    else f.textContent='-';\n"
"  }).catch(function(){});\n"
"},1000);\n"
"</script></body></html>\n";

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, g_index_html, strlen(g_index_html));
}

/* -------------------------------------------------------------------------
 * /favicon.ico -> quiet 204 (browsers always ask for it)
 * ---------------------------------------------------------------------- */
static esp_err_t handler_favicon(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_sendstr(req, "");
}

/* -------------------------------------------------------------------------
 * Detection overlay: RGB565 drawing helpers
 * ---------------------------------------------------------------------- */
static const uint16_t g_color_rgb565[6] = {
    0xF800,  /* red    */
    0xFD20,  /* orange */
    0xFFE0,  /* yellow */
    0x07E0,  /* green  */
    0x001F,  /* blue   */
    0xF81F,  /* purple */
};

static void draw_rect(uint16_t *rgb, int W, int H, int x, int y, int w, int h,
                      uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;

    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            rgb[j * W + i] = color;
        }
    }
}

static void draw_box(uint16_t *rgb, int W, int H, int x, int y, int w, int h,
                     uint16_t color)
{
    const int t = 2;
    draw_rect(rgb, W, H, x, y, w, t, color);                /* top    */
    draw_rect(rgb, W, H, x, y + h - t, w, t, color);        /* bottom */
    draw_rect(rgb, W, H, x, y, t, h, color);                /* left   */
    draw_rect(rgb, W, H, x + w - t, y, t, h, color);        /* right  */
}

/* Capture a frame, run detection, draw boxes, return as a JPEG buffer.
 * Caller owns *jpg_buf (free it). */
static esp_err_t stream_detect_frame(camera_fb_t *fb, uint8_t **jpg_buf,
                                     size_t *jpg_len)
{
    int W = fb->width, H = fb->height;
    uint8_t *rgb = heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM);
    if (!rgb) {
        return ESP_ERR_NO_MEM;
    }

    if (fb->format == PIXFORMAT_RGB565) {
        memcpy(rgb, fb->buf, (size_t)W * H * 2);
    } else if (fb->format == PIXFORMAT_JPEG) {
        if (!jpg2rgb565(fb->buf, fb->len, rgb, JPG_SCALE_NONE)) {
            ESP_LOGE(TAG, "JPEG decode failed");
            heap_caps_free(rgb);
            return ESP_FAIL;
        }
    } else {
        heap_caps_free(rgb);
        return ESP_ERR_NOT_SUPPORTED;
    }

    classify_result_t res;
    if (classify_rgb565((const uint16_t *)rgb, W, H, 2, &res) == ESP_OK) {
        for (int i = 0; i < res.count; i++) {
            /* dets[] is ordered by color class 0..5 -> red..purple */
            classify_det_t *d = &res.dets[i];
            uint16_t col = g_color_rgb565[i];
            draw_box((uint16_t *)rgb, W, H, d->x, d->y, d->w, d->h, col);
            draw_rect((uint16_t *)rgb, W, H, d->x, d->y, 24, 8, col);
        }
    }

    bool ok = fmt2jpg(rgb, (size_t)W * H * 2, W, H, PIXFORMAT_RGB565, 70,
                      jpg_buf, jpg_len);
    heap_caps_free(rgb);
    return ok ? ESP_OK : ESP_FAIL;
}

/* -------------------------------------------------------------------------
 * /stream (own server on port 81) -> MJPEG multipart stream
 * ---------------------------------------------------------------------- */
static esp_err_t handler_stream(httpd_req_t *req)
{
    static int64_t last_fps_ts = 0;
    static uint32_t frames = 0;

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    g_streaming = 1;
    g_fps = 0.0f;
    last_fps_ts = 0;
    frames = 0;

    esp_err_t res = ESP_OK;
    bool first = true;
    /* Per-second stall diagnostics: separates camera-side (fb_get) stalls
     * from network-side (send) stalls. */
    int64_t grab_max = 0, send_max = 0;
    int n_frames = 0, n_slow_grab = 0, n_slow_send = 0;
    while (true) {
        int64_t t_grab0 = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        int64_t t_grab = esp_timer_get_time() - t_grab0;
        if (t_grab > grab_max) grab_max = t_grab;
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            break;
        }
        if (t_grab > 200000) n_slow_grab++;
        if (first) {
            /* First frame after stream start can be missing the JPEG SOI
             * marker (cam_hal NO-SOI). Discard it, the next one is clean. */
            first = false;
            esp_camera_fb_return(fb);
            continue;
        }

        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        if (g_detect) {
            if (stream_detect_frame(fb, &jpg_buf, &jpg_len) != ESP_OK) {
                ESP_LOGE(TAG, "Detection/encode failed");
                esp_camera_fb_return(fb);
                break;
            }
        } else if (fb->format != PIXFORMAT_JPEG) {
            if (!frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
                ESP_LOGE(TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                break;
            }
        } else {
            jpg_buf = fb->buf;
            jpg_len = fb->len;
        }

        char part_buf[64];
        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)jpg_len);
        if (hlen < 0 || hlen >= (int)sizeof(part_buf)) {
            ESP_LOGE(TAG, "Part header truncated");
        } else {
            res = httpd_resp_send_chunk(req, part_buf, (size_t)hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }
        int64_t t_send = esp_timer_get_time() - t_grab0;
        if (t_send > send_max) send_max = t_send;
        if (t_send > 200000) n_slow_send++;

        if (g_detect || fb->format != PIXFORMAT_JPEG) {
            free(jpg_buf);   /* detection path always allocates its own JPEG */
        }
        esp_camera_fb_return(fb);
        if (res != ESP_OK) {
            break;   /* client closed the connection (Stop/reload) */
        }

        frames++;
        n_frames++;
        int64_t now = esp_timer_get_time();
        if (last_fps_ts == 0) {
            last_fps_ts = now;
        }
        if (now - last_fps_ts >= 1000000) {
            g_fps = (float)frames * 1000000.0f / (float)(now - last_fps_ts);
            frames = 0;
            last_fps_ts = now;
            ESP_LOGI(TAG, "diag: fps=%.1f grab_max=%dms send_max=%dms slow_grab=%d slow_send=%d",
                     (double)g_fps, (int)(grab_max / 1000), (int)(send_max / 1000),
                     n_slow_grab, n_slow_send);
            grab_max = send_max = 0;
            n_slow_grab = n_slow_send = 0;
        }
    }

    g_streaming = 0;
    g_fps = 0.0f;
    ESP_LOGI(TAG, "diag: stream ended after %d frames (grab_max=%dms send_max=%dms)",
             n_frames, (int)(grab_max / 1000), (int)(send_max / 1000));
    return ESP_OK;   /* suppress "uri handler execution failed" on client disconnect */
}

/* -------------------------------------------------------------------------
 * /capture -> single JPEG image
 * ---------------------------------------------------------------------- */
static esp_err_t handler_capture(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    esp_err_t res = ESP_OK;
    if (fb->format == PIXFORMAT_JPEG) {
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        uint8_t *jpg = NULL;
        size_t jlen = 0;
        if (frame2jpg(fb, 80, &jpg, &jlen)) {
            res = httpd_resp_send(req, (const char *)jpg, jlen);
            free(jpg);
        } else {
            httpd_resp_send_500(req);
            res = ESP_FAIL;
        }
    }

    esp_camera_fb_return(fb);
    return res;
}

/* -------------------------------------------------------------------------
 * /status -> JSON with streaming flag, fps, resolution, format
 * ---------------------------------------------------------------------- */
static esp_err_t handler_status(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    int w = 320, h = 240;
    int fmt = PIXFORMAT_JPEG;
    if (s) {
        fmt = s->pixformat;
        if (s->status.framesize < FRAMESIZE_INVALID) {
            w = resolution[s->status.framesize].width;
            h = resolution[s->status.framesize].height;
        }
    }

    char buf[160];
    int len = snprintf(buf, sizeof(buf),
                       "{\"streaming\":%d,\"fps\":%.1f,\"w\":%d,\"h\":%d,\"format\":%d,\"quality\":%d,\"detect\":%d,\"led\":%d}",
                       (int)g_streaming, (double)g_fps, w, h, fmt, g_quality, (int)g_detect, g_led);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, len);
}

/* -------------------------------------------------------------------------
 * /classify -> JSON list of detected color+shape objects in one frame
 * ---------------------------------------------------------------------- */
static esp_err_t handler_classify(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    classify_result_t res;
    esp_err_t err = classify_frame(fb, 2, &res);
    esp_camera_fb_return(fb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Classify failed: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "{\"count\":%d,\"dets\":[", res.count);
    for (int i = 0; i < res.count && len < (int)sizeof(buf); i++) {
        classify_det_t *d = &res.dets[i];
        len += snprintf(buf + len, sizeof(buf) - len,
                        "%s{\"color\":\"%s\",\"shape\":\"%s\",\"area\":%u,"
                        "\"bbox\":[%d,%d,%d,%d],\"cx\":%d,\"cy\":%d,"
                        "\"fill\":%.2f,\"aspect\":%.2f}",
                        i ? "," : "", d->color, d->shape, (unsigned)d->area,
                        d->x, d->y, d->w, d->h, d->cx, d->cy,
                        (double)d->fill, (double)d->aspect);
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, len);
}

/* -------------------------------------------------------------------------
 * /control?res=...&quality=...&pixfmt=...&rot=...  (GET, applied immediately)
 * ---------------------------------------------------------------------- */
static esp_err_t handler_control(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) {
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }
    if (qlen > 255) {
        qlen = 255;
    }
    char query[256];
    httpd_req_get_url_query_str(req, query, qlen + 1);

    char val[64];
    framesize_t new_framesize = g_framesize;
    pixformat_t new_pixfmt = g_pixfmt;
    int new_rot = g_rot;
    int new_quality = g_quality;

    if (httpd_query_key_value(query, "res", val, sizeof(val)) == ESP_OK) {
        for (int i = 0; g_res_map[i].name; i++) {
            if (strcmp(g_res_map[i].name, val) == 0) {
                new_framesize = g_res_map[i].size;
                ESP_LOGI(TAG, "Set framesize: %s", val);
                break;
            }
        }
    }
    if (httpd_query_key_value(query, "quality", val, sizeof(val)) == ESP_OK) {
        int q = atoi(val);
        if (q < 0) q = 0;
        if (q > 63) q = 63;
        new_quality = q;
        ESP_LOGI(TAG, "Set quality: %d", q);
    }
    if (httpd_query_key_value(query, "pixfmt", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "RGB565") == 0) {
            new_pixfmt = PIXFORMAT_RGB565;
            ESP_LOGI(TAG, "Set pixformat: RGB565");
        } else {
            new_pixfmt = PIXFORMAT_JPEG;
            ESP_LOGI(TAG, "Set pixformat: JPEG");
        }
    }
    if (httpd_query_key_value(query, "rot", val, sizeof(val)) == ESP_OK) {
        new_rot = atoi(val);
        ESP_LOGI(TAG, "Set rotation: %d deg", new_rot);
    }
    if (httpd_query_key_value(query, "detect", val, sizeof(val)) == ESP_OK) {
        g_detect = (atoi(val) != 0) ? 1 : 0;
        ESP_LOGI(TAG, "Detection: %s", g_detect ? "ON" : "OFF");
    }
    if (httpd_query_key_value(query, "led", val, sizeof(val)) == ESP_OK) {
        int p = atoi(val);
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        g_led = p;
        camera_led_set_percent(p);
        ESP_LOGI(TAG, "LED intensity: %d%%", p);
    }

    /* Changing resolution or pixel format requires a full camera reinit:
     * cam_hal allocates its DMA frame buffer at init (JPEG: w*h/5, RGB565:
     * w*h*2), so s->set_framesize() alone overflows it at larger sizes. */
    if (new_framesize != g_framesize || new_pixfmt != g_pixfmt) {
        ESP_LOGI(TAG, "Reconfiguring camera (%d -> %d, pixfmt %d -> %d)...",
                 (int)g_framesize, (int)new_framesize, (int)g_pixfmt, (int)new_pixfmt);
        esp_err_t err = camera_reconfigure(new_framesize, new_pixfmt);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reconfigure failed: %s", esp_err_to_name(err));
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        g_framesize = new_framesize;
        g_pixfmt = new_pixfmt;
        /* Reinit resets all sensor settings to defaults -> re-apply them. */
        new_quality = 12;
        /* The first grab after a reinit often has no SOI marker (cam_hal
         * NO-SOI) or can time out while the sensor settles at the new size.
         * Warm up with a single discard frame so the next /capture or /stream
         * is clean. One grab only, so the control handler never blocks for
         * multiple frame timeouts. */
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
            ESP_LOGI(TAG, "Reconfigure warmup frame captured");
        } else {
            ESP_LOGW(TAG, "Reconfigure warmup grab failed");
        }
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_quality(s, new_quality);
        g_quality = new_quality;
        int vflip = 0, hmirror = 0;
        switch (new_rot) {
            case 90:  vflip = 1; hmirror = 0; break;
            case 180: vflip = 1; hmirror = 1; break;
            case 270: vflip = 0; hmirror = 1; break;
            default:  new_rot = 0; break;
        }
        g_rot = new_rot;
        s->set_vflip(s, vflip);
        s->set_hmirror(s, hmirror);
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t register_uri(httpd_handle_t server, const char *uri,
                              esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t u = { .uri = uri, .method = HTTP_GET, .handler = handler, .user_ctx = NULL };
    return httpd_register_uri_handler(server, &u);
}

/* -------------------------------------------------------------------------
 * Server bootstrap (two httpd instances: main + stream)
 * ---------------------------------------------------------------------- */
esp_err_t start_camera_server(void)
{
    /* Main server (port 80): page, capture, status, control. */
    httpd_config_t main_cfg = HTTPD_DEFAULT_CONFIG();
    main_cfg.server_port = MAIN_SERVER_PORT;
    main_cfg.lru_purge_enable = true;
    main_cfg.stack_size = 8192;

    httpd_handle_t main_server = NULL;
    esp_err_t err = httpd_start(&main_server, &main_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start main server: %s", esp_err_to_name(err));
        return err;
    }
    register_uri(main_server, "/", handler_root);
    register_uri(main_server, "/favicon.ico", handler_favicon);
    register_uri(main_server, "/capture", handler_capture);
    register_uri(main_server, "/status", handler_status);
    register_uri(main_server, "/control", handler_control);
    register_uri(main_server, "/classify", handler_classify);
    ESP_LOGI(TAG, "Main server started on port %d", MAIN_SERVER_PORT);

    /* Stream server (port 81): MJPEG only, so it can block forever without
     * stalling /status and /control on the main server. */
    httpd_config_t stream_cfg = HTTPD_DEFAULT_CONFIG();
    stream_cfg.server_port = STREAM_SERVER_PORT;
    stream_cfg.stack_size = 16384;
    /* Both instances default to ctrl_port 32768 (a UDP socket bound to
     * 127.0.0.1), so the second httpd_start fails with EADDRINUSE (112).
     * Give the stream server its own ctrl port. */
    stream_cfg.ctrl_port = STREAM_SERVER_PORT + 32768;

    httpd_handle_t stream_server = NULL;
    err = httpd_start(&stream_server, &stream_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start stream server: %s", esp_err_to_name(err));
        return err;
    }
    register_uri(stream_server, "/stream", handler_stream);
    ESP_LOGI(TAG, "Stream server started on port %d", STREAM_SERVER_PORT);

    return ESP_OK;
}
