// RC522 RFID reader, on the doll. Power it from 3.3V, NEVER 5V.
// RC522 -> ESP32:
// 3.3V -> 3.3V, GND -> GND, RST -> 3.3V.
// SCK -> GPIO18, MISO -> GPIO19, MOSI -> GPIO23, SDA/SS -> GPIO27.
// Here SDA means SPI chip select. It is not an I2C connection.
// Upload environment: rfid. Monitor: 115200.
// Present a tag within 1-3 cm. Expect an uppercase UID.
// Remove and present it again to repeat. Save each book's UID.
// Version 0x00/0xFF means check SPI wiring and power.
// This library test checks the part. It does not count as your own E8 driver.
// Guide: https://lumpi-build-guide.vercel.app/station/rfid
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

const int chipSelectPin = 27;
MFRC522 reader(chipSelectPin, MFRC522::UNUSED_PIN);  // RST tied to 3.3V.

void setup() {
    Serial.begin(115200);
    delay(500);
    SPI.begin(18, 19, 23, chipSelectPin);  // SCK, MISO, MOSI, SS.
    reader.PCD_Init();

    byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
    Serial.printf("Reader version: 0x%02X\n", version);
    Serial.println("Hold a tag near the reader.");
}

void loop() {
    if (!reader.PICC_IsNewCardPresent()) {
        delay(100);
        return;
    }

    if (!reader.PICC_ReadCardSerial()) {
        return;
    }

    Serial.print("UID: ");
    for (byte i = 0; i < reader.uid.size; i++) {
        Serial.printf("%02X", reader.uid.uidByte[i]);
    }
    Serial.println();

    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();
}
