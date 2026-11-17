// T06 gateway raw-light upload and reconnect checkpoint.
// BH1750 wiring: VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22.
// Copy config/private.example.h to config/private.h and add real private values.
// Upload environment: p3_cloud. Monitor: 115200.
// Expected: Wi-Fi and cloud change to connected, then one room event every 10 s.
// Guide: https://lumpi-build-guide.vercel.app/gateway/sensors-cloud
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <atomic>
#include <ctime>
#include <mqtt_client.h>
#include <esp_system.h>

#include "gateway_cloud_logic.h"
#include "real_io.h"
#include "settings.h"

std::atomic<bool> cloud_connected(false);
esp_mqtt_client_handle_t mqtt_client = nullptr;
char device_id[24];
char event_topic[64];
uint32_t last_room_event_ms = 0;
uint32_t boot_number = 0;
uint32_t event_number = 0;
wl_status_t previous_wifi_status = WL_NO_SHIELD;

void on_mqtt_event(
    void*,
    esp_event_base_t,
    int32_t event_id,
    void* event_data
) {
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    if (event_id == MQTT_EVENT_CONNECTED) {
        cloud_connected.store(true);
        Serial.println("[cloud] connected");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        cloud_connected.store(false);
        Serial.println("[cloud] disconnected; automatic reconnect is active");
    } else if (event_id == MQTT_EVENT_PUBLISHED) {
        Serial.printf("[cloud] PUBACK message=%d\n", event->msg_id);
    }
}

void start_cloud() {
    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.uri = LUMPI_MQTT_URI;
    mqtt_config.cert_pem = LUMPI_MQTT_CA;
    mqtt_config.username = LUMPI_MQTT_USER;
    mqtt_config.password = LUMPI_MQTT_PASSWORD;
    mqtt_config.client_id = device_id;
    mqtt_config.disable_auto_reconnect = false;

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    if (mqtt_client == nullptr) {
        Serial.println("[cloud] client setup failed");
        return;
    }

    esp_mqtt_client_register_event(
        mqtt_client,
        MQTT_EVENT_ANY,
        on_mqtt_event,
        nullptr
    );
    esp_mqtt_client_start(mqtt_client);
    Serial.println("[cloud] TLS client started");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    hardware::light_begin();

    uint32_t chip_id = static_cast<uint32_t>(ESP.getEfuseMac());
    std::snprintf(
        device_id,
        sizeof(device_id),
        "lumpi_%08lx",
        static_cast<unsigned long>(chip_id)
    );
    std::snprintf(event_topic, sizeof(event_topic), "lumpi/%s/events", device_id);
    boot_number = esp_random() % 1000000;

    Serial.println("=== T06: cloud raw light upload ===");

    if (LUMPI_WIFI_SSID[0] == '\0') {
        Serial.println("[wifi] not configured; fill config/private.h");
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.persistent(false);
        WiFi.begin(LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD);
        configTime(0, 0, "pool.ntp.org");
        Serial.println("[wifi] connecting");
    }

    if (!gateway_cloud_logic::secure_config(LUMPI_MQTT_URI, LUMPI_MQTT_CA)) {
        Serial.println("[cloud] not configured: mqtts:// URI and CA are required");
    }
}

void loop() {
    wl_status_t wifi_status = WiFi.status();
    if (wifi_status != previous_wifi_status) {
        Serial.println(
            wifi_status == WL_CONNECTED
                ? "[wifi] connected"
                : "[wifi] disconnected"
        );
        previous_wifi_status = wifi_status;
    }

    bool config_is_secure = gateway_cloud_logic::secure_config(
        LUMPI_MQTT_URI,
        LUMPI_MQTT_CA
    );

    if (gateway_cloud_logic::ready_to_connect(
        wifi_status == WL_CONNECTED,
        static_cast<uint64_t>(std::time(nullptr)),
        mqtt_client != nullptr,
        config_is_secure
    )) {
        start_cloud();
    }

    uint32_t now_ms = millis();
    if (gateway_cloud_logic::room_event_due(
        cloud_connected.load(),
        now_ms,
        last_room_event_ms
    )) {
        float lux = NAN;
        hardware::light_read(lux);

        event_number += 1;

        char event[256];
        bool event_ready = gateway_cloud_logic::format_room_event(
            event,
            sizeof(event),
            device_id,
            boot_number,
            event_number,
            static_cast<uint64_t>(std::time(nullptr)),
            lux
        );

        if (event_ready) {
            int message_id = esp_mqtt_client_publish(
                mqtt_client,
                event_topic,
                event,
                0,
                1,
                0
            );
            Serial.printf("[cloud] publish message=%d %s\n", message_id, event);
        } else {
            Serial.println("[cloud] room event was too large");
        }

        last_room_event_ms = now_ms;
    }

    delay(100);
}
