#include <WiFi.h>
#include <WebServer.h>
#include <NewPing.h>
#include "html.h"

// Motor control pins - FIXED CONNECTIONS
#define IN1 14  // Motor A Direction 1
#define IN2 15  // Motor A Direction 2
#define IN3 18  // Motor B Direction 1
#define IN4 19  // Motor B Direction 2
#define ENA 13  // Motor A Speed (PWM)
#define ENB 12  // Motor B Speed (PWM)

// Fire sensor pins
#define FIRE_SENSOR_1 2
#define FIRE_SENSOR_2 4
#define FIRE_SENSOR_3 5
#define FIRE_SENSOR_4 22

// Ultrasonic sensor pins for obstacle avoidance
#define OBSTACLE_TRIGGER 23
#define OBSTACLE_ECHO 32

// Water pump relay pin
#define WATER_PUMP_RELAY 26

// Water level sensor (optional - if you have it)
#define WATER_LEVEL_TRIGGER 33
#define WATER_LEVEL_ECHO 25

// WiFi access point credentials
const char* apSsid = "FireFighter-Robot";
const char* apPassword = "12345678"; // Password for security

// Web server
WebServer server(80);

// Ultrasonic sensor for obstacle detection
NewPing obstacleSonar(OBSTACLE_TRIGGER, OBSTACLE_ECHO, 200); // Max distance 200cm
NewPing waterLevelSonar(WATER_LEVEL_TRIGGER, WATER_LEVEL_ECHO, 100); // Optional

// Robot status
struct RobotStatus {
  bool fireDetected = false;
  int waterLevel = 100; // Percentage
  int obstacleDistance = 0;
  String lastUpdate = "";
  bool isMoving = false;
  String currentDirection = "stop";
  bool autoMode = false;
  bool waterPumpActive = false;
  bool manualPumpControl = false; // Manual pump control flag
};

RobotStatus robotStatus;

// PWM properties for ESP32
const int freq = 5000;
const int resolution = 8;

// Timers
unsigned long lastMovement = 0;
unsigned long waterPumpStartTime = 0;
unsigned long lastObstacleDetect = 0;
bool obstacleAvoidanceActive = false;

// Motor control functions - CORRECTED DIRECTION
// Updated for ESP32 Arduino Core 3.x
void setupMotors() {
  // Configure PWM channels using ESP32 Arduino Core 3.x API
  // ledcAttach(pin, freq, resolution) returns channel number
  ledcAttach(ENA, freq, resolution);
  ledcAttach(ENB, freq, resolution);
}

void moveForward() {
  Serial.println("Moving Forward");
  // Left motor forward
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  // Right motor forward
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(ENA, 200);
  ledcWrite(ENB, 200);
  robotStatus.isMoving = true;
  robotStatus.currentDirection = "forward";
  if (!robotStatus.autoMode) lastMovement = millis();
}

void moveBackward() {
  Serial.println("Moving Backward");
  // Left motor backward
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  // Right motor backward
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  ledcWrite(ENA, 200);
  ledcWrite(ENB, 200);
  robotStatus.isMoving = true;
  robotStatus.currentDirection = "backward";
  if (!robotStatus.autoMode) lastMovement = millis();
}

void turnLeft() {
  Serial.println("Turning Left");
  // Left motor backward
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  // Right motor forward
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(ENA, 150);
  ledcWrite(ENB, 150);
  robotStatus.isMoving = true;
  robotStatus.currentDirection = "left";
  if (!robotStatus.autoMode) lastMovement = millis();
}

void turnRight() {
  Serial.println("Turning Right");
  // Left motor forward
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  // Right motor backward
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  ledcWrite(ENA, 150);
  ledcWrite(ENB, 150);
  robotStatus.isMoving = true;
  robotStatus.currentDirection = "right";
  if (!robotStatus.autoMode) lastMovement = millis();
}

void stopRobot() {
  Serial.println("Stopping Robot");
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
  robotStatus.isMoving = false;
  robotStatus.currentDirection = "stop";
}

// Water pump control functions
void startWaterPump(bool manual = false) {
  digitalWrite(WATER_PUMP_RELAY, HIGH); // Relay ON
  robotStatus.waterPumpActive = true;
  robotStatus.manualPumpControl = manual; // If manual, don't auto-stop on fire extinguished
  waterPumpStartTime = millis();
  Serial.println("Water pump started");
}

