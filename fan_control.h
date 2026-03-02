#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <Arduino.h>

// Stepper motor configuration (28BYJ-48 with 1/64 reduction)
// J1 connector mapping (empirically determined):
// J1 Pin 8 (A) = GPIO 12
// J1 Pin 3 (B) = GPIO 13
// J1 Pin 7 (C) = GPIO 27
// J1 Pin 2 (D) = GPIO 14
#ifndef PIN_STEPPER_IN1
#define PIN_STEPPER_IN1 12
#endif
#ifndef PIN_STEPPER_IN2
#define PIN_STEPPER_IN2 13
#endif
#ifndef PIN_STEPPER_IN3
#define PIN_STEPPER_IN3 27
#endif
#ifndef PIN_STEPPER_IN4
#define PIN_STEPPER_IN4 14
#endif

// J1 Pin 1 - FAN SPEED CONTROL via PWM (GPIO 26)
// Empirically determined: GPIO 26 controls fan speed via PWM at 25kHz
// Logic is INVERTED: higher duty cycle = lower speed
#ifndef PIN_FAN_PWM_CONTROL
#define PIN_FAN_PWM_CONTROL 26
#endif

// J1 Pin 6 - FAN POWER CONTROL (GPIO 15)
// ON/OFF power control: HIGH = fan on, LOW = fan off
#ifndef PIN_FAN_POWER
#define PIN_FAN_POWER 15
#endif

// Stepper motor parameters
// Observed: ~1.5 revolutions + extra 512 steps for direction change
#define STEPPER_STEPS_PER_REV 4096    // Full revolution in half-step mode (empirical)
#define STEPPER_STEPS_FOR_REVERSAL 6654  // ~1.5 revolutions + 512 steps
#define STEPPER_STEP_DELAY_US 1350       // Microseconds between steps (~8.3s total)

// Fan timing
#ifndef FAN_RAMP_UP_MS
#define FAN_RAMP_UP_MS 3000
#endif

// PWM values for fan speed control (GPIO 26)
// Logic is INVERTED: 255 = slowest, 0 = fastest
// Values measured from original firmware (voltage on J1 Pin 1)
static const uint8_t FAN_SPEED_TO_PWM[] = {
  255,  // speed 0: OFF (max duty = min speed) (0.71V)
  172,  // speed 1: slowest (2.5V)
  138,  // speed 2 (3.23V)
  124,  // speed 3 (3.55V)
  98,   // speed 4 (4.1V)
  68,   // speed 5 (4.76V)
  47,   // speed 6 (5.2V)
  32    // speed 7: fastest (5.53V)
};

// Half-step sequence for 28BYJ-48
// Pattern values: 1 = energize coil, 0 = de-energize
// Note: If using ULN2003, the driver inverts the signal (HIGH input = LOW output to GND)
static const uint8_t STEPPER_HALF_STEP_SEQ[8][4] = {
  {1, 0, 0, 0},  // Step 0: A
  {1, 1, 0, 0},  // Step 1: AB
  {0, 1, 0, 0},  // Step 2: B
  {0, 1, 1, 0},  // Step 3: BC
  {0, 0, 1, 0},  // Step 4: C
  {0, 0, 1, 1},  // Step 5: CD
  {0, 0, 0, 1},  // Step 6: D
  {1, 0, 0, 1}   // Step 7: DA
};

static uint8_t fanCurrentPwm = 0;
static uint8_t fanTargetPwm = 0;
static uint8_t fanSpeed = 0;
static bool fanInflow = false; // Default position on OFF = outflow
static bool fanMaxMode = false;
static bool fanRamping = false;
static unsigned long fanRampStart = 0;
static uint8_t fanRampFrom = 0;
static bool fanPendingApply = false;
static uint8_t fanPendingPwm = 0;
static uint8_t fanPendingSpeed = 0;
static bool fanPendingMaxMode = false;
static bool fanPowerOffAfterRotate = false;

// Stepper motor state
static int stepperCurrentStep = 0;
static bool stepperRotating = false;
static int stepperTargetSteps = 0;
static int stepperStepsDone = 0;
static unsigned long stepperLastStepTime = 0;
static bool stepperTargetDirection = true; // true = inflow, false = outflow
static bool stepperClockwise = true;       // Rotation direction: true = CW, false = CCW
static bool stepperDirInvert = false;      // If true, swap CW/CCW electrically

static inline bool fanPinValid(int pin) {
  return pin >= 0;
}

static uint8_t fanComputePwm(uint8_t speed, bool maxMode) {
  if (speed < 1 || speed > 7) {
    return 255;  // Inverted: 255 = stopped/min speed
  }
  return maxMode ? 0 : FAN_SPEED_TO_PWM[speed];  // Inverted: 0 = max speed
}

