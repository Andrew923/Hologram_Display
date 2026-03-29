# Hologram Display

A persistence-of-vision (POV) hologram display driven by a Raspberry Pi, an RGB LED matrix panel, and a Hall-effect sensor for rotation synchronization.

---

## Hardware Overview

| Component | Notes |
|-----------|-------|
| Raspberry Pi Zero 2 W | Main controller |
| Adafruit HUB75 Triple Bonnet | LED matrix interface |
| HUB75 RGB LED matrix panel(s) | Display output |
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
- `hologram_display` – main application
- `test_hall` – hall sensor diagnostic tool
- `test_led` – LED matrix diagnostic tool

---

## Configuration (`config/default.cfg`)

All settings live in a simple `key=value` file.  Pass an alternate path as the first CLI argument to any binary.

```
# Network
udp_port=4210

# Hall Effect Sensor
hall_gpio_pin=26        # BCM GPIO number
hall_bias=pull_up       # pull_up | pull_down | none
hall_edge=falling       # falling | rising | both
```

### `hall_bias`
| Value | Behaviour |
|-------|-----------|
| `pull_up` (default) | Enables the Pi's internal pull-up. **Required for open-collector sensors like A3144.** |
| `pull_down` | Enables the Pi's internal pull-down. |
| `none` | No internal bias. Use only when an external pull resistor is fitted. |

### `hall_edge`
| Value | Behaviour |
|-------|-----------|
| `falling` (default) | Triggers when the line goes HIGH → LOW.  Correct for A3144 (magnet present = output pulled low). |
| `rising` | Triggers when the line goes LOW → HIGH. |
| `both` | Triggers on every transition (useful for debugging). |

---

## Running `test_hall`

```bash
sudo ./build/test_hall                           # use config/default.cfg
sudo ./build/test_hall config/default.cfg        # explicit config
sudo ./build/test_hall --edge=both               # override edge mode
sudo ./build/test_hall --bias=none --edge=rising # override both
sudo ./build/test_hall myconfig.cfg --edge=both  # config + override
```

On each timeout (1 s with no edge), the tool prints the current GPIO level together with the active bias and edge settings so you can diagnose stuck-high / stuck-low conditions without guessing.

---

## Running the Main Application

```bash
sudo ./build/hologram_display                    # default config
sudo ./build/hologram_display /path/to/my.cfg   # custom config
```

The display receives 128×64 pixel slices via UDP (port 4210 by default) and synchronizes rendering to the hall sensor rotation period.
