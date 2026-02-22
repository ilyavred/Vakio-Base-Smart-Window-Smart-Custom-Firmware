#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

#include <Arduino.h>
#include "fan_control.h"
#include "button_panel.h"
#include "mqtt.h"

// ============================================================================
// DEV/TEST STATE
// ============================================================================
static bool devLedScanMode = false;
static int devLedScanIndex = -1;
static const int DEV_LED_SCAN_PINS[] = {
  PIN_FAN_PWM_CONTROL
};
static const int DEV_LED_SCAN_PIN_COUNT =
  sizeof(DEV_LED_SCAN_PINS) / sizeof(DEV_LED_SCAN_PINS[0]);

static bool devFanMapMode = false;
static int devFanMapIndex = -1;
static int devFanMapAllIndex = -1;
static int devFanMapActivePin = -1;
static bool devFanMapSignalHigh = false;
static unsigned long devFanMapLastToggle = 0;
static const unsigned long DEV_FAN_MAP_TOGGLE_MS = 500;
static const int DEV_FAN_MAP_DEFAULT_PINS[] = {
  PIN_FAN_PWM_CONTROL,  // J1 Pin 1
  25,                   // J1 Pin 4 (fan line)
  PIN_STEPPER_IN1,      // J1 Pin 8 (A)
  PIN_STEPPER_IN2,      // J1 Pin 3 (B)
  PIN_STEPPER_IN3,      // J1 Pin 7 (C)
  PIN_STEPPER_IN4       // J1 Pin 2 (D)
};
static const int DEV_FAN_MAP_DEFAULT_PIN_COUNT =
  sizeof(DEV_FAN_MAP_DEFAULT_PINS) / sizeof(DEV_FAN_MAP_DEFAULT_PINS[0]);
static const int DEV_FAN_MAP_FORBIDDEN_PINS[] = {
  0, 1, 2, 3,
  6, 7, 8, 9, 10, 11,
  12, 13, 14, 15,
  25, 26, 27
};
static const int DEV_FAN_MAP_INPUT_ONLY_PINS[] = {34, 35, 36, 39};
static const int DEV_FAN_MAP_MAX_GPIO = 39;

static volatile uint32_t devTachPulseCount = 0;
static void IRAM_ATTR devTachPulseISR() {
  devTachPulseCount++;
}

static const int DEV_ANALOG_PINS[] = {32, 33, 34, 35, 36, 39};
static const int DEV_ANALOG_PIN_COUNT =
  sizeof(DEV_ANALOG_PINS) / sizeof(DEV_ANALOG_PINS[0]);

extern ButtonPanelState buttonState;
extern MqttManager mqttMgr;

static inline void devLedScanApply(int index) {
  // Turn off all J1 control pins
  digitalWrite(PIN_STEPPER_IN1, LOW);  // A
  digitalWrite(PIN_STEPPER_IN2, LOW);  // B
  digitalWrite(PIN_STEPPER_IN3, LOW);  // C
  digitalWrite(PIN_STEPPER_IN4, LOW);  // D
  ledcWrite(PIN_FAN_PWM_CONTROL, 0);   // Fan control (J1-1)

  if (index < 0 || index >= DEV_LED_SCAN_PIN_COUNT) {
    return;
  }

  int pin = DEV_LED_SCAN_PINS[index];
  if (pin < 0) {
    return;
  }
  if (pin == PIN_FAN_PWM_CONTROL) {
    ledcWrite(pin, 255);
  } else {
    digitalWrite(pin, HIGH);
  }
}

static inline bool devFanMapPinInList(int pin, const int* list, int count) {
  for (int i = 0; i < count; ++i) {
    if (list[i] == pin) {
      return true;
    }
  }
  return false;
}

static inline bool devFanMapPinAllowed(int pin) {
  if (pin < 0) {
    return false;
  }
  if (devFanMapPinInList(pin, DEV_FAN_MAP_FORBIDDEN_PINS,
                         sizeof(DEV_FAN_MAP_FORBIDDEN_PINS) / sizeof(DEV_FAN_MAP_FORBIDDEN_PINS[0]))) {
    return false;
  }
  if (devFanMapPinInList(pin, DEV_FAN_MAP_INPUT_ONLY_PINS,
                         sizeof(DEV_FAN_MAP_INPUT_ONLY_PINS) / sizeof(DEV_FAN_MAP_INPUT_ONLY_PINS[0]))) {
    return false;
  }
  return true;
}

static inline void devFanMapWrite(int pin, bool high) {
  if (pin == PIN_FAN_PWM_CONTROL) {
    ledcWrite(pin, high ? 255 : 0);
  } else {
    digitalWrite(pin, high ? HIGH : LOW);
  }
}

