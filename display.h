#ifndef DISPLAY_H
#define DISPLAY_H

#include <U8g2lib.h>

// ============================================================================
// QRCode - using ESP_QRcode low-level functions
// ============================================================================
// #define USE_QRCODE 1

#ifdef USE_QRCODE
    // Include low-level QR encoder from ESP_QRcode library
    extern "C" {
      #include "qrencode.h"
    }
    extern unsigned char strinbuf[];
    extern unsigned char qrframe[];
    extern unsigned char WD, WDB;
#endif

// ============================================================================
// DISPLAY CONFIGURATION
// ============================================================================

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define STATUS_BAR_HEIGHT 12

// Icon positions (from lopaka.app)
#define ICON_WIFI_X     100
#define ICON_WIFI_Y     5
#define ICON_WIFI_W     25
#define ICON_WIFI_H     5

#define ICON_MQTT_X     49
#define ICON_MQTT_Y     4
#define ICON_MQTT_W     40
#define ICON_MQTT_H     7

#define ICON_AP_X       3
#define ICON_AP_Y       5
#define ICON_AP_W       34
#define ICON_AP_H       5

// ============================================================================
// CUSTOM XBM ICONS (created with lopaka.app)
// ============================================================================

// WiFi connected icon (25x5)
static const unsigned char icon_wifi_on[] = {
  0x3e, 0xa2, 0x70, 0x01,
  0x41, 0x22, 0x10, 0x00,
  0x1c, 0xaa, 0x76, 0x01,
  0x22, 0xaa, 0x10, 0x01,
  0x08, 0x94, 0x10, 0x01
};

// WiFi disconnected icon (25x5) - outline/faded
static const unsigned char icon_wifi_off[] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// MQTT connected icon (34x7)
static const unsigned char icon_mqtt_on[] = {
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x93, 0xc8, 0xee, 0x00, 0x00,
  0x84, 0x2d, 0x45, 0x00, 0x00,
  0x89, 0x2a, 0x45, 0x00, 0x00,
  0x92, 0xaa, 0x45, 0x00, 0x00,
  0x94, 0xca, 0x45, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00
};

// MQTT disconnected icon (34x7)
static const unsigned char icon_mqtt_off[] = {
  0x40, 0x00, 0x00, 0x00, 0x00,
  0x26, 0x91, 0xdd, 0x61, 0xee,
  0x10, 0x5b, 0x8a, 0x90, 0x22,
  0x0a, 0x55, 0x8a, 0x90, 0x66,
  0x24, 0x55, 0x8b, 0x90, 0x22,
  0x2a, 0x95, 0x8b, 0x60, 0x22,
  0x01, 0x00, 0x00, 0x00, 0x00
};

// AP mode icon (40x5)
static const unsigned char icon_ap[] = {
  0x04, 0x19, 0x22, 0x20, 0x00,
  0x8a, 0x2a, 0x36, 0x31, 0x01,
  0x95, 0x2a, 0xaa, 0xaa, 0x02,
  0x8a, 0x1b, 0xaa, 0xaa, 0x01,
  0x84, 0x0a, 0x22, 0x31, 0x03
};


// ============================================================================
// STATUS FLAGS
// ============================================================================

struct DisplayStatus {
  bool wifiConnected;
  bool mqttConnected;
  bool apMode;
};

// ============================================================================
// DISPLAY MANAGER CLASS
// ============================================================================

class DisplayManager {
private:
  U8G2_SH1106_128X64_NONAME_F_HW_I2C* display;
  DisplayStatus status;

public:
  DisplayManager(U8G2_SH1106_128X64_NONAME_F_HW_I2C* disp) : display(disp) {
    status.wifiConnected = false;
    status.mqttConnected = false;
    status.apMode = false;
  }

  void begin() {
    display->begin();
    display->setFont(u8g2_font_6x10_tf);
  }

  // Update status
  void setWifiConnected(bool connected) { status.wifiConnected = connected; }
  void setMqttConnected(bool connected) { status.mqttConnected = connected; }
  void setApMode(bool active) { status.apMode = active; }

  // Draw status bar at top with XBM icons
  void drawStatusBar() {
    // Set bitmap mode for transparency
    display->setFontMode(1);
    display->setBitmapMode(1);

    // Draw separator line
    display->drawHLine(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH);

    // Draw WiFi icon
    if (status.wifiConnected) {
      display->drawXBM(ICON_WIFI_X, ICON_WIFI_Y, ICON_WIFI_W, ICON_WIFI_H, icon_wifi_on);
    } else if (!status.apMode) {
      // Show disconnected icon only when not in AP mode
      display->drawXBM(ICON_WIFI_X, ICON_WIFI_Y, ICON_WIFI_W, ICON_WIFI_H, icon_wifi_off);
    }

    // Draw MQTT icon
    if (status.mqttConnected) {
      display->drawXBM(ICON_MQTT_X, ICON_MQTT_Y, ICON_MQTT_W, ICON_MQTT_H, icon_mqtt_on);
    } else if (status.wifiConnected) {
      display->drawXBM(ICON_MQTT_X, ICON_MQTT_Y, ICON_MQTT_W, ICON_MQTT_H, icon_mqtt_off);
    }

    // Draw AP mode icon at the left side of status bar
    if (status.apMode) {
      display->drawXBM(ICON_AP_X, ICON_AP_Y, ICON_AP_W, ICON_AP_H, icon_ap);
    }
  }

