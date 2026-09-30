# ESP32 Projects

ESP-IDF projects for seven board variants.

| Board | Directory | SoC | Flash | PSRAM | USB | LED |
|-------|-----------|-----|-------|-------|-----|-----|
| SuperMini | `c3-supermini/` | ESP32-C3 | Embedded 4 MB | None | Native USB-Serial-JTAG | GPIO 8 (1 LED) |
| Classic | `c3-classic/` | ESP32-C3 | External 4 MB | None | CH343 UART bridge | GPIO 12 (2 LEDs) |
| S3-N16R8 | `s3-n16r8/` | ESP32-S3 | External 16 MB (QIO) | 8 MB (PSRAM) | CH343 UART bridge | WS2812 RGB on GPIO 48 |
| S3-N8R8 OV3660 | `s3-n8r8-ov3660/` | ESP32-S3 | External 8 MB | 8 MB (PSRAM) | UART bridge | None (OV3660 camera) |
| C6 SuperMini | `c6-supermini/` | ESP32-C6 | Embedded 4 MB | None | Native USB-Serial-JTAG | GPIO 15 + WS2812 GPIO 8 |
| Thing | `thing/` | ESP32 | External 4 MB | None | CP2102 UART bridge | GPIO 5 (1 blue LED) |
| D0WD-V3-OV2640 | `D0WD-V3-OV2640/` | ESP32 (ESP32-CAM) | External 4 MB | 4 MB (PSRAM) | External UART adapter (IO0 low for download) | Flash LED GPIO 4 |

> **Note:** The `ov3660_test` project reads the camera ID over SCCB (I2C on
> GPIO 4/5) with XCLK generated on GPIO 15 via LEDC.

## Hardware reference

[ESP32-C3-Flash.md](ESP32-C3-Flash.md) 鈥?guide to verifying embedded flash and
understanding flash variant differences.

---

## SuperMini (`c3-supermini/`)

| Project | Path | GPIO | Connection |
|---------|------|------|------------|
| `c3_empty` | `c3-supermini/c3_empty/` | GPIO 8 | On-board LED |
| `c3_oled` | `c3-supermini/c3_oled/` | GPIO 5 (SDA), GPIO 6 (SCL) | 0.42" 72x40 OLED (I2C addr 0x3C) |
| `dhry_160m` | `c3-supermini/dhry_160m/` | GPIO 8 | LED activity indicator |
| `wifi_con_test` | `c3-supermini/wifi_con_test/` | GPIO 8 | LED blink + WiFi |

## Classic (`c3-classic/`)

Same projects as SuperMini, ported for the C3 Classic board.

| Project | Path | GPIO | Notes |
|---------|------|------|-------|
| `c3_empty` | `c3-classic/c3_empty/` | GPIO 12 | LED blink (was GPIO 8) |
| `dhry_160m` | `c3-classic/dhry_160m/` | GPIO 12 | Dhrystone benchmark w/ LED |
| `coremark_160m` | `c3-classic/coremark_160m/` | (none) | CoreMark benchmark |
| `wifi_con_test` | `c3-classic/wifi_con_test/` | GPIO 12 | WiFi scan, connect, LED |

### Key differences from SuperMini

| Aspect | SuperMini | Classic |
|--------|-----------|---------|
| Flash | Embedded 4 MB (in-package) | External 4 MB (separate chip) |
| USB connection | Native USB-Serial-JTAG (no extra chip) | CH343 USB-UART bridge |
| Console | UART0 via native USB | UART0 via CH343 (GPIO 20/21) |
| Onboard LED | 1 LED on GPIO 8 | 2 LEDs on GPIO 12, GPIO 13 |
| OLED | GPIO 5/6 (I2C) | Not present |
| Flash pin usage | None (all GPIOs free) | SPI bus occupies GPIOs 10鈥?7 |

