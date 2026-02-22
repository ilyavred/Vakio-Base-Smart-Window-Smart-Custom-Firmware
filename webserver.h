#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include "config.h"

// ============================================================================
// CAPTIVE PORTAL + HTTP SERVER
// ============================================================================

#define DNS_PORT 53

class WebServerManager {
private:
  WiFiServer server;
  DNSServer dnsServer;
  ConfigManager* configMgr;
  bool restartPending;
  bool resetPending;
  bool captivePortalActive;
  IPAddress apIP;
  
  // URL decode helper
  String urlDecode(const String& text) {
    String decoded = "";
    char temp[] = "0x00";
    unsigned int len = text.length();
    unsigned int i = 0;
    
    while (i < len) {
      char c = text.charAt(i++);
      if (c == '+') {
        decoded += ' ';
      } else if (c == '%') {
        if (i + 2 <= len) {
          temp[2] = text.charAt(i++);
          temp[3] = text.charAt(i++);
          decoded += (char)strtol(temp, NULL, 16);
        }
      } else {
        decoded += c;
      }
    }
    return decoded;
  }
  
  // Parse POST data
  String getPostValue(const String& data, const String& key) {
    String search = key + "=";
    int start = data.indexOf(search);
    if (start == -1) return "";
    
    start += search.length();
    int end = data.indexOf("&", start);
    if (end == -1) end = data.length();
    
    return urlDecode(data.substring(start, end));
  }
  
  // Generate HTML page
  String getPage(const char* statusMsg = nullptr, const char* statusType = "info") {
    Config& cfg = configMgr->getConfig();
    
    String html = F("<!DOCTYPE html><html><head>");
    html += F("<meta charset='UTF-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>VAKIO Config</title>");
    html += F("<style>");
    html += F("*{box-sizing:border-box;margin:0;padding:0}");
    html += F("body{font-family:system-ui,sans-serif;background:#1a1a2e;min-height:100vh;color:#fff;padding:16px}");
    html += F(".c{max-width:420px;margin:0 auto}");
    html += F("h1{text-align:center;margin-bottom:20px;color:#00d4ff;font-size:22px}");
    html += F(".card{background:rgba(255,255,255,0.1);border-radius:12px;padding:16px;margin-bottom:12px}");
    html += F(".card h2{font-size:14px;margin-bottom:12px;color:#00d4ff}");
    html += F("label{display:block;font-size:11px;color:#aaa;margin-bottom:4px}");
    html += F("input{width:100%;padding:10px;border:1px solid rgba(255,255,255,0.2);border-radius:8px;");
    html += F("background:rgba(0,0,0,0.3);color:#fff;font-size:16px;margin-bottom:10px}");
    html += F("input:focus{outline:none;border-color:#00d4ff}");
    html += F(".btn{width:100%;padding:12px;border:none;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer}");
    html += F(".btn-p{background:linear-gradient(135deg,#00d4ff,#0099cc);color:#000}");
    html += F(".btn-d{background:linear-gradient(135deg,#ff4757,#cc0000);color:#fff;margin-top:8px}");
    html += F(".row{display:flex;gap:8px}.row>div{flex:1}");
    html += F(".st{text-align:center;padding:10px;border-radius:8px;margin-bottom:12px;font-size:13px}");
    html += F(".st.ok{background:rgba(0,255,136,0.2);border:1px solid #0f8}");
    html += F(".st.er{background:rgba(255,71,87,0.2);border:1px solid #ff4757}");
    html += F(".st.in{background:rgba(0,212,255,0.2);border:1px solid #00d4ff}");
    html += F(".warn{font-size:11px;color:#ff4757;margin-top:6px}");
    html += F(".info{font-size:11px;color:#888;margin-top:2px}");
    html += F("</style></head><body><div class='c'>");
    html += F("<h1>VAKIO Config</h1>");
    
    // Status message
    if (statusMsg) {
      html += F("<div class='st ");
      html += statusType;
      html += F("'>");
      html += statusMsg;
      html += F("</div>");
    }
    
    // WiFi Card
    html += F("<div class='card'><h2>WiFi Settings</h2>");
    html += F("<form method='POST' action='/wifi'>");
    html += F("<label>Network Name (SSID)</label>");
    html += F("<input type='text' name='ssid' value='");
    html += cfg.wifi_ssid;
    html += F("' placeholder='WiFi name' required>");
    html += F("<label>Password</label>");
    html += F("<input type='text' name='password' value='");
    html += cfg.wifi_password;
    html += F("' placeholder='WiFi password'>");
    html += F("<button class='btn btn-p'>Save WiFi</button>");
    html += F("</form></div>");
    
    // MQTT Card
    html += F("<div class='card'><h2>MQTT Settings</h2>");
    html += F("<form method='POST' action='/mqtt'>");
    html += F("<label>Broker Address</label>");
    html += F("<input type='text' name='server' value='");
    html += cfg.mqtt_server;
    html += F("' placeholder='mqtt.example.com'>");
    html += F("<div class='row'><div><label>Port</label>");
    html += F("<input type='number' name='port' value='");
    html += String(cfg.mqtt_port);
    html += F("' placeholder='1883'></div>");
    html += F("<div><label>Client ID</label>");
    html += F("<input type='text' name='client_id' value='");
    html += cfg.mqtt_client_id;
    html += F("' placeholder='vakio'></div></div>");
    html += F("<label>Username</label>");
    html += F("<input type='text' name='user' value='");
    html += cfg.mqtt_user;
    html += F("' placeholder='Optional'>");
    html += F("<label>Password</label>");
    html += F("<input type='text' name='mqtt_pass' value='");
    html += cfg.mqtt_password;
    html += F("' placeholder='Optional'>");
    html += F("<label>Topic Prefix</label>");
    html += F("<input type='text' name='topic' value='");
    html += cfg.mqtt_topic_prefix;
    html += F("' placeholder='vakio'>");
    html += F("<p class='info'>Topics: {prefix}/state, {prefix}/mode, {prefix}/speed</p>");
    html += F("<button class='btn btn-p'>Save MQTT</button>");
    html += F("</form></div>");
    
    // System Card
    html += F("<div class='card'><h2>System</h2>");
    html += F("<form method='POST' action='/restart'>");
    html += F("<button class='btn btn-p'>Restart Device</button>");
    html += F("</form>");
    html += F("<form method='POST' action='/reset' onsubmit=\"return confirm('Erase all settings?')\">");
    html += F("<button class='btn btn-d'>Factory Reset</button>");
    html += F("<p class='warn'>All settings will be deleted</p>");
    html += F("</form></div>");
    
    html += F("</div></body></html>");
    return html;
  }
  
