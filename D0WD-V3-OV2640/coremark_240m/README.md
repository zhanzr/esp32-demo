# ESP32 D0WD-V3-OV2640 board (ESP32-CAM)

Xtensa LX6 240MHz, 4 MB flash + 4 MB PSRAM (8 MB device, 4 MB mapped), 40 MHz
crystal.

Ported from `thing/coremark_240m` (SparkFun ESP32 Thing). Differences: 40 MHz
crystal instead of 26 MHz, PSRAM enabled, banner text updated.

## coremark 1.0.1

Measured on hardware @ 240 MHz, GCC 15.2.0 (all runs `crcfinal 0x25b5`,
correct operation validated):

| Flags | Iterations/Sec | Time (s) |
|-------|----------------|----------|
| `-Ofast -funroll-loops` (thing baseline) | 462.443 | 15.137 |
| `-Ofast -ffp-contract=fast -funroll-all-loops` (kept) | **465.735** | 15.030 |

The second configuration is the GCC recipe from `nano-f411/coremark_100m`
and is the default in this project's `main/CMakeLists.txt`. On the F411
(Cortex-M4F) `-funroll-all-loops` gained ~5%; on the Xtensa LX6 it gains only
**+0.7%** (repeatable: two identical runs per configuration).

### Flag comparison notes

- `-ffp-contract=fast` alone: no measurable effect (checked on Dhrystone,
  see `dhry_240m/README.md`).
- `-funroll-all-loops` vs `-funroll-loops`: +0.7% on this board.
- LTO was **not** tried here; see `thing` sibling notes — on the F411 GCC LTO
  gains ~1% on CoreMark but catastrophically cheats Dhrystone.

> This board scores ~5% below the Thing at the same 240 MHz (465.7 vs
> 489.4 it/s), consistent with the Dhrystone gap — likely a side effect of
> the PSRAM-enabled config.
