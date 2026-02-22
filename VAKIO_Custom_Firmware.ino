/**
 * VAKIO Custom Firmware
 *
 * Плата: VK-06N03 ESP32 Rev_2
 * Чип: ESP32-D0WDQ6
 *
 * MQTT API совместим с официальным VAKIO API:
 * https://github.com/vakio-ru/vakio-public-api
 *
 * Конфигурация Arduino IDE:
 *   Board: ESP32 Dev Module
 *   Upload Speed: 115200
 *   Flash Mode: DIO
 *   Flash Frequency: 40 MHz
 *
 * Библиотеки:
 *   - U8g2 (дисплей)
 *   - PubSubClient (MQTT)
 *
 * См. HARDWARE_SPECS.md для полной документации по пинам
 */

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// IR Receiver
#define PIN_IR_RECV     4

// Buttons (Active LOW)
#define PIN_BTN_1       5
#define PIN_BTN_2       18
#define PIN_BTN_3       23

// I2C (OLED Display)
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     22
#define OLED_ADDR       0x3C

// Fan Control (ULN2003 driver - inverted logic)
// HIGH на входе ULN2003 → выход к GND
// J1 connector empirical mapping (updated):
//   Pin 2: GPIO 14 (Stepper D)
//   Pin 3: GPIO 13 (Stepper B)
//   Pin 7: GPIO 27 (Stepper C)
//   Pin 8: GPIO 12 (Stepper A)
//   Pin 1: GPIO 26 (Fan control line, direct to fan)
//   Pin 4: GPIO 25 (Fan line, direct to fan)
//   Pin 6: GPIO 15 (Fan power +12V ON/OFF)
// GPIO 32, 33 not connected to J1
#define PIN_FAN_PWM_1   26    // Fan control on J1 Pin 1

// PWM Configuration
#define PWM_FREQ        25000
#define PWM_RESOLUTION  8
#define FAN_DIR_CHANGE_DELAY_MS 5000
#define FAN_RAMP_UP_MS  3000

// Button LED Configuration
#define PIN_LED_BTN_1   16    // LED кнопки <
#define PIN_LED_BTN_2   17    // LED кнопки o
#define PIN_LED_BTN_3   19    // LED кнопки >
#define BUTTON_LED_BRIGHTNESS 30  // Яркость светодиодов кнопок (0-255)

// ============================================================================
// INCLUDES
// ============================================================================

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024
#endif

#include <Wire.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <PubSubClient.h>
// QR code handled by ESP_QRcode library (included in display.h)

#include "app_types.h"
#include "config.h"
#include "display.h"
#include "webserver.h"
#include "mqtt.h"
#include "fan_control.h"
#include "display_controller.h"
#include "button_panel.h"
#include "dev_console.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define WIFI_CONNECT_TIMEOUT    15000
#define WIFI_RECONNECT_INTERVAL 30000
#define MQTT_RECONNECT_INTERVAL 5000
#define BUTTON_LONG_PRESS       5000
#define BUTTON_DEBOUNCE_MS      40
#define BUTTON_IGNORE_AFTER_BOOT 5000
#define BUTTON_WARMUP_MS        3000
#define BUTTON_ARM_STABLE_MS    200
#define DISPLAY_UPDATE_INTERVAL 1000
#define AP_MODE_TIMEOUT         300000  // 5 минут для режима AP
#define BACKLIGHT_TIMEOUT       600000  // 10 минут для подсветки
#define DISPLAY_BRIGHTNESS_MIN  0
#define DISPLAY_BRIGHTNESS_MAX  255
#define DISPLAY_BRIGHTNESS_STEP 5
#define DISPLAY_BRIGHTNESS_INTERVAL_MS 30
#define BUTTON_DEBUG           0
#define I2C_SCAN_ON_BOOT       0
#define IR_ENABLE              1
#define IR_PRINT_RAW           0
#define STATE_SAVE_DEBOUNCE_MS  10000 // 10 seconds to wait before saving state to flash
#define DEVICE_STARTS_OFF      1