static inline void devFanMapDeactivatePin(int pin) {
  if (pin < 0) {
    return;
  }
  devFanMapWrite(pin, false);
}

static inline void devFanMapSetActivePin(int pin) {
  if (devFanMapActivePin == pin) {
    return;
  }
  devFanMapDeactivatePin(devFanMapActivePin);
  devFanMapSignalHigh = false;
  devFanMapLastToggle = millis();
  devFanMapActivePin = pin;
  if (devFanMapActivePin >= 0) {
    pinMode(devFanMapActivePin, OUTPUT);
    devFanMapWrite(devFanMapActivePin, false);
  }
}

static inline void devFanMapStart() {
  devFanMapMode = true;
  devFanMapIndex = -1;
  devFanMapSetActivePin(-1);
  devLedScanMode = false;
  devLedScanIndex = -1;
  devLedScanApply(-1);
}

static inline void devFanMapStop() {
  devFanMapMode = false;
  devFanMapIndex = -1;
  devFanMapAllIndex = -1;
  devFanMapSetActivePin(-1);
}

static inline void devFanMapNextDefault() {
  if (!devFanMapMode) {
    devFanMapStart();
  }
  do {
    devFanMapIndex = (devFanMapIndex + 1) % DEV_FAN_MAP_DEFAULT_PIN_COUNT;
  } while (devFanMapIndex >= 0 &&
           !devFanMapPinAllowed(DEV_FAN_MAP_DEFAULT_PINS[devFanMapIndex]));
  int pin = DEV_FAN_MAP_DEFAULT_PINS[devFanMapIndex];
  if (!devFanMapPinAllowed(pin)) {
    return;
  }
  devFanMapSetActivePin(pin);
  Serial.print("FAN map pin: GPIO ");
  Serial.println(pin);
}

static inline void devFanMapNextAll() {
  if (!devFanMapMode) {
    devFanMapStart();
  }
  int tries = 0;
  do {
    devFanMapAllIndex = (devFanMapAllIndex + 1) % (DEV_FAN_MAP_MAX_GPIO + 1);
    tries++;
  } while (tries <= DEV_FAN_MAP_MAX_GPIO + 1 && !devFanMapPinAllowed(devFanMapAllIndex));
  if (!devFanMapPinAllowed(devFanMapAllIndex)) {
    return;
  }
  devFanMapSetActivePin(devFanMapAllIndex);
  Serial.print("FAN map pin: GPIO ");
  Serial.println(devFanMapAllIndex);
}

static inline void devFanMapSetPin(int pin) {
  if (!devFanMapPinAllowed(pin)) {
    Serial.println("FAN map: pin not allowed");
    return;
  }
  if (!devFanMapMode) {
    devFanMapStart();
  }
  devFanMapSetActivePin(pin);
  Serial.print("FAN map pin: GPIO ");
  Serial.println(pin);
}

static inline void devFanMapUpdate() {
  if (!devFanMapMode || devFanMapActivePin < 0) {
    return;
  }
  unsigned long now = millis();
  if (now - devFanMapLastToggle >= DEV_FAN_MAP_TOGGLE_MS) {
    devFanMapSignalHigh = !devFanMapSignalHigh;
    devFanMapWrite(devFanMapActivePin, devFanMapSignalHigh);
    devFanMapLastToggle = now;
  }
}

static inline void devPrintButtonState() {
  bool rawLeft = digitalRead(PIN_BTN_1);
  bool rawSet = digitalRead(PIN_BTN_2);
  bool rawRight = digitalRead(PIN_BTN_3);

  bool btnLeftPressed = (rawLeft == LOW);
  bool btnSetPressed = (rawSet == LOW);
  bool btnRightPressed = (rawRight == LOW);

  Serial.print("BTN raw L/S/R=");
  Serial.print(rawLeft);
  Serial.print("/");
  Serial.print(rawSet);
  Serial.print("/");
  Serial.print(rawRight);

  Serial.print(" pressed (assuming active-low)=");
  Serial.print(btnLeftPressed ? 1 : 0);
  Serial.print("/");
  Serial.print(btnSetPressed ? 1 : 0);
  Serial.print("/");
  Serial.print(btnRightPressed ? 1 : 0);

  Serial.print(" armed=");
  Serial.print(buttonState.buttonsArmed ? 1 : 0);

  Serial.print(" armStart=");
  Serial.print(buttonState.buttonsArmStart);

  Serial.print(" warmup=");
  Serial.print((millis() - buttonState.bootTime < BUTTON_WARMUP_MS) ? "YES" : "NO");

  Serial.print(" bootTime=");
  Serial.print(millis() - buttonState.bootTime);
  Serial.println("ms");
}

