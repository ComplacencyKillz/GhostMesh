---
---
Heltec v3:
    --> BN-220GPS Module
        Heltec GND --> BN-220GPS (Black Wire)
        Heltec GPIO-33 --> BN-220GPS (Green Wire)
        Heltec GPIO-34 --> BN-220GPS (White Wire)
        Heltec 3v3 --> BN-220GPS (Red Wire)
    --> Tilt Sensor <<<<<"HAVEN'T GOTTEN THIS TO WORK YET">>>>>
        Heltec GPIO-2 --> Tilt Sensor (White Wire) 
        Heltec GND --> Tilt Sensor (Black Wire)
    --> Flipepr Zero
        Heltec GND --> Flipper GND (GPIO-11) (Black Wire)
        Heltec GPIO-7 --> Flipper TX (GPIO-13)
        Heltec GPIO-6 --> Flipper RX (GPIO-14)
    --> I2C HUB
        Heltec GPIO-41 --> I2C HUB SDA (Yellow Wire)
        Heltec GPIO-42 --> I2C HUB SCL (Orange Wire)
        Heltec GND --> I2C HUB (Red Wire)
        Heltec 3v3 --> I2C HUB VIN (Brown Wire)


I2C Hub:
    --> Heltec v3:
        Heltec GND --> I2C HUB GND (Red Wire)
        Heltec GPIO-41 --> I2C HUB SDA (Yellow Wire)
        Heltec GPIO-42 --> I2C HUB SCL (Orange Wire)
        Heltec 3v3 --> I2C HUB VIN (Brown Wire)
    --> BME-280 Environment Sensor:
        BME-280 VCC --> I2C HUB VIN (Brown Wire)
        BME-280 GND --> I2C HUB GND (Red Wire)
        BME-280 SCL --> I2C HUB SCL (Orange Wire)
        BME-280 SDF (or SDP???) --> I2C HUB SDA (Yellow Wire)