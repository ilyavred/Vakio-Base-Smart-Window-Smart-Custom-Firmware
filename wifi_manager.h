#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include "webserver.h"
#include "app_types.h"
#include "display_controller.h"

extern ConfigManager configMgr;
extern DisplayManager displayMgr;
extern WebServerManager* webServer;
extern DeviceMode currentMode;
extern unsigned long apModeStartTime;
extern unsigned long lastWifiAttempt;
extern DisplayController displayCtrl;
void updateButtonLeds();

static void wifiStartStationMode() {
  Serial.println("Starting Station mode...");

  if (webServer != nullptr) {
    webServer->stopCaptivePortal();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(configMgr.getConfig().wifi_ssid, configMgr.getConfig().wifi_password);
  currentMode = MODE_CONNECTING;
  lastWifiAttempt = millis();
  displayMgr.setApMode(false);
  displayMgr.showConnecting(configMgr.getConfig().wifi_ssid);
  displayControllerWake(&displayCtrl);
}

static void wifiStartAPMode() {
  Serial.println("Starting AP mode...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(configMgr.getAPSSID(), configMgr.getAPPassword(), AP_CHANNEL, 0, AP_MAX_CONNECTIONS);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(ip);

  currentMode = MODE_AP;
  apModeStartTime = millis();
  displayMgr.setApMode(true);
  displayMgr.setWifiConnected(false);
  displayMgr.setMqttConnected(false);

  if (webServer == nullptr) {
    webServer = new WebServerManager(&configMgr);
  }
  webServer->begin();
  webServer->startCaptivePortal();

  displayMgr.showAPScreen(configMgr.getAPSSID(), configMgr.getAPPassword(), ip.toString().c_str());
  displayControllerWake(&displayCtrl);
}

static bool wifiConnectToWiFi() {
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_CONNECT_TIMEOUT) {
      Serial.println("WiFi connection timeout");
      return false;
    }
    updateButtonLeds();
    delay(100);
    Serial.print(".");
    displayMgr.showConnecting(configMgr.getConfig().wifi_ssid);
  }

  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  currentMode = MODE_CONNECTED;
  displayMgr.setWifiConnected(true);
  displayMgr.setApMode(false);

  if (webServer == nullptr) {
    webServer = new WebServerManager(&configMgr);
  }
  webServer->begin();

  displayControllerWake(&displayCtrl);
  return true;
}

#endif // WIFI_MANAGER_H
