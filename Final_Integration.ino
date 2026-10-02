#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ==========================
// Configuration
// ==========================
const char* WIFI_SSID = "Airtel_Zerotouch";
const char* WIFI_PASSWORD = "Airtel@123";

const uint8_t LED_PIN = D4;
const uint8_t I2C_SDA_PIN = D2;
const uint8_t I2C_SCL_PIN = D1;

const uint8_t LORA_NSS_PIN = D8;
const uint8_t LORA_RST_PIN = D0;
const uint8_t LORA_DIO0_PIN = D3;

const long LORA_FREQUENCY = 433E6;
const float TRIGGER_ANGLE_Y = 65.0f;
const float RETRIGGER_ANGLE_Y = 60.0f;
const unsigned long WIFI_TIMEOUT_MS = 15000UL;
const unsigned long LED_ON_TIME_MS = 5000UL;
const unsigned long SENSOR_UPDATE_MS = 10UL;
const unsigned long LORA_REINIT_INTERVAL_MS = 10000UL;

ESP8266WebServer server(80);
Adafruit_MPU6050 mpu;

// ==========================
// Sensor State
// ==========================
float accelX = 0.0f;
float accelY = 0.0f;
float accelZ = 0.0f;
float gyroX = 0.0f;
float gyroY = 0.0f;
float angleX = 0.0f;
float angleY = 0.0f;
float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
bool isCalibrating = true;
int calibrationCount = 0;
const int CALIBRATION_SAMPLES = 300;
float calibrationGyroX = 0.0f;
float calibrationGyroY = 0.0f;
const float COMPLEMENTARY_ALPHA = 0.96f;

// ==========================
// Communication State
// ==========================
bool wifiConnected = false;
bool webServerEnabled = false;
bool loraReady = false;
bool triggerLatched = false;
bool ledForcedOn = false;
unsigned long ledOffAt = 0;
unsigned long lastSensorUpdate = 0;
unsigned long lastComputeTime = 0;
unsigned long lastLoRaInitAttempt = 0;
unsigned long txCount = 0;
unsigned long rxCount = 0;
unsigned long packetSequence = 0;
String deviceId;
String lastTxPayload = "";
String lastRxPayload = "";
String lastRxSender = "";
String lastRxMsgId = "";
String lastRxAngleX = "";
String lastRxAngleY = "";
String statusLog = "";

struct PacketSnapshot {
  String sender;
  String msgId;
  String payload;
  String angleX;
  String angleY;
  unsigned long receivedAt;
};

PacketSnapshot recentPackets[8];
uint8_t recentPacketCount = 0;

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      continue;
    } else {
      out += c;
    }
  }
  return out;
}

void addLog(const String& message) {
  String line = "[" + String(millis()) + "ms] " + message;
  statusLog = line + "\n" + statusLog;
  if (statusLog.length() > 2600) {
    statusLog = statusLog.substring(0, 2600);
  }
  Serial.println(line);
}

String buildDeviceId() {
  return String("ESP-") + String(ESP.getChipId(), HEX);
}

String extractJsonStringValue(const String& json, const String& key, const String& fallback = "") {
  String token = "\"" + key + "\"";
  int pos = json.indexOf(token);
  if (pos < 0) return fallback;
  pos = json.indexOf(':', pos);
  if (pos < 0) return fallback;
  pos++;
  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= (int)json.length() || json[pos] != '"') return fallback;
  pos++;
  int end = pos;
  String value;
  while (end < (int)json.length()) {
    char c = json[end];
    if (c == '"' && json[end - 1] != '\\') break;
    value += c;
    end++;
  }
  return value;
}

