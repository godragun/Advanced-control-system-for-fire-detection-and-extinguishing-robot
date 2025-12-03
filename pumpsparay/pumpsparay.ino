#include <WiFi.h>
#include <WebServer.h>

#define FIRE_PIN 32        // Fire sensor DO pin
#define RELAY_PIN 25       // Relay control pin

// WiFi credentials
const char* ssid = "FIRE_SYSTEM";     // AP Name
const char* password = "12345678";    // AP Password

WebServer server(80);

bool manualControl = false;   // FALSE = Auto mode, TRUE = Manual ON/OFF
bool pumpState = false;       // Relay state

// Function to update relay
void updatePump() {
  digitalWrite(RELAY_PIN, pumpState ? HIGH : LOW);
}

// Website HTML Page
String webpage() {
  String fireStatus;
  int fireState = digitalRead(FIRE_PIN);
  fireStatus = (fireState == LOW) ? "🔥 FIRE DETECTED" : "No fire detected";

  String page = "<html><head>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  page += "<style>";
  page += "body { font-family: Arial; text-align: center; padding: 20px; }";
  page += "button { padding: 15px; font-size: 20px; margin: 10px; width: 200px; }";
  page += "</style></head><body>";
  page += "<h1>🔥 Fire Detection System</h1>";
  page += "<h2>Status: <b>" + fireStatus + "</b></h2>";

  page += "<h3>Pump Control:</h3>";
  page += "<p>Mode: <b>" + String(manualControl ? "Manual" : "Auto") + "</b></p>";

  page += "<a href='/manual_on'><button>Manual ON</button></a>";
  page += "<a href='/manual_off'><button>Manual OFF</button></a>";
  page += "<a href='/auto'><button>Switch to AUTO MODE</button></a>";

  page += "<h3>Current Pump State: <b>";
  page += pumpState ? "ON" : "OFF";
  page += "</b></h3>";

  page += "</body></html>";

  return page;
}

// Web routes
void handleRoot() {
  server.send(200, "text/html", webpage());
}

void handleManualOn() {
  manualControl = true;
  pumpState = true;
  updatePump();
  Serial.println("Manual: Pump ON");
  server.send(200, "text/html", webpage());
}

void handleManualOff() {
  manualControl = true;
  pumpState = false;
  updatePump();
  Serial.println("Manual: Pump OFF");
  server.send(200, "text/html", webpage());
}

void handleAutoMode() {
  manualControl = false;
  Serial.println("Switched to AUTO mode");
  server.send(200, "text/html", webpage());
}

void setup() {
  Serial.begin(115200);

  pinMode(FIRE_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Serial.println("Starting Access Point...");
  WiFi.softAP(ssid, password);

  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/manual_on", handleManualOn);
  server.on("/manual_off", handleManualOff);
  server.on("/auto", handleAutoMode);

  server.begin();
  Serial.println("Web server online");
}

void loop() {
  server.handleClient();

  int fireState = digitalRead(FIRE_PIN);

  if (!manualControl) { 
    // AUTO MODE
    if (fireState == LOW) { // Fire detected
      if (!pumpState) {
        Serial.println("🔥 FIRE DETECTED! Pump ON (AUTO)");
      }
      pumpState = true;
    } else {
      if (pumpState) {
        Serial.println("No fire. Pump OFF (AUTO)");
      }
      pumpState = false;
    }
    updatePump();
  }

  delay(200);
}
