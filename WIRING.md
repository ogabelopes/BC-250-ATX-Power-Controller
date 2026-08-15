# BC-250 ATX Power Controller — Wiring Guide

This guide shows how to wire the ESP32-S3, relay, BC-250 TPMS1 header, case button, and ATX power supply together.

> **Board used in this guide:** ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1 module)  
> **Target motherboard:** AMD BC-250  
> **Important:** ESP32-S3-DevKitC-1 has two revisions. The built-in RGB LED is on **GPIO 48** on v1.0 and **GPIO 38** on v1.1. The firmware defaults to GPIO 48; change `NEOPIXEL_PIN` in `src/main.cpp` if your board is v1.1.

---

## Parts Needed

| Quantity | Part | Notes |
|----------|------|-------|
| 1 | ESP32-S3 dev board | DevKitC-1 or similar with GPIO 0, 2, 4, 5, 6 accessible. Built-in RGB LED is GPIO 48 (v1.0) or GPIO 38 (v1.1). |
| 1 | 1-channel 3.3V relay module | **Low-level trigger** (energizes on LOW). |
| 1 | Momentary switch / button | For the smart case power button. |
| ~ | Dupont jumper wires | Female-to-male and male-to-male as needed. |
| 1 | 1kΩ resistor (optional) | Between TPMS1 pin 9 and GPIO 6 for protection. |
| 1 | ATX power supply | Must provide 5VSB (purple wire) and 12V. |
| 1 | AMD BC-250 motherboard | Or compatible board. |

---

## ESP32 Pin Assignments

| GPIO | Function | Direction | Details |
|------|----------|-----------|---------|
| **GPIO 5** | Relay control | Output | Active-low: LOW energizes the relay. |
| **GPIO 6** | HOST_ON sense | Input | Reads TPMS1 pin 9; HIGH = BC-250 powered on. |
| **GPIO 4** | Case button | Input | Active-low; internal pull-up enabled. |
| **GPIO 0** | BOOT button | Input | Onboard button; active-low; internal pull-up. |
| **GPIO 2** | External status LED | Output | Optional; mirrors power state. |
| **GPIO 48 / 38** | RGB LED | Output | Built-in WS2812 status LED. GPIO 48 on DevKitC-1 v1.0, GPIO 38 on v1.1. |
| **3.3V** | Power rail | — | Powers relay logic side. |
| **GND** | Ground | — | Common ground reference. |
| **5V / VIN** | Power input | — | From ATX 5VSB (purple wire). |

---

## Step 1 — Power the ESP32

The ESP32 must stay powered even when the BC-250 is off, so it can listen for button presses and trigger the relay.

```
ATX 24-pin connector          ESP32
    5VSB (purple)    ────────►  5V / VIN
    GND (black)      ────────►  GND
```

- Use any black ground wire from the ATX 24-pin connector.
- Connect the purple **5VSB** wire to the ESP32 **5V** or **VIN** pin.
- Do not use the ATX 3.3V (orange) to power the ESP32; the ESP32-S3 DevKitC expects 5V input on VIN.

---

## Step 2 — Relay Wiring

The relay bridges ATX `PS_ON` to ground when energized. This tells the ATX supply to turn on.

### Relay module pins

| Relay Pin | Connects To | Note |
|-----------|-------------|------|
| `VCC` | ESP32 3.3V | Logic power for the relay module. |
| `GND` | ESP32 GND | Common ground. |
| `IN` | ESP32 GPIO 5 | Control signal (active-low). |
| `COM` | ATX `PS_ON` (green wire, pin 16 on 24-pin) | Common contact. |
| `NO` | ATX GND (black wire) | Normally open; closes to COM when relay energizes. |

```
ESP32                    Relay Module
3.3V      ─────────────► VCC
GND       ─────────────► GND
GPIO 5    ─────────────► IN

ATX 24-pin               Relay Module
PS_ON (green) ─────────► COM
GND (black)   ─────────► NO
```