static inline void devConsoleHandleSerial() {
  if (!Serial.available()) {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "next" || cmd == "n") {
    if (!devLedScanMode) {
      devLedScanMode = true;
      fanStop();
      devFanMapStop();
      Serial.println("LED scan mode ON");
    }
    devLedScanIndex = (devLedScanIndex + 1) % DEV_LED_SCAN_PIN_COUNT;
    devLedScanApply(devLedScanIndex);
    Serial.print("LED scan pin: GPIO ");
    Serial.println(DEV_LED_SCAN_PINS[devLedScanIndex]);
  } else if (cmd == "exit" || cmd == "stop") {
    devLedScanMode = false;
    devLedScanIndex = -1;
    devLedScanApply(-1);
    fanStop();
    Serial.println("LED scan mode OFF");
  } else if (cmd == "fanmap" || cmd == "fanmap next" || cmd == "fm" || cmd == "fm next") {
    devFanMapNextDefault();
  } else if (cmd == "fanmap all" || cmd == "fm all") {
    devFanMapNextAll();
  } else if (cmd == "fanmap off" || cmd == "fanmap stop" || cmd == "fm off" || cmd == "fm stop") {
    devFanMapStop();
    Serial.println("FAN map mode OFF");
  } else if (cmd == "fanmap list" || cmd == "fm list") {
    Serial.print("FAN map default pins: ");
    for (int i = 0; i < DEV_FAN_MAP_DEFAULT_PIN_COUNT; ++i) {
      if (i > 0) {
        Serial.print(", ");
      }
      Serial.print(DEV_FAN_MAP_DEFAULT_PINS[i]);
    }
    Serial.println();
  } else if (cmd.startsWith("fanmap pin ") || cmd.startsWith("fm pin ")) {
    int spaceIdx = cmd.lastIndexOf(' ');
    int pin = cmd.substring(spaceIdx + 1).toInt();
    devFanMapSetPin(pin);
  } else if (cmd == "inflow") {
    Serial.println("Setting INFLOW direction (CW rotation)");
    fanRequest(true, 3, false);
  } else if (cmd == "outflow") {
    Serial.println("Setting OUTFLOW direction (CCW rotation)");
    fanRequest(false, 3, false);
  } else if (cmd.startsWith("speed ")) {
    int speed = cmd.substring(6).toInt();
    if (speed >= 1 && speed <= 7) {
      Serial.printf("Setting fan speed to %d\n", speed);
      fanRequest(true, speed, false);
    } else {
      Serial.println("Speed must be 1-7");
    }
  } else if (cmd == "btn" || cmd == "b") {
    devPrintButtonState();
  } else if (cmd == "stepper" || cmd == "st") {
    Serial.println("Starting stepper rotation test...");
    fanStop();
    delay(100);
    fanRequest(true, 3, false);
    delay(500);
    fanRequest(false, 3, false);
  } else if (cmd.startsWith("stepdiag")) {
    int delayMs = 5;
    int steps = 64;
    char dir[8] = {0};
    char holdArg[8] = {0};
    int parsed = sscanf(cmd.c_str(), "stepdiag %d %d %7s %7s", &delayMs, &steps, dir, holdArg);
    bool clockwise = true;
    bool hold = false;
    if (parsed >= 3) {
      if (strcmp(dir, "ccw") == 0) {
        clockwise = false;
      } else if (strcmp(dir, "cw") == 0) {
        clockwise = true;
      }
    }
    if (parsed >= 4) {
      if (strcmp(holdArg, "hold") == 0) {
        hold = true;
      }
    }

    Serial.printf("Stepper diag: delay=%dms steps=%d dir=%s%s\n",
                  delayMs, steps, clockwise ? "CW" : "CCW", hold ? " hold" : "");
    stepperClockwise = clockwise ^ stepperDirInvert;
    stepperRotating = false;
    stepperStepsDone = 0;
    for (int i = 0; i < steps; ++i) {
      stepperDoStep();
      delay(delayMs);
    }
    if (!hold) {
      stepperDisable();
    }
  } else if (cmd == "stepdir") {
    Serial.printf("Stepper dir invert: %s\n", stepperDirInvert ? "ON" : "OFF");
  } else if (cmd == "stepdir invert") {
    stepperDirInvert = true;
    Serial.println("Stepper dir invert: ON");
  } else if (cmd == "stepdir normal") {
    stepperDirInvert = false;
    Serial.println("Stepper dir invert: OFF");
  } else if (cmd.startsWith("tach")) {
    int windowMs = 5000;
    int ppr = 2;
    sscanf(cmd.c_str(), "tach %d %d", &windowMs, &ppr);
    if (windowMs < 500) windowMs = 500;
    if (ppr < 1) ppr = 1;

    Serial.printf("Tach test on GPIO 25: window=%dms ppr=%d\n", windowMs, ppr);
    pinMode(25, INPUT_PULLUP);
    devTachPulseCount = 0;
    attachInterrupt(digitalPinToInterrupt(25), devTachPulseISR, FALLING);
    unsigned long start = millis();
    while (millis() - start < (unsigned long)windowMs) {
      delay(10);
    }
    detachInterrupt(digitalPinToInterrupt(25));
    uint32_t pulses = devTachPulseCount;
    float seconds = windowMs / 1000.0f;
    float hz = pulses / seconds;
    float rpm = (hz * 60.0f) / (float)ppr;
    Serial.printf("Pulses: %lu, Hz: %.2f, RPM: %.1f\n", (unsigned long)pulses, hz, rpm);
  } else if (cmd.startsWith("analog")) {
    int count = 1;
    int delayMs = 500;
    sscanf(cmd.c_str(), "analog %d %d", &count, &delayMs);
    if (count < 1) count = 1;
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 5000) delayMs = 5000;

    Serial.printf("Analog scan: count=%d delay=%dms (approx V)\n", count, delayMs);
    for (int i = 0; i < DEV_ANALOG_PIN_COUNT; ++i) {
      analogSetPinAttenuation(DEV_ANALOG_PINS[i], ADC_11db);
    }
    for (int c = 0; c < count; ++c) {
      Serial.printf("Sample %d:\n", c + 1);
      for (int i = 0; i < DEV_ANALOG_PIN_COUNT; ++i) {
        int pin = DEV_ANALOG_PINS[i];
        int raw = analogRead(pin);
        float v = (raw * 3.3f) / 4095.0f;
        Serial.printf("  GPIO %d: raw=%d, V=%.3f\n", pin, raw, v);
      }
      if (c + 1 < count) {
        delay(delayMs);
      }
    }
  } else if (cmd.startsWith("steptest")) {
    Serial.println("Manual stepper test: 1.5 rotations CW");
    stepperStartRotation(STEPPER_STEPS_FOR_REVERSAL, true);
  } else if (cmd == "step360") {
    Serial.println("Testing full 360° rotation CW...");
    stepperStartRotation(STEPPER_STEPS_PER_REV, true);
    Serial.printf("Started rotation: %d steps\n", STEPPER_STEPS_PER_REV);
  } else if (cmd == "step360ccw") {
    Serial.println("Testing full 360° rotation CCW...");
    stepperStartRotation(STEPPER_STEPS_PER_REV, false);
    Serial.printf("Started rotation: %d steps CCW\n", STEPPER_STEPS_PER_REV);
  } else if (cmd == "step10") {
    Serial.println("Testing 10 full rotations CW...");
    stepperStartRotation(STEPPER_STEPS_PER_REV * 10, true);
  } else if (cmd == "step10ccw") {
    Serial.println("Testing 10 full rotations CCW...");
    stepperStartRotation(STEPPER_STEPS_PER_REV * 10, false);
  } else if (cmd == "stepinit") {
    Serial.println("Reinitializing stepper pins...");
    pinMode(PIN_STEPPER_IN1, OUTPUT);
    pinMode(PIN_STEPPER_IN2, OUTPUT);
    pinMode(PIN_STEPPER_IN3, OUTPUT);
    pinMode(PIN_STEPPER_IN4, OUTPUT);
    Serial.println("Stepper pins reinitialized");
  } else if (cmd == "fantest") {
    Serial.println("=== Fan PWM Test ===");
    Serial.println("Testing GPIO 26 (J1 Pin 1) PWM control");

    for (int pwm = 0; pwm <= 255; pwm += 51) {
      Serial.printf("PWM = %d\n", pwm);
      ledcWrite(26, pwm);
      delay(2000);
    }

    Serial.println("Test complete, setting PWM = 0");
    ledcWrite(26, 0);
  } else if (cmd == "gpio15test") {
    Serial.println("=== GPIO 15 (J1 Pin 6) Test ===");
    Serial.println("Toggling GPIO 15 (possible ENABLE or power control)");

    digitalWrite(15, LOW);
    Serial.println("GPIO 15 = LOW (waiting 3 sec) - fan should stop");
    delay(3000);

    digitalWrite(15, HIGH);
    Serial.println("GPIO 15 = HIGH (waiting 3 sec) - fan should run");
    delay(3000);

    digitalWrite(15, LOW);
    Serial.println("GPIO 15 = LOW again - fan should stop");
    delay(2000);

    Serial.println("\nIf fan stops/starts, GPIO 15 is main power control!");
  } else if (cmd == "gpio15pwm") {
    Serial.println("=== Testing if GPIO 15 can do PWM ===");
    Serial.println("Maybe GPIO 15 controls fan speed via PWM?");

    ledcAttach(15, 25000, 8);

    for (int pwm = 0; pwm <= 255; pwm += 51) {
      Serial.printf("GPIO 15 PWM = %d\n", pwm);
      ledcWrite(15, pwm);
      delay(2000);
    }

    Serial.println("Test complete, setting PWM = 0");
    ledcWrite(15, 0);
    ledcDetach(15);
    pinMode(15, OUTPUT);
    digitalWrite(15, LOW);
  } else if (cmd == "allgpiotest") {
    Serial.println("=== Testing ALL possible GPIO ===");
    Serial.println("This will toggle each GPIO and you can check J1 pins");
    Serial.println();

    int testGpios[] = {12, 13, 14, 15, 25, 26, 27, 32, 33};
    const char* gpioNames[] = {
      "GPIO 12", "GPIO 13", "GPIO 14", "GPIO 15",
      "GPIO 25", "GPIO 26", "GPIO 27", "GPIO 32", "GPIO 33"
    };

    for (int i = 0; i < 9; i++) {
      pinMode(testGpios[i], OUTPUT);
      digitalWrite(testGpios[i], LOW);
    }

    Serial.println("All GPIO set to LOW (baseline)");
    delay(2000);

    for (int i = 0; i < 9; i++) {
      Serial.printf("\n=== Testing %s ===\n", gpioNames[i]);
      Serial.println("Setting HIGH for 2 seconds...");

      digitalWrite(testGpios[i], HIGH);
      delay(2000);

      Serial.println("Back to LOW");
      digitalWrite(testGpios[i], LOW);
      delay(500);

      Serial.println("Press Enter to continue...");
      while (!Serial.available()) delay(10);
      while (Serial.available()) Serial.read();
    }

    Serial.println("\n=== Test complete ===");
  } else if (cmd == "stepperbrute") {
    Serial.println("=== Brute-force stepper pin search ===");
    Serial.println("Trying different GPIO combinations for stepper");

    int possibleGpios[] = {12, 13, 14, 15, 25, 26, 27, 32, 33};
    (void)possibleGpios;

    Serial.println("Testing if stepper responds to any 4-pin combination...");
    Serial.println("Testing combination: 14,13,25,27 (current)");

    for (int repeat = 0; repeat < 3; repeat++) {
      for (int step = 0; step < 8; step++) {
        const uint8_t* pattern = STEPPER_HALF_STEP_SEQ[step];
        digitalWrite(14, pattern[0] ? HIGH : LOW);
        digitalWrite(13, pattern[1] ? HIGH : LOW);
        digitalWrite(25, pattern[2] ? HIGH : LOW);
        digitalWrite(27, pattern[3] ? HIGH : LOW);
        delay(200);
      }
    }

    Serial.println("If motor didn't move, pins are wrong!");
    Serial.println("Try other combinations manually or check hardware");
  } else if (cmd == "debugstate") {
    Serial.println("=== Current State Debug ===");
    Serial.printf("fanSpeed: %d\n", fanSpeed);
    Serial.printf("fanCurrentPwm: %d\n", fanCurrentPwm);
    Serial.printf("fanTargetPwm: %d\n", fanTargetPwm);
    Serial.printf("fanInflow: %s\n", fanInflow ? "true" : "false");
    Serial.printf("stepperRotating: %s\n", stepperRotating ? "true" : "false");
    Serial.printf("PIN_FAN_PWM_CONTROL (GPIO 26): %d\n", PIN_FAN_PWM_CONTROL);
    Serial.printf("PIN_FAN_POWER (GPIO 15): %d\n", PIN_FAN_POWER);

    VakioState& state = mqttMgr.getState();
    Serial.printf("MQTT powerOn: %s\n", state.powerOn ? "true" : "false");
    Serial.printf("MQTT workmode: %d\n", state.workmode);
    Serial.printf("MQTT speed: %d\n", state.speed);
  } else if (cmd == "pwmtest") {
    Serial.println("=== Testing PWM on GPIO 15 step by step ===");
    Serial.println("This will test if GPIO 15 PWM actually controls fan speed");

    ledcAttach(15, 25000, 8);

    Serial.println("PWM = 0 (fan should be OFF)");
    ledcWrite(15, 0);
    delay(3000);

    Serial.println("PWM = 50 (fan should be SLOW)");
    ledcWrite(15, 50);
    delay(3000);

    Serial.println("PWM = 100");
    ledcWrite(15, 100);
    delay(3000);

    Serial.println("PWM = 150");
    ledcWrite(15, 150);
    delay(3000);

    Serial.println("PWM = 200");
    ledcWrite(15, 200);
    delay(3000);

    Serial.println("PWM = 255 (fan should be MAXIMUM)");
    ledcWrite(15, 255);
    delay(3000);

    Serial.println("PWM = 0 (stopping)");
    ledcWrite(15, 0);

    Serial.println("\nIf fan speed changed, GPIO 15 PWM works!");
    Serial.println("If fan stayed at max speed, problem is elsewhere.");
  } else if (cmd == "gpio26dac") {
    Serial.println("=== Testing GPIO 26 (J1 Pin 1) with DAC ===");
    Serial.println("ESP32 has 8-bit DAC on GPIO 25 and 26");
    Serial.println("Testing if GPIO 26 controls fan speed via analog voltage");
    Serial.println();

    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    Serial.println("GPIO 15 set HIGH (providing 12V power)");
    delay(500);

    Serial.println("DAC = 0 (0V, fan should be OFF or MINIMUM)");
    dacWrite(26, 0);
    delay(3000);

    Serial.println("DAC = 50 (~0.65V)");
    dacWrite(26, 50);
    delay(3000);

    Serial.println("DAC = 100 (~1.3V)");
    dacWrite(26, 100);
    delay(3000);

    Serial.println("DAC = 150 (~2.0V)");
    dacWrite(26, 150);
    delay(3000);

    Serial.println("DAC = 200 (~2.6V)");
    dacWrite(26, 200);
    delay(3000);

    Serial.println("DAC = 255 (~3.3V, fan should be MAXIMUM)");
    dacWrite(26, 255);
    delay(3000);

    Serial.println("DAC = 0 (stopping)");
    dacWrite(26, 0);

    Serial.println("\nIf fan speed changed with DAC values:");
    Serial.println("  => GPIO 26 controls fan speed via analog voltage!");
    Serial.println("  => GPIO 15 is just power on/off");
  } else if (cmd == "dacfull") {
    Serial.println("=== Testing FULL DAC range on GPIO 26 ===");
    Serial.println("Testing from current minimum (85) down to 0 (hardware max)");
    Serial.println();

    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    Serial.println("GPIO 15 set HIGH (power ON)");
    delay(500);

    Serial.println("\nTesting current range (our current min to max):");
    Serial.println("DAC = 255 (should be OFF/minimum)");
    dacWrite(26, 255);
    delay(3000);

    Serial.println("DAC = 219 (speed 1)");
    dacWrite(26, 219);
    delay(3000);

    Serial.println("DAC = 146 (speed 3)");
    dacWrite(26, 146);
    delay(3000);

    Serial.println("DAC = 85 (speed 7, current max)");
    dacWrite(26, 85);
    delay(3000);

    Serial.println("\n=== NOW TESTING BELOW CURRENT MINIMUM ===");
    Serial.println("Testing if we can go faster than current 'speed 7':");

    Serial.println("DAC = 70");
    dacWrite(26, 70);
    delay(3000);

    Serial.println("DAC = 50");
    dacWrite(26, 50);
    delay(3000);

    Serial.println("DAC = 30");
    dacWrite(26, 30);
    delay(3000);

    Serial.println("DAC = 10");
    dacWrite(26, 10);
    delay(3000);

    Serial.println("DAC = 0 (hardware maximum speed!)");
    dacWrite(26, 0);
    delay(3000);

    Serial.println("\nDAC = 255 (stopping)");
    dacWrite(26, 255);

    Serial.println("\n=== Analysis ===");
    Serial.println("If speeds 85->70->50->30->10->0 were all different:");
    Serial.println("  => We can use lower DAC values for more speed levels");
    Serial.println("If speeds plateaued (e.g., 85 and 70 felt the same):");
    Serial.println("  => Hardware has discrete speed steps");
  } else if (cmd == "volttest") {
    Serial.println("=== Testing voltage output on J1 Pin 1 (GPIO 26) ===");
    Serial.println("Measuring if GPIO 26 DAC creates voltage gradient on J1 Pin 1");
    Serial.println("Use multimeter on J1 Pin 1 to measure voltage");
    Serial.println();

    pinMode(15, OUTPUT);
    digitalWrite(15, LOW);
    Serial.println("GPIO 15 = LOW (main power OFF to isolate signal)");
    delay(1000);

    Serial.println("\nDAC = 0 (ESP32 outputs 0V)");
    dacWrite(26, 0);
    Serial.println("Measure J1 Pin 1 voltage - should be ~0V");
    delay(5000);

    Serial.println("\nDAC = 85");
    dacWrite(26, 85);
    Serial.println("Measure J1 Pin 1 voltage - should be ~1.1V");
    delay(5000);

    Serial.println("\nDAC = 128");
    dacWrite(26, 128);
    Serial.println("Measure J1 Pin 1 voltage - should be ~1.65V");
    delay(5000);

    Serial.println("\nDAC = 192");
    dacWrite(26, 192);
    Serial.println("Measure J1 Pin 1 voltage - should be ~2.5V");
    delay(5000);

    Serial.println("\nDAC = 255 (ESP32 outputs 3.3V)");
    dacWrite(26, 255);
    Serial.println("Measure J1 Pin 1 voltage - should be ~3.3V");
    delay(5000);

    Serial.println("\nNow testing WITH power ON:");
    digitalWrite(15, HIGH);
    Serial.println("GPIO 15 = HIGH (main power ON)");
    delay(1000);

    Serial.println("\nDAC = 0");
    dacWrite(26, 0);
    Serial.println("Measure J1 Pin 1 voltage WITH POWER");
    delay(5000);

    Serial.println("\nDAC = 128");
    dacWrite(26, 128);
    Serial.println("Measure J1 Pin 1 voltage WITH POWER");
    delay(5000);

    Serial.println("\nDAC = 255");
    dacWrite(26, 255);
    Serial.println("Measure J1 Pin 1 voltage WITH POWER");
    delay(5000);

    dacWrite(26, 255);
    digitalWrite(15, LOW);
    Serial.println("\nTest complete. Report voltage measurements.");
  } else if (cmd == "pwmfan") {
    Serial.println("=== Testing if fan responds to traditional PWM ===");
    Serial.println("Maybe fan needs PWM frequency, not analog voltage?");
    Serial.println();

    pinMode(26, OUTPUT);

    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    Serial.println("GPIO 15 = HIGH (power ON)");
    delay(500);

    Serial.println("\nTrying 25kHz PWM on GPIO 26:");
    ledcAttach(26, 25000, 8);

    Serial.println("PWM 25kHz, duty=0 (0%)");
    ledcWrite(26, 0);
    delay(3000);

    Serial.println("PWM 25kHz, duty=64 (25%)");
    ledcWrite(26, 64);
    delay(3000);

    Serial.println("PWM 25kHz, duty=128 (50%)");
    ledcWrite(26, 128);
    delay(3000);

    Serial.println("PWM 25kHz, duty=192 (75%)");
    ledcWrite(26, 192);
    delay(3000);

    Serial.println("PWM 25kHz, duty=255 (100%)");
    ledcWrite(26, 255);
    delay(3000);

    ledcDetach(26);

    Serial.println("\nTrying 1kHz PWM on GPIO 26:");
    ledcAttach(26, 1000, 8);

    Serial.println("PWM 1kHz, duty=64 (25%)");
    ledcWrite(26, 64);
    delay(3000);

    Serial.println("PWM 1kHz, duty=128 (50%)");
    ledcWrite(26, 128);
    delay(3000);

    Serial.println("PWM 1kHz, duty=192 (75%)");
    ledcWrite(26, 192);
    delay(3000);

    ledcDetach(26);
    pinMode(26, OUTPUT);
    digitalWrite(26, LOW);
    digitalWrite(15, LOW);

    Serial.println("\nTest complete. Did fan speed change with PWM?");
  } else if (cmd == "stepraw") {
    Serial.println("Raw stepper test - one full sequence");
    Serial.println("GPIO 12 (J1-8), 13 (J1-3), 27 (J1-7), 14 (J1-2)");
    for (int i = 0; i < 8; i++) {
      const uint8_t* pattern = STEPPER_HALF_STEP_SEQ[i];
      digitalWrite(12, pattern[0] ? HIGH : LOW);  // A (IN1)
      digitalWrite(13, pattern[1] ? HIGH : LOW);  // B (IN2)
      digitalWrite(27, pattern[2] ? HIGH : LOW);  // C (IN3)
      digitalWrite(14, pattern[3] ? HIGH : LOW);  // D (IN4)
      Serial.printf("Step %d: IN1=%d IN2=%d IN3=%d IN4=%d\n",
                    i, pattern[0], pattern[1], pattern[2], pattern[3]);
      delay(100);
    }
  } else if (cmd == "j1scan") {
    Serial.println("=== J1 10-pin connector scan (excluding 16/17/19) ===");
    Serial.println("Testing each GPIO - check J1 pins (2,3,7,8) with multimeter");
    Serial.println("Expecting ULN2003 inverted output:");
    Serial.println("  LOW input  -> J1 pin ~12V (floating, pulled up by motor coil)");
    Serial.println("  HIGH input -> J1 pin ~0.8V (pulled to GND through ULN2003)");
    Serial.println();

    int testPins[] = {32, 33, 25, 26, 27, 12, 13, 14, 15};
    const char* pinNames[] = {
      "GPIO 32",
      "GPIO 33",
      "GPIO 25",
      "GPIO 26",
      "GPIO 27",
      "GPIO 12",
      "GPIO 13",
      "GPIO 14",
      "GPIO 15"
    };

    Serial.println("Known J1 pins:");
    Serial.println("  Pin 1:  +6.25V");
    Serial.println("  Pin 5:  GND");
    Serial.println("  Pin 6:  +12V");
    Serial.println("  Pin 9:  +3.3V");
    Serial.println("  Pin 10: GND");
    Serial.println();
    Serial.println("Testing pins: 2, 3, 7, 8");
    Serial.println("Expected: 4 pins for stepper (2,3,7,8). Pins 1/4 go direct to fan.");

    for (int i = 0; i < 9; i++) {
      if (testPins[i] == 32) {
        ledcDetach(32);
      }
      pinMode(testPins[i], OUTPUT);

      Serial.printf("\n=== Testing %s ===\n", pinNames[i]);
      Serial.println("Watch J1 pins (2,3,7,8) - one should change!");

      for (int j = 0; j < 9; j++) {
        digitalWrite(testPins[j], HIGH);
      }

      delay(1000);

      Serial.printf("All HIGH (baseline: J1 should be ~0.8V)\n");
      delay(2000);

      digitalWrite(testPins[i], LOW);
      Serial.printf("Set %s to LOW -> J1 should go to ~12V if connected\n", pinNames[i]);
      delay(3000);

      for (int j = 0; j < 5; j++) {
        digitalWrite(testPins[i], HIGH);
        Serial.println("  HIGH (~0.8V)");
        delay(800);

        digitalWrite(testPins[i], LOW);
        Serial.println("  LOW  (~12V)");
        delay(800);
      }

      digitalWrite(testPins[i], LOW);

      Serial.println("\nDid you see voltage change on J1 pins (2,3,7,8)?");
      Serial.println("Press Enter for next GPIO...\n");
      while (!Serial.available()) {
        delay(100);
      }
      while (Serial.available()) {
        Serial.read();
      }
    }

    Serial.println("=== Scan complete ===");
    Serial.println("Reattaching PWM to GPIO 32...");
    ledcAttach(32, 25000, 8);
  } else if (cmd == "help" || cmd == "h") {
    Serial.println("Commands:");
    Serial.println("  btn|b         - Show button state");
    Serial.println("  debugstate    - Show current fan/stepper state");
    Serial.println("  fantest       - Test fan PWM (GPIO 26)");
    Serial.println("  gpio15test    - Test GPIO 15 on/off");
    Serial.println("  gpio15pwm     - Test GPIO 15 PWM control");
    Serial.println("  allgpiotest   - Test all GPIO one by one");
    Serial.println("  stepperbrute  - Brute-force stepper pin test");
    Serial.println("  stepper|st    - Test stepper (CW then CCW)");
    Serial.println("  stepdiag [ms] [steps] [cw|ccw] [hold] - Slow stepper diag");
    Serial.println("  stepdir [normal|invert] - Toggle CW/CCW inversion");
    Serial.println("  tach [ms] [ppr] - Read tach pulses on GPIO 25");
    Serial.println("  analog [count] [ms] - Read ADC1 pins (32,33,34,35,36,39)");
    Serial.println("  step10      - 10 rotations CW");
    Serial.println("  step10ccw   - 10 rotations CCW");
    Serial.println("  step360     - 1 rotation CW");
    Serial.println("  step360ccw  - 1 rotation CCW");
    Serial.println("  steptest    - Quick test (1.5 rotations CW)");
    Serial.println("  stepinit    - Reinitialize stepper GPIO pins");
    Serial.println("  stepraw     - Raw stepper pattern test (slow)");
    Serial.println("  j1scan      - Scan J1 connector pins (use with multimeter)");
    Serial.println("  next|n      - LED scan next pin");
    Serial.println("  exit|stop   - Exit LED scan mode");
    Serial.println("  fanmap|fm   - FAN map next default pin");
    Serial.println("  fanmap all  - FAN map next pin from full GPIO scan");
    Serial.println("  fanmap pin <gpio> - FAN map selected GPIO");
    Serial.println("  fanmap list - Show FAN map default pins");
    Serial.println("  fanmap off  - Exit FAN map mode");
    Serial.println("  help|h      - Show this help");
  } else {
    Serial.println("Unknown command. Type 'help' for commands.");
  }
}

static inline void devConsoleUpdate() {
  devFanMapUpdate();
}

static inline bool devConsoleIsActive() {
  return devLedScanMode || devFanMapMode;
}

#endif // DEV_CONSOLE_H
