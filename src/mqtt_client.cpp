#include "mqtt_client.h"
#include "screen_brightness.h"
#include "rgb_led.h"
#include "config.h"
#include "secrets.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>

static WiFiClient   s_wifi_client;
static PubSubClient s_mqtt(s_wifi_client);
static Config      *s_cfg     = nullptr;
static bool         s_enabled = false;

static const uint32_t RECONNECT_INTERVAL_MS = 5000;
static uint32_t s_last_reconnect_ms = 0;

static bool mqtt_publish_stat(const char *suffix, const String &payload) {
    char topic[128];
    snprintf(topic, sizeof(topic), "stat/mcmddevices/%s", suffix);
    bool ok = s_mqtt.publish(topic, payload.c_str());
    if (!ok) {
        Serial.printf("[MQTT] Publish failed for topic '%s'\n", topic);
    }
    return ok;
}

static void mqttTransmitInitStat(String deviceName) {
    String payload = "{\"value1\":\"" + WiFi.localIP().toString() +
                     "\",\"value2\":\"" + WiFi.macAddress() +
                     "\",\"value3\":\"" + deviceName + "\"}";
    mqtt_publish_stat("init", payload);
}

static void mqttTransmitInitStat() {
    mqttTransmitInitStat(String(DEVICE_CLIENT_NAME));
}

// ── Message handler ───────────────────────────────────────────────────────────

static void on_message(const char *topic, byte *payload, unsigned int len) {
    if (len == 0 || len > 4) return; // 0-100 is at most 3 digits

    char buf[5];
    memcpy(buf, payload, len);
    buf[len] = '\0';

    int raw = atoi(buf);
    if (raw < 0)   raw = 0;
    if (raw > 100) raw = 100;
    uint8_t pct = (uint8_t)raw;

    // Combined topic takes precedence — sets both LCD and LED
    if (strcmp(topic, s_cfg->mqtt_combined_topic) == 0) {
        Serial.printf("[MQTT] Combined brightness -> %d%%\n", pct);
        s_cfg->brightness     = pct;
        s_cfg->rgb_brightness = pct;
        set_screen_brightness_pct(pct);
        rgb_led_set_brightness_pct(pct);
        config_save(*s_cfg);
    } else if (strcmp(topic, s_cfg->mqtt_lcd_topic) == 0) {
        Serial.printf("[MQTT] LCD brightness -> %d%%\n", pct);
        s_cfg->brightness = pct;
        set_screen_brightness_pct(pct);
        config_save(*s_cfg);
    } else if (strcmp(topic, s_cfg->mqtt_led_brightness_topic) == 0) {
        Serial.printf("[MQTT] LED brightness -> %d%%\n", pct);
        s_cfg->rgb_brightness = pct;
        rgb_led_set_brightness_pct(pct);
        config_save(*s_cfg);
    }
}

// ── Connection helper ─────────────────────────────────────────────────────────

static bool try_connect() {
    Serial.printf("[MQTT] Connecting to %s:%u ...\n",
                  s_cfg->mqtt_broker, s_cfg->mqtt_port);
    if (!s_mqtt.connect("GithubMonitor")) {
        Serial.printf("[MQTT] Failed, state=%d\n", s_mqtt.state());
        return false;
    }
    s_mqtt.subscribe(s_cfg->mqtt_combined_topic);
    s_mqtt.subscribe(s_cfg->mqtt_lcd_topic);
    s_mqtt.subscribe(s_cfg->mqtt_led_brightness_topic);
    Serial.printf("[MQTT] Connected. Subscribed to '%s', '%s', '%s'\n",
                  s_cfg->mqtt_combined_topic, s_cfg->mqtt_lcd_topic,
                  s_cfg->mqtt_led_brightness_topic);
    mqttTransmitInitStat();
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void mqtt_client_init(Config &cfg) {
    s_cfg = &cfg;
    if (cfg.mqtt_broker[0] == '\0') {
        Serial.println("[MQTT] No broker configured — disabled");
        return;
    }
    s_enabled = true;
    s_mqtt.setServer(cfg.mqtt_broker, cfg.mqtt_port);
    s_mqtt.setCallback(on_message);
    try_connect();
}

void mqtt_client_reinit() {
    if (!s_cfg) return;
    // Disconnect cleanly from old broker
    if (s_mqtt.connected()) s_mqtt.disconnect();
    s_enabled = false;
    if (s_cfg->mqtt_broker[0] == '\0') {
        Serial.println("[MQTT] Broker cleared — disabled");
        return;
    }
    s_enabled = true;
    s_mqtt.setServer(s_cfg->mqtt_broker, s_cfg->mqtt_port);
    s_last_reconnect_ms = 0;
    try_connect();
}

void mqtt_client_tick() {
    if (!s_enabled) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (s_mqtt.connected()) {
        s_mqtt.loop();
        return;
    }
    uint32_t now = millis();
    if (now - s_last_reconnect_ms >= RECONNECT_INTERVAL_MS) {
        s_last_reconnect_ms = now;
        try_connect();
    }
}
