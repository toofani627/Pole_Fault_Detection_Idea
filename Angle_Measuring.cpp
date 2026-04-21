#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ===== CONFIGURATION =====
const char* ssid = "Airtel_Zerotouch";
const char* password = "Airtel@123";
const int LED_PIN = D4;

// ===== SENSOR OBJECTS =====
Adafruit_MPU6050 mpu;
ESP8266WebServer server(80);

// ===== SENSOR DATA =====
float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;
float angleX = 0, angleY = 0;

// ===== CALIBRATION DATA =====
float gyroBiasX = 0, gyroBiasY = 0;
bool isCalibrating = false;
int calibrationCount = 0;
const int CALIBRATION_SAMPLES = 300;
float calibrationGyroX = 0, calibrationGyroY = 0;

// ===== LED CONTROL =====
bool ledActive = false;
int ledBrightness = 0;

// ===== FILTER PARAMETERS =====
const float ALPHA = 0.96;
unsigned long lastUpdate = 0;
unsigned long lastComputeTime = 0;
const unsigned long UPDATE_INTERVAL = 10; // 100Hz update rate

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n==========================================");
  Serial.println("  2D DICE ANGLE VISUALIZER WITH LED");
  Serial.println("==========================================\n");
  
  // Configure LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("✅ LED Pin Configured (GPIO2/D4)");
  
  // Initialize I2C
  Wire.begin(4, 5);
  delay(100);
  
  // Initialize sensors and WiFi
  initMPU6050();
  connectToWiFi();
  setupWebServer();
  
  server.begin();
  Serial.println("\n⚙️  Starting Gyroscope Calibration...\n");
  isCalibrating = true;
}

// =====================================================================
void loop() {
  server.handleClient();
  
  if (millis() - lastUpdate >= UPDATE_INTERVAL) {
    if (isCalibrating) {
      calibrateSensor();
    } else {
      readMPU6050Data();
      controlLED();
    }
    lastUpdate = millis();
  }
}

// =====================================================================
// INITIALIZATION FUNCTIONS
// =====================================================================

void initMPU6050() {
  Serial.println("📡 Initializing MPU6050...");
  
  if (!mpu.begin()) {
    Serial.println("❌ MPU6050 NOT FOUND!");
    while (1) delay(1000);
  }
  
  Serial.println("✅ MPU6050 Found!");
  
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
  mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  Serial.println("✅ Sensor Configured");
  Serial.println("   • Accel: ±16G");
  Serial.println("   • Gyro: ±2000°/s");
  Serial.println("   • Filter: 21Hz\n");
}

void connectToWiFi() {
  Serial.print("🔗 Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi Connected!");
    Serial.print("📍 IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("🌐 Open: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/\n");
  } else {
    Serial.println("❌ WiFi Connection Failed\n");
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/angles", HTTP_GET, handleAnglesData);
  server.on("/api/raw", HTTP_GET, handleRawData);
  server.on("/api/led", HTTP_GET, handleLEDData);
}

// =====================================================================
// WEB SERVER HANDLERS
// =====================================================================

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
  server.send(200, "text/html", getHTMLPage());
}

void handleAnglesData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json; charset=utf-8");
  
  String ledActiveStr = ledActive ? "true" : "false";
  String calibratingStr = isCalibrating ? "true" : "false";
  
  String json = "{\"angleX\":" + String(angleX, 2) + 
                ",\"angleY\":" + String(angleY, 2) +
                ",\"accelX\":" + String(accelX, 3) +
                ",\"accelY\":" + String(accelY, 3) +
                ",\"accelZ\":" + String(accelZ, 3) +
                ",\"ledActive\":" + ledActiveStr +
                ",\"ledBrightness\":" + String(ledBrightness) +
                ",\"calibrating\":" + calibratingStr +
                ",\"calibrationProgress\":" + String((calibrationCount * 100) / CALIBRATION_SAMPLES) + "}";
  
  server.send(200, "application/json", json);
}