> **Note:** On the Classic board, the external SPI flash uses GPIOs 10鈥?7, so
> those pins are **not available** for other uses.

---

### `c3-classic/c3_empty/`

Minimal LED blink on GPIO 12, toggling every 500 ms. Good starting point for verifying a new board.

**Worth reading:** `main/main.c` 鈥?clean example of GPIO output + FreeRTOS task delay + ESP logging.

## S3-N16R8 (`s3-n16r8/`)

Ported from C3 Classic, adapted for the ESP32-S3 N16R8 module (16 MB flash, 8 MB PSRAM).

| Project | Path | Indicator | Notes |
|---------|------|-----------|-------|
| `s3_empty` | `s3-n16r8/s3_empty/` | WS2812 RGB | Color-cycle demo on GPIO 48 |
| `dhry_240m` | `s3-n16r8/dhry_240m/` | WS2812 RGB | Dhrystone benchmark (green during run) |
| `coremark_240m` | `s3-n16r8/coremark_240m/` | (none) | CoreMark benchmark |
| `wifi_con_test` | `s3-n16r8/wifi_con_test/` | WS2812 RGB | WiFi scan, connect, green/red blink |

### Key differences from C3

| Aspect | C3 (RISC-V) | S3-N16R8 (Xtensa LX7) |
|--------|-------------|----------------------|
| CPU | Single-core RISC-V @ 160 MHz | Dual-core Xtensa LX7 @ 240 MHz |
| Toolchain | `riscv32-esp-elf-gcc` | `xtensa-esp32s3-elf-gcc` |
| Flash | 4 MB (DIO, 40 MHz) | 16 MB (QIO, 80 MHz) |
| PSRAM | None | 8 MB embedded PSRAM (AP_3v3) |
| LED | Simple GPIO blink (on/off) | WS2812 RGB via RMT (GPIO 48) |
| USB bridge | CH343 on C3 Classic | CH343 (separate port from native USB) |

### WS2812 LED driver

All S3 projects that use an indicator share `ws2812_led.c` / `ws2812_led.h` 鈥?an RMT-based driver for the WS2812 RGB LED on GPIO 48. The driver uses the ESP-IDF RMT TX channel with a copy encoder at 10 MHz resolution.

```c
ws2812_init(48);
ws2812_set_rgb(255, 0, 0);  // red
ws2812_clear();              // off
```

## S3-N8R8 OV3660 (`s3-n8r8-ov3660/`)

Ported from `s3-n16r8`, adapted for the ESP32-S3 N8R8 module (8 MB flash, 8 MB PSRAM) with an **OV3660 camera**. This board has **no GPIO LED and no WS2812 LED**, so all LED code was removed. LED-only projects just log the no-LED situation; the camera is only probed for its device ID so far (see `ov3660_test`).

| Project | Path | Indicator | Notes |
|---------|------|-----------|-------|
| `s3_empty` | `s3-n8r8-ov3660/s3_empty/` | (none) | Logs board status + no-LED message |
| `dhry_240m` | `s3-n8r8-ov3660/dhry_240m/` | (none) | Dhrystone benchmark |
| `coremark_240m` | `s3-n8r8-ov3660/coremark_240m/` | (none) | CoreMark benchmark |
| `wifi_con_test` | `s3-n8r8-ov3660/wifi_con_test/` | (none) | WiFi scan, connect |
| `ov3660_test` | `s3-n8r8-ov3660/ov3660_test/` | (none) | Read OV3660 camera device ID over SCCB |

> **Note:** The benchmark projects suspend the task watchdog only around each
> benchmark run (`esp_task_wdt_deinit()` / `esp_task_wdt_init()`), so the WDT
> stays active the rest of the time instead of being disabled globally.

### Key differences from S3-N16R8