#include "wifi_manager.h"
#include "ir_remote.h"

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

// Display
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
DisplayManager displayMgr(&u8g2);

// Configuration
ConfigManager configMgr;

// Web Server
WebServerManager* webServer = nullptr;

// MQTT
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
MqttManager mqttMgr(&mqttClient);

// ============================================================================
// STATE VARIABLES
// ============================================================================

DeviceMode currentMode = MODE_INIT;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long apModeStartTime = 0;        // Время начала режима AP
bool deviceOff = false;                    // Устройство выключено (кнопкой <)

// State saving debounce
unsigned long lastStateChangeTime = 0;
bool stateChangeNeedsSaving = false;

DisplayController displayCtrl;
ButtonPanelState buttonState;
ButtonPanelCallbacks buttonCallbacks;
static bool buttonLedsEnabled = false;

// ============================================================================ 
// RECUPERATOR LOGIC
// ============================================================================
static bool recupPhaseInflow = true;
static unsigned long recupPhaseStartMs = 0;
static uint32_t recupOutflowCount = 0;
static const unsigned long RECUP_PHASE_MS = 180000;      // 3 minutes
static const unsigned long RECUP_WINTER_LONG_MS = 900000; // 15 minutes
static unsigned long maxModeStartMs = 0;
static const unsigned long MAX_MODE_DURATION_MS = 600000; // 10 minutes

// ============================================================================
// STATE PERSISTENCE
// ============================================================================

void triggerStateSave() {
  stateChangeNeedsSaving = true;
  lastStateChangeTime = millis();
}

// ============================================================================
// UI & DEVICE LOGIC
// ============================================================================

// Forward declarations from fan_control.h
extern bool stepperRotating;
extern void stepperStartRotation(int steps, bool clockwise);
extern bool stepperUpdate();
extern const uint8_t STEPPER_HALF_STEP_SEQ[8][4];
extern uint8_t fanSpeed;
extern uint8_t fanCurrentPwm;
extern uint8_t fanTargetPwm;
extern bool fanInflow;

// Управление светодиодами кнопок
// ВАЖНО: GPIO 16/17/19 используются также для шагового двигателя
// Светодиоды автоматически отключаются при вращении шагового двигателя
void updateButtonLeds() {
  static bool initialized = false;

  if (!initialized) {
    pinMode(PIN_LED_BTN_1, OUTPUT);
    pinMode(PIN_LED_BTN_2, OUTPUT);
    pinMode(PIN_LED_BTN_3, OUTPUT);

    digitalWrite(PIN_LED_BTN_1, HIGH);
    digitalWrite(PIN_LED_BTN_2, HIGH);
    digitalWrite(PIN_LED_BTN_3, HIGH);
    ledcWrite(PIN_FAN_PWM_1, 0);
    initialized = true;
    buttonLedsEnabled = false;
  }

  // Don't touch button LEDs while stepper is rotating
  if (stepperRotating) {
    return;
  }

  unsigned long now = millis();
  if (now < BUTTON_IGNORE_AFTER_BOOT) {
    const unsigned long onMs = 600;
    const unsigned long gapMs = 100;
    const unsigned long slotMs = onMs + gapMs;
    unsigned long phase = now % (slotMs * 3);
    int ledIndex = (int)(phase / slotMs);
    unsigned long within = phase % slotMs;
    bool ledOn = (within < onMs);

    digitalWrite(PIN_LED_BTN_1, HIGH);
    digitalWrite(PIN_LED_BTN_2, HIGH);
    digitalWrite(PIN_LED_BTN_3, HIGH);

    if (ledOn) {
      if (ledIndex == 0) {
        digitalWrite(PIN_LED_BTN_1, LOW);
      } else if (ledIndex == 1) {
        digitalWrite(PIN_LED_BTN_2, LOW);
      } else {
        digitalWrite(PIN_LED_BTN_3, LOW);
      }
    }
    return;
  }

  bool shouldBeOn = displayControllerIsOn(&displayCtrl) && deviceOff;

  if (shouldBeOn != buttonLedsEnabled) {
    buttonLedsEnabled = shouldBeOn;

    if (buttonLedsEnabled) {
      digitalWrite(PIN_LED_BTN_1, LOW);
      digitalWrite(PIN_LED_BTN_2, LOW);
      digitalWrite(PIN_LED_BTN_3, LOW);
    } else {
      digitalWrite(PIN_LED_BTN_1, HIGH);
      digitalWrite(PIN_LED_BTN_2, HIGH);
      digitalWrite(PIN_LED_BTN_3, HIGH);
    }
  }
}