float extractJsonFloatValue(const String& json, const String& key, float fallback = 0.0f) {
  String token = "\"" + key + "\"";
  int pos = json.indexOf(token);
  if (pos < 0) return fallback;
  pos = json.indexOf(':', pos);
  if (pos < 0) return fallback;
  pos++;
  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  int end = pos;
  while (end < (int)json.length()) {
    char c = json[end];
    if (!(isDigit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
    end++;
  }
  if (end <= pos) return fallback;
  return json.substring(pos, end).toFloat();
}

String buildSensorPayload(bool triggered) {
  String json = "{";
  json += "\"deviceId\":\"" + deviceId + "\",";
  json += "\"msgId\":\"" + String(millis()) + "-" + String(txCount + 1) + "\",";
  json += "\"angleX\":" + String(angleX, 2) + ",";
  json += "\"angleY\":" + String(angleY, 2) + ",";
  json += "\"accelX\":" + String(accelX, 3) + ",";
  json += "\"accelY\":" + String(accelY, 3) + ",";
  json += "\"accelZ\":" + String(accelZ, 3) + ",";
  json += "\"triggered\":" + String(triggered ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis());
  json += "}";
  return json;
}

void pushRecentPacket(const String& sender, const String& msgId, const String& payload, const String& rxAngleX, const String& rxAngleY) {
  for (int i = 7; i > 0; --i) {
    recentPackets[i] = recentPackets[i - 1];
  }
  recentPackets[0] = { sender, msgId, payload, rxAngleX, rxAngleY, millis() };
  if (recentPacketCount < 8) recentPacketCount++;
}

void initMPU6050() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(50);
  if (!mpu.begin()) {
    addLog("MPU6050 not found. Check I2C wiring.");
    while (true) {
      delay(1000);
      yield();
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
  mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  addLog("MPU6050 initialized.");
}

bool initLoRa() {
  SPI.begin();
  LoRa.setPins(LORA_NSS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    addLog("LoRa init failed.");
    return false;
  }
  LoRa.enableCrc();
  LoRa.receive();
  addLog("LoRa ready at 433 MHz.");
  return true;
}

void connectWiFiWithTimeout() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  addLog(String("Connecting WiFi: ") + WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(250);
    yield();
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    addLog(String("WiFi connected: ") + WiFi.localIP().toString());
  } else {
    addLog("WiFi timeout. Continuing without web server.");
  }
}

void handleRoot();
void handleState();
void handleHistory();

void setupWebServer() {
  if (!wifiConnected) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.begin();
  webServerEnabled = true;
  addLog("Web server started.");
}

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html; charset=utf-8", R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>MPU6050 + LoRa Integration</title>
  <style>
    :root {
      --bg-1: #07111f;
      --bg-2: #0d2038;
      --panel: rgba(10, 18, 33, 0.72);
      --line: rgba(122, 197, 255, 0.22);
      --accent: #77d5ff;
      --accent-2: #49ffa7;
      --text: #e9f4ff;
      --muted: #9fb4c7;
      --danger: #ff7070;
    }
    * { box-sizing: border-box; }
    html { scroll-behavior: smooth; }
    body {
      margin: 0;
      font-family: Inter, Segoe UI, Arial, sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(119, 213, 255, 0.16), transparent 30%),
        radial-gradient(circle at right center, rgba(73, 255, 167, 0.12), transparent 28%),
        linear-gradient(180deg, var(--bg-1), var(--bg-2));
      overflow-x: hidden;
    }
    .section { position: relative; width: 100%; min-height: 100vh; }
    .hero {
      display: grid;
      grid-template-columns: 1.2fr 0.8fr;
      gap: 24px;
      align-items: center;
      padding: 36px;
    }
    .hero-copy { max-width: 620px; z-index: 2; }
    .eyebrow {
      display: inline-flex;
      align-items: center;
      gap: 10px;
      padding: 8px 14px;
      border: 1px solid var(--line);
      border-radius: 999px;
      color: var(--accent);
      background: rgba(255,255,255,0.04);
      backdrop-filter: blur(10px);
      letter-spacing: 0.14em;
      text-transform: uppercase;
      font-size: 0.72rem;
    }
    h1 {
      margin: 18px 0 10px;
      font-size: clamp(2.8rem, 6vw, 5.6rem);
      line-height: 0.95;
      letter-spacing: -0.05em;
    }
    .lead { margin: 0; color: var(--muted); font-size: 1.05rem; max-width: 58ch; line-height: 1.7; }
    .stats { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 14px; margin-top: 24px; }
    .stat { padding: 16px; border-radius: 18px; background: var(--panel); border: 1px solid var(--line); }
    .stat .label { color: var(--muted); font-size: 0.82rem; }
    .stat .value { display: block; margin-top: 8px; font-size: 1.55rem; font-weight: 700; }
    .viewer-shell {
      position: relative;
      min-height: 82vh;
      border-radius: 30px;
      background: linear-gradient(180deg, rgba(119,213,255,0.08), rgba(255,255,255,0.02)), rgba(5, 11, 21, 0.68);
      border: 1px solid var(--line);
      overflow: hidden;
    }
    #viewer { width: 100%; height: 82vh; min-height: 620px; }
    .floating-card {
      position: absolute; inset: auto 18px 18px 18px; display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; pointer-events: none;
    }
    .chip-card { padding: 14px 16px; border-radius: 18px; background: rgba(3, 8, 15, 0.72); border: 1px solid rgba(119,213,255,0.18); }
    .chip-card small { color: var(--muted); display: block; margin-bottom: 6px; }
    .chip-card strong { font-size: 1.05rem; }
    .scroll-hint { margin-top: 24px; color: var(--accent-2); font-size: 0.95rem; letter-spacing: 0.08em; text-transform: uppercase; }
    .comm { padding: 34px 36px 60px; background: linear-gradient(180deg, rgba(4, 10, 18, 0.18), rgba(0,0,0,0.34)); border-top: 1px solid rgba(255,255,255,0.06); }
    .comm-head { display: flex; align-items: flex-end; justify-content: space-between; gap: 18px; margin-bottom: 22px; }
    .comm h2 { margin: 0; font-size: clamp(2rem, 4vw, 3.2rem); letter-spacing: -0.04em; }
    .comm-sub { margin: 0; color: var(--muted); max-width: 56ch; }
    .dashboard { display: grid; grid-template-columns: 1.1fr 0.9fr; gap: 18px; }
    .panel { padding: 18px; border-radius: 22px; background: var(--panel); border: 1px solid var(--line); }
    .panel h3 { margin: 0 0 14px; font-size: 1.05rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--accent); }
    .kv { display: grid; grid-template-columns: 1fr auto; gap: 10px; padding: 10px 0; border-bottom: 1px solid rgba(255,255,255,0.07); }
    .kv:last-child { border-bottom: 0; }
    .kv span:first-child { color: var(--muted); }
    .kv span:last-child { font-weight: 700; }
    .feed { display: grid; gap: 12px; }
    .feed-item { padding: 14px; border-radius: 16px; background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.06); }
    .feed-item .meta { color: var(--muted); font-size: 0.78rem; margin-bottom: 8px; }
    .feed-item .body { word-break: break-word; white-space: pre-wrap; color: #eef7ff; }
    .stepbox { display: grid; gap: 8px; margin-top: 12px; }
    .step { padding: 12px; border-radius: 14px; background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.06); color: var(--muted); }
    .step.active { color: var(--text); border-color: rgba(73,255,167,0.35); }
    .badge { display: inline-flex; align-items: center; gap: 8px; padding: 8px 12px; border-radius: 999px; background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.08); color: var(--text); font-size: 0.85rem; }
    .dot { width: 10px; height: 10px; border-radius: 50%; background: var(--danger); box-shadow: 0 0 14px rgba(255,112,112,0.55); }
    .dot.ok { background: var(--accent-2); box-shadow: 0 0 14px rgba(73,255,167,0.55); }
    @media (max-width: 1024px) {
      .hero, .dashboard { grid-template-columns: 1fr; }
      .viewer-shell, #viewer { min-height: 520px; height: 66vh; }
      .stats { grid-template-columns: 1fr; }
      .comm-head { align-items: flex-start; flex-direction: column; }
    }
  </style>
