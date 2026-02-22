#ifndef MQTT_H
#define MQTT_H

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024
#endif

#include <PubSubClient.h>
#include <WiFi.h>

// ============================================================================
// VAKIO MQTT API - Compatible with official VAKIO API
// https://github.com/vakio-ru/vakio-public-api
// ============================================================================

// Work modes
enum VakioWorkMode {
  WORKMODE_OFF = 0,
  WORKMODE_INFLOW,
  WORKMODE_INFLOW_MAX,
  WORKMODE_OUTFLOW,
  WORKMODE_OUTFLOW_MAX,
  WORKMODE_RECUPERATOR,  // Summer
  WORKMODE_WINTER,       // Winter recuperation
  WORKMODE_NIGHT
};

// Device state
struct VakioState {
  bool powerOn;
  VakioWorkMode workmode;
  uint8_t speed;  // 1-7

  VakioState() : powerOn(false), workmode(WORKMODE_OFF), speed(1) {}
};

// ============================================================================
// MQTT MANAGER CLASS
// ============================================================================

class MqttManager {
private:
  PubSubClient* client;
  String topicPrefix;
  VakioState state;
  VakioState lastPublishedState;
  uint8_t lastUserSpeed = 1;
  unsigned long lastPublishTime = 0;
  static const unsigned long PUBLISH_INTERVAL_MS = 1000;

  // Callbacks
  void (*onStateChange)(VakioState& state);

  // Topic names
  String topicSystem;
  String topicMode; // VAKIO API original topic
  
  // HA-style separate command and state topics
  String topicStateSet;
  String topicStateState;
  String topicWorkmodeSet;
  String topicWorkmodeState;
  String topicSpeedSet;
  String topicSpeedState;

  // Get MAC address for registration
  String getMacAddress() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
  }

  String getMacId() {
    String mac = getMacAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    return mac + "_test";
  }

  // Convert workmode to string
  const char* workmodeToString(VakioWorkMode mode) {
    switch (mode) {
      case WORKMODE_INFLOW:      return "inflow";
      case WORKMODE_INFLOW_MAX:  return "inflow_max";
      case WORKMODE_OUTFLOW:     return "outflow";
      case WORKMODE_OUTFLOW_MAX: return "outflow_max";
      case WORKMODE_RECUPERATOR: return "recuperator";
      case WORKMODE_WINTER:      return "winter";
      case WORKMODE_NIGHT:       return "night";
      default:                   return "off";
    }
  }

  // Parse workmode from string
  VakioWorkMode parseWorkmode(const String& str) {
    if (str == "inflow")      return WORKMODE_INFLOW;
    if (str == "inflow_max")  return WORKMODE_INFLOW_MAX;
    if (str == "outflow")     return WORKMODE_OUTFLOW;
    if (str == "outflow_max") return WORKMODE_OUTFLOW_MAX;
    if (str == "recuperator") return WORKMODE_RECUPERATOR;
    if (str == "winter")      return WORKMODE_WINTER;
    if (str == "night")       return WORKMODE_NIGHT;
    return WORKMODE_OFF;
  }

  // Handle VAKIO /mode topic
  void handleModeCommand(const String& payload) {
    Serial.println("Mode command: " + payload);

    if (payload.startsWith("0600")) {
      setPower(payload.charAt(4) == '1');
    }
    else if (payload.startsWith("0601")) {
      setWorkmode((payload.charAt(4) == '0') ? WORKMODE_RECUPERATOR : WORKMODE_WINTER);
    }
    else if (payload.startsWith("0602")) {
      setWorkmode((payload.charAt(4) == '1') ? WORKMODE_INFLOW : WORKMODE_INFLOW_MAX);
    }
    else if (payload.startsWith("0603")) {
      setWorkmode((payload.charAt(4) == '1') ? WORKMODE_OUTFLOW : WORKMODE_OUTFLOW_MAX);
    }
    else if (payload.startsWith("0604")) {
      setWorkmode(WORKMODE_NIGHT);
    }
    else if (payload.startsWith("0650")) {
      int speed = payload.charAt(4) - '0';
      setSpeed(speed);
    }
  }

  // Handle HA .../state/set topic
  void handleStateCommand(const String& payload) {
    Serial.println("State command: " + payload);
    String cmd = payload;
    cmd.toLowerCase();
    cmd.trim();
    setPower(cmd == "on");
  }

  // Handle HA .../workmode/set topic
  void handleWorkmodeCommand(const String& payload) {
    Serial.println("Workmode command: " + payload);
    String cmd = payload;
    cmd.toLowerCase();
    cmd.trim();
    setWorkmode(parseWorkmode(cmd));
  }

  // Handle HA .../speed/set topic
  void handleSpeedCommand(const String& payload) {
    Serial.println("Speed command: " + payload);
    int speed = payload.toInt();
    setSpeed(speed);
  }

  // Notify callback
  void notifyStateChange() {
    if (onStateChange) {
      onStateChange(state);
    }
  }

  bool stateChangedSinceLastPublish() {
    return state.powerOn != lastPublishedState.powerOn ||
           state.workmode != lastPublishedState.workmode ||
           state.speed != lastPublishedState.speed;
  }