void onBacklightChange(bool on) {
  updateButtonLeds();
}

// Выключение устройства (кнопка <)
void turnOffDevice() {
  deviceOff = true;
  fanStop();
  mqttMgr.setPower(false);
  displayMgr.showMessage("VAKIO", "Off");
  displayControllerWake(&displayCtrl);
  updateButtonLeds();
  // Serial.println("Device turned OFF");
}

// Включение устройства
void turnOnDevice() {
  deviceOff = false;
  mqttMgr.setWorkmode(WORKMODE_INFLOW);
  displayControllerWake(&displayCtrl);
  updateButtonLeds();
  // Serial.println("Device turned ON");
}

// Обработчик IR команд
void handleIrCommand(uint8_t cmd) {
  displayControllerWake(&displayCtrl);

  switch (cmd) {
    case IR_CMD_OFF:
      if (deviceOff) {
        // Serial.println("IR: ON");
        turnOnDevice();
      } else {
        // Serial.println("IR: OFF");
        turnOffDevice();
      }
      break;

    case IR_CMD_INFLOW:
      // Serial.println("IR: INFLOW mode");
      if (deviceOff) turnOnDevice();
      mqttMgr.setWorkmode(WORKMODE_INFLOW);
      break;

    case IR_CMD_OUTFLOW:
      // Serial.println("IR: OUTFLOW mode");
      if (deviceOff) turnOnDevice();
      mqttMgr.setWorkmode(WORKMODE_OUTFLOW);
      break;

    case IR_CMD_RECUPERATION:
      // Serial.println("IR: RECUPERATION mode");
      if (deviceOff) turnOnDevice();
      mqttMgr.setWorkmode(WORKMODE_RECUPERATOR);
      break;

    case IR_CMD_WINTER:
      // Serial.println("IR: WINTER mode");
      if (deviceOff) turnOnDevice();
      mqttMgr.setWorkmode(WORKMODE_WINTER);
      break;

    case IR_CMD_NIGHT:
      // Serial.println("IR: NIGHT mode");
      if (deviceOff) turnOnDevice();
      mqttMgr.setWorkmode(WORKMODE_NIGHT);
      break;

    case IR_CMD_SPEED_UP:
      // Serial.println("IR: Speed UP");
      if (!deviceOff) {
        VakioState& state = mqttMgr.getState();
        if (state.speed < 7) {
          mqttMgr.setSpeed(state.speed + 1);
        }
      }
      break;

    case IR_CMD_SPEED_DOWN:
      // Serial.println("IR: Speed DOWN");
      if (!deviceOff) {
        VakioState& state = mqttMgr.getState();
        if (state.speed > 1) {
          mqttMgr.setSpeed(state.speed - 1);
        }
      }
      break;

    default:
      // Serial.print("IR: Unknown command 0x");
      // Serial.println(cmd, HEX);
      break;
  }
}

