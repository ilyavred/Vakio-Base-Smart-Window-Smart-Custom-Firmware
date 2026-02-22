#ifndef BUTTON_PANEL_H
#define BUTTON_PANEL_H

#include <Arduino.h>
#include "display_controller.h"

#ifndef BUTTON_LONG_PRESS
#define BUTTON_LONG_PRESS 3000
#endif
#ifndef BUTTON_DEBOUNCE_MS
#define BUTTON_DEBOUNCE_MS 150
#endif
#ifndef BUTTON_IGNORE_AFTER_BOOT
#define BUTTON_IGNORE_AFTER_BOOT 5000
#endif
#ifndef BUTTON_WARMUP_MS
#define BUTTON_WARMUP_MS 3000
#endif
#ifndef BUTTON_ARM_STABLE_MS
#define BUTTON_ARM_STABLE_MS 500
#endif
#ifndef BUTTON_MIN_PRESS_MS
#define BUTTON_MIN_PRESS_MS 150
#endif
#ifndef BUTTON_REPEAT_GUARD_MS
#define BUTTON_REPEAT_GUARD_MS 1000
#endif
#ifndef BUTTON_RELEASE_GUARD_MS
#define BUTTON_RELEASE_GUARD_MS 150
#endif
#ifndef BUTTON_DEBUG
#define BUTTON_DEBUG 0
#endif
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW 0
#endif

#ifndef DISPLAY_BRIGHTNESS_MIN
#define DISPLAY_BRIGHTNESS_MIN 0
#endif
#ifndef DISPLAY_BRIGHTNESS_MAX
#define DISPLAY_BRIGHTNESS_MAX 255
#endif
#ifndef DISPLAY_BRIGHTNESS_STEP
#define DISPLAY_BRIGHTNESS_STEP 5
#endif
#ifndef DISPLAY_BRIGHTNESS_INTERVAL_MS
#define DISPLAY_BRIGHTNESS_INTERVAL_MS 30
#endif

struct ButtonPanelCallbacks {
  void (*onLeftShort)();
  void (*onLeftLong)();
  void (*onSetShort)();
  void (*onRightShort)();
  void (*onRightLong)();
};

struct ButtonPanelState {
  unsigned long bootTime;
  bool buttonsArmed;
  unsigned long buttonsArmStart;
  bool buttonLogicDetected;
  bool btnLeftActiveLow;
  bool btnSetActiveLow;
  bool btnRightActiveLow;
  unsigned int leftHighCount;
  unsigned int leftLowCount;
  unsigned int setHighCount;
  unsigned int setLowCount;
  unsigned int rightHighCount;
  unsigned int rightLowCount;
  unsigned long lastButtonDebug;
  unsigned long stuckPressedStart;
  bool lastAllPressed;
  bool lastDebouncedLeft;
  bool lastDebouncedSet;
  bool lastDebouncedRight;
  bool lastPressedLeft;
  bool lastPressedSet;
  bool lastPressedRight;
  unsigned long leftChangeTime;
  unsigned long setChangeTime;
  unsigned long rightChangeTime;
  bool rawBtnLeft;
  bool rawBtnSet;
  bool rawBtnRight;
  unsigned long btnLeftPressStart;
  unsigned long btnSetPressStart;
  unsigned long btnRightPressStart;
  bool btnLeftLongHandled;
  bool btnSetLongHandled;
  bool btnRightLongHandled;
  unsigned long lastLeftActionTime;
  unsigned long lastSetActionTime;
  unsigned long lastRightActionTime;
  uint8_t activeButton;
  bool ignoreUntilRelease;
  bool leftReady;
  bool setReady;
  bool rightReady;
  unsigned long leftReleaseStart;
  unsigned long setReleaseStart;
  unsigned long rightReleaseStart;
  bool leftShortPending;
  bool setShortPending;
  bool rightShortPending;
  unsigned long leftReleaseCandidate;
  unsigned long setReleaseCandidate;
  unsigned long rightReleaseCandidate;
  unsigned long leftPressDuration;
  unsigned long setPressDuration;
  unsigned long rightPressDuration;
  bool brightnessDirectionUp;
  bool brightnessAdjusting;
  unsigned long brightnessLastStepTime;
};

