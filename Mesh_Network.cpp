#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <FS.h>
#include <map>
#include <list>

// ===== CONFIGURATION =====
const char* ssid = "Airtel_Zerotouch";
const char* password = "Airtel@123";
ESP8266WebServer server(80);

// ===== ESP-NOW STRUCTS =====
typedef struct struct_message {
    uint8_t msgType;   // 0 = DATA, 1 = ACK
    uint32_t msgId;
    uint8_t originalSender[6];
    uint8_t immediateSender[6]; // Used for ACKs
    char payload[50];
} struct_message;

struct_message outgoingData;
struct_message incomingData;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===== CACHE & LOGS =====
typedef struct {
    unsigned long ts;
    uint8_t senderMac[6];
} cache_entry_t;
std::map<uint32_t, cache_entry_t> messageCache; // ID -> {timestamp, originalSenderMac}
bool cacheNeedsSave = false; // Lazy save flag
String systemLog = "";

// Fixed length history to prevent RAM exhaust
std::list<String> historyList;
const int MAX_HISTORY_ITEMS = 15;

int totalReceived = 0;
int totalRelayed = 0;
int totalDropped = 0;

const int DEDUPLICATION_WINDOW_MS = 60000; // 60 seconds
unsigned long lastCacheCleanup = 0;

// ===== MESSAGE QUEUES & STATE MACHINE =====
#define RX_QUEUE_SIZE 10
typedef struct { uint8_t mac[6]; struct_message msg; } rx_item_t;
rx_item_t rxQueue[RX_QUEUE_SIZE];
volatile int rxHead = 0, rxTail = 0;

#define MAX_TX_JOBS 5
typedef struct { 
    bool active; 
    struct_message msg; 
    unsigned long firstSendTime;
    unsigned long lastSendTime;
    unsigned long retryInterval;
    unsigned long nextJitter;
} tx_job_t;
tx_job_t txJobs[MAX_TX_JOBS];
const unsigned long MAX_BROADCAST_DURATION = 30000; // 30 seconds

// ===== UTILS =====
String macToString(const uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

void addLog(String message) {
    String timeStr = String(millis());
    systemLog = "[" + timeStr + "ms] " + message + "\n" + systemLog;
    if (systemLog.length() > 2000) {
        systemLog = systemLog.substring(0, 2000); // Keep log size manageable
    }
    Serial.println(message);
}

// ===== SPIFFS CACHE MANAGEMENT =====
void saveCacheToSPIFFS() {
    File f = SPIFFS.open("/cache.txt", "w");
    if (!f) {
        addLog("Failed to open cache file for writing");
        return;
    }
    unsigned long now = millis();
    for (auto const& x : messageCache) {
        // Save the elapsed time since the message was received instead of absolute millis()
        // so it makes sense when reloaded after a fast reboot.
        unsigned long age = now - x.second.ts;
        f.printf("%u,%lu\n", x.first, age); // (MAC not saved to flash, negligible edge case)
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
            // Store as raw millis offset to prevent immediate deletion due to reboot timing
            unsigned long ts = line.substring(separator + 1).toInt();
            if (ts < DEDUPLICATION_WINDOW_MS) { // Only keep if recently saved before reboot
                messageCache[id].ts = now - ts; 
                memset(messageCache[id].senderMac, 0, 6);
            }
        }
    }
    f.close();
    addLog("Loaded IDs from SPIFFS cache.");
}

