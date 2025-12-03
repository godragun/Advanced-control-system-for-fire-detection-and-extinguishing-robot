#include <WiFi.h>
#include <WebServer.h>
#include <NewPing.h>

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
const int pwmChannelA = 0;
const int pwmChannelB = 1;
const int resolution = 8;

// Timers
unsigned long lastMovement = 0;
unsigned long waterPumpStartTime = 0;
unsigned long lastObstacleDetect = 0;
bool obstacleAvoidanceActive = false;

// Motor control functions - CORRECTED DIRECTION
void setupMotors() {
  // Configure PWM channels
  ledcSetup(pwmChannelA, freq, resolution);
  ledcSetup(pwmChannelB, freq, resolution);
  
  // Attach pins to PWM channels
  ledcAttachPin(ENA, pwmChannelA);
  ledcAttachPin(ENB, pwmChannelB);
}

void moveForward() {
  Serial.println("Moving Forward");
  // Left motor forward
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  // Right motor forward
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(pwmChannelA, 200);
  ledcWrite(pwmChannelB, 200);
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
  ledcWrite(pwmChannelA, 200);
  ledcWrite(pwmChannelB, 200);
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
  ledcWrite(pwmChannelA, 150);
  ledcWrite(pwmChannelB, 150);
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
  ledcWrite(pwmChannelA, 150);
  ledcWrite(pwmChannelB, 150);
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
  ledcWrite(pwmChannelA, 0);
  ledcWrite(pwmChannelB, 0);
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

// HTML Interface with ARROW buttons only
String getHtmlPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Firefighting Robot Control</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            -webkit-user-select: none;
            -webkit-touch-callout: none;
            touch-action: manipulation;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #0a0e14 0%, #1a1f2e 100%);
            color: #e0e6ed;
            min-height: 100vh;
            padding: 20px;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
        }

        .header {
            text-align: center;
            margin-bottom: 30px;
            padding-bottom: 20px;
            border-bottom: 2px solid rgba(0, 180, 255, 0.3);
        }

        .header h1 {
            color: #00b4ff;
            font-size: 2.5em;
            margin-bottom: 10px;
            text-shadow: 0 0 20px rgba(0, 180, 255, 0.5);
        }

        .header p {
            color: #a0aec0;
            font-size: 1.1em;
        }

        .dashboard {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 30px;
            margin-bottom: 30px;
        }

        @media (max-width: 768px) {
            .dashboard {
                grid-template-columns: 1fr;
            }
        }

        .panel {
            background: rgba(28, 33, 40, 0.9);
            border-radius: 20px;
            padding: 30px;
            border: 1px solid rgba(42, 50, 61, 0.5);
            box-shadow: 0 15px 35px rgba(0, 0, 0, 0.5);
            transition: transform 0.3s ease;
        }

        .panel:hover {
            transform: translateY(-5px);
        }

        .panel-title {
            font-size: 1.8em;
            margin-bottom: 25px;
            color: #00b4ff;
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .panel-title i {
            font-size: 1.2em;
        }

        /* Movement Controls - D-Pad Style */
        .controls-wrapper {
            display: flex;
            justify-content: center;
            margin-bottom: 25px;
        }

        .dpad-container {
            display: grid;
            grid-template-columns: repeat(3, 100px);
            grid-template-rows: repeat(3, 100px);
            gap: 10px;
            position: relative;
        }

        @media (max-width: 480px) {
            .dpad-container {
                grid-template-columns: repeat(3, 80px);
                grid-template-rows: repeat(3, 80px);
            }
        }

        .dpad-btn {
            display: flex;
            align-items: center;
            justify-content: center;
            background: rgba(42, 50, 61, 0.8);
            border: 2px solid rgba(0, 180, 255, 0.3);
            border-radius: 15px;
            color: #e0e6ed;
            font-size: 2.5em;
            cursor: pointer;
            transition: all 0.2s ease;
            user-select: none;
        }

        .dpad-btn:hover {
            background: rgba(0, 180, 255, 0.2);
            border-color: #00b4ff;
            transform: scale(1.05);
        }

        .dpad-btn:active {
            background: rgba(0, 180, 255, 0.4);
            transform: scale(0.95);
        }

        .dpad-btn.active {
            background: rgba(0, 180, 255, 0.6);
            border-color: #00b4ff;
            box-shadow: 0 0 20px rgba(0, 180, 255, 0.5);
        }

        /* Position arrows in D-pad layout */
        .arrow-up {
            grid-column: 2;
            grid-row: 1;
        }

        .arrow-left {
            grid-column: 1;
            grid-row: 2;
        }

        .stop-btn {
            grid-column: 2;
            grid-row: 2;
            background: rgba(244, 67, 54, 0.8) !important;
            border-color: rgba(244, 67, 54, 0.5) !important;
            font-size: 2.2em;
            display: flex;
            align-items: center;
            justify-content: center;
        }

        .stop-btn:hover {
            background: rgba(244, 67, 54, 0.6) !important;
            border-color: #f44336 !important;
        }

        .stop-btn:active {
            background: rgba(244, 67, 54, 0.8) !important;
        }

        .stop-btn.active {
            background: rgba(244, 67, 54, 0.9) !important;
            border-color: #f44336 !important;
            box-shadow: 0 0 20px rgba(244, 67, 54, 0.5) !important;
        }

        .arrow-right {
            grid-column: 3;
            grid-row: 2;
        }

        .arrow-down {
            grid-column: 2;
            grid-row: 3;
        }

        /* Arrow styling - larger and centered */
        .arrow-icon {
            display: inline-block;
            transition: transform 0.2s ease;
            text-align: center;
            width: 100%;
        }

        .dpad-btn:hover .arrow-icon {
            transform: scale(1.2);
        }

        /* Mode Controls */
        .mode-controls {
            display: flex;
            gap: 15px;
            margin-bottom: 25px;
        }

        .mode-btn {
            flex: 1;
            padding: 20px;
            border-radius: 15px;
            border: none;
            font-size: 1.2em;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s ease;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
        }

        #autoModeBtn {
            background: linear-gradient(135deg, #4caf50, #2e7d32);
            color: white;
        }

        #autoModeBtn.active {
            background: linear-gradient(135deg, #66bb6a, #388e3c);
            box-shadow: 0 0 20px rgba(76, 175, 80, 0.5);
        }

        #manualModeBtn {
            background: linear-gradient(135deg, #2196f3, #0d47a1);
            color: white;
        }

        #manualModeBtn.active {
            background: linear-gradient(135deg, #42a5f5, #1565c0);
            box-shadow: 0 0 20px rgba(33, 150, 243, 0.5);
        }

        /* Water Pump Control */
        .pump-control {
            text-align: center;
            margin-top: 20px;
        }

        #waterPumpBtn {
            width: 100%;
            padding: 25px;
            border-radius: 15px;
            border: none;
            font-size: 1.5em;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s ease;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 15px;
            background: linear-gradient(135deg, #00b4ff, #0088cc);
            color: white;
        }

        #waterPumpBtn.active {
            background: linear-gradient(135deg, #f44336, #d32f2f);
            box-shadow: 0 0 25px rgba(244, 67, 54, 0.6);
            animation: pulse 1.5s infinite;
        }

        @keyframes pulse {
            0%, 100% { transform: scale(1); }
            50% { transform: scale(1.05); }
        }

        /* Status Panel */
        .status-grid {
            display: flex;
            flex-direction: column;
            gap: 20px;
        }

        .status-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 20px;
            background: rgba(42, 50, 61, 0.6);
            border-radius: 15px;
            border-left: 5px solid #00b4ff;
        }

        .status-item.fire {
            border-left-color: #f44336;
        }

        .status-item.warning {
            border-left-color: #ff9800;
        }

        .status-label {
            display: flex;
            align-items: center;
            gap: 10px;
            font-size: 1.1em;
        }

        .status-value {
            font-weight: bold;
            font-size: 1.2em;
        }

        .status-value.fire-detected {
            color: #f44336;
            animation: blink 1s infinite;
        }

        .status-value.normal {
            color: #4caf50;
        }

        @keyframes blink {
            0%, 50% { opacity: 1; }
            51%, 100% { opacity: 0.5; }
        }

        .progress-bar {
            width: 150px;
            height: 10px;
            background: rgba(255, 255, 255, 0.1);
            border-radius: 5px;
            overflow: hidden;
        }

        .progress-fill {
            height: 100%;
            border-radius: 5px;
            transition: width 0.5s ease;
        }

        .progress-fill.water {
            background: linear-gradient(90deg, #00b4ff, #0088cc);
        }

        .progress-fill.danger {
            background: linear-gradient(90deg, #f44336, #d32f2f);
        }

        /* Connection Status */
        .connection-status {
            text-align: center;
            padding: 15px;
            background: rgba(42, 50, 61, 0.8);
            border-radius: 15px;
            margin-top: 30px;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
        }

        .connection-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #4caf50;
            animation: pulse 2s infinite;
        }

        .connection-dot.disconnected {
            background: #f44336;
        }

        /* Instructions */
        .instructions {
            margin-top: 30px;
            padding: 20px;
            background: rgba(0, 180, 255, 0.1);
            border-radius: 15px;
            font-size: 0.9em;
            color: #a0aec0;
            text-align: center;
        }

        /* Direction labels - hidden as requested */
        .direction-label {
            display: none;
        }
    </style>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body>
    <div class="container">
        <div class="header">
            <h1><i class="fas fa-robot"></i> Firefighting Robot Control</h1>
            <p>Advanced control system for fire detection and extinguishing robot</p>
        </div>

        <div class="dashboard">
            <!-- Movement Controls Panel -->
            <div class="panel">
                <h2 class="panel-title"><i class="fas fa-gamepad"></i> Movement Controls</h2>
                
                <div class="controls-wrapper">
                    <div class="dpad-container">
                        <!-- UP Button -->
                        <button class="dpad-btn arrow-up" data-direction="forward" title="Forward">
                            <span class="arrow-icon">⬆</span>
                        </button>
                        
                        <!-- LEFT Button -->
                        <button class="dpad-btn arrow-left" data-direction="left" title="Left">
                            <span class="arrow-icon">⬅</span>
                        </button>
                        
                        <!-- STOP Button -->
                        <button class="dpad-btn stop-btn" data-direction="stop" title="Stop">
                            <span class="arrow-icon">⏹</span>
                        </button>
                        
                        <!-- RIGHT Button -->
                        <button class="dpad-btn arrow-right" data-direction="right" title="Right">
                            <span class="arrow-icon">➡</span>
                        </button>
                        
                        <!-- DOWN Button -->
                        <button class="dpad-btn arrow-down" data-direction="backward" title="Backward">
                            <span class="arrow-icon">⬇</span>
                        </button>
                    </div>
                </div>

                <div class="mode-controls">
                    <button id="autoModeBtn" class="mode-btn">
                        <i class="fas fa-robot"></i> AUTO MODE
                    </button>
                    <button id="manualModeBtn" class="mode-btn active">
                        <i class="fas fa-hand-paper"></i> MANUAL MODE
                    </button>
                </div>

                <div class="pump-control">
                    <button id="waterPumpBtn" class="pump-btn">
                        <i class="fas fa-tint"></i> START WATER PUMP
                    </button>
                </div>

                <div class="instructions">
                    <p><i class="fas fa-info-circle"></i> Use arrow keys or WASD for keyboard control | Tap and hold buttons for mobile</p>
                </div>
            </div>

            <!-- Status Panel -->
            <div class="panel">
                <h2 class="panel-title"><i class="fas fa-chart-line"></i> System Status</h2>
                
                <div class="status-grid">
                    <div class="status-item fire">
                        <span class="status-label">
                            <i class="fas fa-fire"></i> Fire Detection
                        </span>
                        <span id="fireStatus" class="status-value normal">No Fire</span>
                    </div>
                    
                    <div class="status-item">
                        <span class="status-label">
                            <i class="fas fa-tint"></i> Water Level
                        </span>
                        <div style="display: flex; align-items: center; gap: 15px;">
                            <div class="progress-bar">
                                <div id="waterProgress" class="progress-fill water" style="width: 100%"></div>
                            </div>
                            <span id="waterLevel" class="status-value">100%</span>
                        </div>
                    </div>
                    
                    <div class="status-item">
                        <span class="status-label">
                            <i class="fas fa-ruler"></i> Obstacle Distance
                        </span>
                        <span id="obstacleDistance" class="status-value">-- cm</span>
                    </div>
                    
                    <div class="status-item">
                        <span class="status-label">
                            <i class="fas fa-walking"></i> Robot Status
                        </span>
                        <span id="robotStatusText" class="status-value">Stopped</span>
                    </div>
                    
                    <div class="status-item warning">
                        <span class="status-label">
                            <i class="fas fa-spray-can"></i> Water Pump
                        </span>
                        <span id="waterPumpStatus" class="status-value">OFF</span>
                    </div>
                    
                    <div class="status-item">
                        <span class="status-label">
                            <i class="fas fa-clock"></i> Last Update
                        </span>
                        <span id="lastUpdated" class="status-value">--</span>
                    </div>
                </div>

                <div class="connection-status">
                    <div class="connection-dot" id="connectionDot"></div>
                    <span id="connectionStatus">Connected to Robot</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        let currentMode = 'manual'; // 'auto' or 'manual'
        let waterPumpActive = false;
        let connectionStatus = true;
        let activeDirection = null;

        // DOM Elements
        const autoModeBtn = document.getElementById('autoModeBtn');
        const manualModeBtn = document.getElementById('manualModeBtn');
        const waterPumpBtn = document.getElementById('waterPumpBtn');
        const fireStatusEl = document.getElementById('fireStatus');
        const waterLevelEl = document.getElementById('waterLevel');
        const waterProgressEl = document.getElementById('waterProgress');
        const obstacleDistanceEl = document.getElementById('obstacleDistance');
        const robotStatusEl = document.getElementById('robotStatusText');
        const waterPumpStatusEl = document.getElementById('waterPumpStatus');
        const lastUpdatedEl = document.getElementById('lastUpdated');
        const connectionDotEl = document.getElementById('connectionDot');
        const connectionStatusEl = document.getElementById('connectionStatus');

        // Initialize
        document.addEventListener('DOMContentLoaded', function() {
            updateStatus();
            setInterval(updateStatus, 2000); // Update every 2 seconds
            
            // Test connection on load
            testConnection();
        });

        // Event Listeners for Mode Buttons
        autoModeBtn.addEventListener('click', function() {
            setMode('auto');
        });

        manualModeBtn.addEventListener('click', function() {
            setMode('manual');
        });

        // Water Pump Button
        waterPumpBtn.addEventListener('click', function() {
            toggleWaterPump();
        });

        // Movement Controls (for manual mode)
        document.querySelectorAll('.dpad-btn').forEach(btn => {
            const direction = btn.dataset.direction;
            
            // Mouse events
            btn.addEventListener('mousedown', () => {
                if (currentMode === 'manual') {
                    btn.classList.add('active');
                    sendMovementCommand(direction);
                }
            });
            
            btn.addEventListener('mouseup', () => {
                if (currentMode === 'manual') {
                    btn.classList.remove('active');
                    if (direction !== 'stop') {
                        sendMovementCommand('stop');
                    }
                }
            });
            
            btn.addEventListener('mouseleave', () => {
                if (currentMode === 'manual') {
                    btn.classList.remove('active');
                    if (direction !== 'stop') {
                        sendMovementCommand('stop');
                    }
                }
            });
            
            // Touch events for mobile
            btn.addEventListener('touchstart', (e) => {
                e.preventDefault();
                if (currentMode === 'manual') {
                    btn.classList.add('active');
                    sendMovementCommand(direction);
                }
            });
            
            btn.addEventListener('touchend', (e) => {
                e.preventDefault();
                if (currentMode === 'manual') {
                    btn.classList.remove('active');
                    if (direction !== 'stop') {
                        sendMovementCommand('stop');
                    }
                }
            });
            
            btn.addEventListener('touchcancel', (e) => {
                e.preventDefault();
                if (currentMode === 'manual') {
                    btn.classList.remove('active');
                    if (direction !== 'stop') {
                        sendMovementCommand('stop');
                    }
                }
            });
        });

        // Keyboard Controls
        document.addEventListener('keydown', (e) => {
            if (currentMode !== 'manual') return;
            
            let direction = null;
            let button = null;
            
            switch(e.key) {
                case 'ArrowUp':
                case 'w':
                case 'W':
                    direction = 'forward';
                    button = document.querySelector('.arrow-up');
                    break;
                case 'ArrowDown':
                case 's':
                case 'S':
                    direction = 'backward';
                    button = document.querySelector('.arrow-down');
                    break;
                case 'ArrowLeft':
                case 'a':
                case 'A':
                    direction = 'left';
                    button = document.querySelector('.arrow-left');
                    break;
                case 'ArrowRight':
                case 'd':
                case 'D':
                    direction = 'right';
                    button = document.querySelector('.arrow-right');
                    break;
                case ' ':
                case 'Escape':
                    direction = 'stop';
                    button = document.querySelector('.stop-btn');
                    break;
            }
            
            if (direction && button) {
                e.preventDefault();
                button.classList.add('active');
                sendMovementCommand(direction);
            }
        });

        document.addEventListener('keyup', (e) => {
            if (currentMode !== 'manual') return;
            
            const movementKeys = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 
                                 'w', 'W', 's', 'S', 'a', 'A', 'd', 'D'];
            
            if (movementKeys.includes(e.key)) {
                // Remove active class from all arrow buttons
                document.querySelectorAll('.dpad-btn').forEach(btn => {
                    btn.classList.remove('active');
                });
                
                sendMovementCommand('stop');
            }
        });

        // Functions
        async function setMode(mode) {
            try {
                const response = await fetch(`/mode?set=${mode}`);
                if (response.ok) {
                    currentMode = mode;
                    
                    // Update button states
                    if (mode === 'auto') {
                        autoModeBtn.classList.add('active');
                        manualModeBtn.classList.remove('active');
                    } else {
                        manualModeBtn.classList.add('active');
                        autoModeBtn.classList.remove('active');
                    }
                    
                    // If switching to manual mode, stop any auto movement
                    if (mode === 'manual') {
                        sendMovementCommand('stop');
                    }
                    
                    console.log(`Mode switched to: ${mode}`);
                }
            } catch (error) {
                console.error('Error switching mode:', error);
            }
        }

        async function toggleWaterPump() {
            try {
                const action = waterPumpActive ? 'stop' : 'start';
                const response = await fetch(`/pump?action=${action}&manual=true`);
                
                if (response.ok) {
                    waterPumpActive = !waterPumpActive;
                    waterPumpBtn.classList.toggle('active', waterPumpActive);
                    waterPumpBtn.innerHTML = waterPumpActive ? 
                        '<i class="fas fa-tint"></i> STOP WATER PUMP' : 
                        '<i class="fas fa-tint"></i> START WATER PUMP';
                    
                    console.log(`Water pump ${waterPumpActive ? 'started' : 'stopped'}`);
                }
            } catch (error) {
                console.error('Error toggling water pump:', error);
            }
        }

        async function sendMovementCommand(direction) {
            try {
                const response = await fetch(`/move?direction=${direction}`);
                if (response.ok) {
                    console.log(`Movement command sent: ${direction}`);
                    
                    // Update robot status display immediately
                    if (direction === 'stop') {
                        robotStatusEl.textContent = 'Stopped';
                    } else {
                        robotStatusEl.textContent = `Moving ${direction}`;
                    }
                }
            } catch (error) {
                console.error('Error sending movement command:', error);
            }
        }

        async function updateStatus() {
            try {
                const response = await fetch('/status');
                const text = await response.text();
                
                // Parse the response
                const lines = text.split('\n');
                const status = {};
                lines.forEach(line => {
                    if (line.includes(':')) {
                        const [key, value] = line.split(':');
                        status[key.trim()] = value.trim();
                    }
                });
                
                if (status.fireDetected !== undefined) {
                    // Update UI with status
                    updateConnectionStatus(true);
                    
                    // Fire status
                    const fireDetected = status.fireDetected === 'true';
                    fireStatusEl.textContent = fireDetected ? 'FIRE DETECTED!' : 'No Fire';
                    fireStatusEl.className = fireDetected ? 'status-value fire-detected' : 'status-value normal';
                    
                    // Water level
                    waterLevelEl.textContent = status.waterLevel + '%';
                    waterProgressEl.style.width = status.waterLevel + '%';
                    waterProgressEl.className = status.waterLevel < 20 ? 
                        'progress-fill danger' : 'progress-fill water';
                    
                    // Obstacle distance
                    obstacleDistanceEl.textContent = status.obstacleDistance + ' cm';
                    
                    // Robot status
                    const isMoving = status.isMoving === 'true';
                    const currentDirection = status.currentDirection;
                    
                    if (isMoving) {
                        robotStatusEl.textContent = `Moving ${currentDirection}`;
                        
                        // Highlight active direction button
                        document.querySelectorAll('.dpad-btn').forEach(btn => {
                            btn.classList.remove('active');
                        });
                        
                        const activeBtn = document.querySelector(`[data-direction="${currentDirection}"]`);
                        if (activeBtn) {
                            activeBtn.classList.add('active');
                        }
                    } else {
                        robotStatusEl.textContent = 'Stopped';
                        // Remove active class from all direction buttons
                        document.querySelectorAll('.dpad-btn').forEach(btn => {
                            btn.classList.remove('active');
                        });
                    }
                    
                    // Water pump status
                    waterPumpActive = status.waterPumpActive === 'true';
                    waterPumpStatusEl.textContent = waterPumpActive ? 'ON' : 'OFF';
                    waterPumpStatusEl.className = waterPumpActive ? 'status-value fire-detected' : 'status-value normal';
                    waterPumpBtn.classList.toggle('active', waterPumpActive);
                    waterPumpBtn.innerHTML = waterPumpActive ? 
                        '<i class="fas fa-tint"></i> STOP WATER PUMP' : 
                        '<i class="fas fa-tint"></i> START WATER PUMP';
                    
                    // Mode
                    currentMode = status.autoMode === 'true' ? 'auto' : 'manual';
                    if (currentMode === 'auto') {
                        autoModeBtn.classList.add('active');
                        manualModeBtn.classList.remove('active');
                    } else {
                        manualModeBtn.classList.add('active');
                        autoModeBtn.classList.remove('active');
                    }
                    
                    // Last update
                    lastUpdatedEl.textContent = status.lastUpdate;
                    
                } else {
                    throw new Error('Invalid status format');
                }
                
            } catch (error) {
                console.error('Error updating status:', error);
                updateConnectionStatus(false);
            }
        }

        function updateConnectionStatus(connected) {
            connectionStatus = connected;
            connectionDotEl.className = connected ? 'connection-dot' : 'connection-dot disconnected';
            connectionStatusEl.textContent = connected ? 'Connected to Robot' : 'Disconnected';
            connectionStatusEl.style.color = connected ? '#4caf50' : '#f44336';
        }

        async function testConnection() {
            try {
                const response = await fetch('/test');
                if (response.ok) {
                    updateConnectionStatus(true);
                }
            } catch (error) {
                updateConnectionStatus(false);
            }
        }
    </script>
</body>
</html>
)rawliteral";
  
  return html;
}

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