When GPIO 5 goes **LOW**, the relay closes and connects `PS_ON` to GND, turning the ATX supply on.

---

## Step 3 — BC-250 TPMS1 Connection

TPMS1 is an 18-pin LPC header on the BC-250. We use **pin 9**, which carries the board's 3.3V rail and is **HIGH when the BC-250 is powered on**.

### TPMS1 pinout (relevant pins)

```
[  1   2 ]  PCICLK    GND
[  3   4 ]  FRAME     SMB_CLK_MAIN
[  5   6 ]  PCIRST#   SMB_DATA_MAIN
[  7   8 ]  LAD3      LAD2
[  9  10 ]  3V        LAD1     ◄── pin 9 is what we use
[ 11  12 ]  LAD0      GND
[ 13  14 ]            S_PWRDWN#
[ 15  16 ]  3VSB      SERIRQ#
[ 17  18 ]  GND       GND
```

### Connection

```
BC-250 TPMS1              ESP32
pin 9  (3V)   ──[1kΩ]──► GPIO 6   (HOST_ON)
pin 2  (GND)  ─────────► GND
```

- The 1kΩ resistor is optional but recommended to protect the ESP32 GPIO from transients.
- Connect any TPMS1 GND pin (2, 12, 17, or 18) to the ESP32 GND.
- Do **not** connect TPMS1 pin 15 (3VSB) to GPIO 6; 3VSB is always on and would not indicate board power state.

---

## Step 4 — Case Power Button

Connect a momentary switch between GPIO 4 and GND. The internal pull-up keeps the pin HIGH; pressing the button pulls it LOW.

```
ESP32
GPIO 4  ─────────┬──────── GND
                 │
            [ momentary button ]
```

You can also use the onboard **BOOT button (GPIO 0)** as a secondary test input. Note that GPIO 0 is a strapping pin — holding it at power-on will put the ESP32 into download mode.

---

## Step 5 — Optional External LED

If you want an additional external status LED, connect it to GPIO 2 through a current-limiting resistor (220Ω–1kΩ).

```
ESP32 GPIO 2  ──[220Ω]──► LED anode
LED cathode  ───────────► GND
```

This LED mirrors the power state (on when BC-250 is on, off when off). The built-in RGB LED (GPIO 48 on DevKitC-1 v1.0, GPIO 38 on v1.1) already provides richer status feedback.

---

## Full Wiring Diagram

```text
                              ┌─────────────────────────────┐
                              │      ESP32-S3 DevKitC       │
                              │                             │
   ATX 5VSB (purple) ────────►│ 5V/VIN                      │
   ATX GND (black)   ────────►│ GND                         │
                              │                             │
   Relay VCC ────────────────►│ 3.3V                        │
   Relay GND ────────────────►│ GND                         │
   Relay IN  ────────────────►│ GPIO 5                      │
                              │                             │
   TPMS1 pin 9 ──[1kΩ]───────►│ GPIO 6                      │
   TPMS1 GND   ──────────────►│ GND                         │
                              │                             │
   Case button ──────────────►│ GPIO 4 ─────┬── GND         │
   BOOT button ──────────────►│ GPIO 0      │               │
                              │ GPIO 2 ──[R]── LED ── GND   │
                              │ GPIO 48/38 ──► RGB LED (onboard)*│
                              └─────────────────────────────┘

* DevKitC-1 v1.0 uses GPIO 48; v1.1 uses GPIO 38. Change `NEOPIXEL_PIN` in
  `src/main.cpp` to match your board revision.

   Relay COM ◄──── ATX PS_ON (green)
   Relay NO  ◄──── ATX GND (black)
```

---

## Power-On Behavior

1. ATX 5VSB powers the ESP32 immediately.
2. ESP32 boots, initializes GPIOs, and sets relay `IN` to HIGH (relay open → PSU stays off).
3. RGB LED flashes red → green → blue as a self-test, then shows the current BC-250 state.
4. Pressing the case button or sending `w` over serial pulls `PS_ON` to ground via the relay, turning on the ATX supply and the BC-250.
5. Once the BC-250 is on, TPMS1 pin 9 goes HIGH → ESP32 sees `isPCisOn = true` → RGB LED turns green.

