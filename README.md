# Hologram Display

Pi Zero 2 W receiver for a swept-volume persistence-of-vision hologram
display. Receives 128×64 pixel slices over UDP from the Jetson sender
([Andrew923/Hologram](https://github.com/Andrew923/Hologram)), drives
two HUB75 LED panels via DMA, and uses an A3144 Hall-effect sensor to
sync the panel content to the rotor's angular position.

## Architecture

The Pi runs four cooperating threads:

| Thread | Responsibility |
|---|---|
| `UDPReceiver` | Decodes RLE slice packets into a triple-buffered `FrameSet` |
| `HallSensor` (libgpiod) | Times each rotor revolution, debounced to ignore edges within 500 µs |
| `DMAOutput` (SCHED_FIFO 80) | Each frame, computes current slice index from `(now − lastEdgeUs) × SLICE_COUNT / rotationPeriod`, repacks pixel data into the DMA buffer |
| Background DMA | Continuously refreshes the panel from `pixel_buf` via a CB chain on DMA channel 5 |

The two panels are mounted 180° apart on the rotor; `DMAOutput` drives
slice `S` on one panel and slice `S + SLICE_COUNT/2` on the other so a
single rotor pass covers the full ring of 240 angular positions.

## Hardware Overview

| Component | Notes |
|-----------|-------|
| Raspberry Pi Zero 2 W | Main controller |
| Adafruit HUB75 Triple Bonnet | LED matrix interface |
| 2× HUB75 RGB LED matrix panels (128×64) | Display output, mounted 180° apart on the rotor |
| A3144 Hall-effect sensor module | Rotation detection (open-collector output) |

---

## Hall Sensor Wiring

### A3144 module pin-out
| Module pin | Connect to |
|------------|------------|
| VCC | 3.3 V (Pi header pin 1 or 17) |
| GND | Ground (Pi header pin 6, 9, …) |
| OUT | GPIO signal input (see below) |

### Choosing a GPIO pin (important when using the HUB75 bonnet)

The HUB75 bonnet repurposes many of the Pi's GPIOs for matrix signals (R1/G1/B1, A/B/C/D, CLK, LAT, OE, etc.).  Routing the hall sensor through a HUB75 port pin that the bonnet actively drives will result in the signal being stuck high or low.

**Recommended pins** – use any GPIO on the 40-pin header that is *not* claimed by the bonnet.  On a Pi Zero 2 W with a single or double bonnet, the following are typically free:

| GPIO | Header pin | Notes |
|------|------------|-------|
| GPIO 19 | 35 | SPI1 MISO – safe if SPI1 not in use |
| GPIO 20 | 38 | SPI1 MOSI – safe if SPI1 not in use |
| GPIO 26 | 37 | No default alt-function |

> Verify with `pinout` (raspi-gpio / `pinout` command) that your specific bonnet stack leaves these free.

### External pull-up (strongly recommended)

The A3144 output is open-collector: it can only *pull the line low* when a magnet is present.  The Pi's internal pull-up (~50 kΩ) works at short distances, but routing through a bonnet connector adds impedance.  Add a **4.7 kΩ – 10 kΩ resistor from the signal line to 3.3 V** at the Pi header for a reliable logic high when no magnet is present.

```
3.3V ──┬── 4.7kΩ ──┬── GPIO (Pi input)
       │           │
       │           └── A3144 OUT
       │
      GND ────────────── A3144 GND  (shared with Pi GND)
```

---

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Binaries produced:
| Binary | Purpose |
|---|---|
| `hologram_display` | Main application — UDP receiver + DMA driver |
| `test_hall` | Hall sensor diagnostic |
| `test_led`, `test_led_rate` | librgbmatrix-based reference LED test (legacy) |
| `test_dma`, `test_dma_rate` | Direct-DMA test programs |
| `test_colors` | Color-pin verification |
| `test_mailbox` | Mailbox / DMA-buffer allocation test |

---

## Configuration (`config/default.cfg`)

Settings are `key=value`. Pass an alternate path as the first CLI argument to any binary.

```
udp_port=4210                     # incoming slice port
hall_gpio_pin=3                   # BCM GPIO number
hall_bias=none                    # pull_up | pull_down | none
hall_edge=falling                 # falling | rising | both
led_rows=64
led_cols=128
led_parallel=2
slice_count=240
debug_timing=false                # write per-event CSV log if true
timing_log_path=timing.log
```

### `hall_bias`
| Value | Behaviour |
|-------|-----------|
| `pull_up` | Enables the Pi's internal pull-up. **Required for open-collector sensors like A3144 if no external resistor.** |
| `pull_down` | Enables the Pi's internal pull-down. |
| `none` (default) | No internal bias. Use when an external pull resistor is fitted. |

### `hall_edge`
| Value | Behaviour |
|-------|-----------|
| `falling` (default) | Triggers when the line goes HIGH → LOW.  Correct for A3144 (magnet present = output pulled low). |
| `rising` | Triggers when the line goes LOW → HIGH. |
| `both` | Triggers on every transition (useful for debugging). |

---

## Running

```bash
sudo ./build/hologram_display                    # default config (config/default.cfg)
sudo ./build/hologram_display /path/to/my.cfg    # custom config
```

`sudo` is required for `/dev/mem` (DMA + GPIO) and `/dev/vcio` (mailbox
allocation of contiguous DMA memory).

The display listens on UDP port 4210 (configurable) for slice packets
from the Jetson sender. Each packet carries one of 240 angular slices
of a 128×64×4-byte RGBA8 image, RLE-compressed in
column-major RGB888 order. See
[`include/Protocol.h`](include/Protocol.h) for the wire format.

---

## DMA pipeline

The HUB75 panels are driven by a custom DMA chain in
[`src/hub75_dma.c`](src/hub75_dma.c) (replaces librgbmatrix). Each
chain pass is **3 control blocks per pixel** (down from 4 — the
post-shift CLK-clear was redundant since the next pixel's CB_A clears
CLK along with the data bits) plus 6 transition CBs per row, for
**12,480 + 1 telemetry** CBs total.

| Metric | Value |
|---|---|
| Panel refresh rate | ~300 Hz (measured on Pi Zero 2 W, DMA channel 5) |
| Per-CB cost | ~280 ns (lite-channel GPIO writes) |
| Slice dwell @ 3.6 rev/s rotor | ~1.16 ms |
| CPU `update_panels` per slice swap | ~180 µs |

The chain ends with a single telemetry CB that copies the BCM2835
1 MHz system timer (`STC_LO`) into a shared-memory slot. Read it via
`hub75_dma_chain_timer(&dma)` from C++ if you want to derive the
actual refresh rate at runtime — sample twice and count distinct
values, or compare to wall-clock for a frequency.

### Why no PWM colour depth

A 2-bit BCM (4 levels per channel = 64 colours) implementation was
attempted. The chain length math worked out but the latched second
bit-plane corrupted the image on these specific shift-register chips,
so it's not in master. Each LED is currently 1 bit per channel = 8
distinct colours total. The git history has the exploration if it's
worth revisiting.

---

## Diagnostic Tools

### `test_hall`

```bash
sudo ./build/test_hall                           # use config/default.cfg
sudo ./build/test_hall --edge=both               # override edge mode
sudo ./build/test_hall --bias=none --edge=rising # override both
```

On each timeout (1 s with no edge), prints the current GPIO level
together with the active bias and edge settings so you can diagnose
stuck-high / stuck-low conditions without guessing.

### `test_dma_rate` / `test_led_rate`

Free-running benchmarks that report raw DMA / librgbmatrix throughput
without the rest of the application stack.