  // Captive portal detection pages
  String getCaptivePortalResponse() {
    // Redirect to config page
    String html = F("<!DOCTYPE html><html><head>");
    html += F("<meta http-equiv='refresh' content='0;url=http://");
    html += apIP.toString();
    html += F("/'>");
    html += F("</head><body>Redirecting to configuration...</body></html>");
    return html;
  }
  
  // Send HTTP response
  void sendResponse(WiFiClient& client, int code, const String& contentType, const String& content) {
    client.println("HTTP/1.1 " + String(code) + " OK");
    client.println("Content-Type: " + contentType);
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.println("Connection: close");
    client.println("Content-Length: " + String(content.length()));
    client.println();
    client.print(content);
  }
  
  // Send redirect
  void sendRedirect(WiFiClient& client, const String& location) {
    client.println("HTTP/1.1 302 Found");
    client.println("Location: " + location);
    client.println("Cache-Control: no-cache");
    client.println("Connection: close");
    client.println();
  }
  
  // Check if this is a captive portal detection request
  bool isCaptivePortalRequest(const String& request) {
    // Android
    if (request.indexOf("/generate_204") >= 0) return true;
    if (request.indexOf("/gen_204") >= 0) return true;
    if (request.indexOf("/connecttest.txt") >= 0) return true;
    // Apple
    if (request.indexOf("/hotspot-detect.html") >= 0) return true;
    if (request.indexOf("/library/test/success.html") >= 0) return true;
    if (request.indexOf("/captive.apple.com") >= 0) return true;
    // Windows
    if (request.indexOf("/ncsi.txt") >= 0) return true;
    if (request.indexOf("/connecttest.txt") >= 0) return true;
    if (request.indexOf("/redirect") >= 0) return true;
    // Firefox
    if (request.indexOf("/success.txt") >= 0) return true;
    // Generic
    if (request.indexOf("/fwlink") >= 0) return true;
    
    return false;
  }

public:
  WebServerManager(ConfigManager* cfg) : server(80), configMgr(cfg) {
    restartPending = false;
    resetPending = false;
    captivePortalActive = false;
    apIP = IPAddress(192, 168, 4, 1);
  }
  
  void begin() {
    server.begin();
    Serial.println("Web server started on port 80");
  }
  
