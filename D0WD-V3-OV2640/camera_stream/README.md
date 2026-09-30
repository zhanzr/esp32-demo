# OV2640 MJPEG Stream Server (ESP32-CAM, ESP32-D0WD-V3 + OV2640)

STA WiFi web server that streams the OV2640 as MJPEG and exposes a control
page (resolution / quality / pixel format / rotation) with a live FPS readout
and an HSV color/shape classifier. Ported from
`s3-n16r8-ov5640/camera_stream` (the more complete baseline — it has the
`camera_reconfigure()` path, WiFi power-save fix, per-second stall
diagnostics; the `s3-n8r8-ov3660` copy lacks those).

## Hardware connection (module datasheet pin map)

PWDN = GPIO 32, no reset pin.

| Signal | GPIO | Signal | GPIO |
|--------|------|--------|------|
| XCLK   | 0    | D7 (Y9) | 35  |
| SIOD   | 26   | D6 (Y8) | 34  |
| SIOC   | 27   | D5 (Y7) | 39  |
| VSYNC  | 25   | D4 (Y6) | 36  |
| HREF   | 23   | D3 (Y5) | 21  |
| PCLK   | 22   | D2 (Y4) | 19  |
| PWDN   | 32   | D1 (Y3) | 18  |
|        |      | D0 (Y2) | 5   |

The ESP32-CAM module wires the camera to the exact AI-Thinker pin map above
(`CAMERA_MODEL_AI_THINKER` in esp32-camera examples). Note GPIO 0 (XCLK) is
also the download-mode strap pin — the camera starts after boot, so flashing
still works.

## Sensor differences: OV2640 vs the S3 boards' OV5640/OV3660

| Aspect | OV2640 (this board) | OV5640 (`s3-n16r8-ov5640`) | OV3660 (`s3-n8r8-ov3660`) |
|--------|--------------------|-----------------------------|----------------------------|
| Sensor ID (PID) | 0x26 | 0x5640 | 0x3660 |
| Max resolution | **UXGA 1600x1200** (2 MP) | QSXGA 2560x1920 / 5MP (driver-clamped to QSXGA) | QXGA 2048x1536 |
| JPEG encoder | In-chip, fast even at UXGA | 5MP-class, stalls under 4 s timeout at QSXGA/5MP low quality | similar class |
| Internal PLL | none (PCLK fixed by XCLK divider) | PLL (PCLK 11.25 MHz) | PLL (PCLK 10 MHz) |
| Autofocus | none | optional AF lens (`CAMERA_AF_SUPPORT`) | none |

Port consequences:

- Resolution list capped at **UXGA** — QXGA/QSXGA/5MP removed from the web UI
  and `g_res_map` (the OV5640-only options).
- The `/control` workaround that forced `quality >= 30` at QSXGA/5MP was
  removed — not needed: the OV2640's UXGA JPEG encode at quality 12 finishes
  well inside the driver's 4 s frame timeout.
- No `CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_CUSTOM` in sdkconfig.defaults: the
  AUTO JPEG buffer (`w*h/5` = 384 KB at UXGA) plus 2 frame buffers fit the
  4 MB PSRAM with room to spare (the S3 port needed a fixed 2 MB buffer for
  5MP stills).
- Everything else (two httpd instances, warmup-frame discard, `fb_count = 2`
  + `CAMERA_GRAB_LATEST`, `WIFI_PS_NONE`) carries over unchanged — those are
  target/sensor-independent fixes.

## Web UI

Same page as the S3 version served from `/`: Start/Stop/Grab, resolution
(QQVGA / QVGA / **VGA**), quality slider, JPEG/RGB565, rotation, FPS + status
polling, and the color/shape classifier (`/classify`) with stream box overlay
(`detect=1`). `classifier.c` is plain CV (HSV buckets + connected components)
and ports unchanged.

- **LED slider** (default 0%): drives the on-board flash LED (GPIO 4) via a
  second LEDC timer/channel (timer 1 / ch 1 — timer 0 / ch 0 generates the
  camera XCLK). Applies immediately on slider release (`/control?led=N`);
  the current intensity is also reported by `/status` (`"led":N`). Note this
  is the same physical LED `blink_hello` pulses at 5%.
- **Resolutions capped at VGA 640x480** (QQVGA/QQVGA/VGA): the ESP32-CAM's
  4 MB PSRAM + WiFi bandwidth make anything above VGA pointless for
  streaming — use `/capture` stills at higher sizes if ever needed (the
  `res` map accepts up to UXGA via URL even though the UI hides it).

## Component & config

- `main/idf_component.yml`: `espressif/esp32-camera: "^2.1.7"` — supports the
  classic ESP32 (DVP/LLCam path).
- sdkconfig.defaults: esp32 @ 240 MHz, 4 MB flash, 40 MHz crystal,
  `CONFIG_SPIRAM=y` @ 40 MHz (the mapped 4 MB of the 8 MB PSRAM device).
- `xclk_freq_hz = 20 MHz` (module datasheet test value).
- WiFi: STA via `main/wifi_config.h` (same credentials as `wifi_con_test`;
  `.example` has placeholders).
- Rotation uses `set_vflip`/`set_hmirror` (supported by the OV2640 driver).

## Status

Built and verified on hardware: boots, joins WiFi, `Sensor: pid=0x26`,
stream/capture/classify endpoints live — see the log capture in the repo
history. Expected performance: the ESP32 (LX6) MJPEG streams slower than the
S3 boards (single LSU bandwidth + 40 MHz SPIRAM); QVGA should still run
smoothly, UXGA is stills territory (use `/capture`).
