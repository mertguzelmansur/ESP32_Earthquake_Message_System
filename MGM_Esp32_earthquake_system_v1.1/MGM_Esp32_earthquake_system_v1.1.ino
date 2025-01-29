#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <MPU6050.h>
#include <ArduinoJson.h>  // Add this for JSON parsing

// Configuration
const char* apSSID = "EarthquakeAlertSystem"; // Default SSID
const char* apPassword = ""; // Open network (no password)
const float SHAKE_THRESHOLD = 2.5; // Sensitivity for earthquake detection
const int LED_PIN = 2; // Built-in LED for indicators
const int MAX_LOCATIONS = 10; // Maximum number of stored locations
const int MAX_MESSAGES = 50;      // Maximum number of messages to store
const int MAX_HELP_REQUESTS = 20; // Maximum number of help requests to store

const byte DNS_PORT = 53;  // Add this
DNSServer dnsServer;      // Add this
IPAddress apIP(192, 168, 4, 1);  // Add this

WebServer server(80);
MPU6050 mpu;

// System State
bool earthquakeAlert = false;
String emergencyMessage = "No current emergency";
String messages[MAX_MESSAGES];
int messageCount = 0;
String helpRequests[MAX_HELP_REQUESTS];
int helpRequestCount = 0;
unsigned long lastShakeTime = 0;
unsigned long startTime = 0;

// Location tracking structure
struct UserLocation {
    String user;
    float lat;
    float lng;
    bool active;
};

// Array to store user locations
UserLocation userLocations[MAX_LOCATIONS];
int locationCount = 0;

// Admin Credentials
const String adminUsername = "admin";
const String adminPassword = "123";

// Function to add or update user location
bool updateUserLocation(const String& user, float lat, float lng) {
    // First try to update existing user
    for (int i = 0; i < MAX_LOCATIONS; i++) {
        if (userLocations[i].active && userLocations[i].user == user) {
            userLocations[i].lat = lat;
            userLocations[i].lng = lng;
            return true;
        }
    }
    
    // If user not found, add new entry
    if (locationCount < MAX_LOCATIONS) {
        for (int i = 0; i < MAX_LOCATIONS; i++) {
            if (!userLocations[i].active) {
                userLocations[i].user = user;
                userLocations[i].lat = lat;
                userLocations[i].lng = lng;
                userLocations[i].active = true;
                locationCount++;
                return true;
            }
        }
    }
    return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // Initialize Filesystem
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  // Initialize Accelerometer
  Wire.begin();
  mpu.initialize();

  // Configure Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID, apPassword);
  
  // Start DNS server
  dnsServer.start(DNS_PORT, "*", apIP);  // Redirect all DNS requests to AP IP

  // Server Routes
  server.on("/", HTTP_GET, handleRoot); // Main interface
  server.on("/send", HTTP_POST, handleSendMessage); // Send message
  server.on("/help", HTTP_POST, handleHelpRequest); // Request help
  server.on("/admin", HTTP_GET, handleAdmin); // Admin panel
  server.on("/reset", HTTP_POST, handleReset); // Reset ESP32
  server.on("/updateSSID", HTTP_POST, handleUpdateSSID); // Change SSID
  server.on("/messages", HTTP_GET, handleGetMessages); // Get all messages
  server.on("/help-requests", HTTP_GET, handleGetHelpRequests); // Get help requests
  server.on("/system-stats", HTTP_GET, handleSystemStats);
  server.on("/sensor-data", HTTP_GET, handleSensorData);
  server.on("/update-location", HTTP_POST, handleUpdateLocation);
  server.on("/clear-data", HTTP_POST, handleClearData);
  server.on("/locations", HTTP_GET, handleGetLocations);
  server.on("/generate_204", handleCaptivePortal);  // Android captive portal
  server.on("/fwlink", handleCaptivePortal);       // Microsoft captive portal
  server.onNotFound(handleCaptivePortal);          // Catch-all handler
  server.serveStatic("/", LittleFS, "/"); // Serve static files

  // Initialize user locations
  for (int i = 0; i < MAX_LOCATIONS; i++) {
    userLocations[i].active = false;
  }

  startTime = millis();

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  dnsServer.processNextRequest();  // Add this at the beginning
  detectEarthquake();
  updateLED();
  server.handleClient();
}

// --- Earthquake Detection ---
void detectEarthquake() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float acceleration = sqrt(pow(ax, 2) + pow(ay, 2) + pow(az, 2)) / 16384.0;

  if (acceleration > SHAKE_THRESHOLD && millis() - lastShakeTime > 10000) {
    earthquakeAlert = true;
    emergencyMessage = "Earthquake detected! Seek shelter!";
    lastShakeTime = millis();
    
    // Add system message with overflow protection
    if (messageCount < MAX_MESSAGES) {
      messages[messageCount++] = "System: " + emergencyMessage;
    } else {
      // Shift messages to make room for new one
      for (int i = 0; i < MAX_MESSAGES - 1; i++) {
        messages[i] = messages[i + 1];
      }
      messages[MAX_MESSAGES - 1] = "System: " + emergencyMessage;
    }
    
    activateSiren();
  }
}

// --- LED Indicator ---
void updateLED() {
  if (earthquakeAlert) {
    digitalWrite(LED_PIN, HIGH); // LED on during earthquake alert
  } else if (!helpRequestCount) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Blink LED if help requests exist
    delay(500);
  } else {
    digitalWrite(LED_PIN, LOW); // LED off otherwise
  }
}

// --- Siren (LED Blink Pattern) ---
void activateSiren() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(200);
    digitalWrite(LED_PIN, LOW); delay(200);
  }
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(600);
    digitalWrite(LED_PIN, LOW); delay(200);
  }
}