  // Start captive portal (call this in AP mode)
  void startCaptivePortal() {
    apIP = WiFi.softAPIP();
    
    // Start DNS server - redirect ALL domains to our IP
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);
    
    captivePortalActive = true;
    Serial.println("Captive Portal started");
    Serial.print("DNS redirecting all requests to: ");
    Serial.println(apIP);
  }
  
  // Stop captive portal
  void stopCaptivePortal() {
    if (captivePortalActive) {
      dnsServer.stop();
      captivePortalActive = false;
      Serial.println("Captive Portal stopped");
    }
  }
  
  void handle() {
    // Process DNS requests (for captive portal)
    if (captivePortalActive) {
      dnsServer.processNextRequest();
    }
    
    WiFiClient client = server.available();
    
    if (client) {
      String request = "";
      String postData = "";
      String host = "";
      bool isPost = false;
      int contentLength = 0;
      
      // Read request
      unsigned long timeout = millis() + 3000;
      while (client.connected() && millis() < timeout) {
        if (client.available()) {
          String line = client.readStringUntil('\n');
          line.trim();
          
          if (request.length() == 0) {
            request = line;
            isPost = request.startsWith("POST");
          }
          
          if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
          }
          
          if (line.startsWith("Host:")) {
            host = line.substring(5);
            host.trim();
          }
          
          // End of headers
          if (line.length() == 0) {
            if (isPost && contentLength > 0) {
              postData = client.readString();
            }
            break;
          }
        }
      }
      
      Serial.println("Request: " + request);
      
      // Check for captive portal detection
      if (captivePortalActive && isCaptivePortalRequest(request)) {
        Serial.println("Captive portal detection - redirecting");
        sendRedirect(client, "http://" + apIP.toString() + "/");
        delay(10);
        client.stop();
        return;
      }
      
      // If host is not our IP and captive portal is active, redirect
      if (captivePortalActive && host.length() > 0) {
        String ourHost = apIP.toString();
        if (host.indexOf(ourHost) < 0 && host != "192.168.4.1") {
          Serial.println("Foreign host detected, redirecting: " + host);
          sendRedirect(client, "http://" + apIP.toString() + "/");
          delay(10);
          client.stop();
          return;
        }
      }
      
      // Route handling
      String statusMsg = "";
      String statusType = "in";
      
      if (request.indexOf("POST /wifi") >= 0) {
        String ssid = getPostValue(postData, "ssid");
        String password = getPostValue(postData, "password");
        
        if (ssid.length() > 0) {
          configMgr->setWiFi(ssid.c_str(), password.c_str());
          statusMsg = "WiFi saved. Restarting...";
          statusType = "ok";
          restartPending = true;
        } else {
          statusMsg = "SSID is required";
          statusType = "er";
        }
      }
      else if (request.indexOf("POST /mqtt") >= 0) {
        String srv = getPostValue(postData, "server");
        uint16_t port = getPostValue(postData, "port").toInt();
        String user = getPostValue(postData, "user");
        String pass = getPostValue(postData, "mqtt_pass");
        String clientId = getPostValue(postData, "client_id");
        String topic = getPostValue(postData, "topic");
        
        if (port == 0) port = 1883;
        if (clientId.length() == 0) clientId = "vakio";
        if (topic.length() == 0) topic = "vakio";
        
        configMgr->setMQTT(srv.c_str(), port, user.c_str(), pass.c_str(), clientId.c_str(), topic.c_str());
        statusMsg = "MQTT settings saved";
        statusType = "ok";
      }
      else if (request.indexOf("POST /restart") >= 0) {
        statusMsg = "Restarting...";
        statusType = "in";
        restartPending = true;
      }
      else if (request.indexOf("POST /reset") >= 0) {
        statusMsg = "Factory reset...";
        statusType = "in";
        resetPending = true;
      }
      
      // Send response
      String page = getPage(statusMsg.length() > 0 ? statusMsg.c_str() : nullptr, statusType.c_str());
      sendResponse(client, 200, "text/html", page);
      
      delay(10);
      client.stop();
    }
    
    // Handle pending actions
    if (restartPending) {
      delay(1000);
      ESP.restart();
    }
    
    if (resetPending) {
      delay(500);
      configMgr->factoryReset();
      delay(500);
      ESP.restart();
    }
  }
  
  void stop() {
    stopCaptivePortal();
    server.stop();
  }
  
  bool isRestartPending() { return restartPending; }
  bool isResetPending() { return resetPending; }
  bool isCaptivePortalActive() { return captivePortalActive; }
};

#endif // WEBSERVER_H