void stopWaterPump() {
  digitalWrite(WATER_PUMP_RELAY, LOW); // Relay OFF
  robotStatus.waterPumpActive = false;
  robotStatus.manualPumpControl = false;
  waterPumpStartTime = 0;
  Serial.println("Water pump stopped");
}

// Auto-pilot with obstacle avoidance
void autoPilot() {
  int distance = obstacleSonar.ping_cm();
  robotStatus.obstacleDistance = distance;
  
  // Simple obstacle avoidance: if obstacle < 20cm, move back and turn
  if (distance > 0 && distance < 20) {
    if (!obstacleAvoidanceActive) {
      Serial.println("Obstacle detected! Distance: " + String(distance) + "cm");
      obstacleAvoidanceActive = true;
      lastObstacleDetect = millis();
    }
    
    // Move backward for 1 second
    moveBackward();
    delay(1000);
    stopRobot();
    
    // Turn right 90 degrees
    turnRight();
    delay(500);
    stopRobot();
    
    // Move forward for 2 seconds
    moveForward();
    delay(2000);
    stopRobot();
    
    // Turn left 90 degrees to return to original direction
    turnLeft();
    delay(500);
    stopRobot();
    
    obstacleAvoidanceActive = false;
  } else {
    // No obstacle, keep moving forward
    if (!obstacleAvoidanceActive) {
      moveForward();
    }
  }
}

// Sensor reading functions
void updateSensors() {
  // Read fire sensors (LOW = Fire detected, HIGH = No fire)
  robotStatus.fireDetected = digitalRead(FIRE_SENSOR_1) == LOW || 
                            digitalRead(FIRE_SENSOR_2) == LOW ||
                            digitalRead(FIRE_SENSOR_3) == LOW ||
                            digitalRead(FIRE_SENSOR_4) == LOW;
  
  // Read water level (optional - adjust based on your sensor)
  if (WATER_LEVEL_TRIGGER != -1) {
    int waterDistance = waterLevelSonar.ping_cm();
    if (waterDistance > 0) {
      robotStatus.waterLevel = map(constrain(waterDistance, 0, 50), 0, 50, 100, 0);
    }
  }
  
  // Read obstacle distance
  robotStatus.obstacleDistance = obstacleSonar.ping_cm();
  robotStatus.lastUpdate = String(millis() / 1000) + "s ago";
  
  // Automatic water spraying when fire is detected (only in auto mode)
  if (robotStatus.autoMode && robotStatus.fireDetected && !robotStatus.waterPumpActive && !robotStatus.manualPumpControl) {
    startWaterPump(false);
    Serial.println("FIRE DETECTED! Starting automatic water spray!");
  } else if (robotStatus.autoMode && !robotStatus.fireDetected && robotStatus.waterPumpActive && !robotStatus.manualPumpControl) {
    stopWaterPump();
    Serial.println("Fire extinguished. Stopping water spray.");
  }
  
  Serial.printf("Fire: %s, Water: %d%%, Obstacle: %dcm, Pump: %s, Mode: %s\n",
                robotStatus.fireDetected ? "YES" : "NO", 
                robotStatus.waterLevel, 
                robotStatus.obstacleDistance,
                robotStatus.waterPumpActive ? "ON" : "OFF",
                robotStatus.autoMode ? "AUTO" : "MANUAL");
}

// HTML Interface function is now in html.h

// API endpoints
void handleRoot() {
  server.send(200, "text/html", getHtmlPage());
}

void handleMove() {
  if (server.hasArg("direction")) {
    String direction = server.arg("direction");
    Serial.println("Movement command: " + direction);
    
    if (direction == "forward") {
      moveForward();
    } else if (direction == "backward") {
      moveBackward();
    } else if (direction == "left") {
      turnLeft();
    } else if (direction == "right") {
      turnRight();
    } else if (direction == "stop") {
      stopRobot();
    } else {
      server.send(400, "text/plain", "Invalid direction");
      return;
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "No direction specified");
  }
}

void handleMode() {
  if (server.hasArg("set")) {
    String mode = server.arg("set");
    Serial.println("Mode change request: " + mode);
    
    if (mode == "auto") {
      robotStatus.autoMode = true;
      stopRobot(); // Stop any manual movement
    } else if (mode == "manual") {
      robotStatus.autoMode = false;
      stopRobot();
    } else {
      server.send(400, "text/plain", "Invalid mode");
      return;
    }
    
    server.send(200, "text/plain", "Mode set to " + mode);
  } else {
    server.send(400, "text/plain", "No mode specified");
  }
}

