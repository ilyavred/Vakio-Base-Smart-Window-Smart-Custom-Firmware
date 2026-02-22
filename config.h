#ifndef CONFIG_H
#define CONFIG_H

#include <Preferences.h>

// ============================================================================
// CONFIGURATION STRUCTURE
// ============================================================================

struct Config {
  // WiFi Settings
  char wifi_ssid[64];
  char wifi_password[64];
  
  // MQTT Settings
  char mqtt_server[128];
  uint16_t mqtt_port;
  char mqtt_user[64];
  char mqtt_password[64];
  char mqtt_client_id[32];
  char mqtt_topic_prefix[64];
  
  // Device Settings
  bool configured;

  // Saved State
  uint8_t last_workmode;
  uint8_t last_speed;
  uint8_t display_brightness;
};

// ============================================================================
// DEFAULT VALUES
// ============================================================================

#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_CLIENT_ID  "vakio_device"
#define DEFAULT_MQTT_TOPIC      "vakio"

// AP Mode Settings
#define AP_SSID_PREFIX          "VAKIO-"
#define AP_PASSWORD             "12345678"
#define AP_CHANNEL              1
#define AP_MAX_CONNECTIONS      4

// ============================================================================
// CONFIG MANAGER CLASS
// ============================================================================

class ConfigManager {
private:
  Preferences preferences;
  Config config;
  String apSSID;
  
public:
  ConfigManager() {
    // Generate unique AP SSID based on chip ID
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i += 8) {
      chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    apSSID = String(AP_SSID_PREFIX) + String(chipId, HEX);
    apSSID.toUpperCase();
  }
  
  void begin() {
    preferences.begin("vakio", false);
    load();
  }
  
  void load() {
    // WiFi
    strlcpy(config.wifi_ssid, 
            preferences.getString("wifi_ssid", "").c_str(), 
            sizeof(config.wifi_ssid));
    strlcpy(config.wifi_password, 
            preferences.getString("wifi_pass", "").c_str(), 
            sizeof(config.wifi_password));
    
    // MQTT
    strlcpy(config.mqtt_server, 
            preferences.getString("mqtt_srv", "").c_str(), 
            sizeof(config.mqtt_server));
    config.mqtt_port = preferences.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
    strlcpy(config.mqtt_user, 
            preferences.getString("mqtt_user", "").c_str(), 
            sizeof(config.mqtt_user));
    strlcpy(config.mqtt_password, 
            preferences.getString("mqtt_pass", "").c_str(), 
            sizeof(config.mqtt_password));
    strlcpy(config.mqtt_client_id, 
            preferences.getString("mqtt_id", DEFAULT_MQTT_CLIENT_ID).c_str(), 
            sizeof(config.mqtt_client_id));
    strlcpy(config.mqtt_topic_prefix, 
            preferences.getString("mqtt_topic", DEFAULT_MQTT_TOPIC).c_str(), 
            sizeof(config.mqtt_topic_prefix));
    
    // Status
    config.configured = preferences.getBool("configured", false);

    // Saved State
    config.last_workmode = preferences.getUChar("last_mode", 0); // Default WORKMODE_OFF
    config.last_speed = preferences.getUChar("last_speed", 1);
    config.display_brightness = preferences.getUChar("disp_bright", 255); // Default MAX
  }
  
  void save() {
    preferences.putString("wifi_ssid", config.wifi_ssid);
    preferences.putString("wifi_pass", config.wifi_password);
    preferences.putString("mqtt_srv", config.mqtt_server);
    preferences.putUShort("mqtt_port", config.mqtt_port);
    preferences.putString("mqtt_user", config.mqtt_user);
    preferences.putString("mqtt_pass", config.mqtt_password);
    preferences.putString("mqtt_id", config.mqtt_client_id);
    preferences.putString("mqtt_topic", config.mqtt_topic_prefix);
    preferences.putBool("configured", config.configured);
  }

  void saveState(uint8_t workmode, uint8_t speed, uint8_t brightness) {
    preferences.putUChar("last_mode", workmode);
    preferences.putUChar("last_speed", speed);
    preferences.putUChar("disp_bright", brightness);
    Serial.println("Device state saved to flash.");
  }
  
  void factoryReset() {
    preferences.clear();
    memset(&config, 0, sizeof(config));
    config.mqtt_port = DEFAULT_MQTT_PORT;
    strlcpy(config.mqtt_client_id, DEFAULT_MQTT_CLIENT_ID, sizeof(config.mqtt_client_id));
    strlcpy(config.mqtt_topic_prefix, DEFAULT_MQTT_TOPIC, sizeof(config.mqtt_topic_prefix));
    config.configured = false;
    save();
  }
  
  // Getters
  Config& getConfig() { return config; }
  const char* getAPSSID() { return apSSID.c_str(); }
  const char* getAPPassword() { return AP_PASSWORD; }
  
  bool isConfigured() { return config.configured && strlen(config.wifi_ssid) > 0; }
  bool hasMQTT() { return strlen(config.mqtt_server) > 0; }
  
  // Setters
  void setWiFi(const char* ssid, const char* password) {
    strlcpy(config.wifi_ssid, ssid, sizeof(config.wifi_ssid));
    strlcpy(config.wifi_password, password, sizeof(config.wifi_password));
    config.configured = true;
    save();
  }
  
  void setMQTT(const char* server, uint16_t port, const char* user, 
               const char* password, const char* clientId, const char* topic) {
    strlcpy(config.mqtt_server, server, sizeof(config.mqtt_server));
    config.mqtt_port = port;
    strlcpy(config.mqtt_user, user, sizeof(config.mqtt_user));
    strlcpy(config.mqtt_password, password, sizeof(config.mqtt_password));
    strlcpy(config.mqtt_client_id, clientId, sizeof(config.mqtt_client_id));
    strlcpy(config.mqtt_topic_prefix, topic, sizeof(config.mqtt_topic_prefix));
    save();
  }
};

#endif // CONFIG_H