// Stepper motor control functions
static void stepperSetPins(uint8_t in1, uint8_t in2, uint8_t in3, uint8_t in4) {
  digitalWrite(PIN_STEPPER_IN1, in1 ? HIGH : LOW);
  digitalWrite(PIN_STEPPER_IN2, in2 ? HIGH : LOW);
  digitalWrite(PIN_STEPPER_IN3, in3 ? HIGH : LOW);
  digitalWrite(PIN_STEPPER_IN4, in4 ? HIGH : LOW);
}

static void stepperDisable() {
  stepperSetPins(0, 0, 0, 0);
  stepperRotating = false;
}

static void stepperDoStep() {
  const uint8_t* pattern = STEPPER_HALF_STEP_SEQ[stepperCurrentStep];
  stepperSetPins(pattern[0], pattern[1], pattern[2], pattern[3]);

  // Move forward or backward in sequence
  if (stepperClockwise) {
    stepperCurrentStep = (stepperCurrentStep + 1) % 8;
  } else {
    stepperCurrentStep = (stepperCurrentStep - 1 + 8) % 8;
  }

  stepperStepsDone++;

  // Debug every 1000 steps (reduced frequency for high speed)
  if (stepperStepsDone % 1000 == 0) {
    Serial.printf("Stepper: %d/%d steps (%s)\n",
                  stepperStepsDone, stepperTargetSteps,
                  stepperClockwise ? "CW" : "CCW");
  }
}

static void stepperStartRotation(int steps, bool clockwise) {
  stepperRotating = true;
  stepperTargetSteps = steps;
  stepperStepsDone = 0;
  stepperClockwise = clockwise ^ stepperDirInvert;
  stepperLastStepTime = micros();
  Serial.printf("Stepper: starting rotation %d steps (%s)\n",
                steps, clockwise ? "CW" : "CCW");
}

static bool stepperUpdate() {
  if (!stepperRotating) {
    return false;
  }

  if (stepperStepsDone >= stepperTargetSteps) {
    stepperDisable();
    fanInflow = stepperTargetDirection;
    if (fanPendingApply) {
      if (fanPinValid(PIN_FAN_PWM_CONTROL)) {
        ledcWrite(PIN_FAN_PWM_CONTROL, fanPendingPwm);
      }
      fanCurrentPwm = fanPendingPwm;
      fanTargetPwm = fanPendingPwm;
      fanSpeed = fanPendingSpeed;
      fanMaxMode = fanPendingMaxMode;
      fanRamping = false;
      fanPendingApply = false;
    }
    if (fanPowerOffAfterRotate && !fanInflow) {
      digitalWrite(PIN_FAN_POWER, LOW);
      fanPowerOffAfterRotate = false;
    }
    Serial.printf("Stepper: rotation complete, now %s\n", fanInflow ? "INFLOW" : "OUTFLOW");
    return false;
  }

  unsigned long now = micros();
  if (now - stepperLastStepTime >= STEPPER_STEP_DELAY_US) {
    stepperDoStep();
    stepperLastStepTime = now;
  }

  return true;
}

static void fanStop() {
  // Stop fan: set PWM to max (inverted logic)
  if (fanPinValid(PIN_FAN_PWM_CONTROL)) {
    ledcWrite(PIN_FAN_PWM_CONTROL, 255);
  }

  fanCurrentPwm = 255;  // Inverted: 255 = stopped
  fanTargetPwm = 255;
  fanSpeed = 0;
  fanMaxMode = false;
  fanRamping = false;
  fanPendingApply = false;
  fanPowerOffAfterRotate = false;

  // On OFF: return to default outflow position if needed
  bool needReturn = fanInflow || stepperRotating;
  stepperDisable();
  if (needReturn) {
    stepperTargetDirection = false;
    digitalWrite(PIN_FAN_POWER, HIGH);
    fanPowerOffAfterRotate = true;
    stepperStartRotation(STEPPER_STEPS_FOR_REVERSAL, false);
  } else {
    // Turn off main power only when already in default position
    digitalWrite(PIN_FAN_POWER, LOW);
  }
}

static void fanApplyImmediate(bool inflow, uint8_t speed, bool maxMode) {
  if (speed < 1 || speed > 7) {
    fanStop();
    return;
  }

  uint8_t pwmValue = fanComputePwm(speed, maxMode);

  // If direction changed, start stepper rotation with REVERSAL
  // inflow = CW rotation, outflow = CCW rotation (reverse)
  if (inflow != fanInflow && !stepperRotating) {
    stepperTargetDirection = inflow;
    bool clockwise = inflow;  // inflow = CW, outflow = CCW
    // While rotating, keep fan at minimum speed and apply target after rotation
    fanPendingApply = true;
    fanPendingPwm = pwmValue;
    fanPendingSpeed = speed;
    fanPendingMaxMode = maxMode;
    uint8_t minPwm = fanComputePwm(1, false);
    stepperStartRotation(STEPPER_STEPS_FOR_REVERSAL, clockwise);
    Serial.printf("Fan: direction change to %s (1.5 rotations %s)\n",
                  inflow ? "INFLOW" : "OUTFLOW",
                  clockwise ? "CW" : "CCW");
    pwmValue = minPwm;
  }

  // Turn on main power
  digitalWrite(PIN_FAN_POWER, HIGH);

  // Set fan speed via PWM (GPIO 26, inverted logic)
  if (fanPinValid(PIN_FAN_PWM_CONTROL)) {
    ledcWrite(PIN_FAN_PWM_CONTROL, pwmValue);
  }

  fanSpeed = speed;
  fanMaxMode = maxMode;
  fanCurrentPwm = pwmValue;
  fanTargetPwm = pwmValue;
  fanRamping = false;

  Serial.printf("Fan: speed=%d, PWM=%d (GPIO 26, inverted)\n", speed, pwmValue);
}