void handlePump() {
  if (server.hasArg("action")) {
    String action = server.arg("action");
    bool manual = server.hasArg("manual") && server.arg("manual") == "true";
    
    Serial.println("Pump control: " + action + (manual ? " (manual)" : ""));
    
    if (action == "start") {
      startWaterPump(manual);
      server.send(200, "text/plain", "Pump started");
    } else if (action == "stop") {
      stopWaterPump();
      server.send(200, "text/plain", "Pump stopped");
    } else {
      server.send(400, "text/plain", "Invalid action");
      return;
    }
  } else {
    server.send(400, "text/plain", "No action specified");
  }
}

void handleStatus() {
  updateSensors();
  
  String response = "fireDetected: " + String(robotStatus.fireDetected ? "true" : "false") + "\n";
  response += "waterLevel: " + String(robotStatus.waterLevel) + "\n";
  response += "obstacleDistance: " + String(robotStatus.obstacleDistance) + "\n";
  response += "lastUpdate: " + robotStatus.lastUpdate + "\n";
  response += "isMoving: " + String(robotStatus.isMoving ? "true" : "false") + "\n";
  response += "currentDirection: " + robotStatus.currentDirection + "\n";
  response += "autoMode: " + String(robotStatus.autoMode ? "true" : "false") + "\n";
  response += "waterPumpActive: " + String(robotStatus.waterPumpActive ? "true" : "false") + "\n";
  
  server.send(200, "text/plain", response);
}

void handleTest() {
  server.send(200, "text/plain", "Robot is online!");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Firefighting Robot Starting ===\n");
  
  // Initialize motor pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  
  // Setup PWM for motors
  setupMotors();
  
  // Initialize sensor pins
  pinMode(FIRE_SENSOR_1, INPUT_PULLUP);
  pinMode(FIRE_SENSOR_2, INPUT_PULLUP);
  pinMode(FIRE_SENSOR_3, INPUT_PULLUP);
  pinMode(FIRE_SENSOR_4, INPUT_PULLUP);
  
  // Initialize water pump relay pin
  pinMode(WATER_PUMP_RELAY, OUTPUT);
  digitalWrite(WATER_PUMP_RELAY, LOW); // Start with pump OFF
  
  // Initialize ultrasonic pins
  pinMode(OBSTACLE_TRIGGER, OUTPUT);
  pinMode(OBSTACLE_ECHO, INPUT);
  
  if (WATER_LEVEL_TRIGGER != -1) {
    pinMode(WATER_LEVEL_TRIGGER, OUTPUT);
    pinMode(WATER_LEVEL_ECHO, INPUT);
  }
  
  // Start WiFi access point
  Serial.println("Starting WiFi Access Point...");
  Serial.print("SSID: ");
  Serial.println(apSsid);
  Serial.print("Password: ");
  Serial.println(apPassword);
  
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(apSsid, apPassword)) {
    Serial.println("Access Point Started Successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Failed to start Access Point!");
  }
  
  // Setup server routes
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/mode", handleMode);
  server.on("/pump", handlePump);
  server.on("/status", handleStatus);
  server.on("/test", handleTest);
  
  server.begin();
  Serial.println("HTTP Server Started");
  Serial.println("Connect to WiFi: " + String(apSsid));
  Serial.println("Open browser and go to: http://" + WiFi.softAPIP().toString());
  Serial.println("\n=== Robot Ready ===\n");
  
  // Initial stop
  stopRobot();
}

void loop() {
  server.handleClient();
  
  // Update sensors every 2 seconds
  static unsigned long lastSensorUpdate = 0;
  if (millis() - lastSensorUpdate > 2000) {
    updateSensors();
    lastSensorUpdate = millis();
  }
  
  // Auto-pilot mode
  if (robotStatus.autoMode) {
    autoPilot();
  }
  
  // Auto-stop after 5 seconds of movement in manual mode
  if (robotStatus.isMoving && !robotStatus.autoMode && millis() - lastMovement > 5000) {
    stopRobot();
    Serial.println("Auto-stop triggered after 5 seconds");
  }
  
  delay(10); // Small delay to prevent watchdog timer
}