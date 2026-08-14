/*
 * ============================================================================
 * BC-250 ATX Power Controller (Hardware Logic Test)
 * ============================================================================
 * 
 * This script serves as the foundational hardware test for building a custom 
 * ATX power adapter for the AMD BC-250 motherboard. It allows an ESP32 to 
 * act as a smart power switch and status monitor.
 * 
 * AUTHOR: Gabriel Lopes (https://github.com/ogabelopes)
 *
 * PURPOSE:
 * 1. Control an ATX power supply by bridging the PS_ON pin to Ground via a 3.3V relay.
 * 2. Safely sense if the BC-250 is running by reading its HOST_ON signal via a PC817 optocoupler.
 * 3. Provide safe startup (short press) and hard shutdown (long press) logic.
 * 4. Intercept the physical case power button for "Smart" triggering.
 * 
 * HARDWARE REQUIRED:
 * - ESP32 Microcontroller (ESP32-S3, C3, or classic WROOM)
 * - 1-Channel 3.3V Relay Module (Crucial: Must trigger on 3.3V logic, not 5V)
 * - PC817 Optocoupler Module (To safely isolate and step down the BC-250 power signal)
 * - Standard ATX Power Supply (Providing 24-pin power and 5VSB standby power)
 * - AMD BC-250 Motherboard (or similar board requiring soft-power emulation)
 * - Momentary Switch (Wired to ESP32 for smart control)
 * 
 * HARDWARE CONFIGURATION:
 * - ESP32 powered via the ATX "Purple Wire" (5VSB) and Ground.
 * - GPIO 5 -> IN pin on 3.3V Relay Module (Switches ATX Green PS_ON wire).
 * - GPIO 6 -> OUT pin on PC817 Optocoupler (Reads BC-250 HOST_ON 3.3V signal).
 * - GPIO 4 -> Physical Case Button (Smart switch trigger, active-low w/ pull-up).
 * - GPIO 0 -> Onboard BOOT button (secondary test input, active-low w/ pull-up).
 *              NOTE: GPIO 0 is a strapping pin. Do not hold it during power-on,
 *              or the ESP32 will enter download mode instead of running this sketch.
 * - GPIO 2 -> Optional external status LED (also mirrors the power state).
 * - GPIO 48 -> Built-in RGB status LED (WS2812 / NeoPixel).
 * 
 * USAGE (Serial Monitor @ 115200 baud or Physical Button):
 * - Send 'w' : Trigger relay for 800ms (Wake).
 * - Send 'h' : Trigger relay for 5500ms (Hard shutdown).
 * - Case/BOOT button: Short press (<1.2s) = wake if OFF / graceful shutdown if ON.
 *                     Long press (>3s)    = hard shutdown if ON.
 * - A 5-second cooldown is enforced after every relay action to protect the PSU.
 * 
 * STATUS LED COLOR MAP:
 * - Red    = PC OFF / standby
 * - Green  = PC ON
 * - Blue   = Wake in progress
 * - Yellow = Normal shutdown in progress
 * - Magenta= Hard shutdown in progress
 * ============================================================================
 */

#include <Arduino.h>
#include <NeoPixelBus.h>

// ---------------------------------------------------------
// HARDWARE PINS
// ---------------------------------------------------------
// The RGB status LED lives on GPIO 48 (WS2812) of the ESP32-S3-DevKitC-1.
#define NEOPIXEL_PIN     48   // Built-in RGB LED (WS2812 / NeoPixel)
#define NEOPIXEL_COUNT   1    // Only one LED on the devkit

// GPIO 4, 5, and 6 are strictly safe, general-purpose I/O pins on the ESP32-S3.
// They do not interfere with the boot/strapping process, which makes them ideal
// for driving our external hardware. GPIO 0 is the BOOT/strapping pin; we only
// use it as a runtime input after boot has completed.
const int RELAY_PIN = 5;         // Controls the 3.3V Relay Module. Active-Low: LOW energizes the relay.
const int OPTOCOUPLER_PIN = 6;   // Reads the PC817 Optocoupler. Uses the ESP32 internal pull-up resistor.
const int EXTERNAL_BTN_PIN = 4;  // The physical case power button (the "Smart" trigger).
const int STATUS_LED_PIN = 2;    // Optional external LED (e.g. wired to GPIO 2).
const int BOOT_BUTTON_PIN = 0;   // The physical "BOOT" button on the ESP32. Secondary test input.

// ---------------------------------------------------------
// STATE TRACKING
// ---------------------------------------------------------
// These variables maintain the context of the motherboard's power state.
// We use edge-detection (comparing current state to previous state) so we 
// only print status messages when the state actually changes, rather than spamming the console.
bool isPCisOn = false;               // Represents the LIVE current power state of the motherboard.
bool wasPCisOnLastCheck = false;     // Stores the PREVIOUS state to detect when the PC turns on or off.
bool controllerWantsPowerOn = false; // A flag raised when the user requests the PC to turn on (Serial 'w' or button).

