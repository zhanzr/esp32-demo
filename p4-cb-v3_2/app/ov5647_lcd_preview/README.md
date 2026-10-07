# OV5647 → MIPI-DSI LCD preview (ESP32-P4 CB V3.2)

Captures from the **WKS-OV5647 V1.0** MIPI-CSI camera module and previews it
live on the **WKS35HV035-WCT** 3.5" MIPI-DSI display (ST7796U, 320x480).

Ported from the vendor example
`main-esp32p4-cb-32/vendor_ex/basic__routines/17_mipicamera` (BSP components
carried over unchanged; banner text updated; panel selection set to the 3.5"
ST7796U).

## Pipeline

- **Camera**: `esp_video` (V4L2-style) + `esp_cam_sensor`, OV5647 auto-detect
  on I2C0 (GPIO 28 SDA / GPIO 29 SCL — shared with the ES8311 codec). The
  sensor streams RAW8 1024x600 @ 30 FPS through the ISP, which outputs
  RGB565.
- **LCD**: MIPI-DSI (1 lane, 560 Mbps for the 3.5" ST7796U), DPI panel with
  double frame buffer in PSRAM.
- **Preview loop** (`main/APP/MIPI_CAM/mipi_cam.c`): `VIDIOC_DQBUF` a camera
  frame → **PPA SRM** crops the center 320x480 region (offset 352,60 of the
  1024x600 frame) and writes it into the hidden LCD frame buffer → double
  buffer flip via `esp_lcd_dpi_panel_get_frame_buffer` swap. FPS logged every
  50 frames; LED0 toggles as heartbeat.

## Hardware

| Peripheral | Connection |
|------------|------------|
| OV5647 module | MIPI-CSI connector (DSI/CSI share the FPC header area); SCCB on I2C0 (GPIO 28/29), reset = GPIO 26 (CSI_RST), power-down = GPIO 27 (CSI_PWDN) |
| 3.5" MIPI-DSI display | DSI connector; panel power enable GPIO 21, reset GPIO 22, backlight GPIO 23, ID GPIO 46; MIPI PHY LDO channel 3 @ 2.5 V |

## Vendor example mapping

| Vendor file | Role |
|-------------|------|
| `components/BSP/LCD/*` | ST7796U MIPI-DSI panel driver + `lcddev` bookkeeping (panel selected in `mipi_lcd.h`) |
| `components/BSP/MYIIC/*` | I2C0 master on GPIO 28/29, shared by camera SCCB (and ES8311) |
| `components/BSP/LED/*` | LED0/LED1 on GPIO 14/13 |
| `main/APP/MIPI_CAM/app_video.c` | esp_video init (SCCB bus reuse), open/stream/buffer helpers |
| `main/APP/MIPI_CAM/mipi_cam.c` | capture + PPA SRM + LCD flip preview task |

## Build / flash

```bash
cd p4-cb-v3_2\app\ov5647_lcd_preview
idf.py build            # first build fetches esp_video/esp_cam_sensor/esp_sccb_intf
idf.py -p COM34 flash monitor
```

The BSP selects panels at compile time via the `MIPILCD_*` macros in
`components/BSP/LCD/mipi_lcd.h` — exactly one must be 1. Currently the 3.5"
ST7796U is selected.

## Known workaround: esp_cam_sensor patch

`esp_cam_sensor` (0.5.3, same as the vendor pins) misses `esp_driver_gpio`
in its `REQUIRES`, so `sensors/ov5647/ov5647.c` fails to find
`driver/gpio.h` on IDF v6. The managed component's `CMakeLists.txt` is
patched locally (`set(requires "driver" "esp_sccb_intf" "esp_driver_gpio")`)
— the same edit the vendor ships in their managed copy. If
`managed_components/` is ever re-fetched, re-apply that one-line change.
