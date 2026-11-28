// RC522 RFID reader, on the doll. Power it from 3.3V, NEVER 5V.
// RC522 -> ESP32:
// 3.3V -> 3.3V, GND -> GND, RST -> 3.3V.
// SCK -> GPIO18, MISO -> GPIO19, MOSI -> GPIO23, SDA/SS -> GPIO27.
// Here SDA means SPI chip select. It is not an I2C connection.
// Upload environment: s1c_rfid. Monitor: 115200.
// Present a tag within 1-3 cm. Expect an uppercase UID.
// Remove and present it again to repeat. Save each book's UID.
// Version 0x00/0xFF means check SPI wiring and power.
// Status repeats every 2 seconds as a table row: time | version | antenna | tags | verdict.
// A tag adds a row with its UID. IRQ is unused; disconnect other SPI-pin devices.
// This library test checks the part.
// Guide: https://lumpi-build-guide.vercel.app/station/rfid
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int chipSelectPin = doll_pins::rfid_cs;
MFRC522 reader(chipSelectPin, MFRC522::UNUSED_PIN);  // RST tied to 3.3V.
unsigned long lastStatusAt = 0;
unsigned long tagCount = 0;
bool readerReady = false;
int rows = 0;  // table rows printed; the header repeats every 20

void printHeaderIfDue() {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | version | antenna | tags | verdict");
        Serial.println("  -------+---------+---------+------+---------------------------------");
    }
    rows++;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1c: RFID READER",
                   "the RC522 answers over SPI and reads a tag's UID.",
                   "version is not 0x00/0xFF, and a UID row appears when a tag is 1-3 cm away.");
    monitor::section("WIRING (RC522 -> ESP32)");
    monitor::item("3.3V", "3.3V  (NEVER 5V)");
    monitor::item("GND", "GND");
    monitor::item("RST", "3.3V  (no GPIO)");
    monitor::item("SCK", "GPIO%d", doll_pins::rfid_sck);
    monitor::item("MISO", "GPIO%d", doll_pins::rfid_miso);
    monitor::item("MOSI", "GPIO%d", doll_pins::rfid_mosi);
    monitor::item("SDA/SS", "GPIO%d  (SPI chip select, not I2C)", chipSelectPin);
    monitor::item("IRQ", "not connected");

    SPI.begin(doll_pins::rfid_sck, doll_pins::rfid_miso, doll_pins::rfid_mosi, chipSelectPin);  // SCK, MISO, MOSI, SS.
    reader.PCD_Init();

    monitor::section("STARTUP CHECK");
    byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
    bool answered = version != 0x00 && version != 0xFF;
    monitor::item("Reader version", "0x%02X = %s", version, answered ? "reader answered" : "no answer");
    if (!answered) {
        monitor::fail("the reader did not answer over SPI (0x00 or 0xFF)",
                      "check 3.3V and GND, RST to 3.3V, and the SCK/MISO/MOSI/SS wires");
    }
    monitor::section("LIVE (status every 2 s; hold a tag 1-3 cm away)");
}

void loop() {
    unsigned long now = millis();
    if (now - lastStatusAt >= 2000) {
        lastStatusAt = now;
        byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
        byte antenna = reader.PCD_ReadRegister(MFRC522::TxControlReg);
        readerReady = version != 0x00 && version != 0xFF;
        const char* verdict = "OK, waiting for a tag";
        if (!readerReady) {
            verdict = "NO ANSWER: check power, RST, SS, SPI";
        } else if ((antenna & 0x03) != 0x03) {
            verdict = "antenna off: restarting reader";
        }
        printHeaderIfDue();
        Serial.printf("  %6.1f |   0x%02X  |   0x%02X  | %4lu | %s\n", now / 1000.0, version, antenna,
                      tagCount, verdict);
        // Retry the reader setup when it did not answer or its antenna is off.
        if (!readerReady || (antenna & 0x03) != 0x03) {
            reader.PCD_Init();
        }
    }

    if (!readerReady) {
        delay(100);
        return;
    }

    if (!reader.PICC_IsNewCardPresent()) {
        delay(100);
        return;
    }

    if (!reader.PICC_ReadCardSerial()) {
        monitor::fail("a tag was seen but its UID could not be read",
                      "hold the tag still, 1-3 cm away; if it repeats, check the wires");
        delay(100);
        return;
    }

    tagCount++;
    char uid[24] = "";
    for (byte i = 0; i < reader.uid.size && i < 10; i++) {
        snprintf(uid + i * 2, 3, "%02X", reader.uid.uidByte[i]);
    }
    printHeaderIfDue();
    Serial.printf("  %6.1f |    -    |    -    | %4lu | TAG READ  UID %s  (save it for this book)\n",
                  millis() / 1000.0, tagCount, uid);

    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();
}