</head>
<body>
  <section class="section hero">
    <div class="hero-copy">
      <span class="eyebrow">MPU6050 + SX1278 LoRa + WiFi dashboard</span>
      <h1>Live motion, wireless trigger, and dashboard feedback in one sketch.</h1>
      <p class="lead">The first screen is a full-height 3D MPU6050 view with richer visuals and live rotation from the sensor. Scroll down for communication status, trigger events, received payloads, and WiFi/LoRa health.</p>
      <div class="stats">
        <div class="stat"><span class="label">Angle X</span><span class="value" id="uiAngleX">0.0°</span></div>
        <div class="stat"><span class="label">Angle Y</span><span class="value" id="uiAngleY">0.0°</span></div>
        <div class="stat"><span class="label">Trigger</span><span class="value" id="uiTrigger">Idle</span></div>
      </div>
      <div class="scroll-hint">Scroll for communication status</div>
    </div>
    <div class="viewer-shell">
      <div id="viewer"></div>
      <div class="floating-card">
        <div class="chip-card"><small>WiFi</small><strong id="uiWifi">Pending</strong></div>
        <div class="chip-card"><small>LoRa</small><strong id="uiLoRa">Pending</strong></div>
      </div>
    </div>
  </section>
  <section class="section comm">
    <div class="comm-head">
      <div>
        <h2>Communication status</h2>
        <p class="comm-sub">This section updates as packets are sent, received, and rendered step-by-step on the dashboard.</p>
      </div>
      <div class="badge"><span class="dot" id="uiLinkDot"></span><span id="uiLinkState">Waiting for data</span></div>
    </div>
    <div class="dashboard">
      <div class="panel">
        <h3>System status</h3>
        <div class="kv"><span>Device</span><span id="uiDeviceId">-</span></div>
        <div class="kv"><span>TX count</span><span id="uiTxCount">0</span></div>
        <div class="kv"><span>RX count</span><span id="uiRxCount">0</span></div>
        <div class="kv"><span>Last TX payload</span><span id="uiLastTx">-</span></div>
        <div class="kv"><span>Last RX payload</span><span id="uiLastRx">-</span></div>
        <div class="kv"><span>LED hold</span><span id="uiLedHold">0 ms</span></div>
        <div class="stepbox">
          <div class="step" id="step-1">1. Packet detected</div>
          <div class="step" id="step-2">2. Sender and ID decoded</div>
          <div class="step" id="step-3">3. Angle values applied to the dashboard</div>
        </div>
      </div>
      <div class="panel">
        <h3>Packet feed</h3>
        <div class="feed" id="feed"></div>
      </div>
    </div>
  </section>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
  <script>
    const viewer = document.getElementById('viewer');
    const scene = new THREE.Scene();
    scene.fog = new THREE.Fog(0x07111f, 8, 28);
    const camera = new THREE.PerspectiveCamera(42, viewer.clientWidth / viewer.clientHeight, 0.1, 100);
    camera.position.set(0, 3.2, 8.2);
    camera.lookAt(0, 0, 0);
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setPixelRatio(window.devicePixelRatio || 1);
    renderer.setSize(viewer.clientWidth, viewer.clientHeight);
    renderer.shadowMap.enabled = true;
    viewer.appendChild(renderer.domElement);
    scene.add(new THREE.AmbientLight(0xbadfff, 1.2));
    const keyLight = new THREE.DirectionalLight(0xffffff, 1.8);
    keyLight.position.set(5, 8, 6);
    keyLight.castShadow = true;
    scene.add(keyLight);
    const fillLight = new THREE.PointLight(0x49ffa7, 1.5, 35);
    fillLight.position.set(-3, 2, 4);
    scene.add(fillLight);
    const board = new THREE.Group();
    const pcb = new THREE.Mesh(new THREE.BoxGeometry(3.8, 0.18, 2.2), new THREE.MeshStandardMaterial({ color: 0x1d7a63, metalness: 0.15, roughness: 0.7 }));
    pcb.receiveShadow = true;
    pcb.castShadow = true;
    board.add(pcb);
    const boardEdge = new THREE.Mesh(new THREE.BoxGeometry(3.95, 0.12, 2.35), new THREE.MeshStandardMaterial({ color: 0x0d3d32, metalness: 0.1, roughness: 0.8, transparent: true, opacity: 0.35 }));
    board.add(boardEdge);
    const chip = new THREE.Mesh(new THREE.BoxGeometry(1.0, 0.26, 0.72), new THREE.MeshStandardMaterial({ color: 0x151b2d, metalness: 0.2, roughness: 0.45 }));
    chip.position.set(0, 0.18, 0);
    chip.castShadow = true;
    board.add(chip);
    const chipTop = new THREE.Mesh(new THREE.BoxGeometry(0.72, 0.12, 0.42), new THREE.MeshStandardMaterial({ color: 0x2f3550, metalness: 0.35, roughness: 0.28, emissive: 0x0a1322, emissiveIntensity: 0.25 }));
    chipTop.position.set(0, 0.28, 0);
    board.add(chipTop);
    const sensorPadMaterial = new THREE.MeshStandardMaterial({ color: 0xffcf5c, metalness: 0.5, roughness: 0.25 });
    for (let i = 0; i < 4; i++) {
      const pad = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.03, 0.24), sensorPadMaterial);
      pad.position.set(-1.5 + i * 1.0, -0.06, 1.02);
      board.add(pad);
    }
    for (let i = 0; i < 4; i++) {
      const pad = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.03, 0.24), sensorPadMaterial);
      pad.position.set(-1.5 + i * 1.0, -0.06, -1.02);
      board.add(pad);
    }
    const ledRing = new THREE.Mesh(new THREE.TorusGeometry(0.55, 0.08, 12, 36), new THREE.MeshStandardMaterial({ color: 0x77d5ff, emissive: 0x77d5ff, emissiveIntensity: 0.45, roughness: 0.2 }));
    ledRing.rotation.x = Math.PI / 2;
    ledRing.position.set(1.2, 0.25, 0.1);
    board.add(ledRing);
    const orbit = new THREE.Mesh(new THREE.TorusGeometry(3.8, 0.02, 8, 120), new THREE.MeshStandardMaterial({ color: 0x77d5ff, emissive: 0x77d5ff, emissiveIntensity: 0.22, transparent: true, opacity: 0.4 }));
    orbit.rotation.x = Math.PI / 2;
    orbit.position.y = -0.55;
    scene.add(orbit);
    const glow = new THREE.PointLight(0x77d5ff, 2.2, 20);
    glow.position.set(0, 2.3, 3.5);
    scene.add(glow);
    board.rotation.x = -0.18;
    board.rotation.y = 0.55;
    board.position.y = 0.4;
    scene.add(board);
    const grid = new THREE.GridHelper(14, 28, 0x244567, 0x17334f);
    grid.position.y = -1.15;
    scene.add(grid);
    function resizeViewer() {
      const width = viewer.clientWidth;
      const height = viewer.clientHeight;
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      renderer.setSize(width, height);
    }
    window.addEventListener('resize', resizeViewer);
    function setStep(step) {
      ['step-1', 'step-2', 'step-3'].forEach((id, index) => {
        document.getElementById(id).classList.toggle('active', index < step);
      });
    }
    let lastSequence = 0;
    let stagedTimer = null;
    function renderFeed(items) {
      const feed = document.getElementById('feed');
      feed.innerHTML = '';
      items.slice(0, 6).forEach(item => {
        const div = document.createElement('div');
        div.className = 'feed-item';
        div.innerHTML = '<div class="meta">' + item.sender + ' | ' + item.msgId + ' | ' + item.receivedAt + '</div><div class="body">' + item.payload + '</div>';
        feed.appendChild(div);
      });
    }
    function stagedUpdate(data) {
      if (stagedTimer) clearTimeout(stagedTimer);
      setStep(1);
      document.getElementById('uiLinkState').textContent = 'Packet received';
      document.getElementById('uiLinkDot').classList.add('ok');
      stagedTimer = setTimeout(() => {
        setStep(2);
        document.getElementById('uiLastRx').textContent = data.lastRxPayload || '-';
        document.getElementById('uiDeviceId').textContent = data.deviceId || '-';
        document.getElementById('uiTxCount').textContent = data.txCount;
        document.getElementById('uiRxCount').textContent = data.rxCount;
        document.getElementById('uiLedHold').textContent = data.ledRemainingMs + ' ms';
        stagedTimer = setTimeout(() => {
          setStep(3);
          document.getElementById('uiLastTx').textContent = data.lastTxPayload || '-';
          document.getElementById('uiWifi').textContent = data.wifiConnected ? 'Connected' : 'Offline';
          document.getElementById('uiLoRa').textContent = data.loraReady ? 'Ready' : 'Idle';
          document.getElementById('uiAngleX').textContent = Number(data.angleX).toFixed(1) + '°';
          document.getElementById('uiAngleY').textContent = Number(data.angleY).toFixed(1) + '°';
          document.getElementById('uiTrigger').textContent = data.triggerLatched ? 'Armed' : 'Idle';
          document.getElementById('uiLinkState').textContent = 'Live';
        }, 220);
      }, 220);
    }
    async function updateState() {
      try {
        const response = await fetch('/api/state', { cache: 'no-store' });
        const data = await response.json();
        if (data.sequence !== lastSequence) {
          lastSequence = data.sequence;
          stagedUpdate(data);
        } else {
          document.getElementById('uiWifi').textContent = data.wifiConnected ? 'Connected' : 'Offline';
          document.getElementById('uiLoRa').textContent = data.loraReady ? 'Ready' : 'Idle';
          document.getElementById('uiDeviceId').textContent = data.deviceId || '-';
          document.getElementById('uiTxCount').textContent = data.txCount;
          document.getElementById('uiRxCount').textContent = data.rxCount;
          document.getElementById('uiLedHold').textContent = data.ledRemainingMs + ' ms';
          document.getElementById('uiAngleX').textContent = Number(data.angleX).toFixed(1) + '°';
          document.getElementById('uiAngleY').textContent = Number(data.angleY).toFixed(1) + '°';
          document.getElementById('uiTrigger').textContent = data.triggerLatched ? 'Armed' : 'Idle';
        }
        document.getElementById('uiLastTx').textContent = data.lastTxPayload || '-';
        document.getElementById('uiLastRx').textContent = data.lastRxPayload || '-';
        document.getElementById('uiLinkDot').classList.toggle('ok', data.wifiConnected || data.loraReady);
        document.getElementById('uiLinkState').textContent = data.wifiConnected ? (data.webServerEnabled ? 'Web dashboard active' : 'WiFi connected') : 'Offline';
        const rxAngleY = Number(data.angleY);
        const rxAngleX = Number(data.angleX);
        board.rotation.x = -0.25 + (rxAngleY * Math.PI / 360.0);
        board.rotation.y = 0.55 + (rxAngleX * Math.PI / 360.0);
        ledRing.material.emissiveIntensity = data.ledActive ? 1.6 : 0.45;
        renderFeed(data.recentPackets || []);
      } catch (error) {
        document.getElementById('uiLinkState').textContent = 'Dashboard unavailable';
        document.getElementById('uiLinkDot').classList.remove('ok');
      }
    }
    function animate() {
      requestAnimationFrame(animate);
      board.rotation.z = Math.sin(Date.now() * 0.0005) * 0.03;
      orbit.rotation.z += 0.002;
      renderer.render(scene, camera);
    }
    animate();
    resizeViewer();
    updateState();
    setInterval(updateState, 400);
  </script>