// ---------------------------------------------------------
// SAFETY TIMEOUTS & HARDWARE STATE
// ---------------------------------------------------------
// Using millis() instead of delay() allows the ESP32 to keep running its loop
// without freezing. This is critical for reading sensors simultaneously.
unsigned long lastPowerToggleTime = 0;

// Hardware protection: Prevents spamming the ATX power supply. 
// Once the relay is triggered, it cannot be triggered again for 5 seconds.
const unsigned long COOLDOWN_PERIOD = 5000; 

// ATX Standard: Holding a power button for ~5 seconds forces a motherboard to immediately cut power.
const unsigned long HARD_SHUTDOWN_HOLD_TIME = 5500; 
bool triggerForceShutdown = false;   // A flag raised when a user requests a hard shutdown.

// A short relay pulse initiates a normal (graceful) shutdown of the OS.
const unsigned long NORMAL_SHUTDOWN_HOLD_TIME = 500;
bool triggerNormalShutdown = false;  // A flag raised for a normal soft-power shutdown.

// --- Physical Button: debounce + short/long press detection ---
// Applies to BOTH the external case button (GPIO 4) and the onboard BOOT button (GPIO 0).
const unsigned long DEBOUNCE_MS = 50;          // Ignore contact bounce for 50ms
const unsigned long SHORT_PRESS_MAX_MS = 1200; // A press released within this time = short press
const unsigned long LONG_PRESS_MS = 3000;      // Holding past this time = long press
bool lastButtonState = false;                  // Last RAW pin reading (released = false)
bool lastDebouncedButtonState = false;         // Last stable (debounced) pin reading
unsigned long lastButtonStateChangeTime = 0;   // When the RAW pin last changed
unsigned long buttonPressStartedTime = 0;      // When the button was pressed down
bool longPressFired = false;                   // True once the long-press action has fired

// ---------------------------------------------------------
// STATUS LED (RGB NEOPIXEL)
// ---------------------------------------------------------
// NeoPixelBus instance for the built-in RGB status LED.
// We use the RMT-based driver (the default on the ESP32-S3), which is reliable
// and never touches gpio_set_level() directly (unlike the bit-bang Adafruit driver).
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN);

// Update the RGB LED to reflect the current power state / pending action.
// Colors:
//   Off/standby            = red
//   PC ON                  = green
//   Wake in progress       = blue
//   Normal shutdown        = yellow
//   Hard shutdown          = magenta
void updateNeoPixel() {
    if (controllerWantsPowerOn) {
        pixel.SetPixelColor(0, RgbColor(0, 0, 255));      // Blue = waking
    } else if (triggerForceShutdown) {
        pixel.SetPixelColor(0, RgbColor(255, 0, 255));    // Magenta = hard shutdown
    } else if (triggerNormalShutdown) {
        pixel.SetPixelColor(0, RgbColor(255, 255, 0));    // Yellow = normal shutdown
    } else if (isPCisOn) {
        pixel.SetPixelColor(0, RgbColor(0, 255, 0));      // Green = ON
    } else {
        pixel.SetPixelColor(0, RgbColor(255, 0, 0));      // Red = OFF / standby
    }
    pixel.Show();
}

// ---------------------------------------------------------
// SETUP
// ---------------------------------------------------------
void setup() {
    // Initialize serial communication at 115200 baud for fast, reliable debugging.
    Serial.begin(115200);
    Serial.println("BC-250 Hardware Logic Test Initializing...");

    // --- Relay Configuration ---
    // CRITICAL: We immediately set it to HIGH. Most relay modules trigger on LOW.
    // By setting it HIGH immediately, we guarantee the PC doesn't accidentally
    // turn on the moment the ESP32 boots.
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); 

    // --- Optocoupler Configuration ---
    // We configure the pin as INPUT_PULLUP. This connects an internal resistor to 3.3V.
    // When the PC is OFF, the optocoupler is open, and the pin reads HIGH (3.3V).
    // When the PC is ON, the optocoupler closes, connecting this pin to GND, making it read LOW (0V).
    pinMode(OPTOCOUPLER_PIN, INPUT_PULLUP);

    // --- Button Configuration ---
    // Both buttons connect to Ground when pressed, so we use the internal pull-ups.
    pinMode(EXTERNAL_BTN_PIN, INPUT_PULLUP); // External case button (Smart Button)
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // Onboard test button

    // --- Status LED Configuration ---
    pinMode(STATUS_LED_PIN, OUTPUT);

    // --- NeoPixel Initialization ---
    pixel.Begin();
    pixel.Show();            // Start with the RGB LED off

    // Quick RGB self-test so you can confirm the LED works
    Serial.println("NeoPixel self-test...");
    pixel.SetPixelColor(0, RgbColor(255, 0, 0)); pixel.Show(); delay(200);
    pixel.SetPixelColor(0, RgbColor(0, 255, 0)); pixel.Show(); delay(200);
    pixel.SetPixelColor(0, RgbColor(0, 0, 255)); pixel.Show(); delay(200);

    // --- State Synchronization ---
    // Check the PC's state right as the ESP32 boots up. 
    // This prevents the ESP32 from thinking the state "changed" if the PC is
    // already running when we plug the ESP32 in.
    int initialState = digitalRead(OPTOCOUPLER_PIN);
    isPCisOn = (initialState == LOW);
    wasPCisOnLastCheck = isPCisOn;
    digitalWrite(STATUS_LED_PIN, isPCisOn ? HIGH : LOW); // External LED reflects state
    updateNeoPixel(); // RGB LED reflects state

    Serial.println("Initialization complete. Waiting for inputs...");
}