static void fanRequest(bool inflow, uint8_t speed, bool maxMode = false) {
  if (speed < 1 || speed > 7) {
    fanStop();
    return;
  }

  uint8_t target = fanComputePwm(speed, maxMode);

  // If stepper is rotating, defer applying speed changes until rotation completes
  if (stepperRotating) {
    fanPendingApply = true;
    fanPendingPwm = target;
    fanPendingSpeed = speed;
    fanPendingMaxMode = maxMode;
    digitalWrite(PIN_FAN_POWER, HIGH);
    return;
  }

  // If direction changed, start stepper rotation with REVERSAL
  if (inflow != fanInflow && !stepperRotating) {
    stepperTargetDirection = inflow;
    bool clockwise = inflow;  // inflow = CW, outflow = CCW
    // While rotating, keep fan at minimum speed and apply target after rotation
    fanPendingApply = true;
    fanPendingPwm = target;
    fanPendingSpeed = speed;
    fanPendingMaxMode = maxMode;
    uint8_t minPwm = fanComputePwm(1, false);
    stepperStartRotation(STEPPER_STEPS_FOR_REVERSAL, clockwise);
    Serial.printf("Fan: direction change to %s (1.5 rotations %s)\n",
                  inflow ? "INFLOW" : "OUTFLOW",
                  clockwise ? "CW" : "CCW");
    target = minPwm;
  }

  // Turn on main power
  digitalWrite(PIN_FAN_POWER, HIGH);

  // Apply speed change immediately (ramping disabled for now)
  if (fanPinValid(PIN_FAN_PWM_CONTROL)) {
    ledcWrite(PIN_FAN_PWM_CONTROL, target);
  }
  fanCurrentPwm = target;
  fanTargetPwm = target;
  fanSpeed = speed;
  fanMaxMode = maxMode;
  fanRamping = false;
}

static void fanUpdate() {
  // Update stepper motor if rotating (runs in background)
  stepperUpdate();

  // Handle PWM ramping for speed increases
  if (fanRamping) {
    unsigned long elapsed = millis() - fanRampStart;
    if (elapsed >= FAN_RAMP_UP_MS) {
      fanCurrentPwm = fanTargetPwm;
      fanRamping = false;
    } else {
      // Inverted logic: ramping DOWN the PWM value increases speed
      int32_t delta = (int32_t)(fanTargetPwm - fanRampFrom);
      fanCurrentPwm = (uint8_t)(fanRampFrom + (delta * elapsed) / FAN_RAMP_UP_MS);
    }
    if (fanPinValid(PIN_FAN_PWM_CONTROL)) {
      ledcWrite(PIN_FAN_PWM_CONTROL, fanCurrentPwm);
    }
  }
}

static void fanInit() {
  // Initialize fan PWM control on GPIO 26 (J1 Pin 1)
  // Empirically determined: GPIO 26 controls fan speed via PWM at 25kHz (inverted)
  if (fanPinValid(PIN_FAN_PWM_CONTROL)) {
    ledcAttach(PIN_FAN_PWM_CONTROL, 25000, 8);  // 25kHz, 8-bit resolution
    ledcWrite(PIN_FAN_PWM_CONTROL, 255);  // Start with fan stopped (inverted logic)
    Serial.println("Fan PWM initialized on GPIO 26 (J1 Pin 1) at 25kHz - INVERTED logic");
  }

  // Initialize fan power control on GPIO 15 (J1 Pin 6)
  pinMode(PIN_FAN_POWER, OUTPUT);
  digitalWrite(PIN_FAN_POWER, LOW);  // Start with power off
  Serial.println("Fan power control on GPIO 15 (J1 Pin 6) - OFF");

  // Initialize stepper motor pins
  pinMode(PIN_STEPPER_IN1, OUTPUT);
  pinMode(PIN_STEPPER_IN2, OUTPUT);
  pinMode(PIN_STEPPER_IN3, OUTPUT);
  pinMode(PIN_STEPPER_IN4, OUTPUT);

  stepperDisable();
  fanStop();
}

#endif // FAN_CONTROL_H