// Called when MQTT state changes
void onMqttStateChange(VakioState& state) {
  // Serial.printf("State change: power=%d, mode=%d, speed=%d\n",
  //               state.powerOn, state.workmode, state.speed);

  // Включаем подсветку при любом изменении состояния
  displayControllerWake(&displayCtrl);

  // Если устройство было выключено кнопкой, сбрасываем флаг при включении
  if (state.powerOn) {
    deviceOff = false;
  }

  if (!state.powerOn) {
    fanStop();
    if (deviceOff) {
      displayMgr.showMessage("VAKIO", "Off");
    } else {
      displayControllerUpdateMain(&displayCtrl, currentMode, deviceOff, state);
    }
  } else {
    switch (state.workmode) {
      case WORKMODE_INFLOW:
        fanRequest(true, state.speed, false);
        break;
      case WORKMODE_INFLOW_MAX:
        maxModeStartMs = millis();
        fanRequest(true, state.speed, true);
        break;
      case WORKMODE_OUTFLOW:
        fanRequest(false, state.speed, false);
        break;
      case WORKMODE_OUTFLOW_MAX:
        maxModeStartMs = millis();
        fanRequest(false, state.speed, true);
        break;
      case WORKMODE_RECUPERATOR:
      case WORKMODE_WINTER:
        // Recuperation: alternating inflow/outflow
        recupPhaseInflow = true;
        recupPhaseStartMs = millis();
        recupOutflowCount = 0;
        fanRequest(true, state.speed, false);
        break;
      case WORKMODE_NIGHT:
        // Night mode: low speed inflow
        fanRequest(true, 1, false);
        break;
      default:
        fanStop();
        break;
    }
  }

  // Update display
  displayControllerUpdateMain(&displayCtrl, currentMode, deviceOff, state);
  
  // Trigger a debounced save to flash
  triggerStateSave();
}

// ============================================================================
// MQTT FUNCTIONS
// ============================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  mqttMgr.handleMessage(topic, payload, length);
}

bool connectMQTT() {
  if (!configMgr.hasMQTT()) {
    return false;
  }

  Config& cfg = configMgr.getConfig();

  mqttClient.setServer(cfg.mqtt_server, cfg.mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);

  // Set topic prefix from config
  mqttMgr.setTopicPrefix(cfg.mqtt_topic_prefix);

  Serial.print("Connecting to MQTT...");

  bool connected;
  if (strlen(cfg.mqtt_user) > 0) {
    connected = mqttClient.connect(cfg.mqtt_client_id, cfg.mqtt_user, cfg.mqtt_password);
  } else {
    connected = mqttClient.connect(cfg.mqtt_client_id);
  }

  if (connected) {
    Serial.println("connected!");
    displayMgr.setMqttConnected(true);
    mqttMgr.onConnect();
    // Включаем подсветку при успешном подключении
    displayControllerWake(&displayCtrl);
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
    displayMgr.setMqttConnected(false);
    return false;
  }
}

// ============================================================================
// DISPLAY
// ============================================================================

// ============================================================================
// BUTTON HANDLING
// ============================================================================
// Layout:  < (BTN1)    o (BTN2)    > (BTN3)
//          LEFT        SET         RIGHT
//          GPIO 5      GPIO 18     GPIO 23

// Button timing
static unsigned long btnLeftPressStart = 0;
static unsigned long btnSetPressStart = 0;
static unsigned long btnRightPressStart = 0;
static bool btnLeftLongHandled = false;
static bool btnSetLongHandled = false;
static bool btnRightLongHandled = false;

void cycleModePrev() {
  VakioState& state = mqttMgr.getState();
  if (!state.powerOn) {
    mqttMgr.setWorkmode(WORKMODE_INFLOW);
    return;
  }
  // Cycle backwards: INFLOW ← NIGHT ← RECUPERATOR ← OUTFLOW ← INFLOW
  switch (state.workmode) {
    case WORKMODE_INFLOW:
    case WORKMODE_INFLOW_MAX:
      mqttMgr.setWorkmode(WORKMODE_NIGHT);
      break;
    case WORKMODE_NIGHT:
      mqttMgr.setWorkmode(WORKMODE_RECUPERATOR);
      break;
    case WORKMODE_RECUPERATOR:
    case WORKMODE_WINTER:
      mqttMgr.setWorkmode(WORKMODE_OUTFLOW);
      break;
    case WORKMODE_OUTFLOW:
    case WORKMODE_OUTFLOW_MAX:
      mqttMgr.setWorkmode(WORKMODE_INFLOW);
      break;
    default:
      mqttMgr.setWorkmode(WORKMODE_INFLOW);
      break;
  }
}

