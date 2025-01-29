#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <MPU6050.h>

// Configuration
const char* apSSID = "EarthquakeAlertSystem"; // Default SSID
const char* apPassword = ""; // Open network (no password)
const float SHAKE_THRESHOLD = 2.5; // Sensitivity for earthquake detection
const int LED_PIN = 2; // Built-in LED for indicators

WebServer server(80);
MPU6050 mpu;

// System State
bool earthquakeAlert = false;
String emergencyMessage = "No current emergency";
std::vector<String> messages; // Stores all messages (like a chat group)
std::vector<String> helpRequests; // Stores help requests
unsigned long lastShakeTime = 0;

// Admin Credentials
const String adminUsername = "admin";
const String adminPassword = "earthquake123";

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

  // Start Access Point
  WiFi.softAP(apSSID, apPassword);
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());

  // Server Routes
  server.on("/", HTTP_GET, handleRoot); // Main interface
  server.on("/send", HTTP_POST, handleSendMessage); // Send message
  server.on("/help", HTTP_POST, handleHelpRequest); // Request help
  server.on("/admin", HTTP_GET, handleAdmin); // Admin panel
  server.on("/reset", HTTP_POST, handleReset); // Reset ESP32
  server.on("/updateSSID", HTTP_POST, handleUpdateSSID); // Change SSID
  server.on("/messages", HTTP_GET, handleGetMessages); // Get all messages
  server.on("/help-requests", HTTP_GET, handleGetHelpRequests); // Get help requests
  server.serveStatic("/", LittleFS, "/"); // Serve static files

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  detectEarthquake(); // Check for earthquakes
  updateLED(); // Update LED based on system state
  server.handleClient(); // Handle client requests
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
    messages.push_back("System: " + emergencyMessage);
    activateSiren();
  }
}

// --- LED Indicator ---
void updateLED() {
  if (earthquakeAlert) {
    digitalWrite(LED_PIN, HIGH); // LED on during earthquake alert
  } else if (!helpRequests.empty()) {
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
  File file = LittleFS.open("/index.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

void handleSendMessage() {
  if (server.hasArg("user") && server.hasArg("message")) {
    String user = server.arg("user");
    String message = server.arg("message");
    messages.push_back(user + ": " + message);
    server.send(200, "text/plain", "Message sent");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleHelpRequest() {
  if (server.hasArg("user")) {
    String user = server.arg("user");
    helpRequests.push_back(user + " needs help!");
    server.send(200, "text/plain", "Help request received");
  } else {
    server.send(400, "text/plain", "Missing user parameter");
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
  for (auto& msg : messages) json += "\"" + msg + "\",";
  if (!messages.empty()) json.remove(json.length() - 1);
  json += "]";
  server.send(200, "application/json", json);
}

void handleGetHelpRequests() {
  String json = "[";
  for (auto& req : helpRequests) json += "\"" + req + "\",";
  if (!helpRequests.empty()) json.remove(json.length() - 1);
  json += "]";
  server.send(200, "application/json", json);
}

// --- Admin Authentication ---
bool authenticateAdmin() {
  if (!server.authenticate(adminUsername.c_str(), adminPassword.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}