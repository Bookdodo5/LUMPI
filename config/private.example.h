#pragma once
// YOUR SECRETS. Copy this file to config/private.h and fill it in.
// private.h is listed in .gitignore: it is never uploaded to Git (requirement E5).
// All three boards use the SAME private.h.

// Wi-Fi: must be 2.4 GHz (the ESP32 cannot use 5 GHz). A phone hotspot works.
#define LUMPI_WIFI_SSID "YOUR_WIFI_NAME"
#define LUMPI_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Addresses. Give the gateway a fixed address in your router ("DHCP reservation");
// s5_nest prints its own address at start.
#define LUMPI_GATEWAY_URL "http://192.168.1.50"
// Where the nest sends dashboard events and recordings. One of:
//   "https://<your-site>.vercel.app"   the real cloud
//   "http://<computer>:8081"           the cloud code run locally (python3 api/lumpi.py)
#define LUMPI_ADAPTER_URL "https://<your-site>.vercel.app"
// Optional, https only: the site's root certificate (PEM), so the nest checks it talks to the real site.
// #define LUMPI_CLOUD_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

// One shared password for every local request (doll <-> nest <-> cloud).
// Make a random one:  python -c "import secrets; print(secrets.token_hex(16))"
// Vercel needs the same value (environment variable LUMPI_LAN_KEY).
#define LUMPI_LAN_KEY "PASTE_A_RANDOM_32_CHARACTER_KEY"
