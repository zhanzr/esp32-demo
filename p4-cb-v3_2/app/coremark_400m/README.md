# ESP32-P4 CB V3.2 — coremark_400m

CoreMark 1.0.1 (EEMBC), 7000 iterations, on the ESP32-P4 CB V3.2 core board
@ 400 MHz. Ported from `D0WD-V3-OV2640/coremark_240m` (same sources as
`thing/coremark_240m`).

## Results

GCC 15.2.0, flags per the `nano-f411` GCC recipe
(`-Ofast -ffp-contract=fast -funroll-all-loops`):

```
CoreMark Size    : 666
Total time (secs): 5.561000        (run 2: 5.560)
Iterations/Sec   : 1258.766409     (run 2: 1258.992806)
crcfinal         : 0x25b5 (correct operation validated)
CoreMark/MHz     : 3.15
```

Highly repeatable (~0.02% run-to-run).

## Cross-board comparison (same GCC 15.2.0, same code)

| Board | Flags | Iterations/Sec | CoreMark/MHz |
|-------|-------|----------------|--------------|
| D0WD-V3-OV2640 @ 240 MHz | `-Ofast -ffp-contract=fast -funroll-all-loops` | 465.7 | 1.94 |
| Thing @ 240 MHz | `-Ofast -funroll-loops` | 489.4 | 2.04 |
| **P4 CB V3.2 @ 400 MHz** | same as D0WD-V3-OV2640 | **1258.8** | **3.15** |

Single-core run (MULTITHREAD=1 with one context, app_main); the second core
is idle. The RISC-V core delivers ~1.6x the classic ESP32's per-MHz
CoreMark. LTO was not tried (see the Dhrystone LTO caution in
`dhry_100m` docs on the F411 side — it inflates Dhrystone scores).
