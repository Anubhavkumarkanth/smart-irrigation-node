#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "irrigation.h"
#include "payload.h"

// Fill these in before flashing.
static const char* WIFI_SSID = "YOUR_WIFI_NAME";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// test.mosquitto.org is a public broker, so anyone can read these topics.
// Change DEVICE_ID to something unique and do not send private data.
static const char* MQTT_HOST = "test.mosquitto.org";
static const uint16_t MQTT_PORT = 1883;
static const char* DEVICE_ID = "irrigation-node-01";

static const uint8_t PIN_SOIL = 34;   // capacitive soil sensor, ADC1
static const uint8_t PIN_PUMP = 26;   // relay module input, active HIGH
static const uint8_t PIN_TANK = 27;   // float switch to GND, LOW = water present

static const uint32_t SENSE_EVERY_MS = 500;
static const uint32_t PUBLISH_EVERY_MS = 10000;
static const uint32_t MQTT_RETRY_MS = 5000;
static const uint32_t WIFI_RETRY_MS = 10000;

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static irrigation::SoilCalibration soilCal;
static irrigation::Controller controller(irrigation::Config{});

static float soilMv = 0;
static float moisture = 0;
static bool sensorOk = false;
static bool tankOk = false;
static bool haveReading = false;

static char topicBuf[80];

static const char* topicFor(const char* suffix) {
    snprintf(topicBuf, sizeof(topicBuf), "irrigation/%s/%s", DEVICE_ID, suffix);
    return topicBuf;
}

// The ESP32 ADC is noisy, so average a batch of samples.
static float readSoilMv() {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogReadMilliVolts(PIN_SOIL);
    }
    return sum / 16.0f;
}

static void readSensors() {
    const float mv = readSoilMv();
    sensorOk = irrigation::sensorPlausible(mv);
    if (sensorOk) {
        // light smoothing so one noisy sample does not flip the pump
        soilMv = haveReading ? soilMv + 0.3f * (mv - soilMv) : mv;
        haveReading = true;
        moisture = irrigation::moisturePercent(soilMv, soilCal);
    }
    tankOk = digitalRead(PIN_TANK) == LOW;
}

static void onMessage(char* topic, uint8_t* payload, unsigned int len) {
    float v = 0;
    if (!irrigation::parsePercent(payload, len, &v)) return;

    if (strstr(topic, "/cmd/start_below")) {
        controller.setStartBelow(v);
    } else if (strstr(topic, "/cmd/stop_above")) {
        controller.setStopAbove(v);
    }
}

static void keepMqttAlive(uint32_t now) {
    static uint32_t lastTry = 0;

    if (mqtt.connected()) {
        mqtt.loop();
        return;
    }
    if (WiFi.status() != WL_CONNECTED || now - lastTry < MQTT_RETRY_MS) return;
    lastTry = now;

    char statusTopic[80];
    snprintf(statusTopic, sizeof(statusTopic), "irrigation/%s/status", DEVICE_ID);
    if (mqtt.connect(DEVICE_ID, statusTopic, 1, true, "offline")) {
        mqtt.publish(statusTopic, "online", true);
        char cmdTopic[80];
        snprintf(cmdTopic, sizeof(cmdTopic), "irrigation/%s/cmd/#", DEVICE_ID);
        mqtt.subscribe(cmdTopic);
    }
}

static void publishTelemetry() {
    if (!mqtt.connected()) return;

    char json[200];
    snprintf(json, sizeof(json),
             "{\"moisture\":%.1f,\"soil_mv\":%.0f,\"state\":\"%s\",\"pump\":%s,"
             "\"tank_ok\":%s,\"runs\":%lu,\"start_below\":%.0f,\"stop_above\":%.0f,"
             "\"uptime_s\":%lu}",
             moisture, soilMv, irrigation::toString(controller.state()),
             controller.pumpOn() ? "true" : "false", tankOk ? "true" : "false",
             (unsigned long)controller.runs(), controller.config().start_below,
             controller.config().stop_above, (unsigned long)(millis() / 1000));
    mqtt.publish(topicFor("telemetry"), json);
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_PUMP, OUTPUT);
    digitalWrite(PIN_PUMP, LOW);
    pinMode(PIN_TANK, INPUT_PULLUP);
    analogSetPinAttenuation(PIN_SOIL, ADC_11db);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMessage);
}

void loop() {
    static uint32_t lastSense = 0;
    static uint32_t lastPublish = 0;
    static uint32_t lastWifiTry = 0;
    const uint32_t now = millis();

    // Watering does not depend on the network, so it keeps working offline.
    if (now - lastSense >= SENSE_EVERY_MS) {
        lastSense = now;
        readSensors();
        controller.update(now, moisture, sensorOk && haveReading, tankOk);
        digitalWrite(PIN_PUMP, controller.pumpOn() ? HIGH : LOW);
    }

    if (WiFi.status() != WL_CONNECTED && now - lastWifiTry >= WIFI_RETRY_MS) {
        lastWifiTry = now;
        WiFi.reconnect();
    }
    keepMqttAlive(now);

    if (now - lastPublish >= PUBLISH_EVERY_MS) {
        lastPublish = now;
        publishTelemetry();
    }
}
