// RC522 RFID reader, on the doll. Power it from 3.3V, NEVER 5V.
// RC522 -> ESP32:
// 3.3V -> 3.3V, GND -> GND, RST -> 3.3V.
// SCK -> GPIO18, MISO -> GPIO19, MOSI -> GPIO23, SDA/SS -> GPIO27.
// Here SDA means SPI chip select. It is not an I2C connection.
// Upload environment: rfid. Monitor: 115200.
// Present a tag within 1-3 cm. Expect an uppercase UID.
// Remove and present it again to repeat. Save each book's UID.
// Version 0x00/0xFF means check SPI wiring and power.
// Status repeats every 2 seconds. IRQ is unused; disconnect other SPI-pin devices.
// This library test checks the part. It does not count as your own E8 driver.
// Guide: https://lumpi-build-guide.vercel.app/station/rfid
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

const int chipSelectPin = 27;
MFRC522 reader(chipSelectPin, MFRC522::UNUSED_PIN);  // RST tied to 3.3V.
unsigned long lastStatusAt = 0;
unsigned long tagCount = 0;
bool readerReady = false;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("RC522 RFID bench debug");
    Serial.println("Power=3.3V, GND=GND, RST=3.3V, IRQ=unconnected");
    Serial.println("SCK=18, MISO=19, MOSI=23, SDA/SS=27. Monitor=115200.");
    Serial.println("Starting SPI and reader initialization...");
    SPI.begin(18, 19, 23, chipSelectPin);  // SCK, MISO, MOSI, SS.
    reader.PCD_Init();

    byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
    Serial.printf("Reader version: 0x%02X\n", version);
    Serial.println("Hold a tag near the reader. Status repeats every 2 seconds.");
}

void loop() {
    unsigned long now = millis();
    if (now - lastStatusAt >= 2000) {
        lastStatusAt = now;
        byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
        byte antenna = reader.PCD_ReadRegister(MFRC522::TxControlReg);
        readerReady = version != 0x00 && version != 0xFF;
        Serial.printf("[%lu ms] Version=0x%02X, antenna register=0x%02X, tags=%lu\n",
                      now, version, antenna, tagCount);
        if (!readerReady) {
            Serial.println("SPI check FAILED: no valid reader version. Check power, RST, SS and SPI wires.");
            Serial.println("Retrying reader initialization...");
            reader.PCD_Init();
        } else {
            Serial.println("Reader responds; version alone does not verify tag reading.");
            if ((antenna & 0x03) != 0x03) {
                Serial.println("Antenna outputs are not both enabled; retrying initialization.");
                reader.PCD_Init();
            }
            Serial.println("Waiting for a tag 1-3 cm from the reader. Remove it before presenting again.");
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
        Serial.println("Tag detected, but UID read failed. Keep tag steady and check connections.");
        delay(100);
        return;
    }

    tagCount++;
    Serial.print("UID: ");
    for (byte i = 0; i < reader.uid.size; i++) {
        Serial.printf("%02X", reader.uid.uidByte[i]);
    }
    Serial.println();

    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();
}