</body>
</html>
)rawliteral");
}

void handleState() {
  String json = "{";
  json += "\"sequence\":" + String(packetSequence) + ",";
  json += "\"deviceId\":\"" + deviceId + "\",";
  json += "\"wifiConnected\":" + String(wifiConnected ? "true" : "false") + ",";
  json += "\"webServerEnabled\":" + String(webServerEnabled ? "true" : "false") + ",";
  json += "\"loraReady\":" + String(loraReady ? "true" : "false") + ",";
  json += "\"triggerLatched\":" + String(triggerLatched ? "true" : "false") + ",";
  json += "\"ledActive\":" + String(ledForcedOn ? "true" : "false") + ",";
  json += "\"ledRemainingMs\":" + String(ledForcedOn ? (long)(ledOffAt > millis() ? ledOffAt - millis() : 0) : 0) + ",";
  json += "\"txCount\":" + String(txCount) + ",";
  json += "\"rxCount\":" + String(rxCount) + ",";
  json += "\"angleX\":" + String(angleX, 2) + ",";
  json += "\"angleY\":" + String(angleY, 2) + ",";
  json += "\"lastTxPayload\":\"" + htmlEscape(lastTxPayload) + "\",";
  json += "\"lastRxPayload\":\"" + htmlEscape(lastRxPayload) + "\",";
  json += "\"lastRxSender\":\"" + htmlEscape(lastRxSender) + "\",";
  json += "\"lastRxMsgId\":\"" + htmlEscape(lastRxMsgId) + "\",";
  json += "\"recentPackets\":[";
  for (uint8_t i = 0; i < recentPacketCount; ++i) {
    if (i) json += ",";
    json += "{";
    json += "\"sender\":\"" + htmlEscape(recentPackets[i].sender) + "\",";
    json += "\"msgId\":\"" + htmlEscape(recentPackets[i].msgId) + "\",";
    json += "\"payload\":\"" + htmlEscape(recentPackets[i].payload) + "\",";
    json += "\"angleX\":\"" + htmlEscape(recentPackets[i].angleX) + "\",";
    json += "\"angleY\":\"" + htmlEscape(recentPackets[i].angleY) + "\",";
    json += "\"receivedAt\":\"" + String(recentPackets[i].receivedAt) + "ms\"";
    json += "}";
  }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json; charset=utf-8", json);
}

