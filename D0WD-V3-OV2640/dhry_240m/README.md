# ESP32 D0WD-V3-OV2640 board (ESP32-CAM)

Xtensa LX6 240MHz, 4 MB flash + 4 MB PSRAM (8 MB device, 4 MB mapped), 40 MHz
crystal.

Ported from `thing/dhry_240m` (SparkFun ESP32 Thing). Differences: 40 MHz
crystal instead of 26 MHz, PSRAM enabled, banner text updated. No GPIO
indicator used by this project.

## dhrystone

Measured on hardware @ 240 MHz, GCC 15.2.0, `-Ofast -funroll-loops`
(same flags as `thing/dhry_240m`):

```
MicroSecond for one run through Dhrystone:    2.506
Dhrystones per Second:  399042.312
DMIPS/MHz:      0.946
```

Stable across runs (3 identical measurements).

### Flag comparison vs the F411 recipe

`nano-f411/dhry_100m` uses `-Ofast -ffp-contract=fast -funroll-loops`.
Adding `-ffp-contract=fast` on this board measured **identical** results
(399042.312 D/s) — Dhrystone 2.1's timed loop has no float mul-add fusion
opportunities on Xtensa LX6, so the F411's extra flag gains nothing here. The
original `-Ofast -funroll-loops` is kept.

> This board scores ~9.5% below the Thing at the same 240 MHz (0.946 vs
> 1.046 DMIPS/MHz), likely a side effect of the PSRAM-enabled config; the
> benchmark data itself resides in internal DRAM on both.
