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

static void wifiStartStationMode() {
  Serial.println("Starting Station mode...");

  if (webServer != nullptr) {
    webServer->stopCaptivePortal();
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(configMgr.getConfig().wifi_ssid, configMgr.getConfig().wifi_password);
  currentMode = MODE_CONNECTING;
  lastWifiAttempt = millis();
  displayMgr.setApMode(false);
  displayMgr.setWifiConnected(false);
  displayMgr.setMqttConnected(false);
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

static bool wifiFinishStationConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  bool becameConnected = (currentMode != MODE_CONNECTED);
  if (becameConnected) {
    Serial.println();
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  }

  currentMode = MODE_CONNECTED;
  displayMgr.setWifiConnected(true);
  displayMgr.setApMode(false);

  if (becameConnected || webServer == nullptr) {
    if (webServer == nullptr) {
      webServer = new WebServerManager(&configMgr);
    }
    webServer->begin();
    displayControllerWake(&displayCtrl);
  }

  return true;
}

static bool wifiMaintainStationConnection() {
  if (!configMgr.isConfigured()) {
    return false;
  }

  if (wifiFinishStationConnection()) {
    return true;
  }

  displayMgr.setWifiConnected(false);
  displayMgr.setMqttConnected(false);

  if (currentMode == MODE_CONNECTED) {
    Serial.println("WiFi connection lost");
    currentMode = MODE_CONNECTING;
  }

  if (currentMode != MODE_CONNECTING && currentMode != MODE_ERROR) {
    return false;
  }

  unsigned long now = millis();
  if (lastWifiAttempt == 0 || now - lastWifiAttempt > WIFI_RECONNECT_INTERVAL) {
    Serial.println("WiFi not connected, retrying...");
    wifiStartStationMode();
  }

  return false;
}

#endif // WIFI_MANAGER_H
