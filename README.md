# Pole Tilt Detection & Alert System (Simplified)

A solar-powered, ESP8266-based pole-monitoring node that detects dangerous tilt angles via MPU6050, triggers WS2812B visual and buzzer audio alerts, and broadcasts the event over SX1278 LoRa.

## 1. Power Supply Network (Using ESP8266 Onboard Regulator)

Since you are not using an external HT7833 regulator, we will rely on the ESP8266 development board's (e.g., NodeMCU or Wemos D1 Mini) built-in voltage regulator.

*   **Solar Panel (70x70mm, 5V):** Connects to `IN+` and `IN-` on the TP4056 module.
*   **TP4056 Charger:** Connects to the two parallel 18650 batteries via `BAT+` and `BAT-`.
*   **18650 Batteries (x2, Parallel):** Provides ~3.7V - 4.2V. 
*   **ESP8266 Power Input:** Connect the battery positive (`BAT+`) directly to the `VIN` (or `5V`) pin on the ESP8266 board. The ESP8266's onboard regulator will convert this to a safe 3.3V.
*   **Power Rails:**
    *   **Battery Rail (~3.7-4.2V):** Powers the WS2812B LEDs and the ESP8266 `VIN`.
    *   **3.3V Rail:** Taken from the ESP8266's `3V3` pin to power the MPU6050, SX1278, and Piezo Buzzer.
    *   **Common GND:** All grounds must be tied together.

---

## 2. Component Pinouts & Connections

### ESP8266 (NodeMCU/Wemos D1 Mini)
*   **VIN / 5V:** Connected to Battery `BAT+`.
*   **GND:** Connected to **Common GND**.

### MPU6050 (Accelerometer & Gyroscope)
*   **VCC:** ESP8266 `3V3`
*   **GND:** Common GND
*   **SCL:** ESP8266 `D1` (GPIO5)
*   **SDA:** ESP8266 `D2` (GPIO4)
*   **AD0:** Common GND (Sets I2C address to 0x68)

### SX1278 LoRa Module (SPI)
*   **VCC:** ESP8266 `3V3` (Optional: add one 10µF capacitor here between VCC and GND if you experience resets).
*   **GND:** Common GND
*   **SCK:** ESP8266 `D5` (GPIO14)
*   **MISO:** ESP8266 `D6` (GPIO12)
*   **MOSI:** ESP8266 `D7` (GPIO13)
*   **NSS (CS):** ESP8266 `D8` (GPIO15)
*   **RST:** ESP8266 `D0` (GPIO16)
*   **DIO0:** ESP8266 `D3` (GPIO0)

### WS2812B Addressable RGB LEDs (x5 Daisy-chained)
*   **5V/VCC:** Battery `BAT+` (Optional: add one 10µF capacitor here between VCC and GND).
*   **GND:** Common GND
*   **DIN (First LED):** ESP8266 `D4` (GPIO2) via a **330Ω series resistor**.
*   *Subsequent LEDs connect DOUT to DIN.*

### Piezo Buzzer (Audio Alert)
*   **Positive (+):** ESP8266 `RX` (GPIO3)
*   **Negative (-):** Common GND

### Lever Button (Manual Reset Switch)
*   **COM:** ESP8266 `TX` (GPIO1)
*   **NO (Normally Open):** Common GND
*   *(Note: Enable internal pull-up resistor in software for this pin)*

---

## 3. Power Analysis (Solar Panel)

**Is one 70x70mm (5V) solar panel enough?**

**No.** A single 70x70mm panel provides ~100-120mA under ideal conditions, yielding around 500-600 mAh/day. 

The system draws:
*   **~36 mA** continuously without WiFi (864 mAh/day).
*   **~101 mA** continuously with WiFi connected (2,424 mAh/day).
*   **Peak spikes >300 mA** during alerts (LEDs + LoRa TX).

**Recommendations:**
1.  **Add a second 70x70mm solar panel** wired in parallel to double the charging current.
2.  **Implement Deep Sleep:** Modify the software to wake the ESP8266 periodically (e.g., every 30 seconds), check the sensor, and return to sleep. This will reduce average power consumption to <5mA and allow the solar panels to easily maintain the battery charge.
