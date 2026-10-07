# ESP32-P4 CB V3.2 — dhry_400m

Dhrystone 2.1 (2,000,000 → **10,000,000** runs — the P4 finishes 2M runs in
~1.4 s, below the port's 2 s "measured time too small" guard), on the
ESP32-P4 CB V3.2 core board @ 400 MHz. Ported from
`D0WD-V3-OV2640/dhry_240m` (same sources as `thing/dhry_240m`).

## Results

GCC 15.2.0, flags per the `nano-f411` GCC recipe
(`-Ofast -ffp-contract=fast -funroll-loops`):

```
MicroSecond for one run through Dhrystone:    0.717   (run 2: 0.713)
Dhrystones per Second:  1395673.375           (run 2: 1403508.750)
DMIPS/MHz:      1.986                         (run 2: 1.997)
```

Stable across runs (~±0.5%).

## Cross-board comparison (same GCC 15.2.0, same code)

| Board | Flags | Dhrystones/s | DMIPS/MHz |
|-------|-------|--------------|-----------|
| D0WD-V3-OV2640 @ 240 MHz | `-Ofast -ffp-contract=fast -funroll-loops` | 399,042 | 0.946 |
| Thing @ 240 MHz | `-Ofast -funroll-loops` | 440,917 | 1.046 |
| **P4 CB V3.2 @ 400 MHz** | same as D0WD-V3-OV2640 | **1,395,673** | **1.986** |

The P4's RISC-V cores score ~2 DMIPS/MHz here — roughly double the classic
ESP32's LX6 per-MHz score in this port. Note this benchmark runs single-core
(app_main context); the second core sits idle.