void scanI2CBus() {
  Serial.println("I2C scan start");
  uint8_t count = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device at 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
      count++;
    }
  }
  Serial.print("I2C scan done, devices: ");
  Serial.println(count);
}

void cycleModeNext() {
  VakioState& state = mqttMgr.getState();
  if (!state.powerOn) {
    mqttMgr.setWorkmode(WORKMODE_INFLOW);
    return;
  }
  // Cycle forward: INFLOW → OUTFLOW → RECUPERATOR → NIGHT → INFLOW
  switch (state.workmode) {
    case WORKMODE_INFLOW:
    case WORKMODE_INFLOW_MAX:
      mqttMgr.setWorkmode(WORKMODE_OUTFLOW);
      break;
    case WORKMODE_OUTFLOW:
    case WORKMODE_OUTFLOW_MAX:
      mqttMgr.setWorkmode(WORKMODE_RECUPERATOR);
      break;
    case WORKMODE_RECUPERATOR:
    case WORKMODE_WINTER:
      mqttMgr.setWorkmode(WORKMODE_NIGHT);
      break;
    case WORKMODE_NIGHT:
      mqttMgr.setWorkmode(WORKMODE_INFLOW);
      break;
    default:
      mqttMgr.setWorkmode(WORKMODE_INFLOW);
      break;
  }
}

void onLeftShort() {
  if (!deviceOff) {
    // Serial.println("< Short: Speed down");
    VakioState& state = mqttMgr.getState();
    uint8_t newSpeed = state.speed;
    if (newSpeed > 1) {
      newSpeed--;
      mqttMgr.setSpeed(newSpeed);
    }
  } else {
    turnOnDevice();
  }
}

void onLeftLong() {
  // Serial.println("< Long: Turn OFF device");
  turnOffDevice();
}

void onSetShort() {
  if (deviceOff) {
    turnOnDevice();
  } else {
    // Serial.println("o Short: Next mode");
    cycleModeNext();
  }
}

void onRightShort() {
  if (!deviceOff) {
    // Serial.println("> Short: Speed up");
    VakioState& state = mqttMgr.getState();
    uint8_t newSpeed = state.speed;
    if (newSpeed < 7) {
      newSpeed++;
      mqttMgr.setSpeed(newSpeed);
    }
  } else {
    turnOnDevice();
  }
}