static void buttonPanelInit(ButtonPanelState* st) {
  st->bootTime = millis();
  st->buttonsArmed = false;
  st->buttonsArmStart = 0;
  st->buttonLogicDetected = true;
  st->btnLeftActiveLow = (BUTTON_ACTIVE_LOW != 0);
  st->btnSetActiveLow = (BUTTON_ACTIVE_LOW != 0);
  st->btnRightActiveLow = (BUTTON_ACTIVE_LOW != 0);
  st->leftHighCount = 0;
  st->leftLowCount = 0;
  st->setHighCount = 0;
  st->setLowCount = 0;
  st->rightHighCount = 0;
  st->rightLowCount = 0;
  st->lastButtonDebug = 0;
  st->stuckPressedStart = 0;
  st->lastAllPressed = false;
  st->lastDebouncedLeft = HIGH;
  st->lastDebouncedSet = HIGH;
  st->lastDebouncedRight = HIGH;
  st->lastPressedLeft = false;
  st->lastPressedSet = false;
  st->lastPressedRight = false;
  st->leftChangeTime = 0;
  st->setChangeTime = 0;
  st->rightChangeTime = 0;
  st->rawBtnLeft = HIGH;
  st->rawBtnSet = HIGH;
  st->rawBtnRight = HIGH;
  st->btnLeftPressStart = 0;
  st->btnSetPressStart = 0;
  st->btnRightPressStart = 0;
  st->btnLeftLongHandled = false;
  st->btnSetLongHandled = false;
  st->btnRightLongHandled = false;
  st->lastLeftActionTime = 0;
  st->lastSetActionTime = 0;
  st->lastRightActionTime = 0;
  st->activeButton = 0;
  st->ignoreUntilRelease = false;
  st->leftReady = false;
  st->setReady = false;
  st->rightReady = false;
  st->leftReleaseStart = 0;
  st->setReleaseStart = 0;
  st->rightReleaseStart = 0;
  st->leftShortPending = false;
  st->setShortPending = false;
  st->rightShortPending = false;
  st->leftReleaseCandidate = 0;
  st->setReleaseCandidate = 0;
  st->rightReleaseCandidate = 0;
  st->leftPressDuration = 0;
  st->setPressDuration = 0;
  st->rightPressDuration = 0;
  st->brightnessDirectionUp = false;
  st->brightnessAdjusting = false;
  st->brightnessLastStepTime = 0;
}

