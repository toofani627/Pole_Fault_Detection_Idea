#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <SPI.h>
#include <LoRa.h>
#include <FS.h>
#include <map>
#include <list>

// ===== CONFIGURATION =====
const char* ssid = "Airtel_Zerotouch";
const char* password = "Airtel@123";
const int LED_PIN = 2; // GPIO 2 (D4)
bool hasWifi = false;

// ===== ESP-NOW REPLACEMENT (LoRa STRUCTS) =====
typedef struct struct_message {
    uint8_t msgType;   // 0 = DATA, 1 = ACK
    uint32_t msgId;
    uint8_t originalSender[6];
    uint8_t immediateSender[6];
    char payload[50];
} struct_message;

struct_message outgoingData;
uint8_t myMac[6]; 

typedef struct {
    unsigned long ts;
    uint8_t senderMac[6];
} cache_entry_t;

#define RX_QUEUE_SIZE 10
typedef struct { uint8_t mac[6]; struct_message msg; } rx_item_t;

#define MAX_TX_JOBS 5
typedef struct { 
    bool active; 
    struct_message msg; 
    unsigned long firstSendTime;
    unsigned long lastSendTime;
    unsigned long retryInterval;
    unsigned long nextJitter;
} tx_job_t;

// ===== FORWARD DECLARATIONS =====
void initMPU6050();
void connectToWiFi();
void setupWebServer();
void handleRoot();
void handleAnglesData();
void handleRawData();
void handleLEDData();
void handleSend();
void handleStats();
void handleHistory();
void calibrateSensor();
void readMPU6050Data();
void controlLED();
String getHTMLPage();
String macToString(const uint8_t* mac);
void addLog(String message);
void saveCacheToSPIFFS();
void loadCacheFromSPIFFS();
void cleanupCache();
void processLoRaRX();
void startTransmission(struct_message msg);
void manageTransmissions();
void loRaBroadcast(struct_message msg);
void sendAck(uint8_t* targetMac, uint32_t msgId);
void processRxQueue();

// ===== SENSOR OBJECTS & DATA =====
Adafruit_MPU6050 mpu;
ESP8266WebServer server(80);

float accelX = 0, accelY = 0, accelZ = 0;
float gyroX = 0, gyroY = 0;
float angleX = 0, angleY = 0;
float gyroBiasX = 0, gyroBiasY = 0;
bool isCalibrating = false;
int calibrationCount = 0;
const int CALIBRATION_SAMPLES = 300;
float calibrationGyroX = 0, calibrationGyroY = 0;
const float ALPHA = 0.96;
unsigned long lastUpdate = 0;
unsigned long lastComputeTime = 0;
const unsigned long UPDATE_INTERVAL = 10; 

// ===== LED & TRIGGER CONTROL =====
bool ledActive = false;
int ledBrightness = 0;
bool ledRxActive = false;
unsigned long ledRxTimer = 0;
unsigned long lastAngleTx = 0;

// ===== MESH STATE =====
std::map<uint32_t, cache_entry_t> messageCache; 
bool cacheNeedsSave = false; 
String systemLog = "";
std::list<String> historyList;
const int MAX_HISTORY_ITEMS = 15;

int totalReceived = 0, totalRelayed = 0, totalDropped = 0;
const int DEDUPLICATION_WINDOW_MS = 60000; 
unsigned long lastCacheCleanup = 0;

rx_item_t rxQueue[RX_QUEUE_SIZE];
volatile int rxHead = 0, rxTail = 0;
tx_job_t txJobs[MAX_TX_JOBS];
const unsigned long MAX_BROADCAST_DURATION = 30000; 

// =====================================================================
// SETUP & LOOP
// =====================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Configure LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Non-Blocking WiFi
    WiFi.mode(WIFI_STA);
    WiFi.macAddress(myMac); // Generate identifier for LoRa routing
    WiFi.begin(ssid, password);
    
    unsigned long wifiStart = millis();
    Serial.print("🔗 Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        hasWifi = true;
        setupWebServer();
        server.begin();
        Serial.println("✅ WiFi Connected!");
    } else {
        Serial.println("❌ WiFi Timeout (15s). Proceeding to mesh loop.");
    }

    // Initialize SPIFFS
    if (SPIFFS.begin()) loadCacheFromSPIFFS();

    // Initialize I2C (MPU6050)
    Wire.begin(4, 5); 
    initMPU6050();
    isCalibrating = true;

    // Initialize SPI (LoRa)
    LoRa.setPins(16, 0, -1); // NSS=D0(16), RST=D3(0), DIO0=-1 (Unused); 
    if (!LoRa.begin(433E6)) {
        Serial.println("❌ LoRa Module NOT FOUND!");
        while (1) delay(1000);
    }
    Serial.println("✅ LoRa Ready (433MHz)");
}