void onRightLong() {
  // Serial.println("> Long: AP mode");
  WiFi.disconnect();
  wifiStartAPMode();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("================================");
  Serial.println("VAKIO Custom Firmware v1.1-TEST");
  Serial.println("LED/FAN DISABLED FOR TESTING");
  Serial.println("MQTT API Compatible");
  Serial.println("================================");

  // Initialize buttons
  pinMode(PIN_BTN_1, INPUT_PULLUP);
  pinMode(PIN_BTN_2, INPUT_PULLUP);
  pinMode(PIN_BTN_3, INPUT_PULLUP);

  // Initialize IR receiver
  irInit(PIN_IR_RECV);
  setIrCommandCallback(handleIrCommand);

  // Initialize fan PWM
  fanInit();

  // Initialize I2C and display
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
#if I2C_SCAN_ON_BOOT
  delay(500);
  Serial.println("Starting I2C scan...");
  Serial.flush();
  scanI2CBus();
#endif
  displayMgr.begin();
  displayControllerInit(&displayCtrl, &displayMgr);
  displayControllerSetBacklightCallback(&displayCtrl, onBacklightChange);
  updateButtonLeds();

  // Set MQTT state change callback
  mqttMgr.setStateChangeCallback(onMqttStateChange);

  // Load configuration
  configMgr.begin();

  // Restore saved state
  Config& cfg = configMgr.getConfig();
  displayControllerSetBrightness(&displayCtrl, cfg.display_brightness);
  VakioWorkMode savedMode = (VakioWorkMode)cfg.last_workmode;
  if (DEVICE_STARTS_OFF) {
    deviceOff = true;
    mqttMgr.restoreState(WORKMODE_OFF, cfg.last_speed);
  } else if (savedMode != WORKMODE_OFF) {
    mqttMgr.restoreState(savedMode, cfg.last_speed);
  }

  buttonPanelInit(&buttonState);
  buttonCallbacks.onLeftShort = onLeftShort;
  buttonCallbacks.onLeftLong = onLeftLong;
  buttonCallbacks.onSetShort = onSetShort;
  buttonCallbacks.onRightShort = onRightShort;
  buttonCallbacks.onRightLong = onRightLong;

  // Show welcome
  displayMgr.showMessage("VAKIO", "Starting...");
  delay(1000);

  // Check for saved WiFi config
  if (configMgr.isConfigured()) {
    Serial.println("Found saved WiFi config, connecting...");
    wifiStartStationMode();

    if (wifiConnectToWiFi()) {
      if (configMgr.hasMQTT()) {
        connectMQTT();
      }
      displayControllerUpdateMain(&displayCtrl, currentMode, deviceOff, mqttMgr.getState());
    } else {
      // WiFi не удалось подключить — показываем сообщение
      Serial.println("WiFi failed, waiting for button > to start AP");
      currentMode = MODE_ERROR;
      displayMgr.showMessage("WiFi Error", "Hold [>] setup");
    }
  } else {
    // Нет сохранённой конфигурации — НЕ запускаем AP автоматически
    Serial.println("No WiFi config, waiting for button > to start AP");
    currentMode = MODE_INIT;
    displayMgr.showNoConfig();
  }

  Serial.println("Setup complete!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  devConsoleHandleSerial();
  devConsoleUpdate();
  // Recuperator phase handling
  if (!deviceOff) {
    VakioState& state = mqttMgr.getState();
    // Auto downgrade MAX modes after timeout
    if (state.powerOn) {
      if (state.workmode == WORKMODE_INFLOW_MAX ||
          state.workmode == WORKMODE_OUTFLOW_MAX) {
        unsigned long now = millis();
        if (maxModeStartMs > 0 && (now - maxModeStartMs) >= MAX_MODE_DURATION_MS) {
          if (state.workmode == WORKMODE_INFLOW_MAX) {
            mqttMgr.setWorkmode(WORKMODE_INFLOW);
          } else {
            mqttMgr.setWorkmode(WORKMODE_OUTFLOW);
          }
          maxModeStartMs = 0;
        }
      } else {
        maxModeStartMs = 0;
      }
    }
    if (state.powerOn &&
        (state.workmode == WORKMODE_RECUPERATOR || state.workmode == WORKMODE_WINTER)) {
      unsigned long now = millis();
      unsigned long phaseMs = RECUP_PHASE_MS;
      if (!recupPhaseInflow && state.workmode == WORKMODE_WINTER) {
        if (recupOutflowCount > 0 && (recupOutflowCount % 10) == 0) {
          phaseMs = RECUP_WINTER_LONG_MS;
        }
      }
      if (now - recupPhaseStartMs >= phaseMs) {
        recupPhaseInflow = !recupPhaseInflow;
        recupPhaseStartMs = now;
        if (!recupPhaseInflow && state.workmode == WORKMODE_WINTER) {
          recupOutflowCount++;
        }
        fanRequest(recupPhaseInflow, state.speed, false);
      }
    }
  }
  if (!devConsoleIsActive()) {
    fanUpdate();
  }

  irUpdate();

  // Handle web server
  if (webServer != nullptr) {
    webServer->handle();
  }

  // Handle buttons
  buttonPanelUpdate(&buttonState, &buttonCallbacks, &displayCtrl);

  // ─────────────────────────────────────────────────────────────
  // AP Mode timeout (10 минут без подключения клиентов)
  // ─────────────────────────────────────────────────────────────
  if (currentMode == MODE_AP) {
    // Проверяем количество подключенных клиентов
    int numClients = WiFi.softAPgetStationNum();

    if (numClients > 0) {
      // Есть подключения - сбрасываем таймер
      apModeStartTime = millis();
    } else if (millis() - apModeStartTime > AP_MODE_TIMEOUT) {
      // Нет подключений и прошло 10 минут
      Serial.println("AP mode timeout - no connections for 10 minutes");

      // Останавливаем AP режим
      if (webServer != nullptr) {
        webServer->stopCaptivePortal();
      }
      WiFi.softAPdisconnect(true);

      // Если есть сохранённая конфигурация WiFi, пробуем подключиться
      if (configMgr.isConfigured()) {
        Serial.println("Trying to connect to saved WiFi...");
    wifiStartStationMode();
    if (!wifiConnectToWiFi()) {
          // Не удалось - показываем сообщение об ошибке
          Serial.println("WiFi failed, hold [>] to start AP mode");
          currentMode = MODE_ERROR;
          displayMgr.showMessage("WiFi Error", "Hold [>] setup");
        } else {
          if (configMgr.hasMQTT()) {
            connectMQTT();
          }
          displayControllerUpdateMain(&displayCtrl, currentMode, deviceOff, mqttMgr.getState());
        }
      } else {
        // Нет сохранённой конфигурации - показываем экран "No Config"
        Serial.println("No saved config, hold [>] to start AP mode");
        currentMode = MODE_INIT;
        displayMgr.showNoConfig();
      }
    }
  }

  // WiFi status update and reconnection
  if (currentMode == MODE_CONNECTED || currentMode == MODE_CONNECTING) {
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    displayMgr.setWifiConnected(wifiConnected);

    if (!wifiConnected) {
      displayMgr.setMqttConnected(false);

      if (millis() - lastWifiAttempt > WIFI_RECONNECT_INTERVAL) {
        Serial.println("WiFi disconnected, reconnecting...");
        lastWifiAttempt = millis();
        wifiStartStationMode();
        wifiConnectToWiFi();
      }
    }
  }

  // MQTT handling
  if (currentMode == MODE_CONNECTED && WiFi.status() == WL_CONNECTED) {
    if (configMgr.hasMQTT()) {
      if (!mqttClient.connected()) {
        displayMgr.setMqttConnected(false);

        if (millis() - lastMqttAttempt > MQTT_RECONNECT_INTERVAL) {
          lastMqttAttempt = millis();
          connectMQTT();
        }
      } else {
        displayMgr.setMqttConnected(true);  // Keep MQTT status updated
        mqttClient.loop();
      }
    }
  }

  // Periodic display update
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = millis();
    if (currentMode == MODE_CONNECTED && !deviceOff) {
      displayControllerUpdateMain(&displayCtrl, currentMode, deviceOff, mqttMgr.getState());
    }
  }

  // ─────────────────────────────────────────────────────────────
  // Backlight timeout (10 минут бездействия)
  // ─────────────────────────────────────────────────────────────
  displayControllerTick(&displayCtrl);

  // Debounced state saving
  if (stateChangeNeedsSaving && (millis() - lastStateChangeTime > STATE_SAVE_DEBOUNCE_MS)) {
    stateChangeNeedsSaving = false;
    
    VakioState& currentState = mqttMgr.getState();
    uint8_t currentBrightness = displayControllerGetBrightness(&displayCtrl);
    
    configMgr.saveState((uint8_t)currentState.workmode, currentState.speed, currentBrightness);
  }

  // Обновление состояния светодиодов кнопок
  updateButtonLeds();

  // Small delay to prevent CPU hogging, but allow fast stepper updates
  delay(1);
}