// ---------------------------------------------------------
// LOOP
// ---------------------------------------------------------
void loop() {
    // =========================================================
    // 1. POLL THE OPTOCOUPLER
    // =========================================================
    // Continuously read the BC-250's HOST_ON signal so the state
    // tracking below reflects real power changes as they happen.
    isPCisOn = (digitalRead(OPTOCOUPLER_PIN) == LOW);

    // =========================================================
    // 1B. DETECT STATE CHANGES
    // =========================================================
    if (isPCisOn != wasPCisOnLastCheck) {
        if (isPCisOn) {
            Serial.println("Status: BC-250 has powered ON.");
            digitalWrite(STATUS_LED_PIN, HIGH); 

            // If the PC powered on by itself, cancel any pending commands.
            controllerWantsPowerOn = false;
            triggerNormalShutdown = false;
            triggerForceShutdown = false;
        } else {
            Serial.println("Status: BC-250 has powered OFF.");
            digitalWrite(STATUS_LED_PIN, LOW);  

            // Safety measure: If the PC suddenly turns off (e.g., pulled plug), 
            // ensure the relay pin is reset to its default un-triggered state,
            // and cancel ALL pending commands so a stale flag can never
            // fire an unexpected relay pulse later.
            digitalWrite(RELAY_PIN, HIGH); 
            controllerWantsPowerOn = false;
            triggerNormalShutdown = false;
            triggerForceShutdown = false;
        }
        updateNeoPixel();
        // Save the current state for the next loop comparison
        wasPCisOnLastCheck = isPCisOn;
    }

    // =========================================================
    // 2. PARSE SERIAL COMMANDS
    // =========================================================
    // Check if the user has typed anything into the Serial Monitor
    if (Serial.available() > 0) {
        char inChar = Serial.read();
        
        // Command: Wake / Normal Power On ('w' or 'W')
        // We also check the cooldown timer. If 5000ms haven't passed since the last press, ignore it.
        if ((inChar == 'w' || inChar == 'W') && (millis() - lastPowerToggleTime > COOLDOWN_PERIOD)) {
            if (!isPCisOn) {
                // Cancel any pending shutdown, then queue the wake action.
                triggerNormalShutdown = false;
                triggerForceShutdown = false;
                Serial.println("Serial command received! Flagging ATX power sequence.");
                controllerWantsPowerOn = true; // Queue the power on action
            }
        }
        
        // Command: Force Hard Shutdown ('h' or 'H')
        // Only allow this if the cooldown has passed AND the PC is actually turned on.
        if ((inChar == 'h' || inChar == 'H') && (millis() - lastPowerToggleTime > COOLDOWN_PERIOD)) {
            if (isPCisOn) {
                // Cancel any pending wake, then queue the shutdown action.
                controllerWantsPowerOn = false;
                triggerNormalShutdown = false;
                Serial.println("WARNING: Initiating Hard Shutdown (Holding relay for 5.5s)...");
                triggerForceShutdown = true; // Queue the shutdown action
            }
        }
    }

    // =========================================================
    // 2B. PARSE PHYSICAL BUTTONS (GPIO 4 Case Button OR GPIO 0 BOOT Button)
    // =========================================================
    // Either button counts as a press. Debounced edge detection means a press
    // is only acted on ONCE, no matter how long you hold the button down.
    //   - Short press (< 1.2s): Wake if OFF, or normal graceful shutdown if ON.
    //   - Long press  (> 3s):   Force hard shutdown if ON (ignored when OFF).
    // Like the serial commands, each button action cancels any opposing pending
    // action so a stale flag can never fire the wrong relay sequence.
    bool rawButtonState = (digitalRead(EXTERNAL_BTN_PIN) == LOW) || (digitalRead(BOOT_BUTTON_PIN) == LOW);

    if (rawButtonState != lastButtonState) {
        lastButtonStateChangeTime = millis();
    }
    lastButtonState = rawButtonState;

    if (millis() - lastButtonStateChangeTime > DEBOUNCE_MS) {
        bool stableState = rawButtonState;

        if (stableState != lastDebouncedButtonState) {
            lastDebouncedButtonState = stableState;
            if (stableState) {
                // Button was just pressed down.
                buttonPressStartedTime = millis();
                longPressFired = false;
            } else {
                // Button was just released.
                unsigned long holdTime = millis() - buttonPressStartedTime;
                if (holdTime <= SHORT_PRESS_MAX_MS && !longPressFired) {
                    // COOLDOWN CHECK FOR SHORT PRESS
                    if (millis() - lastPowerToggleTime > COOLDOWN_PERIOD) {
                        if (!isPCisOn) {
                            // Cancel any pending shutdown, then queue the wake action.
                            triggerNormalShutdown = false;
                            triggerForceShutdown = false;
                            Serial.println("Short press: Waking the BC-250...");
                            controllerWantsPowerOn = true;
                        } else {
                            // Cancel any pending wake, then queue the normal shutdown.
                            controllerWantsPowerOn = false;
                            triggerForceShutdown = false;
                            Serial.println("Short press: Normal graceful shutdown...");
                            triggerNormalShutdown = true;
                        }
                    } else {
                        Serial.println("Button ignored: Cooldown active.");
                    }
                }
            }
        } else if (stableState && !longPressFired && (millis() - buttonPressStartedTime >= LONG_PRESS_MS)) {
            // Button held down past the long-press threshold.
            longPressFired = true;
            // COOLDOWN CHECK FOR LONG PRESS
            if (millis() - lastPowerToggleTime > COOLDOWN_PERIOD) {
                if (isPCisOn) {
                    // Cancel any pending wake/shutdown, then queue the hard shutdown.
                    controllerWantsPowerOn = false;
                    triggerNormalShutdown = false;
                    Serial.println("Long press: Initiating Hard Shutdown...");
                    triggerForceShutdown = true;
                }
            }
        }
    }

    // =========================================================
    // 3. EXECUTE WAKE SEQUENCE (Normal Boot)
    // =========================================================
    // If the serial/button input flagged 'controllerWantsPowerOn' and the PC is confirmed OFF
    if (controllerWantsPowerOn && !isPCisOn) {
        Serial.println("Action: Waking up the ATX Power Supply...");
        updateNeoPixel();
        
        digitalWrite(RELAY_PIN, LOW);  // TRIGGER RELAY: Connect PS_ON to Ground
        delay(800);                    // HOLD: 800ms mimics a human pressing a case power button
        digitalWrite(RELAY_PIN, HIGH); // RELEASE RELAY: Disconnect PS_ON from Ground
        
        lastPowerToggleTime = millis(); // Reset our safety cooldown timer
        controllerWantsPowerOn = false; // Clear the flag so it doesn't trigger repeatedly
        updateNeoPixel();
    }

    // =========================================================
    // 3B. EXECUTE NORMAL SHUTDOWN SEQUENCE (Graceful Soft Power-Off)
    // =========================================================
    if (triggerNormalShutdown && isPCisOn) {
        Serial.println("Action: Initiating normal shutdown (short relay pulse)...");
        updateNeoPixel();
        
        digitalWrite(RELAY_PIN, LOW);   // TRIGGER RELAY: Connect PS_ON to Ground
        delay(NORMAL_SHUTDOWN_HOLD_TIME); // HOLD: 500ms = a normal case-power-button press
        digitalWrite(RELAY_PIN, HIGH);  // RELEASE RELAY
        
        lastPowerToggleTime = millis();   // Reset our safety cooldown timer
        triggerNormalShutdown = false;    // Clear the flag
        updateNeoPixel();
    }

    // =========================================================
    // 4. EXECUTE HARD SHUTDOWN SEQUENCE
    // =========================================================
    // If the serial/button input flagged 'triggerForceShutdown' and the PC is confirmed ON
    if (triggerForceShutdown && isPCisOn) {
        Serial.println("Action: Initiating hard shutdown (5.5s relay hold)...");
        updateNeoPixel();
        
        digitalWrite(RELAY_PIN, LOW);   // TRIGGER RELAY: Connect PS_ON to Ground
        delay(HARD_SHUTDOWN_HOLD_TIME); // HOLD: 5500ms forces the motherboard to kill the power
        digitalWrite(RELAY_PIN, HIGH);  // RELEASE RELAY
        
        lastPowerToggleTime = millis(); // Reset our safety cooldown timer
        triggerForceShutdown = false;   // Clear the flag
        updateNeoPixel();
    }

    // A tiny delay prevents the ESP32 from running this loop millions of times per second,
    // saving processor cycles and preventing watchdog timer crashes.
    delay(15); 
}
