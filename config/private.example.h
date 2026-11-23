#pragma once
// YOUR SECRETS. Copy this file to config/private.h and fill it in.
// private.h is listed in .gitignore: it is never uploaded to Git (requirement E5).
// All three boards use the SAME private.h.

// Wi-Fi: must be 2.4 GHz (the ESP32 cannot use 5 GHz). A phone hotspot works.
#define LUMPI_WIFI_SSID "YOUR_WIFI_NAME"
#define LUMPI_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Addresses on that Wi-Fi. Give the gateway and the laptop fixed addresses
// in your router ("DHCP reservation"); s5_nest prints its own address at start.
#define LUMPI_GATEWAY_URL "http://192.168.1.50"
// Where the nest sends dashboard events and recordings. One of:
//   "http://192.168.1.60:8080"         laptop dashboard (laptop/dashboard_server.py), no AI
//   "http://192.168.1.60:8081"         cloud API run on the laptop (python3 api/index.py)
//   "https://<your-site>.vercel.app"   the real cloud
#define LUMPI_ADAPTER_URL "http://192.168.1.60:8080"
// Optional, https only: the site's root certificate (PEM), so the nest checks it talks to the real site.
// #define LUMPI_CLOUD_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

// One shared password for every local request (station <-> gateway <-> laptop).
// Make a random one:  python -c "import secrets; print(secrets.token_hex(16))"
// The laptop needs the same value:  $env:LUMPI_LAN_KEY = '...'
#define LUMPI_LAN_KEY "PASTE_A_RANDOM_32_CHARACTER_KEY"

// Cloud (MQTT over TLS), e.g. a free HiveMQ Cloud broker. Optional for local tests.
#define LUMPI_MQTT_URI "mqtts://YOUR_BROKER_HOST:8883"
#define LUMPI_MQTT_USER "YOUR_GATEWAY_USERNAME"
#define LUMPI_MQTT_PASSWORD "YOUR_GATEWAY_PASSWORD"
// The broker's CA certificate: lets the gateway check it talks to the REAL broker.
// (A text constant, not a #define: a #define must fit on one line, a certificate cannot.)
static const char lumpi_mqtt_ca_text[] = R"CERT(-----BEGIN CERTIFICATE-----
PASTE_YOUR_BROKER_CA_CERTIFICATE_HERE
-----END CERTIFICATE-----
)CERT";
#define LUMPI_MQTT_CA lumpi_mqtt_ca_text
