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
}
bool wifiConnected = false;
