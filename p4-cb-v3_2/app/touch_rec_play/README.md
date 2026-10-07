# touch_rec_play — touch UI for 10 s record & play (ESP32-P4 CB V3.2)

Touch-driven voice recorder on the 3.5" WKS35HV035-WCT MIPI-DSI touch display:

- **After reset**: `Rec` (red) and `Play` (blue) buttons; only **Rec** is
  touch-able, `Play` is grayed out
- **Touch Rec**: both buttons become un-touch-able, a fixed **10 s recording**
  starts and the progress bar fills (red)
- **Recording complete**: the progress bar changes color (green) and `Play`
  becomes touch-able
- **Touch Play**: both buttons un-touch-able, playback runs, progress bar
  fills (blue)
- **Play complete**: back to the initial status (only `Rec` touch-able)

## Hardware usage

| Resource | Use |
|----------|-----|
| MEMS mic → ES8311 ADC → I2S0 RX | recording (16 kHz, 16-bit mono) |
| I2S0 TX → ES8311 DAC → NS4150B PA (GPIO 11) | playback (PA enabled only while playing) |
| 3.5" MIPI-DSI panel + GT9xx capacitive touch (I2C0, shared with ES8311) | UI (`tp_dev` scan, buttons hit-tested) |
| 32 MB PSRAM | the 10 s record buffer (320 KB) — no SD card needed |

## State notes

- Buttons are un-touch-able during a process simply because the state
  handler blocks the touch scan for the whole 10 s (record or play).
- After play the app returns to the initial status (only `Rec` touch-able).
  The recording stays in PSRAM — to allow replay instead, keep `button(true, true)`
  after `do_play()` in `main.c`.
- **I2C wiring**: the codec and the touch controller sit on the same physical
  wires (GPIO 28/29) but are attached through **different I2C controllers** —
  the codec creates its own I2C0 master in `myes8311_init()`, the touch uses
  I2C1 in `myiic.h` (`IIC_NUM_PORT = I2C_NUM_1`). Attaching the codec to the
  myiic-created bus NACKed every access on this board, so the two drivers get
  separate controllers; contention is not an issue because the codec is only
  accessed once at init and the touch is not scanned during record/playback.
- The es8311 managed component also needs `esp_driver_gpio` added to its
  CMakeLists `REQUIRES` on IDF v6 (same regression class as `esp_cam_sensor`).

## Vendor sources

- `25_recoding` — I2S/ES8311 BSP, I2S RX record path
- `24_music` — I2S TX playback path
- `13_mipilcd_touch_screen` — LCD + GT9xx touch BSP, `tp_dev` API