void loop() {
    if (hasWifi) server.handleClient();
    
    processLoRaRX();
    processRxQueue();        
    manageTransmissions();   
    cleanupCache();          

    // RX LED Timer (5 seconds)
    if (ledRxActive && millis() - ledRxTimer >= 5000) {
        ledRxActive = false;
        digitalWrite(LED_PIN, HIGH);
    }

    // Sensor Update Loop
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        if (isCalibrating) {
            calibrateSensor();
        } else {
            readMPU6050Data();
            controlLED();
            
            // TX Logic Trigger
            if (angleY > 65.0 && millis() - lastAngleTx > 5000) { // 5s debounce
                lastAngleTx = millis();
                
                uint32_t newId = millis() + random(1000);
                outgoingData.msgType = 0; 
                outgoingData.msgId = newId;
                memcpy(outgoingData.originalSender, myMac, 6);
                memcpy(outgoingData.immediateSender, myMac, 6);
                
                String jsonStr = "{\"angleY\":" + String(angleY, 1) + "}";
                strncpy(outgoingData.payload, jsonStr.c_str(), sizeof(outgoingData.payload));
                
                messageCache[newId].ts = millis();
                memcpy(messageCache[newId].senderMac, myMac, 6);
                cacheNeedsSave = true; 
                
                addLog("TRIGGER: Auto TX AngleY=" + String(angleY, 1));
                startTransmission(outgoingData);
            }
        }
        lastUpdate = millis();
    }
}

// =====================================================================
// INITIALIZATION FUNCTIONS
// =====================================================================

void initMPU6050() {
    if (!mpu.begin()) {
        Serial.println("❌ MPU6050 NOT FOUND!");
        while (1) delay(1000);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/angles", HTTP_GET, handleAnglesData);
    server.on("/api/raw", HTTP_GET, handleRawData);
    server.on("/api/led", HTTP_GET, handleLEDData);
    server.on("/api/send", HTTP_POST, handleSend);
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/history", HTTP_GET, handleHistory);
}

// =====================================================================
// SENSOR READING & CALIBRATION
// =====================================================================

void calibrateSensor() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    calibrationGyroX += (g.gyro.x * 180.0) / PI;
    calibrationGyroY += (g.gyro.y * 180.0) / PI;
    calibrationCount++;
    
    if (calibrationCount >= CALIBRATION_SAMPLES) {
        gyroBiasX = calibrationGyroX / CALIBRATION_SAMPLES;
        gyroBiasY = calibrationGyroY / CALIBRATION_SAMPLES;
        isCalibrating = false;
        
        mpu.getEvent(&a, &g, &temp);
        accelX = a.acceleration.x / 9.81;
        accelY = a.acceleration.y / 9.81;
        accelZ = a.acceleration.z / 9.81;
        angleX = atan2(accelY, accelZ) * 180.0 / PI;
        angleY = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
        lastComputeTime = millis();
    }
}

void readMPU6050Data() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    unsigned long now = millis();
    float dt = (now - lastComputeTime) / 1000.0;
    if(lastComputeTime == 0) dt = UPDATE_INTERVAL / 1000.0;
    lastComputeTime = now;

    float rawAccelX = a.acceleration.x;
    float rawAccelY = a.acceleration.y;
    float rawAccelZ = a.acceleration.z;
    
    accelX = rawAccelX / 9.81;
    accelY = rawAccelY / 9.81;
    accelZ = rawAccelZ / 9.81;
    
    gyroX = (g.gyro.x * 180.0) / PI - gyroBiasX;
    gyroY = (g.gyro.y * 180.0) / PI - gyroBiasY;
    
    float accelAngleX = atan2(rawAccelY, rawAccelZ) * 180.0 / PI; 
    float accelAngleY = atan2(-rawAccelX, sqrt(rawAccelY * rawAccelY + rawAccelZ * rawAccelZ)) * 180.0 / PI; 
    
    float gyroAngleX = angleX + gyroX * dt;
    float gyroAngleY = angleY + gyroY * dt;
    
    angleX = ALPHA * gyroAngleX + (1.0 - ALPHA) * accelAngleX;
    angleY = ALPHA * gyroAngleY + (1.0 - ALPHA) * accelAngleY;
    
    if (angleX > 180) angleX -= 360;
    if (angleX < -180) angleX += 360;
    if (angleY > 180) angleY -= 360;
    if (angleY < -180) angleY += 360;
}