void handleRawData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json; charset=utf-8");
  
  String json = "{\"accelX\":" + String(accelX, 3) +
                ",\"accelY\":" + String(accelY, 3) +
                ",\"accelZ\":" + String(accelZ, 3) +
                ",\"gyroX\":" + String(gyroX, 3) +
                ",\"gyroY\":" + String(gyroY, 3) +
                ",\"biasX\":" + String(gyroBiasX, 4) +
                ",\"biasY\":" + String(gyroBiasY, 4) + "}";
  
  server.send(200, "application/json", json);
}

void handleLEDData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json; charset=utf-8");
  
  String ledActiveStr = ledActive ? "true" : "false";
  
  String json = "{\"ledActive\":" + ledActiveStr +
                ",\"ledBrightness\":" + String(ledBrightness) +
                ",\"angleY\":" + String(angleY, 2) +
                ",\"ledMinAngle\":65,\"ledMaxAngle\":90}";
  
  server.send(200, "application/json", json);
}

// =====================================================================
// CALIBRATION
// =====================================================================

void calibrateSensor() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  calibrationGyroX += (g.gyro.x * 180.0) / PI;
  calibrationGyroY += (g.gyro.y * 180.0) / PI;
  
  calibrationCount++;
  
  if (calibrationCount % 50 == 0) Serial.print(".");
  
  if (calibrationCount >= CALIBRATION_SAMPLES) {
    gyroBiasX = calibrationGyroX / CALIBRATION_SAMPLES;
    gyroBiasY = calibrationGyroY / CALIBRATION_SAMPLES;
    
    isCalibrating = false;
    
    Serial.println("\n\n✅ CALIBRATION COMPLETE!");
    Serial.print("Gyro Bias X: ");
    Serial.print(gyroBiasX, 4);
    Serial.print("°/s | Y: ");
    Serial.print(gyroBiasY, 4);
    Serial.println("°/s\n");
    
    // Initialize angles
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    accelX = a.acceleration.x / 9.81;
    accelY = a.acceleration.y / 9.81;
    accelZ = a.acceleration.z / 9.81;
    
    angleX = atan2(accelY, accelZ) * 180.0 / PI;
    angleY = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
    
    // Reset compute time to avoid huge initial dt
    lastComputeTime = millis();
  }
}

// =====================================================================
// SENSOR READING & ANGLE CALCULATION
// =====================================================================

void readMPU6050Data() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Calculate precise dt
  unsigned long now = millis();
  float dt = (now - lastComputeTime) / 1000.0;
  if(lastComputeTime == 0) dt = UPDATE_INTERVAL / 1000.0;
  lastComputeTime = now;

  // Raw acceleration
  float rawAccelX = a.acceleration.x;
  float rawAccelY = a.acceleration.y;
  float rawAccelZ = a.acceleration.z;
  
  // Normalize to g
  accelX = rawAccelX / 9.81;
  accelY = rawAccelY / 9.81;
  accelZ = rawAccelZ / 9.81;
  
  // Corrected gyro (subtract bias)
  gyroX = (g.gyro.x * 180.0) / PI - gyroBiasX;
  gyroY = (g.gyro.y * 180.0) / PI - gyroBiasY;
  
  // ===== CALCULATE ANGLES FROM ACCELERATION =====
  // Use correct formulas (matches calibration step) for pitch and roll
  float accelAngleX = atan2(rawAccelY, rawAccelZ) * 180.0 / PI; // Roll
  float accelAngleY = atan2(-rawAccelX, sqrt(rawAccelY * rawAccelY + rawAccelZ * rawAccelZ)) * 180.0 / PI; // Pitch
  
  // ===== GYRO INTEGRATION =====
  float gyroAngleX = angleX + gyroX * dt;
  float gyroAngleY = angleY + gyroY * dt;
  
  // ===== COMPLEMENTARY FILTER =====
  angleX = ALPHA * gyroAngleX + (1.0 - ALPHA) * accelAngleX;
  angleY = ALPHA * gyroAngleY + (1.0 - ALPHA) * accelAngleY;
  
  // ===== LIMIT ANGLES =====
  // Limit removed to let angles freely track up to ±180° for full 3D rotations.
  // This ensures 'angleY > 90' in controlLED() works correctly if inverted.
  if (angleX > 180) angleX -= 360;
  if (angleX < -180) angleX += 360;
  if (angleY > 180) angleY -= 360;
  if (angleY < -180) angleY += 360;
  
  // Debug serial output
  Serial.print("X:");
  Serial.print(angleX, 1); Serial.print("° Y:");
  Serial.print(angleY, 1); Serial.print("° | LED:");
  Serial.print(ledActive ? "ON" : "OFF"); Serial.print(" ");
  Serial.print(ledBrightness); Serial.println("/255");
}