// --- Web Handlers ---
void handleRoot() {
  if (server.hostHeader() != WiFi.softAPIP().toString()) {
    // If accessed through captive portal check
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
  }
  
  File file = LittleFS.open("/index.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

void handleSendMessage() {
    if (server.hasArg("user") && server.hasArg("message")) {
        String user = server.arg("user");
        String message = server.arg("message");
        
        // Add message to array with overflow protection
        if (messageCount < MAX_MESSAGES) {
            messages[messageCount++] = user + ": " + message;
        } else {
            // Shift messages to make room for new one
            for (int i = 0; i < MAX_MESSAGES - 1; i++) {
                messages[i] = messages[i + 1];
            }
            messages[MAX_MESSAGES - 1] = user + ": " + message;
        }
        
        server.send(200, "text/plain", "Message sent");
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

void handleHelpRequest() {
    if (server.hasArg("plain")) {
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        
        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        if (doc.containsKey("user")) {
            String user = doc["user"].as<String>();
            String helpMsg = user + " needs help!";
            
            if (doc.containsKey("location") && !doc["location"].isNull()) {
                float lat = doc["location"]["lat"].as<float>();
                float lng = doc["location"]["lng"].as<float>();
                helpMsg += " Location: " + String(lat, 6) + ", " + String(lng, 6);
                
                updateUserLocation(user, lat, lng);
            }
            
            // Add help request with overflow protection
            if (helpRequestCount < MAX_HELP_REQUESTS) {
                helpRequests[helpRequestCount++] = helpMsg;
            } else {
                // Shift requests to make room for new one
                for (int i = 0; i < MAX_HELP_REQUESTS - 1; i++) {
                    helpRequests[i] = helpRequests[i + 1];
                }
                helpRequests[MAX_HELP_REQUESTS - 1] = helpMsg;
            }
            
            server.send(200, "text/plain", "Help request received");
        } else {
            server.send(400, "text/plain", "Missing user parameter");
        }
    } else {
        server.send(400, "text/plain", "No data received");
    }
}

void handleAdmin() {
  if (!authenticateAdmin()) return;
  File file = LittleFS.open("/admin.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

void handleReset() {
  if (!authenticateAdmin()) return;
  server.send(200, "text/plain", "Resetting ESP32...");
  delay(1000);
  ESP.restart();
}

void handleUpdateSSID() {
  if (!authenticateAdmin()) return;
  if (server.hasArg("ssid")) {
    String newSSID = server.arg("ssid");
    WiFi.softAP(newSSID.c_str(), apPassword);
    server.send(200, "text/plain", "SSID updated to: " + newSSID);
  } else {
    server.send(400, "text/plain", "Missing SSID parameter");
  }
}

void handleGetMessages() {
    String json = "[";
    for (int i = 0; i < messageCount; i++) {
        if (i > 0) json += ",";
        json += "\"" + messages[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json", json);
}

void handleGetHelpRequests() {
    String json = "[";
    for (int i = 0; i < helpRequestCount; i++) {
        if (i > 0) json += ",";
        json += "\"" + helpRequests[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json", json);
}

void handleSystemStats() {
    String json = "{";
    json += "\"uptime\":" + String((millis() - startTime) / 1000) + ",";
    json += "\"freeMemory\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"connectedDevices\":" + String(WiFi.softAPgetStationNum()) + ",";
    json += "\"connectedUsers\":" + String(WiFi.softAPgetStationNum()) + ",";
    json += "\"activeHelpRequests\":" + String(helpRequestCount) + ",";
    json += "\"signalStrength\":" + String(WiFi.RSSI()) + "";
    json += "}";
    server.send(200, "application/json", json);
}

void handleSensorData() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;
  
  String json = "{";
  json += "\"x\":" + String(x) + ",";
  json += "\"y\":" + String(y) + ",";
  json += "\"z\":" + String(z);
  json += "}";
  server.send(200, "application/json", json);
}

void handleUpdateLocation() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    if (doc.containsKey("user") && doc.containsKey("lat") && doc.containsKey("lng")) {
      String user = doc["user"].as<String>();
      float lat = doc["lat"].as<float>();
      float lng = doc["lng"].as<float>();
      
      if (updateUserLocation(user, lat, lng)) {
        server.send(200, "text/plain", "Location updated");
      } else {
        server.send(507, "text/plain", "Location storage full");
      }
    } else {
      server.send(400, "text/plain", "Missing required fields");
    }
  } else {
    server.send(400, "text/plain", "No data received");
  }
}

void handleClearData() {
    messageCount = 0;
    helpRequestCount = 0;
    for (int i = 0; i < MAX_LOCATIONS; i++) {
        userLocations[i].active = false;
    }
    locationCount = 0;
    server.send(200, "text/plain", "All data cleared");
}

void handleGetLocations() {
  String json = "[";
  bool first = true;
  
  for (int i = 0; i < MAX_LOCATIONS; i++) {
    if (userLocations[i].active) {
      if (!first) json += ",";
      json += "{\"user\":\"" + userLocations[i].user + "\",";
      json += "\"lat\":" + String(userLocations[i].lat, 6) + ",";
      json += "\"lng\":" + String(userLocations[i].lng, 6) + "}";
      first = false;
    }
  }
  json += "]";
  
  server.send(200, "application/json", json);
}

void handleCaptivePortal() {
    String host = server.hostHeader();
    if (host != WiFi.softAPIP().toString() && host != "earthquake.local") {
        // Redirect to our IP if not already there
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        server.send(302, "text/plain", "");
    } else {
        handleRoot();  // Show the main page
    }
}

// --- Admin Authentication ---
bool authenticateAdmin() {
  if (!server.authenticate(adminUsername.c_str(), adminPassword.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}