| Aspect | S3-N16R8 | S3-N8R8 OV3660 |
|--------|----------|----------------|
| Flash | 16 MB | 8 MB |
| LED | WS2812 RGB on GPIO 48 (via RMT) | None (no GPIO LED, no WS2812) |
| Camera | None | OV3660 (not yet driven) |
| Flash size config | `2MB` default | `8MB` (`CONFIG_ESPTOOLPY_FLASHSIZE`) |

## Thing (`thing/`)

Ported from `s3-n16r8`, adapted for the **SparkFun ESP32 Thing** (classic ESP32, ESP32-D0WDQ6, 4 MB external flash, 26 MHz crystal). This board has no WS2812 LED, so all RMT/WS2812 code was removed and the on-board blue LED on **GPIO 5** is used as the indicator.

| Project | Path | Indicator | Notes |
|---------|------|-----------|-------|
| `blink` | `thing/blink/` | GPIO 5 | LED blink (default LED) |
| `dhry_240m` | `thing/dhry_240m/` | (none) | Dhrystone benchmark @ 240 MHz |
| `coremark_240m` | `thing/coremark_240m/` | (none) | CoreMark benchmark |
| `wifi_con_test` | `thing/wifi_con_test/` | GPIO 5 | WiFi scan, connect, LED blink |

### Key differences from S3-N16R8

| Aspect | S3-N16R8 | Thing |
|--------|----------|-------|
| SoC | ESP32-S3 (Xtensa LX7, dual-core) | ESP32 (Xtensa LX6, dual-core) |
| Toolchain | `xtensa-esp32s3-elf-gcc` | `xtensa-esp32-elf-gcc` |
| Flash | 16 MB (QIO, 80 MHz) | 4 MB (DIO, 40 MHz) |
| Crystal | 40 MHz | 26 MHz (`CONFIG_XTAL_FREQ=26`) |
| LED | WS2812 RGB on GPIO 48 (via RMT) | Blue LED on GPIO 5 (simple GPIO) |
| USB bridge | CH343 UART bridge | CP2102 UART bridge |

> **Note:** The ESP32 Thing runs on a **26 MHz crystal**; the ESP-IDF default
> assumes 40 MHz, so the `sdkconfig.defaults` set `CONFIG_XTAL_FREQ=26`
> (otherwise UART output is garbled and the boot log warns about a crystal
> mismatch).

## D0WD-V3-OV2640 (`D0WD-V3-OV2640/`)

Ported from `thing`, adapted for an **ESP32-CAM style board** with an
**ESP32-D0WD-V3** SoC (classic ESP32, Xtensa LX6, 4 MB flash + 4 MB PSRAM,
**40 MHz crystal**) and an **OV2640 camera**. The camera occupies most GPIOs,
so the on-board indicator is the white **flash LED on GPIO 4** (also SD
DATA1). GPIO 5 is camera data line D0 and must not be used as an LED.

Camera pin map (from the module datasheet):

| CAM signal | GPIO | | SD signal | GPIO |
|------------|------|-|-----------|------|
| D0鈥揇7 | 5, 18, 19, 21, 36, 39, 34, 35 | | CLK | 14 |
| XCLK | 0 | | CMD | 15 |
| PCLK | 22 | | DATA0 | 2 |
| VSYNC | 25 | | DATA1 / flash LED | 4 |
| HREF | 23 | | DATA2 | 12 |
| SDA / SCL | 26 / 27 | | DATA3 | 13 |
| POWER (PWDN) | 32 | | | |

| Project | Path | Indicator | Notes |
|---------|------|-----------|-------|
| `blink_hello` | `D0WD-V3-OV2640/blink_hello/` | GPIO 4 | Flash LED **PWM @ 5% intensity** + ADC2 header-pin readings |
| `dhry_240m` | `D0WD-V3-OV2640/dhry_240m/` | (none) | Dhrystone benchmark @ 240 MHz |
| `coremark_240m` | `D0WD-V3-OV2640/coremark_240m/` | (none) | CoreMark benchmark |
| `wifi_con_test` | `D0WD-V3-OV2640/wifi_con_test/` | GPIO 4 | WiFi scan, connect, flash LED blink @ 5% PWM |
| `camera_stream` | `D0WD-V3-OV2640/camera_stream/` | (none) | OV2640 MJPEG stream server (ported from `s3-n16r8-ov5640`; max UXGA) |