void cleanupCache() {
    unsigned long now = millis();
    // Use a robust signed difference to handle millis() rollover natively
    if (now - lastCacheCleanup > 10000) { // Every 10s
        bool changed = false;
        for (auto it = messageCache.begin(); it != messageCache.end(); ) {
            // Check true elapsed time
            if (now - it->second.ts > DEDUPLICATION_WINDOW_MS) {
                it = messageCache.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        // Protect Flash Memory (Lazy Write)
        if (changed || cacheNeedsSave) {
            saveCacheToSPIFFS();
            cacheNeedsSave = false;
            addLog("SYSTEM: Synced cache to Flash Memory.");
        }
        lastCacheCleanup = now;
    }
}

// ===== ESP-NOW CALLBACKS =====
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
    addLog(String("Last Packet Send Status: ") + (sendStatus == 0 ? "Success" : "Fail"));
}

void ICACHE_FLASH_ATTR OnDataRecv(uint8_t * mac, uint8_t *incomingDataPtr, uint8_t len) {
    if (len != sizeof(struct_message)) return;
    
    // Lightweight Interrupt Context: Just add to queue and exit!
    int nextHead = (rxHead + 1) % RX_QUEUE_SIZE;
    if (nextHead != rxTail) { // Check if queue has space
        memcpy(rxQueue[rxHead].mac, mac, 6);
        memcpy(&rxQueue[rxHead].msg, incomingDataPtr, sizeof(struct_message));
        rxHead = nextHead;
    }
}

// ===== ACK & RETRY SCHEDULING (Shout & Listen) =====
void startTransmission(struct_message msg) {
    for (int i = 0; i < MAX_TX_JOBS; i++) {
        if (!txJobs[i].active) {
            // Prepare the "Shout & Listen" job
            txJobs[i].msg = msg;
            txJobs[i].firstSendTime = millis();
            txJobs[i].lastSendTime = 0;
            txJobs[i].retryInterval = 200; // 200ms listening window initially
            txJobs[i].nextJitter = random(10, 50); // Initial start spread
            txJobs[i].active = true;
            
            addLog("STATUS [LISTENING AND RELAYING]: Task assigned for MsgID " + String(msg.msgId));
            return;
        }
    }
    addLog("WARNING: TX Queue full! Dropping relay assignment for MsgID " + String(msg.msgId));
}

void manageTransmissions() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_TX_JOBS; i++) {
        if (txJobs[i].active) {
            // 1. Check for total timeout
            if (now - txJobs[i].firstSendTime > MAX_BROADCAST_DURATION) {
                txJobs[i].active = false;
                addLog("STATUS [TIMEOUT]: No confirmation received for MsgID " + String(txJobs[i].msg.msgId) + " after 30s.");
                continue;
            }

            // 2. Broadcast if interval has passed
            if (txJobs[i].lastSendTime == 0 && now >= txJobs[i].firstSendTime + txJobs[i].nextJitter) {
                // First jittered pulse
                esp_now_send(broadcastAddress, (uint8_t *) &txJobs[i].msg, sizeof(struct_message));
                txJobs[i].lastSendTime = now;
                totalRelayed++;
                addLog("=> SENDING/RELAYING: Pulse 1 for MsgID " + String(txJobs[i].msg.msgId));
            } 
            else if (txJobs[i].lastSendTime > 0 && now - txJobs[i].lastSendTime >= txJobs[i].retryInterval) {
                // Subsequent retry pulse
                esp_now_send(broadcastAddress, (uint8_t *) &txJobs[i].msg, sizeof(struct_message));
                txJobs[i].lastSendTime = now;
                totalRelayed++;
                
                // Exponential backoff up to 2 seconds to drastically reduce network flooding
                unsigned long newInterval = txJobs[i].retryInterval * 2;
                txJobs[i].retryInterval = (newInterval > 2000) ? 2000 : newInterval;
                
                addLog("=> SENDING RE-PULSE: MsgID " + String(txJobs[i].msg.msgId) + " (Still Listening...)");
            }
        }
    }
}

void sendAck(uint8_t* targetMac, uint32_t msgId) {
    struct_message ackMsg;
    ackMsg.msgType = 1; // 1 = ACK
    ackMsg.msgId = msgId;
    WiFi.macAddress(ackMsg.originalSender);
    WiFi.macAddress(ackMsg.immediateSender);
    // Address explicitly to the immediate sender (peer-to-peer unicast behavior over broadcast)
    esp_now_send(targetMac, (uint8_t *) &ackMsg, sizeof(struct_message));
    addLog("STATUS [CONFIRMATION SENT]: ACK sent direct to " + macToString(targetMac) + " for MsgID " + String(msgId));
}

