#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "display.h"
#include "mqtt.h"
#include "app_types.h"

#ifndef BACKLIGHT_TIMEOUT
#define BACKLIGHT_TIMEOUT 600000
#endif

#ifndef DISPLAY_BRIGHTNESS_MIN
#define DISPLAY_BRIGHTNESS_MIN 0
#endif

#ifndef DISPLAY_BRIGHTNESS_MAX
#define DISPLAY_BRIGHTNESS_MAX 255
#endif

struct DisplayController {
  DisplayManager* mgr;
  bool backlightOn;
  unsigned long lastActivityTime;
  uint8_t brightness;
  void (*onBacklightChange)(bool on);
};

static void displayControllerInit(DisplayController* dc, DisplayManager* mgr) {
  dc->mgr = mgr;
  dc->backlightOn = true;
  dc->lastActivityTime = millis();
  dc->brightness = DISPLAY_BRIGHTNESS_MAX;
  dc->mgr->getDisplay()->setContrast(dc->brightness);
  dc->onBacklightChange = nullptr;
}

static void displayControllerSetBacklightCallback(DisplayController* dc, void (*callback)(bool)) {
  dc->onBacklightChange = callback;
}

static void displayControllerWake(DisplayController* dc) {
  dc->lastActivityTime = millis();
  if (!dc->backlightOn) {
    dc->backlightOn = true;
    dc->mgr->getDisplay()->setPowerSave(0);
    if (dc->onBacklightChange) dc->onBacklightChange(true);
    Serial.println("Display woke up");
  }
}

static void displayControllerSleep(DisplayController* dc) {
  if (dc->backlightOn) {
    dc->backlightOn = false;
    dc->mgr->getDisplay()->setPowerSave(1);
    if (dc->onBacklightChange) dc->onBacklightChange(false);
    Serial.println("Display sleep");
  }
}

static bool displayControllerIsOn(const DisplayController* dc) {
  return dc->backlightOn;
}

static void displayControllerSetBrightness(DisplayController* dc, uint8_t value) {
  dc->brightness = value;
  dc->mgr->getDisplay()->setContrast(dc->brightness);
}

static uint8_t displayControllerGetBrightness(const DisplayController* dc) {
  return dc->brightness;
}

static void displayControllerUpdateMain(DisplayController* dc, DeviceMode mode, bool deviceOff, VakioState& state) {
  if (mode != MODE_CONNECTED && mode != MODE_AP) return;

  char line1[32];
  char line2[32];

  if (!state.powerOn) {
    strcpy(line1, "OFF");
    strcpy(line2, "");
  } else {
    switch (state.workmode) {
      case WORKMODE_INFLOW:      strcpy(line1, "INFLOW"); break;
      case WORKMODE_INFLOW_MAX:  strcpy(line1, "INFLOW MAX"); break;
      case WORKMODE_OUTFLOW:     strcpy(line1, "OUTFLOW"); break;
      case WORKMODE_OUTFLOW_MAX: strcpy(line1, "OUTFLOW MAX"); break;
      case WORKMODE_RECUPERATOR: strcpy(line1, "RECUPERATOR"); break;
      case WORKMODE_WINTER:      strcpy(line1, "WINTER"); break;
      case WORKMODE_NIGHT:       strcpy(line1, "NIGHT"); break;
      default:                   strcpy(line1, "---"); break;
    }
    sprintf(line2, "Speed: %d", state.speed);
  }

  if (deviceOff) {
    strcpy(line1, "OFF");
    strcpy(line2, "");
  }

  dc->mgr->showMainScreen(line1, line2);
}

static void displayControllerTick(DisplayController* dc) {
  if (dc->backlightOn && (millis() - dc->lastActivityTime > BACKLIGHT_TIMEOUT)) {
    displayControllerSleep(dc);
  }
}

#endif // DISPLAY_CONTROLLER_H