### Key differences from Thing

| Aspect | Thing | D0WD-V3-OV2640 |
|--------|-------|----------------|
| SoC | ESP32-D0WDQ6 | ESP32-D0WD-V3 (same `esp32` target) |
| Crystal | 26 MHz (`CONFIG_XTAL_FREQ=26`) | 40 MHz (default) |
| PSRAM | None | 4 MB (`CONFIG_SPIRAM=y`) |
| LED | Blue LED on GPIO 5 | White flash LED on GPIO 4, LEDC hardware PWM (5% duty when on) |
| Camera | None | OV2640 (pins above; not yet driven) |
| ADC | 鈥?| ADC2 only (GPIO 2/12/13/14/15); ADC1 pins 36/37/39/34/35 are camera data lines. ADC2 is unusable while Wi-Fi is active |
| Flashing | CP2102 UART bridge | External UART adapter + IO0 to GND at reset |

> **Note:** GPIO 4 is the high-power white flash LED 鈥?it is *very* bright
> when driven. GPIO 33 (red status LED on some ESP32-CAM boards) is not
> documented for this module and is not used here.
>
> Measured on hardware: Dhrystone 399,042 D/s (0.946 DMIPS/MHz), CoreMark
> 465.7 it/s (`crcfinal 0x25b5`) 鈥?see the per-project READMEs. The F411 GCC
> flag recipe (`-ffp-contract=fast`, `-funroll-all-loops`) was tested: no
> gain for Dhrystone, +0.7% for CoreMark (kept there).

## C6 SuperMini (`c6-supermini/`)

ESP32-C6 RISC-V projects, ported from C3 SuperMini.

| Project | Path | GPIO | Notes |
|---------|------|------|-------|
| `c6_empty` | `c6-supermini/c6_empty/` | GPIO 15 (LED) + GPIO 8 (WS2812) | Both LEDs: GPIO heartbeat + WS2812 color cycle |
| `dhry_160m` | `c6-supermini/dhry_160m/` | GPIO 15 | Dhrystone benchmark w/ LED indicator |
| `coremark_160m` | `c6-supermini/coremark_160m/` | (none) | CoreMark benchmark |
| `wifi_con_test` | `c6-supermini/wifi_con_test/` | GPIO 15 | WiFi scan, connect, LED blink |

### Key differences from C3

| Aspect | C3 | C6 |
|--------|----|----|
| CPU | Single-core RISC-V @ 160 MHz | Single-core RISC-V @ 160 MHz (WiFi 6 / BLE 5 / Zigbee) |
| Toolchain | `riscv32-esp-elf-gcc` | `riscv32-esp-elf-gcc` (same) |
| WiFi | WiFi 4 (802.11 b/g/n) | WiFi 6 (802.11 ax) |
| Extra radios | 鈥?| Bluetooth 5 (LE) + IEEE 802.15.4 (Thread/Zigbee) |
| LED | GPIO 8 (simple) | GPIO 15 (simple) + GPIO 8 (WS2812 RGB via RMT) |

### `c3-supermini/c3_oled/`

OLED display demo that drives a 72x40 SSD1306-like display over both software bit-bang I2C and hardware I2C (ESP-IDF i2c_master driver), alternating every 10 seconds.

**Worth reading:**

| File | Why |
|------|-----|
| `main/oled.c` | Full OLED driver: soft I2C timing, hardware I2C init (i2c_master_bus + device), display init sequence, character rendering |
| `main/oled.h` | Pin definitions, public API |
| `main/oledfont.c` | 6x8 and 8x16 ASCII bitmap font tables |
| `main/main.c` | Application entry, brownout detector disable, demo loop |
| `build_oled.bat` | Build script sourcing ESP-IDF v6.0.2 and running `idf.py build` |