---

## Safety Checklist

- [ ] Relay module is **low-level trigger** (energizes when `IN` is LOW).
- [ ] ESP32 is powered from **5VSB**, not from a switched ATX rail.
- [ ] TPMS1 pin 9 is used, **not** pin 15 (3VSB).
- [ ] Common ground exists between ESP32, relay, BC-250 TPMS1, and ATX.
- [ ] GPIO 0 is not held low during ESP32 power-on.
- [ ] All Dupont connections are secure before powering on.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| ESP32 does not boot | GPIO 0 held low, or 5VSB missing | Release BOOT button; verify purple wire voltage. |
| PSU does not turn on | Relay polarity wrong, or PS_ON/GND swapped | Verify relay is low-level trigger; check COM/NO wiring. |
| RGB LED stuck red | TPMS1 pin 9 not connected or BC-250 not on | Check TPMS1 pin 9 → GPIO 6; verify BC-250 boots. |
| Button does nothing | Cooldown active, or button wired to wrong pin | Wait 5s after last relay action; check GPIO 4 wiring. |

---

## Testing & Verification

After wiring everything and flashing the firmware, follow these steps to verify every part works correctly.

### Before powering on

1. **Double-check polarities**
   - Relay `VCC` → ESP32 3.3V, `GND` → ESP32 GND, `IN` → GPIO 5.
   - Relay `COM` → ATX `PS_ON` (green), `NO` → ATX GND (black).
   - TPMS1 pin 9 → GPIO 6 (with optional 1kΩ), TPMS1 GND → ESP32 GND.
   - ATX 5VSB (purple) → ESP32 5V/VIN, ATX GND → ESP32 GND.

2. **Make sure the BC-250 can boot on its own**
   - Manually bridge ATX `PS_ON` to GND and confirm the BC-250 starts.
   - If it does not, fix the PSU/motherboard first before blaming the controller.

3. **Open the Serial Monitor**
   ```bash
   pio device monitor -b 115200
   ```

---

### Test 1 — Boot and RGB self-test

**Expected behavior:**

```text
BC-250 Hardware Logic Test Initializing...
NeoPixel self-test...
Initialization complete. Waiting for inputs...
```

- The built-in RGB LED flashes **red → green → blue** for 200 ms each.
- After the self-test, the LED turns **red** if the BC-250 is off, or **green** if it is already on.
- The external LED on GPIO 2 mirrors the same state (on = green/off, off = red/off).

**If this does not happen:**
- Verify the ESP32 is powered from 5VSB (purple wire must have 5V).
- Check that `platformio.ini` has the USB CDC flags and that the Serial Monitor is open at 115200 baud.
- If the RGB LED does nothing, check your DevKitC-1 revision:
  - v1.0 (initial release) uses GPIO 48 for the RGB LED.
  - v1.1 uses GPIO 38.
  - Update `#define NEOPIXEL_PIN` in `src/main.cpp` to match your board. The rest of the firmware still works.

---

### Test 2 — Wake via Serial command

1. Make sure the BC-250 is off (RGB LED red).
2. In the Serial Monitor, type `w` and press Enter.

**Expected behavior:**

```text
Serial command received! Flagging ATX power sequence.
Action: Waking up the ATX Power Supply...
```

- You should hear/feel the relay click.
- The RGB LED turns **blue** during the 800 ms wake pulse.
- After the pulse, the LED returns to **red** briefly, then turns **green** once the BC-250 boots and TPMS1 pin 9 goes HIGH.
- Serial prints: `Status: BC-250 has powered ON.`

**If this does not happen:**
- Verify the relay is **low-level trigger** (energizes when GPIO 5 is LOW).
- Swap relay `COM`/`NO` if the PSU does not turn on during the click.
- Check that ATX `PS_ON` is actually connected to relay `COM` and GND to `NO`.