void handleHistory() {
  String json = "[";
  for (uint8_t i = 0; i < recentPacketCount; ++i) {
    if (i) json += ",";
    json += "{";
    json += "\"sender\":\"" + htmlEscape(recentPackets[i].sender) + "\",";
    json += "\"msgId\":\"" + htmlEscape(recentPackets[i].msgId) + "\",";
    json += "\"payload\":\"" + htmlEscape(recentPackets[i].payload) + "\"";
    json += "}";
  }
  json += "]";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json; charset=utf-8", json);
}

void calibrateSensor() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  calibrationGyroX += (g.gyro.x * 180.0f) / PI;
  calibrationGyroY += (g.gyro.y * 180.0f) / PI;
  calibrationCount++;
  if (calibrationCount >= CALIBRATION_SAMPLES) {
    gyroBiasX = calibrationGyroX / CALIBRATION_SAMPLES;
    gyroBiasY = calibrationGyroY / CALIBRATION_SAMPLES;
    isCalibrating = false;
    accelX = a.acceleration.x / 9.81f;
    accelY = a.acceleration.y / 9.81f;
    accelZ = a.acceleration.z / 9.81f;
    angleX = atan2(accelY, accelZ) * 180.0f / PI;
    angleY = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0f / PI;
    lastComputeTime = millis();
    addLog("Calibration complete.");
  }
}