### `c3-supermini/dhry_160m/`

Dhrystone 2.1 benchmark ported from RP2040, running on ESP32-C3 at 160 MHz. Prints CPU frequency, compiler info, and DMIPS results in a loop with 10-second delays between runs.

**Worth reading:**

| File | Why |
|------|-----|
| `main/dhry_1.c`, `main/dhry_2.c` | Benchmark loop + Proc_1..8, Func_1..3 鈥?untouched Dhrystone 2.1 C source |
| `main/custom_def.h` | Platform adapt: `configTICK_RATE_HZ` guard, `COMPILER_NAME` for RISC-V GCC |
| `main/utils.c` | `HAL_GetTick()` via `esp_timer_get_time() / 1000` |
| `main/CMakeLists.txt` | Applies `-Ofast -funroll-loops` to main component only |

## Dependencies

- ESP-IDF **v6.0.2** (path: `\espidf\.espressif\v6.0.2\esp-idf`)
- Targets: `esp32c3` (C3 boards), `esp32s3` (S3-N16R8, S3-N8R8 OV3660), `esp32c6` (C6 SuperMini), `esp32` (Thing, D0WD-V3-OV2640)

## Building

Build manually with `idf.py build`, or use the auto-detect flash script:

```bash
# Auto-detect port, build & flash
.\flash.ps1 c6-supermini\c6_empty          # flash only
.\flash.ps1 s3-n16r8\s3_empty -Monitor     # flash + monitor
.\flash.ps1 s3-n8r8-ov3660\s3_empty        # S3-N8R8 OV3660 board
.\flash.ps1 c3-supermini\c3_empty build    # build only
.\flash.ps1 thing\blink                    # ESP32 Thing board
.\flash.ps1 D0WD-V3-OV2640\blink_hello     # ESP32-CAM D0WD-V3-OV2640 board

# Manual (if auto-detect has multiple ports)
cd c3-supermini\c3_oled
idf.py build
idf.py -p COM33 flash monitor

# S3 requires first-time target set
cd s3-n16r8\s3_empty
idf.py set-target esp32s3
idf.py build

# Flash via auto-detect from any dir:
.\flash.ps1 s3-n16r8\s3_empty
```

The `flash.ps1` script at the repo root automatically detects the COM port when only one ESP device is connected. When multiple devices are found, it lists them and asks you to specify `-p COMxx`.

Additional commands:
- `list` 鈥?list all serial ports
- `probe` 鈥?probe every serial port with `esptool.py chip_id` to identify ESP devices (reliable even when non-ESP devices like the miniWiggler are connected)

## Clock Configuration

C3 and C6 projects use 160 MHz; S3-N16R8, S3-N8R8 OV3660, Thing, and
D0WD-V3-OV2640 use 240 MHz.

| Clock | C3 / C6 default | S3 / Thing / D0WD-V3-OV2640 default | Config symbol |
|-------|-----------------|-------------------------------------|---------------|
| CPU | 160 MHz | 240 MHz | `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` |
| XTAL | 40 MHz | 40 MHz (Thing: 26 MHz) | `CONFIG_XTAL_FREQ` |
| APB | 80 MHz | 80 MHz | Derived from CPU / 2 |

To change: `idf.py menuconfig` 鈫?Component config 鈫?ESP Timer 鈫?CPU frequency.

### Reading clocks at runtime

```c
#include "esp_clk.h"

int cpu_hz  = esp_clk_cpu_freq();     // 160000000
int apb_hz  = esp_clk_apb_freq();     //  80000000
int xtal_hz = esp_clk_xtal_freq();    //  40000000
```

Or at compile time:

```c
printf("CPU freq: %d MHz\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
```

