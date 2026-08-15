# BC-250 ATX Power Controller

Smart ATX power controller for the **AMD BC-250** motherboard, built on an **ESP32-S3**.

This project lets an ESP32 act as an intelligent power switch and status monitor for the BC-250: it bridges the ATX `PS_ON` wire through a relay, senses the board's power state from the **TPMS1 LPC header**, and intercepts the physical case power button for smart wake/shutdown control.

> **Author:** [Gabriel Lopes](https://github.com/ogabelopes)

---

## What It Does

- **Power control** — safely pulls the ATX `PS_ON` line to ground via a 3.3V relay to turn the BC-250 on or off.
- **Status monitoring** — reads the BC-250 power state from **TPMS1 pin 9** (3.3V rail, active-high when the board is on).
- **Smart button** — intercepts the physical case power button (and the onboard BOOT button) so a short press wakes/gracefully-shutdowns and a long press forces a hard shutdown.
- **Visual feedback** — uses the built-in RGB LED on the ESP32-S3 to show power state and pending actions.
- **Cooldown protection** — enforces a 5-second cooldown after every relay action to protect the power supply from rapid toggling.

---

## Hardware

### Required

| Part | Notes |
|------|-------|
| ESP32-S3 (WROOM-1 module) | Tested with the ESP32-S3-DevKitC-1 board. |
| 1-Channel 3.3V Relay Module | Must be **low-level trigger** and reliable at 3.3V logic. |
| ATX Power Supply | Provides 24-pin power and 5VSB standby power. |
| AMD BC-250 Motherboard | Or any board that needs soft-power emulation. |
| Momentary Switch | Physical case power button wired to the ESP32. |
| 1kΩ resistor (optional) | Series resistor between TPMS1 pin 9 and GPIO 6 for protection. |

### Wiring Quick Reference

For a complete step-by-step wiring guide with diagrams and a troubleshooting table, see **[WIRING.md](./WIRING.md)**.

| ESP32 Pin | Connection | Function |
|-----------|------------|----------|
| **GPIO 5** | Relay `IN` | Controls the relay (active-low). |
| **GPIO 6** | TPMS1 pin 9 (via 1kΩ resistor) | Reads BC-250 power state (HIGH = on, LOW = off). |
| **GPIO 4** | Case button (to GND) | Smart power button. |
| **GPIO 0** | BOOT button | Secondary test input (don't hold at power-on). |
| **GPIO 2** | Optional external LED | Mirrors power state. |
| **GPIO 48** | Built-in WS2812 RGB LED | Status indicator. |
| **3.3V** | Relay VCC | Logic-side power for the relay. |
| **GND** | TPMS1 GND + ATX GND | Common ground. |
| **5VSB (purple)** | ESP32 VIN / 5V | Keeps the ESP32 alive in standby. |

**Relay side:**
- Relay `COM` → ATX `PS_ON` (green wire)
- Relay `NO` (Normally Open) → ATX `GND` (black wire)

When the relay energizes, it connects `PS_ON` to `GND` and the ATX supply turns on.

**TPMS1 connection:**
- TPMS1 pin 9 → 1kΩ resistor → ESP32 GPIO 6
- TPMS1 pin 2/12/17/18 (GND) → ESP32 GND

TPMS1 pin 9 carries the BC-250's 3.3V rail, so it is **HIGH when the board is powered on** and **LOW when it is off**. The 1kΩ resistor is optional protection; the ESP32 GPIO is 3.3V tolerant and high-impedance, so it will work without it too.

---

## Software

### Requirements

- [PlatformIO Core](https://platformio.org/install/cli) (or the PlatformIO extension for VS Code)
- USB-C cable to the ESP32-S3

### Build & Upload

```bash
# Clone the repo
git clone https://github.com/ogabelopes/BC-250-ATX-Power-Controller.git
cd BC-250-ATX-Power-Controller

# Build
pio run

# Upload to the ESP32-S3
pio run -t upload

# Open the serial monitor
pio device monitor -b 115200
```

If upload fails with a timeout, put the board in download mode manually:
1. Hold the **BOOT** button.
2. Press and release the **RESET/EN** button.
3. Release the **BOOT** button.
4. Run `pio run -t upload` again.

---

## Usage

### Serial Commands

Open the Serial Monitor at **115200 baud** and send:

| Command | Action |
|---------|--------|
| `w` | Wake / power on the BC-250 (800 ms relay pulse). |
| `h` | Force hard shutdown (5.5 s relay hold). |

### Physical Buttons

Both the **case button (GPIO 4)** and the **BOOT button (GPIO 0)** work the same way:

| Gesture | Action |
|---------|--------|
| Short press (< 1.2 s) | Wake if the PC is OFF, or graceful shutdown if ON. |
| Long press (> 3 s) | Force hard shutdown if the PC is ON. |

A 5-second cooldown protects the PSU from rapid toggling.

### Status LED Colors

The built-in RGB LED shows the current state:

| Color | Meaning |
|-------|---------|
| Red | PC OFF / standby |
| Green | PC ON |
| Blue | Wake in progress |
| Yellow | Normal shutdown in progress |
| Magenta | Hard shutdown in progress |

---

## Project Structure

```
BC-250-ATX-Power-Controller/
├── platformio.ini              # Board target, flash/PSRAM config, library deps
├── add-platformio-to-path.ps1  # Windows helper to add pio to PATH
├── WIRING.md                   # Detailed wiring guide with diagrams
├── src/
│   └── main.cpp                # Firmware source code
├── .gitignore                  # PlatformIO build artifacts
└── README.md                   # This file
```

---

## Important Notes

- **Relay polarity:** the code assumes a **low-level trigger** relay. If your relay is high-level trigger, invert the `HIGH`/`LOW` writes in `src/main.cpp`.
- **HOST_ON polarity:** the code assumes TPMS1 pin 9 is **HIGH** when the BC-250 is powered on. If you wire it differently, change `isPCisOn = (digitalRead(HOST_ON_PIN) == HIGH);` accordingly.
- **GPIO 0 warning:** GPIO 0 is a strapping pin. Holding it low during power-on will put the ESP32 into download mode instead of running this sketch.
- **USB Serial:** `platformio.ini` enables USB CDC on boot so the Serial Monitor works through the USB-C port.

---

## Future Ideas

- Add Wi-Fi and Alexa / Home Assistant integration for voice control.
- Replace blocking `delay()` calls in relay sequences with non-blocking state machines.
- Expose a web dashboard for remote monitoring and control.

---

## Acknowledgments

- Built for the AMD BC-250 mining motherboard community.
- BC-250 pinout information sourced from [elektricM/amd-bc250-docs](https://elektricm.github.io/amd-bc250-docs/hardware/pinouts/).
- RGB LED handled by [NeoPixelBus](https://github.com/Makuna/NeoPixelBus) (RMT-based, ESP32-S3 friendly).