---

### Test 3 — Graceful shutdown via Serial command

1. Wait until the BC-250 is fully on (RGB LED green).
2. In the Serial Monitor, type `h`.

**Expected behavior:**

```text
WARNING: Initiating Hard Shutdown (Holding relay for 5.5s)...
Action: Initiating hard shutdown (5.5s relay hold)...
```

- The relay holds closed for 5.5 seconds.
- The RGB LED turns **magenta** during the hard shutdown.
- After 5.5 seconds the relay releases.
- Once the BC-250 powers off, TPMS1 pin 9 drops to 0V and the LED turns **red**.
- Serial prints: `Status: BC-250 has powered OFF.`

> Note: the `h` command performs a **hard shutdown**. If you want to test a normal graceful shutdown, use the case button short-press (Test 4).

---

### Test 4 — Case button short press

1. Make sure the BC-250 is off and the 5-second cooldown has passed (RGB LED red).
2. Press and release the case button quickly (< 1.2 seconds).

**Expected behavior when off:**

```text
Short press: Waking the BC-250...
Action: Waking up the ATX Power Supply...
```

- Same as Test 2: relay clicks, LED blue, then green when the board boots.

**Expected behavior when on:**

```text
Short press: Normal graceful shutdown...
Action: Initiating normal shutdown (short relay pulse)...
```

- Relay clicks for 500 ms.
- LED turns **yellow** during the pulse.
- The BC-250 should begin a graceful OS shutdown (if an OS is running).

**If the button does nothing:**
- Wait at least 5 seconds after the last relay action (cooldown).
- Verify the button is wired between GPIO 4 and GND.
- Check the Serial Monitor for `Button ignored: Cooldown active.`

---

### Test 5 — Case button long press (hard shutdown)

1. Make sure the BC-250 is on (RGB LED green).
2. Press and hold the case button for more than 3 seconds, then release.

**Expected behavior:**

```text
Long press: Initiating Hard Shutdown...
Action: Initiating hard shutdown (5.5s relay hold)...
```

- The relay holds closed for 5.5 seconds.
- LED turns **magenta**.
- After release, the BC-250 powers off and the LED turns **red**.

**If a short press triggers instead:**
- You released the button before 3 seconds. Hold it longer.

---

### Test 6 — Onboard BOOT button

The BOOT button (GPIO 0) behaves exactly like the case button:

- Short press when off → wake.
- Short press when on → graceful shutdown.
- Long press (> 3 s) when on → hard shutdown.

**Important:** do not hold GPIO 0 at power-on, or the ESP32 enters download mode instead of running the sketch.

---

### Test 7 — State synchronization

1. Power on the BC-250 manually (bridge PS_ON to GND directly).
2. Power on or reset the ESP32 while the BC-250 is already running.

**Expected behavior:**

- The ESP32 boots, runs the RGB self-test, then immediately turns the LED **green**.
- It does **not** print `Status: BC-250 has powered ON.` because it correctly detects the board was already on.

This confirms the state-synchronization logic at boot works.

---

### Test 8 — Cooldown protection

1. Trigger any action (e.g., wake with `w`).
2. Immediately try to trigger another action within 5 seconds.

**Expected behavior:**

- Serial commands sent during cooldown are silently ignored.
- Button presses during cooldown print: `Button ignored: Cooldown active.`
- After 5 seconds, new commands/button presses work again.

This protects the power supply from rapid toggling.

---

## Quick Reference: What Each LED Color Means

| Color | Meaning |
|-------|---------|
| Red | BC-250 OFF / standby |
| Green | BC-250 ON |
| Blue | Wake pulse in progress |
| Yellow | Normal shutdown pulse in progress |
| Magenta | Hard shutdown in progress |
| Red → Green → Blue flash | RGB self-test at boot |

---

## References

- [BC-250 Pinouts (elektricM/amd-bc250-docs)](https://elektricm.github.io/amd-bc250-docs/hardware/pinouts/)
- [Project README](./README.md)