void controlLED() {
    if (ledRxActive) return; // RX timer takes priority

    if (angleY >= 65 && angleY <= 90) {
        ledActive = true;
        float angleRange = angleY - 65.0;
        float brightnessPercent = (angleRange / 25.0) * 100.0;
        ledBrightness = (int)(brightnessPercent * 2.55);
        analogWrite(LED_PIN, 255 - ledBrightness); 
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
// LORA MESH NETWORKING
// =====================================================================

String macToString(const uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

void addLog(String message) {
    String timeStr = String(millis());
    systemLog = "[" + timeStr + "ms] " + message + "\n" + systemLog;
    if (systemLog.length() > 2000) systemLog = systemLog.substring(0, 2000); 
    Serial.println(message);
}

void saveCacheToSPIFFS() {
    File f = SPIFFS.open("/cache.txt", "w");
    if (!f) return;
    unsigned long now = millis();
    for (auto const& x : messageCache) {
        unsigned long age = now - x.second.ts;
        f.printf("%u,%lu\n", x.first, age); 
    }
    f.close();
}

void loadCacheFromSPIFFS() {
    if (!SPIFFS.exists("/cache.txt")) return;
    File f = SPIFFS.open("/cache.txt", "r");
    if (!f) return;
    unsigned long now = millis();
    while (f.available()) {
        String line = f.readStringUntil('\n');
        int separator = line.indexOf(',');
        if (separator > 0) {
            uint32_t id = line.substring(0, separator).toInt();
            unsigned long ts = line.substring(separator + 1).toInt();
            if (ts < DEDUPLICATION_WINDOW_MS) { 
                messageCache[id].ts = now - ts; 
                memset(messageCache[id].senderMac, 0, 6);
            }
        }
    }
    f.close();
}

void cleanupCache() {
    unsigned long now = millis();
    if (now - lastCacheCleanup > 10000) { 
        bool changed = false;
        for (auto it = messageCache.begin(); it != messageCache.end(); ) {
            if (now - it->second.ts > DEDUPLICATION_WINDOW_MS) {
                it = messageCache.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed || cacheNeedsSave) {
            saveCacheToSPIFFS();
            cacheNeedsSave = false;
        }
        lastCacheCleanup = now;
    }
}

void loRaBroadcast(struct_message msg) {
    LoRa.beginPacket();
    LoRa.write((uint8_t*)&msg, sizeof(struct_message));
    LoRa.endPacket();
}

void processLoRaRX() {
    int packetSize = LoRa.parsePacket();
    if (packetSize == sizeof(struct_message)) {
        struct_message incoming;
        LoRa.readBytes((uint8_t*)&incoming, sizeof(struct_message));

        int nextHead = (rxHead + 1) % RX_QUEUE_SIZE;
        if (nextHead != rxTail) {
            memcpy(rxQueue[rxHead].mac, incoming.immediateSender, 6);
            rxQueue[rxHead].msg = incoming;
            rxHead = nextHead;
        }
    }
}

void startTransmission(struct_message msg) {
    for (int i = 0; i < MAX_TX_JOBS; i++) {
        if (!txJobs[i].active) {
            txJobs[i].msg = msg;
            txJobs[i].firstSendTime = millis();
            txJobs[i].lastSendTime = 0;
            txJobs[i].retryInterval = 200; 
            txJobs[i].nextJitter = random(10, 50); 
            txJobs[i].active = true;
            return;
        }
    }
}

void manageTransmissions() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_TX_JOBS; i++) {
        if (txJobs[i].active) {
            if (now - txJobs[i].firstSendTime > MAX_BROADCAST_DURATION) {
                txJobs[i].active = false;
                continue;
            }
            if (txJobs[i].lastSendTime == 0 && now >= txJobs[i].firstSendTime + txJobs[i].nextJitter) {
                loRaBroadcast(txJobs[i].msg);
                txJobs[i].lastSendTime = now;
                totalRelayed++;
            } 
            else if (txJobs[i].lastSendTime > 0 && now - txJobs[i].lastSendTime >= txJobs[i].retryInterval) {
                loRaBroadcast(txJobs[i].msg);
                txJobs[i].lastSendTime = now;
                totalRelayed++;
                
                unsigned long newInterval = txJobs[i].retryInterval * 2;
                txJobs[i].retryInterval = (newInterval > 2000) ? 2000 : newInterval;
            }
        }
    }
}

void sendAck(uint8_t* targetMac, uint32_t msgId) {
    struct_message ackMsg;
    ackMsg.msgType = 1; 
    ackMsg.msgId = msgId;
    memcpy(ackMsg.originalSender, myMac, 6);
    memcpy(ackMsg.immediateSender, myMac, 6);
    loRaBroadcast(ackMsg);
}

void processRxQueue() {
    while (rxHead != rxTail) {
        struct_message incoming = rxQueue[rxTail].msg;
        uint8_t* mac = rxQueue[rxTail].mac; 
        rxTail = (rxTail + 1) % RX_QUEUE_SIZE;
        
        String senderMAC = macToString(mac);
        uint32_t msgId = incoming.msgId;
        
        // --- HANDLE ACKs ---
        if (incoming.msgType == 1) { 
            for (int i = 0; i < MAX_TX_JOBS; i++) {
                if (txJobs[i].active && txJobs[i].msg.msgId == msgId) {
                    txJobs[i].active = false;
                }
            }
            continue; 
        }

        // --- HANDLE DATA MESSAGES ---
        if (messageCache.find(msgId) != messageCache.end()) {
            totalDropped++;
            bool sameSender = true;
            for(int i=0; i<6; i++) {
                if (mac[i] != messageCache[msgId].senderMac[i]) {
                    sameSender = false; break;
                }
            }
            if (sameSender) sendAck(mac, msgId);
            continue;
        }
        
        // Take Ownership
        sendAck(mac, msgId);
        totalReceived++;
        
        // Activate Local LED (5 Seconds, Priority)
        ledRxActive = true;
        ledRxTimer = millis();
        digitalWrite(LED_PIN, LOW);
        
        messageCache[msgId].ts = millis();
        memcpy(messageCache[msgId].senderMac, mac, 6); 
        cacheNeedsSave = true; 
        
        String newEntry = "{\"id\":\"" + String(msgId) + "\", \"payload\":\"" + String(incoming.payload) + "\"}";
        historyList.push_front(newEntry);
        if (historyList.size() > MAX_HISTORY_ITEMS) historyList.pop_back();
        
        memcpy(incoming.immediateSender, myMac, 6);
        startTransmission(incoming);
    }
}

// =====================================================================
// WEB SERVER HANDLERS & HTML
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
  String json = "{\"ledActive\":" + String(ledActive ? "true" : "false") +
            ",\"ledBrightness\":" + String(ledBrightness) +
   ",\"angleY\":" + String(angleY, 2) +
          ",\"ledMinAngle\":65,\"ledMaxAngle\":90}";
   server.send(200, "application/json", json);
}

void handleSend() {
    uint32_t newId = millis() + random(1000); 
    outgoingData.msgType = 0; 
    outgoingData.msgId = newId;
    memcpy(outgoingData.originalSender, myMac, 6);
    memcpy(outgoingData.immediateSender, myMac, 6);
    strncpy(outgoingData.payload, "Manual UI Trigger", sizeof(outgoingData.payload));
    
    messageCache[newId].ts = millis();
    memcpy(messageCache[newId].senderMac, myMac, 6);
    cacheNeedsSave = true; 
    
    startTransmission(outgoingData);
    server.send(200, "text/plain", "Broadcast packet queued successfully!");
}

void handleStats() {
    String json = "{";
    json += "\"received\":" + String(totalReceived) + ",";
    json += "\"relayed\":" + String(totalRelayed) + ",";
    json += "\"dropped\":" + String(totalDropped) + ",";
    String escapedLog = systemLog;
    escapedLog.replace("\"", "\\\"");
    escapedLog.replace("\n", "\\n");
    escapedLog.replace("\r", "");
    json += "\"log\":\"" + escapedLog + "\"}";
    server.send(200, "application/json", json);
}

void handleHistory() {
    String json = "[";
    bool first = true;
    for (const String& entry : historyList) {
        if (!first) json += ",";
        json += entry;
        first = false;
    }
    json += "]";
    server.send(200, "application/json", json);
}

String getHTMLPage() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Integrated MPU & LoRa Mesh</title>
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
            max-width: 1300px;
            width: 100%;
        }
        h1 { text-align: center; color: #333; margin-bottom: 10px; font-size: 2.2em; }
        .subtitle { text-align: center; color: #666; margin-bottom: 20px; font-size: 0.9em; }
        .wrapper {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 20px;
        }
        #glContainer {
            grid-column: span 2;
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
        .panel h2 { margin-bottom: 15px; color: #333; border-bottom: 3px solid #667eea; padding-bottom: 10px; }
        .data-section h3 { color: #667eea; font-size: 0.9em; margin-bottom: 10px; text-transform: uppercase; letter-spacing: 1px; }
        .data-row {
            display: flex; justify-content: space-between; padding: 10px;
            background: white; margin-bottom: 8px; border-radius: 6px; border-left: 4px solid #667eea;
        }
        .label { font-weight: 600; color: #555; }
        .value { color: #667eea; font-weight: bold; font-family: 'Courier New', monospace; }
        
        .mesh-data { background: #222; color: #0f0; font-family: monospace; padding: 10px; border-radius: 6px; height: 120px; overflow-y: auto; margin-bottom: 15px;}
        button { background: #667eea; color: white; border: none; padding: 10px; border-radius: 5px; cursor: pointer; width: 100%; font-weight: bold;}
        button:hover { background: #764ba2; }
        @media (max-width: 900px) {
            .wrapper { grid-template-columns: 1fr; }
            #glContainer { grid-column: span 1; height: 400px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎲 MPU6050 & LoRa Mesh UI</h1>
        <p class="subtitle">Auto TX on Y-Angle > 65° | RX Active-Low LED for 5s</p>
        
        <div class="wrapper">
            <div id="glContainer"></div>
            
            <div class="panel">
                <h2>📊 MPU Data</h2>
                <div class="data-section">
                    <div class="data-row"><span class="label">Angle X:</span><span class="value" id="angleX">0.0°</span></div>
                    <div class="data-row"><span class="label">Angle Y:</span><span class="value" id="angleY">0.0°</span></div>
                </div>
                
                <h2 style="margin-top:20px;">🌐 Mesh Network</h2>
                <div class="data-section">
                    <div class="data-row"><span class="label">Received:</span><span class="value" id="recv">0</span></div>
                    <div class="data-row"><span class="label">Relayed:</span><span class="value" id="relayed">0</span></div>
                    <div class="data-row"><span class="label">Dropped:</span><span class="value" id="dropped">0</span></div>
                </div>
                
                <h3>Live Log</h3>
                <div class="mesh-data" id="log"></div>
                
                <h3>Payload History</h3>
                <div class="mesh-data" id="history" style="color:white; background:#333;"></div>
                
                <button onclick="triggerBroadcast()">Send Manual Mesh Packet</button>
            </div>
        </div>
    </div>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <script>
        // --- THREE.JS INIT ---
        const container = document.getElementById('glContainer');
        const scene = new THREE.Scene();
        scene.background = new THREE.Color(0xf5f5f5);
        const camera = new THREE.PerspectiveCamera(75, container.clientWidth / container.clientHeight, 0.1, 1000);
        camera.position.set(0, 0, 5);
        
        const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        renderer.setSize(container.clientWidth, container.clientHeight);
        renderer.shadowMap.enabled = true;
        container.appendChild(renderer.domElement);
        
        const light1 = new THREE.DirectionalLight(0xffffff, 1);
        light1.position.set(5, 5, 5);
        light1.castShadow = true;
        scene.add(light1);
        scene.add(new THREE.AmbientLight(0xffffff, 0.7));
        
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
        
        window.addEventListener('resize', () => {
            camera.aspect = container.clientWidth / container.clientHeight;
            camera.updateProjectionMatrix();
            renderer.setSize(container.clientWidth, container.clientHeight);
        });
        // --- DATA POLLING ---
        async function updateMPU() {
            try {
                const res = await fetch('/api/angles');
                const data = await res.json();
                document.getElementById('angleX').textContent = data.angleX.toFixed(1) + '°';
                document.getElementById('angleY').textContent = data.angleY.toFixed(1) + '°';
                
                dice.rotation.order = 'YXZ';
                dice.rotation.y = (data.angleX * Math.PI) / 180;
                dice.rotation.x = (data.angleY * Math.PI) / 180;
            } catch (e) {}
        }
        
        async function updateMesh() {
            try {
                const sRes = await fetch('/api/stats');
                const sData = await sRes.json();
                document.getElementById('recv').innerText = sData.received;
                document.getElementById('relayed').innerText = sData.relayed;
                document.getElementById('dropped').innerText = sData.dropped;
                document.getElementById('log').innerText = sData.log;
                const hRes = await fetch('/api/history');
                const hData = await hRes.json();
                document.getElementById('history').innerText = JSON.stringify(hData, null, 2);
            } catch (e) {}
        }
        function triggerBroadcast() {
            fetch('/api/send', {method: 'POST'}).then(res => res.text()).then(alert);
        }
        
        function animate() {
            requestAnimationFrame(animate);
            renderer.render(scene, camera);
        }
        
        animate();
        setInterval(updateMPU, 50);
        setInterval(updateMesh, 1500);
    </script>
</body>
</html>
)rawliteral";
}
