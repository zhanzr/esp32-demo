# board_server for the ESP32-P4 CB V3.2

Embedded web demo ported from `f4-demo/fire-f429/e_server` (single-page
frontend bundled into `web_assets.h` by `build_web.py` + a C backend), with
the hardware side fully adapted to this board:

| e_server feature | P4 CB V3.2 implementation |
|------------------|---------------------------|
| LED tab (1 LED, PD12) | **2 LEDs**: LED0 (green, GPIO 14) + LED1 (blue, GPIO 13), low active — `GET/POST /api/leds` |
| Sensors tab (8 ADC/IMU/DHT11 plots) | **die temperature** only, from the internal sensor (`GET /api/adc` → `{"temp_c":..}`) |
| Camera tab (placeholder) | **live OV5647 MJPEG stream** (`/stream`), single-frame `/capture`, `/api/camera/control?rot=&quality=` — the same frames are previewed on the 3.5" MIPI-DSI LCD simultaneously |
| Board info tab | `arch: esp32p4` + real LAN IP; public IP / geo / weather stay `null` (no HTTPS client yet) |

Frontend adapted accordingly (2 LED checkboxes, one temperature plot, OV5647
text, P4 board photos embedded from `public/`).

## Runtime pipeline

```
OV5647 (MIPI-CSI, RAW8 1024x600@30) 
  -> ISP -> RGB565
     -> [preview task] PPA crop 480x320 -> LCD frame buffer (DSI, ST7796U)
     -> [same frame]   HW JPEG encoder (esp_driver_jpeg) -> latest-JPEG buffer
             -> HTTP /stream (MJPEG multipart) and /capture
```

One camera consumer feeds both outputs, so the LCD preview and the browser
stream always show the same picture. Frames are skipped (never queued) when
the network is slower than the camera.

WiFi runs on the on-board **ESP32-C6** co-module (esp_hosted, SDIO slot 1 on
GPIO 49–54, reset GPIO 19) — the C6 needs the esp-hosted slave firmware
flashed (see `../wifi_con_test/README.md`).

## API

Same contract as the F429 reference (`server.c`), plus the camera stream:

| Route | Description |
|-------|-------------|
| `GET /` | the page (gzip) |
| `GET /api/leds` | `{"leds":[led0,led1]}` |
| `POST /api/leds` | `{"leds":[1,0]}` → applied, echoed |
| `GET /api/adc` | `{"temp_c":28.6,"ts":..}` |
| `GET /api/camera` | `{"source":"ov5647","ready":1,"w":1024,"h":600,"frames":N,"rot":0,"quality":40,"ts":..}` |
| `GET /api/camera/control?rot=90&quality=60` | sensor vflip/hmirror + JPEG quality |
| `GET /stream` | MJPEG multipart |
| `GET /capture` | latest single JPEG |
| `GET /api/info` | `{"arch":"esp32p4","lan_ip":..,...}` |
| `GET /public/*` | embedded board photos |

## Frontend rebuild

After editing `web/` or `public/`:

```bash
python ..\..\e_server\build_web.py --out main\web_assets.h
idf.py build
```

## Known component patches (same as ov5647_lcd_preview)

- `esp_cam_sensor` 0.5.3: CMakeLists `REQUIRES` needs `esp_driver_gpio`
- `esp_video` 0.5.1: `esp_video_csi_device.c` uses the vendor's v6-adapted
  version (CSI-bridge RAW passthrough + `CAM_CTLR_COLOR_YUV422_UYVY`)