// ===== MAIN PROCESSING QUEUE =====
void processRxQueue() {
    while (rxHead != rxTail) {
        struct_message incoming = rxQueue[rxTail].msg;
        uint8_t* mac = rxQueue[rxTail].mac; // Target the physical MAC it came from
        rxTail = (rxTail + 1) % RX_QUEUE_SIZE;
        
        String senderMAC = macToString(mac);
        uint32_t msgId = incoming.msgId;
        
        // --- HANDLE ACKs ---
        if (incoming.msgType == 1) { // 1 = ACK
            // Stop any active shouts for this message based on the ID
            for (int i = 0; i < MAX_TX_JOBS; i++) {
                if (txJobs[i].active && txJobs[i].msg.msgId == msgId) {
                    txJobs[i].active = false;
                    addLog("STATUS [CONFIRMATION RECEIVED]: MsgID " + String(msgId) + " safely received by " + senderMAC + ". Halting broadcasts.");
                }
            }
            continue; // ACKs are not relayed or cached like DATA
        }

        // --- HANDLE DATA MESSAGES ---
        
        // 1. Deduplication (Prevent infinite processing loops)
        if (messageCache.find(msgId) != messageCache.end()) {
            totalDropped++;
            addLog("DROP (Seen it): MsgID " + String(msgId));
            
            // Prevent the "Reverse ACK Bug"!
            // Only send an ACK for a duplicate IF the MAC we are hearing it from is the exact
            // same MAC that originally gave the message to us. (This covers missed ACKs).
            // If the MAC is different, it means we are simply overhearing a node trying to relay 
            // the message forward. If we ACK them, they will think they succeeded and stop relaying!
            bool sameSender = true;
            for(int i=0; i<6; i++) {
                if (mac[i] != messageCache[msgId].senderMac[i]) {
                    sameSender = false; break;
                }
            }
            if (sameSender) {
                sendAck(mac, msgId);
                addLog("STATUS [RE-ACK SENT]: Resending ACK to " + senderMAC + " (In case my first was lost)");
            }
            continue;
        }
        
        // 2. It's brand new!
        // Provide instant confirmation to the Node that gave it to us
        sendAck(mac, msgId);
        
        totalReceived++;
        addLog("STATUS [NEW PACKET IN]: MsgID " + String(msgId) + " arrived from " + senderMAC);
        
        // 3. Mark for lazy SPIFFS saving
        messageCache[msgId].ts = millis();
        memcpy(messageCache[msgId].senderMac, mac, 6); // Remember exactly who gave this to us
        cacheNeedsSave = true; 
        
        // 4. Update History Dashboard
        String newEntry = "{\"id\":\"" + String(msgId) + "\", \"payload\":\"" + String(incoming.payload) + "\"}";
        historyList.push_front(newEntry);
        if (historyList.size() > MAX_HISTORY_ITEMS) {
            historyList.pop_back();
        }
        
        // 5. Take ownership of the relay packet!
        // We set OUR MAC as the immediate sender so the *next* node ACKs to *us*
        WiFi.macAddress(incoming.immediateSender);
        
        // 6. Enter the Shout & Listen state machine!
        startTransmission(incoming);
    }
}