static void buttonPanelUpdate(ButtonPanelState* st, const ButtonPanelCallbacks* cb, DisplayController* dc) {
  bool rawLeft = digitalRead(PIN_BTN_1);
  bool rawSet = digitalRead(PIN_BTN_2);
  bool rawRight = digitalRead(PIN_BTN_3);

  if (rawLeft != st->rawBtnLeft) {
    st->rawBtnLeft = rawLeft;
    st->leftChangeTime = millis();
  }
  if (rawSet != st->rawBtnSet) {
    st->rawBtnSet = rawSet;
    st->setChangeTime = millis();
  }
  if (rawRight != st->rawBtnRight) {
    st->rawBtnRight = rawRight;
    st->rightChangeTime = millis();
  }

  bool debouncedLeft = st->lastDebouncedLeft;
  bool debouncedSet = st->lastDebouncedSet;
  bool debouncedRight = st->lastDebouncedRight;

  if (millis() - st->leftChangeTime >= BUTTON_DEBOUNCE_MS) {
    debouncedLeft = st->rawBtnLeft;
  }
  if (millis() - st->setChangeTime >= BUTTON_DEBOUNCE_MS) {
    debouncedSet = st->rawBtnSet;
  }
  if (millis() - st->rightChangeTime >= BUTTON_DEBOUNCE_MS) {
    debouncedRight = st->rawBtnRight;
  }

  bool btnLeftPressed = st->btnLeftActiveLow ? (debouncedLeft == LOW) : (debouncedLeft == HIGH);
  bool btnSetPressed = st->btnSetActiveLow ? (debouncedSet == LOW) : (debouncedSet == HIGH);
  bool btnRightPressed = st->btnRightActiveLow ? (debouncedRight == LOW) : (debouncedRight == HIGH);
  bool anyPressed = btnLeftPressed || btnSetPressed || btnRightPressed;

  if (!btnLeftPressed) {
    if (st->leftReleaseStart == 0) {
      st->leftReleaseStart = millis();
    } else if (!st->leftReady &&
               (millis() - st->leftReleaseStart >= BUTTON_RELEASE_GUARD_MS)) {
      st->leftReady = true;
    }
  } else {
    st->leftReleaseStart = 0;
  }

  if (!btnSetPressed) {
    if (st->setReleaseStart == 0) {
      st->setReleaseStart = millis();
    } else if (!st->setReady &&
               (millis() - st->setReleaseStart >= BUTTON_RELEASE_GUARD_MS)) {
      st->setReady = true;
    }
  } else {
    st->setReleaseStart = 0;
  }

  if (!btnRightPressed) {
    if (st->rightReleaseStart == 0) {
      st->rightReleaseStart = millis();
    } else if (!st->rightReady &&
               (millis() - st->rightReleaseStart >= BUTTON_RELEASE_GUARD_MS)) {
      st->rightReady = true;
    }
  } else {
    st->rightReleaseStart = 0;
  }

#if BUTTON_DEBUG
  if (millis() - st->lastButtonDebug >= 1000) {
    st->lastButtonDebug = millis();
    Serial.print("BTN raw L/S/R=");
    Serial.print(st->rawBtnLeft);
    Serial.print("/");
    Serial.print(st->rawBtnSet);
    Serial.print("/");
    Serial.print(st->rawBtnRight);
    Serial.print(" debounced=");
    Serial.print(debouncedLeft);
    Serial.print("/");
    Serial.print(debouncedSet);
    Serial.print("/");
    Serial.print(debouncedRight);
    Serial.print(" pressed=");
    Serial.print(btnLeftPressed ? 1 : 0);
    Serial.print("/");
    Serial.print(btnSetPressed ? 1 : 0);
    Serial.print("/");
    Serial.print(btnRightPressed ? 1 : 0);
    Serial.print(" armed=");
    Serial.print(st->buttonsArmed ? 1 : 0);
    Serial.print(" warmup=");
    Serial.print(millis() - st->bootTime < BUTTON_WARMUP_MS ? 1 : 0);
    Serial.print(" logic=");
    Serial.println(st->buttonLogicDetected ? "ok" : "pending");
  }
#endif

  if (btnLeftPressed || btnSetPressed || btnRightPressed) {
    if (!displayControllerIsOn(dc)) {
      displayControllerWake(dc);
      st->activeButton = 0;
      st->ignoreUntilRelease = true;
      st->lastDebouncedLeft = debouncedLeft;
      st->lastDebouncedSet = debouncedSet;
      st->lastDebouncedRight = debouncedRight;
      st->lastPressedLeft = btnLeftPressed;
      st->lastPressedSet = btnSetPressed;
      st->lastPressedRight = btnRightPressed;
      return;
    }
  }

  if (millis() - st->bootTime < BUTTON_WARMUP_MS) {
    st->lastDebouncedLeft = debouncedLeft;
    st->lastDebouncedSet = debouncedSet;
    st->lastDebouncedRight = debouncedRight;
    st->lastPressedLeft = btnLeftPressed;
    st->lastPressedSet = btnSetPressed;
    st->lastPressedRight = btnRightPressed;
    return;
  }

  if (!st->buttonsArmed) {
    // Принудительно активируем кнопки через 5 секунд после загрузки
    // Это обходит проблему с нестабильными сенсорными кнопками
    unsigned long timeSinceBoot = millis() - st->bootTime;

    if (timeSinceBoot >= 5000) {
      // Прошло 5 секунд - принудительно активируем
      st->buttonsArmed = true;
      Serial.println("Buttons ARMED (forced after 5s timeout)");
    } else {
      // Пытаемся активировать нормально если все кнопки отпущены
      if (!btnLeftPressed && !btnSetPressed && !btnRightPressed) {
        if (st->buttonsArmStart == 0) {
          st->buttonsArmStart = millis();
        } else if (millis() - st->buttonsArmStart >= BUTTON_ARM_STABLE_MS) {
          st->buttonsArmed = true;
          Serial.println("Buttons ARMED (normal - all released)");
        }
      } else {
        st->buttonsArmStart = 0;
      }
    }

    st->lastDebouncedLeft = debouncedLeft;
    st->lastDebouncedSet = debouncedSet;
    st->lastDebouncedRight = debouncedRight;
    st->lastPressedLeft = btnLeftPressed;
    st->lastPressedSet = btnSetPressed;
    st->lastPressedRight = btnRightPressed;
    return;
  }

  if (millis() - st->bootTime < BUTTON_IGNORE_AFTER_BOOT) {
    st->lastDebouncedLeft = debouncedLeft;
    st->lastDebouncedSet = debouncedSet;
    st->lastDebouncedRight = debouncedRight;
    st->lastPressedLeft = btnLeftPressed;
    st->lastPressedSet = btnSetPressed;
    st->lastPressedRight = btnRightPressed;
    return;
  }

  int pressedCount = (btnLeftPressed ? 1 : 0) + (btnSetPressed ? 1 : 0) + (btnRightPressed ? 1 : 0);

  if (st->ignoreUntilRelease) {
    if (pressedCount == 0) {
      st->ignoreUntilRelease = false;
    }
    st->lastDebouncedLeft = debouncedLeft;
    st->lastDebouncedSet = debouncedSet;
    st->lastDebouncedRight = debouncedRight;
    st->lastPressedLeft = btnLeftPressed;
    st->lastPressedSet = btnSetPressed;
    st->lastPressedRight = btnRightPressed;
    return;
  }

  if (st->activeButton == 0) {
    if (btnLeftPressed) {
      st->activeButton = 1;
      if (st->btnLeftPressStart == 0) {
        st->btnLeftPressStart = millis();
        st->btnLeftLongHandled = false;
      }
    } else if (btnSetPressed) {
      st->activeButton = 2;
      if (st->btnSetPressStart == 0) {
        st->btnSetPressStart = millis();
        st->btnSetLongHandled = false;
        st->brightnessAdjusting = false;
      }
    } else if (btnRightPressed) {
      st->activeButton = 3;
      if (st->btnRightPressStart == 0) {
        st->btnRightPressStart = millis();
        st->btnRightLongHandled = false;
      }
    }
  }

  if (st->activeButton == 1) {
    if (btnLeftPressed) {
      if (!st->btnLeftLongHandled &&
          (millis() - st->btnLeftPressStart > BUTTON_LONG_PRESS)) {
        if (millis() - st->lastLeftActionTime >= BUTTON_REPEAT_GUARD_MS) {
          if (cb && cb->onLeftLong) cb->onLeftLong();
          st->lastLeftActionTime = millis();
        }
        st->btnLeftLongHandled = true;
      }
    } else if (st->lastPressedLeft) {
      unsigned long pressMs = millis() - st->btnLeftPressStart;
      if (!st->btnLeftLongHandled &&
          pressMs >= BUTTON_MIN_PRESS_MS &&
          (millis() - st->lastLeftActionTime >= BUTTON_REPEAT_GUARD_MS)) {
        if (cb && cb->onLeftShort) cb->onLeftShort();
        st->lastLeftActionTime = millis();
      }
      st->activeButton = 0;
      st->btnLeftPressStart = 0;
      st->btnLeftLongHandled = false;
    }
  } else if (st->activeButton == 2) {
    if (btnSetPressed) {
      if (!st->btnSetLongHandled &&
          (millis() - st->btnSetPressStart > BUTTON_LONG_PRESS)) {
        if (millis() - st->lastSetActionTime >= BUTTON_REPEAT_GUARD_MS) {
#if BUTTON_DEBUG
          Serial.println("o Long: Brightness adjust");
#endif
          st->btnSetLongHandled = true;
          st->brightnessAdjusting = true;
          st->brightnessLastStepTime = 0;
          st->lastSetActionTime = millis();
        }
      }

      if (st->brightnessAdjusting) {
        unsigned long now = millis();
        if (st->brightnessLastStepTime == 0 ||
            now - st->brightnessLastStepTime >= DISPLAY_BRIGHTNESS_INTERVAL_MS) {
          int delta = st->brightnessDirectionUp ? DISPLAY_BRIGHTNESS_STEP : -DISPLAY_BRIGHTNESS_STEP;
          int next = (int)displayControllerGetBrightness(dc) + delta;
          if (next < DISPLAY_BRIGHTNESS_MIN) {
            next = DISPLAY_BRIGHTNESS_MIN;
          } else if (next > DISPLAY_BRIGHTNESS_MAX) {
            next = DISPLAY_BRIGHTNESS_MAX;
          }
          displayControllerSetBrightness(dc, (uint8_t)next);
          st->brightnessLastStepTime = now;
        }
      }
    } else if (st->lastPressedSet) {
      bool wasLong = st->btnSetLongHandled;
      if (wasLong && st->brightnessAdjusting) {
        st->brightnessAdjusting = false;
        st->brightnessDirectionUp = !st->brightnessDirectionUp;
      }
      if (!wasLong) {
        unsigned long pressMs = millis() - st->btnSetPressStart;
        if (pressMs >= BUTTON_MIN_PRESS_MS &&
            (millis() - st->lastSetActionTime >= BUTTON_REPEAT_GUARD_MS)) {
          if (cb && cb->onSetShort) cb->onSetShort();
          st->lastSetActionTime = millis();
        }
      }
      st->activeButton = 0;
      st->btnSetPressStart = 0;
      st->btnSetLongHandled = false;
    }
  } else if (st->activeButton == 3) {
    if (btnRightPressed) {
      if (!st->btnRightLongHandled &&
          (millis() - st->btnRightPressStart > BUTTON_LONG_PRESS)) {
        if (millis() - st->lastRightActionTime >= BUTTON_REPEAT_GUARD_MS) {
          if (cb && cb->onRightLong) cb->onRightLong();
          st->lastRightActionTime = millis();
        }
        st->btnRightLongHandled = true;
      }
    } else if (st->lastPressedRight) {
      unsigned long pressMs = millis() - st->btnRightPressStart;
      if (!st->btnRightLongHandled &&
          pressMs >= BUTTON_MIN_PRESS_MS &&
          (millis() - st->lastRightActionTime >= BUTTON_REPEAT_GUARD_MS)) {
        if (cb && cb->onRightShort) cb->onRightShort();
        st->lastRightActionTime = millis();
      }
      st->activeButton = 0;
      st->btnRightPressStart = 0;
      st->btnRightLongHandled = false;
    }
  }

  st->lastDebouncedLeft = debouncedLeft;
  st->lastDebouncedSet = debouncedSet;
  st->lastDebouncedRight = debouncedRight;
  st->lastPressedLeft = btnLeftPressed;
  st->lastPressedSet = btnSetPressed;
  st->lastPressedRight = btnRightPressed;
}

#endif // BUTTON_PANEL_H