void readMPU6050Data() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  unsigned long now = millis();
  float dt = (lastComputeTime == 0) ? (SENSOR_UPDATE_MS / 1000.0f) : ((now - lastComputeTime) / 1000.0f);
  lastComputeTime = now;
  float rawAccelX = a.acceleration.x;
  float rawAccelY = a.acceleration.y;
  float rawAccelZ = a.acceleration.z;
  accelX = rawAccelX / 9.81f;
  accelY = rawAccelY / 9.81f;
  accelZ = rawAccelZ / 9.81f;
  gyroX = (g.gyro.x * 180.0f) / PI - gyroBiasX;
  gyroY = (g.gyro.y * 180.0f) / PI - gyroBiasY;
  float accelAngleX = atan2(rawAccelY, rawAccelZ) * 180.0f / PI;
  float accelAngleY = atan2(-rawAccelX, sqrt(rawAccelY * rawAccelY + rawAccelZ * rawAccelZ)) * 180.0f / PI;
  float gyroAngleX = angleX + gyroX * dt;
  float gyroAngleY = angleY + gyroY * dt;
  angleX = COMPLEMENTARY_ALPHA * gyroAngleX + (1.0f - COMPLEMENTARY_ALPHA) * accelAngleX;
  angleY = COMPLEMENTARY_ALPHA * gyroAngleY + (1.0f - COMPLEMENTARY_ALPHA) * accelAngleY;
  if (angleX > 180.0f) angleX -= 360.0f;
  if (angleX < -180.0f) angleX += 360.0f;
  if (angleY > 180.0f) angleY -= 360.0f;
  if (angleY < -180.0f) angleY += 360.0f;
}

