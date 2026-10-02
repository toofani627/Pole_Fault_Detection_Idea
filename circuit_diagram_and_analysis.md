# ⚡ Pole Tilt Detection — Circuit Diagram

> All pin assignments below are taken **exactly** from [`Final_Integration.ino` lines 15–21](file:///d:/Code_base-for-github/Final_Integration.ino#L15-L21). No changes to your tested design.

---

## 1. Pin Definitions (From Your Code)

```cpp
// From Final_Integration.ino — UNCHANGED
const uint8_t LED_PIN        = D4;        // GPIO2  → WS2812B DIN
const uint8_t I2C_SDA_PIN    = D2;        // GPIO4  → MPU6050 SDA
const uint8_t I2C_SCL_PIN    = D1;        // GPIO5  → MPU6050 SCL
const uint8_t LORA_NSS_PIN   = D8;        // GPIO15 → SX1278 NSS/CS
const uint8_t LORA_RST_PIN   = D0;        // GPIO16 → SX1278 RST
const uint8_t LORA_DIO0_PIN  = D3;        // GPIO0  → SX1278 DIO0
// SPI (hardware default):  D5=SCK, D6=MISO, D7=MOSI
// Buzzer:                  RX/GPIO3
// Button:                  TX/GPIO1
```

---

## 2. Full Wiring Diagram

```
═══════════════════════════════════════════════════════════════
                COMPLETE CIRCUIT — ALL CONNECTIONS
═══════════════════════════════════════════════════════════════

  ☀️ SOLAR PANEL (5V, 70×70mm or 6V/2W upgrade)
  ┌───────────┐
  │  + (RED)  │──── D1: 1N5819 Schottky ──►── TP4056 IN+
  │  - (BLK)  │────────────────────────►──── TP4056 IN-
  └───────────┘

  🔌 TP4056 CHARGER
  ┌──────────────┐
  │  IN+    BAT+ │──── Battery +
  │  IN-    BAT- │──── Battery -
  └──────────────┘

  🔋 BATTERY (1200mAh, 3.7V)
  ┌───────────┐
  │     +     │──┬──── ESP8266 VIN
  │           │  └──── WS2812B VCC (all 5 LEDs)
  │     -     │──────── ★ COMMON GND BUS ★
  └───────────┘

  ──────────────────────────────────────────────────────────
  ESP8266 → MPU6050  (I2C Bus)
  ──────────────────────────────────────────────────────────
  ESP8266 3V3  ──────── MPU6050 VCC
  ESP8266 GND  ──────── MPU6050 GND
  ESP8266 D1 (GPIO5) ── MPU6050 SCL
  ESP8266 D2 (GPIO4) ── MPU6050 SDA
  GND ─────────────────  MPU6050 AD0  (address = 0x68)

  ──────────────────────────────────────────────────────────
  ESP8266 → SX1278 LoRa  (SPI Bus — ALL 8 CONNECTIONS)
  ──────────────────────────────────────────────────────────
  ESP8266 3V3  ──────── SX1278 VCC ──┬─[C4: 10µF]─┬── GND
                                     └─[C5: 100nF]─┘
  ESP8266 GND  ──────── SX1278 GND
  ESP8266 D5 (GPIO14) ── SX1278 SCK
  ESP8266 D6 (GPIO12) ── SX1278 MISO
  ESP8266 D7 (GPIO13) ── SX1278 MOSI
  ESP8266 D8 (GPIO15) ── SX1278 NSS ──[R3: 10kΩ]── GND
  ESP8266 D0 (GPIO16) ── SX1278 RST
  ESP8266 D3 (GPIO0)  ── SX1278 DIO0 ──[R4: 10kΩ]── 3V3
  SX1278 ANT ─────────── 17.3cm wire antenna

  ──────────────────────────────────────────────────────────
  ESP8266 → WS2812B LEDs (5× Daisy-chained)
  ──────────────────────────────────────────────────────────
  Battery +  ──────────── WS2812B VCC (all LEDs)
  GND ─────────────────── WS2812B GND (all LEDs)
  ESP8266 D4 (GPIO2) ──[R5: 330Ω]── WS2812B DIN (1st LED)
  LED1 DOUT → LED2 DIN → LED3 DIN → LED4 DIN → LED5 DIN

  ──────────────────────────────────────────────────────────
  ESP8266 → Buzzer + Button
  ──────────────────────────────────────────────────────────
  ESP8266 RX (GPIO3) ──── Buzzer (+)
  GND ────────────────── Buzzer (-)
  ESP8266 TX (GPIO1) ──── Button COM  (internal pull-up)
  GND ────────────────── Button NO
```

---

## 3. Essential Protective Components (6 Only)

| # | Part | Value | Location | Why It's Mandatory |
|---|---|---|---|---|
| D1 | 1N5819 Schottky | 40V/1A | Series on solar + wire | Battery drains through panel at night without it |
| C4 | Electrolytic Cap | 10µF/10V | SX1278 VCC to GND | LoRa TX pulls 120mA spikes — without this, ESP resets |
| C5 | Ceramic Cap | 100nF | Parallel with C4 | Filters high-freq noise from LoRa |
| R3 | Resistor | 10kΩ | D8 (GPIO15) to GND | **ESP won't boot** if D8 floats high |
| R4 | Resistor | 10kΩ | D3 (GPIO0) to 3V3 | **ESP enters flash mode** if D3 floats low |
| R5 | Resistor | 330Ω | Between D4 and LED DIN | Prevents signal reflections on data line |

**Total: ~₹15**

---

## 4. Power Budget (No WiFi — LoRa Only)

Since your real prototype uses **only LoRa** (no WiFi), the budget is:

| Component | Current | 24h Draw |
|---|---|---|
| ESP8266 (active, WiFi OFF) | 15 mA | 360 mAh |
| MPU6050 (continuous) | 4 mA | 96 mAh |
| SX1278 RX (always listening) | 12 mA | 288 mAh |
| SX1278 TX (brief bursts) | ~1 mA avg | 24 mAh |
| WS2812B × 5 (quiescent) | 5 mA | 120 mAh |
| **Total** | **~37 mA** | **~888 mAh/day** |

### Solar Panel Options

| Panel | Daily Output | Surplus/Deficit |
|---|---|---|
| 1× 70×70mm (current) | ~500 mAh | ❌ −388 mAh deficit |
| **1× 6V/2W (~110×140mm)** | **~1,300 mAh** | **✅ +412 mAh surplus** |
| 2× 70×70mm in parallel | ~1,000 mAh | ✅ +112 mAh surplus |

**Recommendation:** One **6V/2W panel** — better margin for cloudy days than two small ones, and simpler wiring. If using two small panels, add a diode (1N5819) on each panel's + wire.

---

## 5. LoRa RX vs TX Power

| Mode | Current | Duration | Notes |
|---|---|---|---|
| RX (listening) | **12 mA** | Always on | Must stay active for mesh |
| TX (transmitting) | **120 mA** | ~100ms per packet | 10× higher but very brief |

Both modes require the antenna connected. **Never transmit without the 17.3cm antenna soldered on.**

---

## 6. Star Ground — All GNDs to One Point

```
  Battery (-) ──┬── ESP8266 GND
                ├── TP4056 IN-
                ├── MPU6050 GND
                ├── SX1278 GND
                ├── WS2812B GND
                ├── Buzzer (-)
                └── Button NO
```

---

## 7. Assembly Steps

1. Solder **D1** on solar + wire → TP4056 IN+
2. Connect battery to TP4056 BAT+/BAT−
3. Battery+ → ESP8266 VIN + WS2812B VCC
4. All GNDs → battery negative (star ground)
5. MPU6050: SDA→D2, SCL→D1, VCC→3V3, AD0→GND
6. SX1278: SCK→D5, MISO→D6, MOSI→D7, NSS→D8, RST→D0, DIO0→D3, VCC→3V3, GND→GND
7. Solder **C4 + C5** across SX1278 VCC/GND
8. Solder **R3** (10kΩ) from D8 to GND
9. Solder **R4** (10kΩ) from D3 to 3V3
10. Solder **17.3cm antenna** to SX1278 ANT pad
11. WS2812B: DIN via **R5** (330Ω) from D4, chain DOUT→DIN
12. Buzzer: + → RX/GPIO3, − → GND
13. Button: COM → TX/GPIO1, NO → GND
14. Power on → verify TP4056 charge LED