public:
  MqttManager(PubSubClient* mqttClient) : client(mqttClient), onStateChange(nullptr) {
    topicPrefix = "vakio";  // Default
  }

  void setTopicPrefix(const char* prefix) {
    topicPrefix = String(prefix);
    topicSystem = topicPrefix + "/system";
    topicMode = topicPrefix + "/mode";

    // HA topics
    topicStateState = topicPrefix + "/state";
    topicStateSet = topicPrefix + "/state/set";
    topicWorkmodeState = topicPrefix + "/workmode";
    topicWorkmodeSet = topicPrefix + "/workmode/set";
    topicSpeedState = topicPrefix + "/speed";
    topicSpeedSet = topicPrefix + "/speed/set";
  }

  void setStateChangeCallback(void (*callback)(VakioState&)) {
    onStateChange = callback;
  }

  VakioState& getState() {
    return state;
  }

  void subscribe() {
    // client->subscribe(topicMode.c_str()); // Temporarily disabled to test loop hypothesis
    client->subscribe(topicSystem.c_str());
    client->subscribe(topicStateSet.c_str());
    client->subscribe(topicWorkmodeSet.c_str());
    client->subscribe(topicSpeedSet.c_str());

    Serial.println("Subscribed to VAKIO topics:");
    // Serial.println("  " + topicMode);
    Serial.println("  " + topicSystem);
    Serial.println("  " + topicStateSet);
    Serial.println("  " + topicWorkmodeSet);
    Serial.println("  " + topicSpeedSet);
  }

  void handleMessage(const char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String message;
    for (unsigned int i = 0; i < length; i++) {
      message += (char)payload[i];
    }

    Serial.print("MQTT [");
    Serial.print(topicStr);
    Serial.print("]: ");
    Serial.println(message);

    // Route to appropriate handler
    if (topicStr == topicMode) {
      handleModeCommand(message);
    } else if (topicStr == topicStateSet) {
      handleStateCommand(message);
    } else if (topicStr == topicWorkmodeSet) {
      handleWorkmodeCommand(message);
    } else if (topicStr == topicSpeedSet) {
      handleSpeedCommand(message);
    } else if (topicStr == topicSystem) {
      handleSystemCommand(message);
    }
  }

  // Handle +/system commands
  void handleSystemCommand(const String& payload) {
    Serial.println("System command: " + payload);

    // 0687 - Request registration repeat
    if (payload == "0687") {
      publishRegistration();
      client->publish(topicSystem.c_str(), "0685");
    }
    // 0608 - Factory reset
    else if (payload == "0608") {
      Serial.println("Factory reset requested via MQTT");
      // Will be handled by main code
    }
    // 0609 - Firmware update (not implemented)
    else if (payload == "0609") {
      Serial.println("Firmware update requested (not implemented)");
    }
  }

  void publishDiscovery() {
    String macId = getMacId();
    String nodeId = "vakio_" + macId;
    String deviceName = "VAKIO " + macId.substring(macId.length() - 4);
    String deviceJson = "\"dev\":{\"ids\":[\"" + macId + "\"],\"name\":\"" + deviceName +
                        "\",\"mdl\":\"VK-06N03\",\"mf\":\"VAKIO\",\"sw\":\"1.0.0\"}";

    // Power switch
    String powerTopic = "homeassistant/switch/" + nodeId + "/power/config";
    String powerPayload = "{"
                          "\"name\":\"Power\","
                          "\"uniq_id\":\"" + nodeId + "_power\","
                          "\"cmd_t\":\"" + topicStateSet + "\","
                          "\"stat_t\":\"" + topicStateState + "\","
                          "\"pl_on\":\"on\","
                          "\"pl_off\":\"off\","
                          + deviceJson +
                          "}";
    bool powerOk = client->publish(powerTopic.c_str(), powerPayload.c_str(), true);
    // Serial.println("HA discovery power: " + powerTopic + " => " + String(powerOk ? "OK" : "FAIL"));
    // Serial.println("  len=" + String(powerPayload.length()) + " connected=" + String(client->connected() ? "yes" : "no"));

    // Workmode select
    String modeTopic = "homeassistant/select/" + nodeId + "/mode/config";
    String modePayload = "{"
                         "\"name\":\"Mode\","
                         "\"uniq_id\":\"" + nodeId + "_mode\","
                         "\"cmd_t\":\"" + topicWorkmodeSet + "\","
                         "\"stat_t\":\"" + topicWorkmodeState + "\","
                         "\"options\":[\"off\",\"inflow\",\"inflow_max\",\"outflow\",\"outflow_max\",\"recuperator\",\"winter\",\"night\"],"
                         + deviceJson +
                         "}";
    bool modeOk = client->publish(modeTopic.c_str(), modePayload.c_str(), true);
    // Serial.println("HA discovery mode: " + modeTopic + " => " + String(modeOk ? "OK" : "FAIL"));
    // Serial.println("  len=" + String(modePayload.length()) + " connected=" + String(client->connected() ? "yes" : "no"));

    // Speed number
    String speedTopic = "homeassistant/number/" + nodeId + "/speed/config";
    String speedPayload = "{"
                          "\"name\":\"Speed\","
                          "\"uniq_id\":\"" + nodeId + "_speed\","
                          "\"cmd_t\":\"" + topicSpeedSet + "\","
                          "\"stat_t\":\"" + topicSpeedState + "\","
                          "\"min\":1,"
                          "\"max\":7,"
                          "\"step\":1,"
                          "\"mode\":\"slider\","
                          + deviceJson +
                          "}";
    bool speedOk = client->publish(speedTopic.c_str(), speedPayload.c_str(), true);
    // Serial.println("HA discovery speed: " + speedTopic + " => " + String(speedOk ? "OK" : "FAIL"));
    // Serial.println("  len=" + String(speedPayload.length()) + " connected=" + String(client->connected() ? "yes" : "no"));
  }

  // Publish device registration (called on connect)
  void publishRegistration() {
    // Format: 0601{series:esp32,subtype:"subtype","xtal_freq":"xtal_freq"}
    String reg1 = "0601{series:esp32,subtype:\"custom\",xtal_freq:\"40\"}";
    client->publish(topicSystem.c_str(), reg1.c_str());

    // Format: 060006versionmacaddress
    String reg2 = "0600061.0.0" + getMacAddress();
    client->publish(topicSystem.c_str(), reg2.c_str());

    Serial.println("Published registration to " + topicSystem);
  }

  // Publish all state to all topics
  void publishAllState() {
    unsigned long now = millis();
    bool changed = stateChangedSinceLastPublish();
    if (!changed && (now - lastPublishTime < PUBLISH_INTERVAL_MS)) {
      return;
    }

    // +/state: on/off
    client->publish(topicStateState.c_str(), state.powerOn ? "on" : "off", true);

    // +/workmode: inflow, outflow, etc.
    client->publish(topicWorkmodeState.c_str(), workmodeToString(state.workmode), true);

    // +/speed: 1-7
    client->publish(topicSpeedState.c_str(), String(state.speed).c_str(), true);

    // +/mode: publish current mode code
    client->publish(topicMode.c_str(), getModeCode().c_str(), true);

    Serial.println("Published state: power=" + String(state.powerOn ? "on" : "off") +
                   ", mode=" + String(workmodeToString(state.workmode)) +
                   ", speed=" + String(state.speed));

    lastPublishedState = state;
    lastPublishTime = now;
  }

  // Get mode code for +/mode topic
  String getModeCode() {
    if (!state.powerOn) return "06000";

    switch (state.workmode) {
      case WORKMODE_RECUPERATOR: return "06010";
      case WORKMODE_WINTER:      return "06011";
      case WORKMODE_INFLOW:      return "06021";
      case WORKMODE_INFLOW_MAX:  return "06022";
      case WORKMODE_OUTFLOW:     return "06031";
      case WORKMODE_OUTFLOW_MAX: return "06032";
      case WORKMODE_NIGHT:       return "06041";
      default:                   return "06000";
    }
  }

  // Called when MQTT connects
  void onConnect() {
    subscribe();
    publishDiscovery();
    publishRegistration();
    publishAllState();
  }

  void restoreState(VakioWorkMode mode, uint8_t speed) {
    // This function should be called ONCE at boot.
    // It sets the initial state without publishing.

    state.workmode = mode;
    state.powerOn = (mode != WORKMODE_OFF);

    // The 'speed' from storage is our best guess for the user's last preferred speed.
    lastUserSpeed = speed; 

    // Now, determine the *actual* operating speed based on the mode.
    bool isMaxMode = (mode == WORKMODE_INFLOW_MAX || mode == WORKMODE_OUTFLOW_MAX);
    if (isMaxMode) {
        state.speed = 7;
    } else {
        state.speed = speed;
    }

    // Sync the state so we don't immediately re-publish it
    lastPublishedState = state;
    
    // Update the hardware to match this restored state
    notifyStateChange();
  }

  // Set state programmatically (e.g., from buttons)
  void setPower(bool on) {
    if (on == state.powerOn) return;

    if (on) {
      if (state.workmode == WORKMODE_OFF) {
        setWorkmode(WORKMODE_INFLOW);
      }
    } else {
        setWorkmode(WORKMODE_OFF);
    }
  }

  void setWorkmode(VakioWorkMode mode) {
    if (state.workmode == mode && (mode == WORKMODE_OFF || state.powerOn)) {
        return; // No change needed
    }

    VakioState oldState = state;

    bool isNewModeMax = (mode == WORKMODE_INFLOW_MAX || mode == WORKMODE_OUTFLOW_MAX);

    // Update speed first, based on new mode
    if (isNewModeMax) {
        state.speed = 7;
    } else if (mode != WORKMODE_OFF) {
        state.speed = lastUserSpeed;
    }

    // Now set the mode and power
    state.workmode = mode;
    state.powerOn = (mode != WORKMODE_OFF);
    
    if (state.workmode != oldState.workmode || state.powerOn != oldState.powerOn || state.speed != oldState.speed) {
        publishAllState();
        notifyStateChange();
    }
  }

  void setSpeed(uint8_t speed) {
    if (speed < 1 || speed > 7) return;

    VakioState oldState = state;

    bool isCurrentModeMax = (state.workmode == WORKMODE_INFLOW_MAX || state.workmode == WORKMODE_OUTFLOW_MAX);

    if (isCurrentModeMax && speed < 7) {
      if (state.workmode == WORKMODE_INFLOW_MAX) {
        state.workmode = WORKMODE_INFLOW;
      } else if (state.workmode == WORKMODE_OUTFLOW_MAX) {
        state.workmode = WORKMODE_OUTFLOW;
      }
    }

    state.speed = speed;

    bool isAfterChangeModeMax = (state.workmode == WORKMODE_INFLOW_MAX || state.workmode == WORKMODE_OUTFLOW_MAX);
    if (!isAfterChangeModeMax) {
      lastUserSpeed = speed;
    }

    if (state.speed != oldState.speed || state.workmode != oldState.workmode) {
      publishAllState();
      notifyStateChange();
    }
  }
};

#endif // MQTT_H