void transmitLoRaPayload(const String& payload) {
  if (!loraReady) return;
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket(true);
  LoRa.receive();
  txCount++;
  lastTxPayload = payload;
  packetSequence++;
}

void triggerLocalLed(unsigned long durationMs = LED_ON_TIME_MS) {
  ledForcedOn = true;
  ledOffAt = millis() + durationMs;
  digitalWrite(LED_PIN, LOW);
}

void processReceivedPacket(const String& packet) {
  rxCount++;
  packetSequence++;
  lastRxPayload = packet;
  lastRxSender = extractJsonStringValue(packet, "deviceId", "unknown");
  lastRxMsgId = extractJsonStringValue(packet, "msgId", "n/a");
  lastRxAngleX = String(extractJsonFloatValue(packet, "angleX", 0.0f), 2);
  lastRxAngleY = String(extractJsonFloatValue(packet, "angleY", 0.0f), 2);
  pushRecentPacket(lastRxSender, lastRxMsgId, packet, lastRxAngleX, lastRxAngleY);
  triggerLocalLed(LED_ON_TIME_MS);
  addLog("LoRa packet received from " + lastRxSender + " id=" + lastRxMsgId);
}

void pollLoRa() {
  if (!loraReady) return;
  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;
  String packet;
  while (LoRa.available()) packet += (char)LoRa.read();
  if (packet.length() == 0) return;
  if (packet.indexOf("\"deviceId\":\"" + deviceId + "\"") >= 0) return;
  processReceivedPacket(packet);
}

