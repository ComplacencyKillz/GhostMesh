// GhostMesh bench test — BME280 on Heltec WiFi LoRa 32 V3, I2C bus 2 (GPIO41/42).
//
// Purpose: prove the Phase 7 environmental sensor and its STEMMA QT wiring are
// electrically sound BEFORE building it into Meshtastic. The loop runs an I2C
// scan first so a wiring fault reads as "no devices found" instead of a
// confusing sensor-init error, then reads the BME280 every 2 seconds.
//
// This is a diagnostic, not deploy firmware. Console comes out the USB-C/CP2102
// bridge at 115200.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// Heltec V3 I2C bus 2 — the external sensor bus. Bus 1 (GPIO17/18) is the
// hardwired OLED and must not be touched here.
static const int PIN_SDA = 41;
static const int PIN_SCL = 42;

// The GhostMesh build straps the BME280 to 0x76 (SDO -> GND). Adafruit's library
// defaults to 0x77, so the address is passed explicitly.
static const uint8_t BME280_ADDR = 0x76;

Adafruit_BME280 bme;
bool sensor_ok = false;

static void i2c_scan() {
  Serial.println("I2C scan on GPIO41(SDA)/42(SCL):");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X%s\n", addr,
                    addr == BME280_ADDR ? "   <- BME280 (expected)" :
                    addr == 0x36        ? "   <- MAX17048 fuel gauge (Phase 9)" :
                    addr == 0x77        ? "   <- BME280 at alt addr (SDO floating/high)" : "");
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  NONE — check SDA/SCL/3V3/GND and the Qwiic pigtail to the header");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== GhostMesh BME280 bench test ===");

  Wire.begin(PIN_SDA, PIN_SCL);
  i2c_scan();

  sensor_ok = bme.begin(BME280_ADDR, &Wire);
  if (!sensor_ok) {
    Serial.println("BME280 begin() FAILED — see scan above (if 0x77 showed, try that address).");
  } else {
    Serial.println("BME280 OK — streaming readings.");
  }
}

void loop() {
  if (!sensor_ok) {
    // Rescan on a loop so you can fix the wiring live without reflashing.
    delay(2000);
    i2c_scan();
    sensor_ok = bme.begin(BME280_ADDR, &Wire);
    if (sensor_ok) Serial.println("BME280 now responding.");
    return;
  }

  float t = bme.readTemperature();        // degC
  float h = bme.readHumidity();           // %RH
  float p = bme.readPressure() / 100.0f;  // hPa

  Serial.printf("T=%.2f C (%.1f F)   RH=%.1f %%   P=%.1f hPa\n",
                t, t * 9.0f / 5.0f + 32.0f, h, p);
  delay(2000);
}