// ===== WEB DASHBOARD API =====
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html><html><head><title>Mesh Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    body { font-family: Arial; padding: 20px; background: #f4f4f9; }
    .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }
    button { background: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; font-size: 16px; }
    button:hover { background: #0056b3; }
    pre { background: #333; color: lime; padding: 10px; border-radius: 5px; height: 200px; overflow-y: auto; }
</style>
</head><body>
    <h1>Mesh Node Dashboard</h1>
    
    <div class="card">
        <h3>Trigger Broadcast</h3>
        <button onclick="triggerBroadcast()">Send Mesh Packet</button>
    </div>
    
    <div class="card">
        <h3>Network Stats</h3>
        <p>Received: <span id="recv">0</span></p>
        <p>Relayed: <span id="relayed">0</span></p>
        <p>Dropped (Duplicates): <span id="dropped">0</span></p>
    </div>
    
    <div class="card">
        <h3>Live Log</h3>
        <pre id="log"></pre>
    </div>
    
    <div class="card">
        <h3>Message History</h3>
        <pre id="history" style="color: white; background: #222;"></pre>
    </div>

<script>
    function triggerBroadcast() {
        fetch('/api/send', {method: 'POST'})
            .then(res => res.text()).then(alert);
    }
    
    function fetchStats() {
        fetch('/api/stats')
            .then(res => res.json())
            .then(data => {
                document.getElementById('recv').innerText = data.received;
                document.getElementById('relayed').innerText = data.relayed;
                document.getElementById('dropped').innerText = data.dropped;
                document.getElementById('log').innerText = data.log;
            });
    }

    function fetchHistory() {
        fetch('/api/history')
            .then(res => res.json())
            .then(data => {
                document.getElementById('history').innerText = JSON.stringify(data, null, 2);
            });
    }

    setInterval(fetchStats, 1500);
    setInterval(fetchHistory, 3000);
</script>
</body></html>
)rawliteral";
    server.send(200, "text/html", html);
}

void handleSend() {
    uint32_t newId = millis() + random(1000); // Generate Unique Message ID
    
    outgoingData.msgType = 0; // 0 = DATA
    outgoingData.msgId = newId;
    WiFi.macAddress(outgoingData.originalSender);
    WiFi.macAddress(outgoingData.immediateSender);
    strncpy(outgoingData.payload, "Hello Mesh Network!", sizeof(outgoingData.payload));
    
    // Add to own cache immediately 
    messageCache[newId].ts = millis();
    WiFi.macAddress(messageCache[newId].senderMac); // We generated it, we are the sender
    cacheNeedsSave = true; // Lazy writing
    
    addLog("--- MANUAL SEND TRIGGERED: MsgID " + String(newId) + " ---");
    
    // Pass to the Shout & Listen Queue!
    startTransmission(outgoingData);
    
    server.send(200, "text/plain", "Broadcast packet queued successfully and Listening for confirmations!");
}

void handleStats() {
    String json = "{";
    json += "\"received\":" + String(totalReceived) + ",";
    json += "\"relayed\":" + String(totalRelayed) + ",";
    json += "\"dropped\":" + String(totalDropped) + ",";
    
    // Escape quotes and newlines for JSON valid string
    String escapedLog = systemLog;
    escapedLog.replace("\"", "\\\"");
    escapedLog.replace("\n", "\\n");
    escapedLog.replace("\r", "");
    
    json += "\"log\":\"" + escapedLog + "\"";
    json += "}";
    
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

// ===== SETUP & LOOP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // 1. Initialize SPIFFS
    if (SPIFFS.begin()) {
        addLog("SPIFFS initialized.");
        loadCacheFromSPIFFS();
    } else {
        addLog("SPIFFS mount failed!");
    }

    // 2. Connect to existing WiFi network (STA mode)
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    addLog("WiFi Connected! IP Address: " + WiFi.localIP().toString());
    
    // 3. Init ESP-NOW
    if (esp_now_init() != 0) {
        addLog("Error initializing ESP-NOW");
        return;
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    // NOTE: For ESP-NOW to work seamlessly while connected to WiFi, 
    // the ESP-NOW communication must happen on the same WiFi channel
    // that the ESP is using to connect to your home router.
    // ESP8266 auto-syncs this in Station mode.
    
    // 4. Setup Web Server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/send", HTTP_POST, handleSend);
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.begin();
    addLog("Web Server started.");
}

void loop() {
    server.handleClient();
    processRxQueue();        // Non-blocking payload/ACK processing
    manageTransmissions();   // Dedicated Shout & Listen Loop
    cleanupCache();          // Periodic cache purge & lazy saving
    delay(5);                // Feed the watchdog!
}