void manageLedTimer() {
  if (ledForcedOn && (long)(millis() - ledOffAt) >= 0) {
    ledForcedOn = false;
    digitalWrite(LED_PIN, HIGH);
  }
}

void ensureLoRaReady() {
  if (loraReady) return;
  if (millis() - lastLoRaInitAttempt < LORA_REINIT_INTERVAL_MS) return;
  lastLoRaInitAttempt = millis();
  loraReady = initLoRa();
}

void handleTriggerLogic() {
  if (!triggerLatched && angleY >= TRIGGER_ANGLE_Y) {
    triggerLatched = true;
    transmitLoRaPayload(buildSensorPayload(true));
    addLog("Trigger crossed. Broadcasting angle packet.");
  } else if (triggerLatched && angleY <= RETRIGGER_ANGLE_Y) {
    triggerLatched = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  deviceId = buildDeviceId();
  addLog("Booting " + deviceId);
  initMPU6050();
  loraReady = initLoRa();
  connectWiFiWithTimeout();
  setupWebServer();
  isCalibrating = true;
}

void loop() {
  if (webServerEnabled) server.handleClient();
  ensureLoRaReady();
  pollLoRa();
  manageLedTimer();
  if (millis() - lastSensorUpdate >= SENSOR_UPDATE_MS) {
    if (isCalibrating) calibrateSensor();
    else {
      readMPU6050Data();
      handleTriggerLogic();
    }
    lastSensorUpdate = millis();
  }
  yield();
}