  // Show AP mode screen with QR code
  void showAPScreen(const char* ssid, const char* password, const char* ip) {
    display->clearBuffer();
    drawStatusBar();

#ifdef USE_QRCODE
    // Generate WiFi QR code: WIFI:T:WPA;S:ssid;P:password;;
    String qrData = "WIFI:T:WPA;S:";
    qrData += ssid;
    qrData += ";P:";
    qrData += password;
    qrData += ";;";

    // Copy data to ESP_QRcode input buffer and generate
    strcpy((char*)strinbuf, qrData.c_str());
    qrencode();

    // QR code size (WD is set by qrencode)
    int qrSize = WD;

    // Position QR code on the right, vertically centered below status bar
    int qrX = SCREEN_WIDTH - qrSize - 4;
    int qrY = STATUS_BAR_HEIGHT + 3;

    // Draw QR code using QRBIT macro
    for (int y = 0; y < qrSize; y++) {
      for (int x = 0; x < qrSize; x++) {
        if (QRBIT(x, y)) {
          display->drawPixel(qrX + x, qrY + y);
        }
      }
    }

    // Text on the left side
    display->setFont(u8g2_font_5x7_tf);

    int textX = 2;
    int textY = STATUS_BAR_HEIGHT + 10;

    display->drawStr(textX, textY, "Scan QR or");
    textY += 9;
    display->drawStr(textX, textY, "connect to:");

    textY += 10;
    String ssidStr = String(ssid);
    if (ssidStr.length() > 10) {
      ssidStr = ssidStr.substring(0, 9) + "~";
    }
    display->drawStr(textX, textY, ssidStr.c_str());

    textY += 9;
    display->drawStr(textX, textY, password);

    textY += 10;
    display->drawStr(textX, textY, ip);

#else
    // No QR code - text only
    display->setFont(u8g2_font_6x10_tf);
    display->drawStr(4, STATUS_BAR_HEIGHT + 12, "WiFi Setup");

    display->setFont(u8g2_font_5x7_tf);
    display->drawStr(4, STATUS_BAR_HEIGHT + 26, "SSID:");
    display->drawStr(34, STATUS_BAR_HEIGHT + 26, ssid);

    display->drawStr(4, STATUS_BAR_HEIGHT + 38, "Pass:");
    display->drawStr(34, STATUS_BAR_HEIGHT + 38, password);

    display->drawStr(4, STATUS_BAR_HEIGHT + 50, "http://");
    display->drawStr(44, STATUS_BAR_HEIGHT + 50, ip);
#endif

    display->sendBuffer();
  }

  // Show connecting screen
  void showConnecting(const char* ssid) {
    display->clearBuffer();
    drawStatusBar();

    display->setFont(u8g2_font_6x10_tf);
    display->drawStr(10, 30, "Connecting to:");

    display->setFont(u8g2_font_5x7_tf);
    String ssidStr = String(ssid);
    if (ssidStr.length() > 20) {
      ssidStr = ssidStr.substring(0, 19) + "~";
    }
    int x = (SCREEN_WIDTH - ssidStr.length() * 5) / 2;
    display->drawStr(x, 42, ssidStr.c_str());

    // Animated dots
    static int dots = 0;
    dots = (dots + 1) % 4;
    String dotsStr = "";
    for (int i = 0; i < dots; i++) dotsStr += ".";
    display->drawStr(60, 54, dotsStr.c_str());

    display->sendBuffer();
  }

  // Show main screen with status
  void showMainScreen(const char* line1, const char* line2) {
    display->clearBuffer();
    drawStatusBar();

    display->setFont(u8g2_font_6x10_tf);

    if (line1) {
      int x1 = (SCREEN_WIDTH - strlen(line1) * 6) / 2;
      display->drawStr(x1, 32, line1);
    }

    if (line2) {
      display->setFont(u8g2_font_5x7_tf);
      int x2 = (SCREEN_WIDTH - strlen(line2) * 5) / 2;
      display->drawStr(x2, 46, line2);
    }

    display->sendBuffer();
  }

  // Show message
  void showMessage(const char* title, const char* msg) {
    display->clearBuffer();
    drawStatusBar();

    display->setFont(u8g2_font_6x10_tf);
    int x1 = (SCREEN_WIDTH - strlen(title) * 6) / 2;
    display->drawStr(x1, 30, title);

    display->setFont(u8g2_font_5x7_tf);
    int x2 = (SCREEN_WIDTH - strlen(msg) * 5) / 2;
    display->drawStr(x2, 45, msg);

    display->sendBuffer();
  }

  // Show factory reset confirmation
  void showFactoryReset() {
    display->clearBuffer();

    display->setFont(u8g2_font_6x10_tf);
    display->drawStr(15, 25, "FACTORY RESET");

    display->setFont(u8g2_font_5x7_tf);
    display->drawStr(20, 40, "Resetting...");

    display->sendBuffer();
  }

  // Show "No Config" screen with hint
  void showNoConfig() {
    display->clearBuffer();
    drawStatusBar();

    display->setFont(u8g2_font_6x10_tf);
    int x1 = (SCREEN_WIDTH - 5 * 6) / 2;
    display->drawStr(x1, 28, "VAKIO");

    display->setFont(u8g2_font_5x7_tf);
    display->drawStr(10, 42, "No WiFi configured");
    display->drawStr(10, 54, "Hold [>] for setup");

    display->sendBuffer();
  }

  // Clear display
  void clear() {
    display->clearBuffer();
    display->sendBuffer();
  }

  // Direct access to display for custom drawing
  U8G2_SH1106_128X64_NONAME_F_HW_I2C* getDisplay() {
    return display;
  }
};

#endif // DISPLAY_H