// =====================================================================
// LED CONTROL
// =====================================================================

void controlLED() {
  if (angleY >= 65 && angleY <= 90) {
    ledActive = true;
    
    // Calculate brightness: 65°=0%, 90°=100%
    float angleRange = angleY - 65.0;
    float brightnessPercent = (angleRange / 25.0) * 100.0;
    ledBrightness = (int)(brightnessPercent * 2.55);
    
    int pwmValue = 255 - ledBrightness;  // Invert for active-low LED
    analogWrite(LED_PIN, pwmValue);
  } 
  else if (angleY > 90) {
    ledActive = true;
    ledBrightness = 255;
    analogWrite(LED_PIN, 0);
  }
  else {
    ledActive = false;
    ledBrightness = 0;
    digitalWrite(LED_PIN, HIGH);
  }
}

// =====================================================================
// HTML PAGE
// =====================================================================

String getHTMLPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>2D Dice Angle with LED</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 10px;
        }
        .container {
            background: white;
            border-radius: 15px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            padding: 25px;
            max-width: 1100px;
            width: 100%;
        }
        h1 { 
            text-align: center; 
            color: #333;
            margin-bottom: 10px;
            font-size: 2.2em;
        }
        .subtitle {
            text-align: center;
            color: #666;
            margin-bottom: 20px;
            font-size: 0.9em;
        }
        .calibration-status {
            background: #fff3cd;
            border: 2px solid #ffc107;
            border-radius: 8px;
            padding: 15px;
            margin-bottom: 20px;
            display: none;
            text-align: center;
        }
        .calibration-status.active {
            display: block;
        }
        .progress-bar {
            width: 100%;
            height: 25px;
            background: #ddd;
            border-radius: 5px;
            overflow: hidden;
            margin-top: 10px;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #667eea, #764ba2);
            width: 0%;
            transition: width 0.1s ease;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: bold;
        }
        .wrapper {
            display: grid;
            grid-template-columns: 1.2fr 1fr;
            gap: 25px;
        }
        #glContainer {
            width: 100%;
            height: 550px;
            border: 3px solid #ddd;
            border-radius: 12px;
            background: linear-gradient(135deg, #f5f5f5 0%, #e0e0e0 100%);
        }
        .panel {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 12px;
            border: 2px solid #e0e0e0;
            max-height: 550px;
            overflow-y: auto;
        }
        .panel h2 { 
            margin-bottom: 15px; 
            color: #333; 
            border-bottom: 3px solid #667eea;
            padding-bottom: 10px;
        }
        .data-section {
            margin-bottom: 20px;
        }
        .data-section h3 {
            color: #667eea;
            font-size: 0.9em;
            margin-bottom: 10px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        .data-row {
            display: flex;
            justify-content: space-between;
            padding: 10px;
            background: white;
            margin-bottom: 8px;
            border-radius: 6px;
            border-left: 4px solid #667eea;
        }
        .label { font-weight: 600; color: #555; }
        .value { 
            color: #667eea; 
            font-weight: bold; 
            font-family: 'Courier New', monospace;
        }
        .led-indicator {
            background: white;
            padding: 15px;
            border-radius: 8px;
            margin-top: 15px;
            border: 2px solid #ddd;
            text-align: center;
        }
        .led-circle {
            width: 60px;
            height: 60px;
            margin: 0 auto 10px;
            border-radius: 50%;
            background: #cccccc;
            box-shadow: 0 0 20px rgba(0,0,0,0.1) inset;
            transition: background 0.1s ease, box-shadow 0.1s ease;
        }
        .led-circle.active {
            background: #ffff00;
            box-shadow: 0 0 30px #ffff00, 0 0 60px #ffff00, inset 0 0 20px rgba(255,255,0,0.5);
        }
        .led-brightness {
            width: 100%;
            height: 8px;
            background: #ddd;
            border-radius: 4px;
            overflow: hidden;
            margin-top: 8px;
        }
        .led-brightness-fill {
            height: 100%;
            background: linear-gradient(90deg, #ffff00, #ffff00);
            width: 0%;
            transition: width 0.05s ease;
        }
        .angle-meter {
            background: white;
            padding: 15px;
            border-radius: 8px;
            margin-top: 10px;
            border: 2px solid #ddd;
        }
        .meter-label {
            font-size: 0.85em;
            color: #666;
            margin-bottom: 8px;
            font-weight: 600;
        }
        .meter-bar {
            width: 100%;
            height: 20px;
            background: #f0f0f0;
            border-radius: 5px;
            overflow: hidden;
            position: relative;
            border: 1px solid #ddd;
        }
        .meter-fill {
            height: 100%;
            background: linear-gradient(90deg, #667eea, #764ba2);
            width: 50%;
            transition: width 0.05s ease;
        }
        .meter-center {
            position: absolute;
            width: 2px;
            height: 100%;
            background: #333;
            left: 50%;
            top: 0;
        }
        .status { 
            text-align: center; 
            margin-top: 20px; 
            padding: 12px; 
            background: #e8f5e9; 
            border-radius: 8px; 
            color: #2e7d32;
            font-weight: bold;
        }
        @media (max-width: 900px) {
            .wrapper { grid-template-columns: 1fr; }
            #glContainer { height: 450px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎲 2D Dice Angle with 💡 LED</h1>
        <p class="subtitle">LED activates at 65-90° | Real-time 3D Visualization</p>
        
        <div class="calibration-status" id="calibrationStatus">
            <strong>⚙️ Calibrating...</strong>
            <div class="progress-bar">
                <div class="progress-fill" id="progressFill">0%</div>
            </div>
        </div>
        
        <div class="wrapper">
            <div id="glContainer"></div>
            
            <div class="panel">
                <h2>📊 Data</h2>
                
                <div class="data-section">
                    <h3>2D Angles</h3>
                    <div class="data-row">
                        <span class="label">Angle X:</span>
                        <span class="value" id="angleX">0.0°</span>
                    </div>
                    <div class="data-row">
                        <span class="label">Angle Y:</span>
                        <span class="value" id="angleY">0.0°</span>
                    </div>
                </div>
                
                <div class="angle-meter">
                    <div class="meter-label">Y-axis (LED Trigger: 65-90°)</div>
                    <div class="meter-bar">
                        <div class="meter-fill" id="meterY"></div>
                        <div class="meter-center"></div>
                    </div>
                </div>
                
                <div class="led-indicator">
                    <div style="font-weight: 600; margin-bottom: 10px;">LED Status</div>
                    <div class="led-circle" id="ledCircle"></div>
                    <div id="ledStatus" style="font-weight: 600; margin-bottom: 5px;">⚫ OFF</div>
                    <div class="led-brightness">
                        <div class="led-brightness-fill" id="ledBrightnessBar"></div>
                    </div>
                    <div style="font-size: 0.75em; color: #999; margin-top: 5px;">
                        <span id="ledBrightnessValue">0</span>/255
                    </div>
                </div>
                
                <div class="data-section" style="margin-top: 15px;">
                    <h3>Acceleration</h3>
                    <div class="data-row">
                        <span class="label">X:</span>
                        <span class="value" id="accelX">0.000g</span>
                    </div>
                    <div class="data-row">
                        <span class="label">Y:</span>
                        <span class="value" id="accelY">0.000g</span>
                    </div>
                    <div class="data-row">
                        <span class="label">Z:</span>
                        <span class="value" id="accelZ">0.000g</span>
                    </div>
                </div>
                
                <div id="status" class="status">🔄 Connecting...</div>
            </div>
        </div>
    </div>

    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <script>
        const container = document.getElementById('glContainer');
        const scene = new THREE.Scene();
        scene.background = new THREE.Color(0xf5f5f5);
        
        const camera = new THREE.PerspectiveCamera(75, container.clientWidth / container.clientHeight, 0.1, 1000);
        camera.position.set(0, 0, 5);
        camera.lookAt(0, 0, 0);
        
        const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        renderer.setSize(container.clientWidth, container.clientHeight);
        renderer.shadowMap.enabled = true;
        container.appendChild(renderer.domElement);
        
        const light1 = new THREE.DirectionalLight(0xffffff, 1);
        light1.position.set(5, 5, 5);
        light1.castShadow = true;
        scene.add(light1);
        
        scene.add(new THREE.AmbientLight(0xffffff, 0.7));
        
        // DICE
        const dice = new THREE.Group();
        const diceGeom = new THREE.BoxGeometry(2, 2, 2);
        const materials = [
            new THREE.MeshPhongMaterial({ color: 0xff4444 }),
            new THREE.MeshPhongMaterial({ color: 0x4444ff }),
            new THREE.MeshPhongMaterial({ color: 0x44ff44 }),
            new THREE.MeshPhongMaterial({ color: 0xffff44 }),
            new THREE.MeshPhongMaterial({ color: 0xff44ff }),
            new THREE.MeshPhongMaterial({ color: 0x44ffff })
        ];
        
        const diceMesh = new THREE.Mesh(diceGeom, materials);
        diceMesh.castShadow = true;
        dice.add(diceMesh);
        
        const edges = new THREE.EdgesGeometry(diceGeom);
        const wireframe = new THREE.LineSegments(edges, new THREE.LineBasicMaterial({ color: 0x000000, linewidth: 2 }));
        diceMesh.add(wireframe);
        
        scene.add(dice);
        
        // GROUND
        const ground = new THREE.Mesh(new THREE.PlaneGeometry(15, 15), new THREE.MeshPhongMaterial({ color: 0x9ccc65 }));
        ground.rotation.x = -Math.PI / 2;
        ground.position.y = -2;
        ground.receiveShadow = true;
        scene.add(ground);
        
        window.addEventListener('resize', () => {
            const w = container.clientWidth, h = container.clientHeight;
            camera.aspect = w / h;
            camera.updateProjectionMatrix();
            renderer.setSize(w, h);
        });
        
        async function updateData() {
            try {
                const response = await fetch('/api/angles');
                const data = await response.json();
                
                if (data.calibrating) {
                    document.getElementById('calibrationStatus').classList.add('active');
                    document.getElementById('progressFill').style.width = data.calibrationProgress + '%';
                } else {
                    document.getElementById('calibrationStatus').classList.remove('active');
                }
                
                document.getElementById('angleX').textContent = data.angleX.toFixed(1) + '°';
                document.getElementById('angleY').textContent = data.angleY.toFixed(1) + '°';
                document.getElementById('accelX').textContent = data.accelX.toFixed(3) + 'g';
                document.getElementById('accelY').textContent = data.accelY.toFixed(3) + 'g';
                document.getElementById('accelZ').textContent = data.accelZ.toFixed(3) + 'g';
                
                const meterYPercent = ((data.angleY + 90) / 180) * 100;
                document.getElementById('meterY').style.width = meterYPercent + '%';
                
                if (data.ledActive) {
                    document.getElementById('ledCircle').classList.add('active');
                    document.getElementById('ledStatus').textContent = '💡 ON';
                    document.getElementById('ledBrightnessBar').style.width = ((data.ledBrightness / 255) * 100) + '%';
                    document.getElementById('ledBrightnessValue').textContent = data.ledBrightness;
                } else {
                    document.getElementById('ledCircle').classList.remove('active');
                    document.getElementById('ledStatus').textContent = '⚫ OFF';
                    document.getElementById('ledBrightnessBar').style.width = '0%';
                    document.getElementById('ledBrightnessValue').textContent = '0';
                }
                
                const angleXRad = (data.angleX * Math.PI) / 180;
                const angleYRad = (data.angleY * Math.PI) / 180;
                
                dice.rotation.order = 'YXZ';
                dice.rotation.y = angleXRad;
                dice.rotation.x = angleYRad;
                
                document.getElementById('status').textContent = '✅ Connected';
            } catch (error) {
                document.getElementById('status').textContent = '❌ Error';
            }
        }
        
        function animate() {
            requestAnimationFrame(animate);
            renderer.render(scene, camera);
        }
        
        animate();
        setInterval(updateData, 50);
        updateData();
    </script>
</body>
</html>
)rawliteral";
}