# Third-party notices

FieldMesh is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore). The FieldMesh and MeshCore code in this repository is under the MIT License ([license.txt](license.txt)).

The prebuilt firmware files of a release are built from this repository at the release tag, plus the libraries and frameworks that PlatformIO downloads during the build. Those keep their own licenses, listed below. Versions are pinned in [platformio.ini](platformio.ini) and the variant files; the linker only keeps the parts that are actually used.

## What this means for the release files

- **Elecrow ThinkNode M1:** the files contain GxEPD2, which is licensed under the GPL-3.0. The M1 firmware files are therefore distributed under the terms of the GPL-3.0.
- **All devices:** the files contain libraries under the LGPL (Arduino cores, MicroNMEA, ESPAsyncWebServer and others, see below).
- **Source code:** the complete source code for every release file is this repository at the matching tag (e.g. `v2.2.0`), together with the library versions in `platformio.ini`. How to build it: [README → Building from source](README.md#building-from-source).

## Libraries downloaded at build time

| Library | License | Used in |
|---|---|---|
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | all |
| [Crypto](https://github.com/rweather/arduinolibs) (rweather) | MIT | all |
| [RTClib](https://github.com/adafruit/RTClib) | MIT | all |
| [Melopero RV3028](https://github.com/melopero/Melopero_RV-3028_Arduino_Library) | MIT | all |
| [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) | MIT | all |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) (dependency of CayenneLPP) | MIT | all |
| [MicroNMEA](https://github.com/stevemarple/MicroNMEA) | LGPL-2.1 | all |
| [CustomLFS](https://github.com/oltaco/CustomLFS) | MIT | M1, L1, T1000-E |
| [base64](https://github.com/Densaugeo/base64_arduino) | MIT | M1, L1, T1000-E |
| [NonBlockingRTTTL](https://github.com/end2endzone/NonBlockingRTTTL) | MIT | M1, L1, T1000-E |
| [GxEPD2](https://github.com/ZinggJM/GxEPD2) | **GPL-3.0** | M1 |
| [CRC32](https://github.com/bakercp/CRC32) | MIT | M1 |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | BSD | M1, L1, Station G2 |
| [Adafruit SH110X](https://github.com/adafruit/Adafruit_SH110x) | BSD | L1, Station G2 |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | MIT | L1, Station G2 |
| [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) | Apache-2.0 | L1, Station G2 |
| Adafruit sensor libraries: [INA219](https://github.com/adafruit/Adafruit_INA219), [INA260](https://github.com/adafruit/Adafruit_INA260), [AHTX0](https://github.com/adafruit/Adafruit_AHTX0), [BME280](https://github.com/adafruit/Adafruit_BME280_Library), [SHTC3](https://github.com/adafruit/Adafruit_SHTC3), [MLX90614](https://github.com/adafruit/Adafruit-MLX90614-Library), [BME680](https://github.com/adafruit/Adafruit_BME680) (includes the Bosch BME68x API), [BMP085](https://github.com/adafruit/Adafruit-BMP085-Library) | BSD | L1, Station G2 |
| Adafruit sensor libraries: [INA3221](https://github.com/adafruit/Adafruit_INA3221), [BMP280](https://github.com/adafruit/Adafruit_BMP280_Library), [VL53L0X](https://github.com/adafruit/Adafruit_VL53L0X) | MIT | L1, Station G2 |
| [INA226](https://github.com/RobTillaart/INA226) (Rob Tillaart) | MIT | L1, Station G2 |
| [Sensirion I2C SHT4x](https://github.com/Sensirion/arduino-i2c-sht4x), [Sensirion Core](https://github.com/Sensirion/arduino-core) | BSD-3-Clause | L1, Station G2 |
| [Arduino_LPS22HB](https://github.com/arduino-libraries/Arduino_LPS22HB) | LGPL-2.1 | L1, Station G2 |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | LGPL-3.0 | Station G2 |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) (dependency of ESPAsyncWebServer) | LGPL-3.0 | Station G2 |

## Frameworks

| Framework | License | Used in |
|---|---|---|
| [Adafruit nRF52 Arduino core](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino) (MeshCore fork) | LGPL-2.1 (Arduino core); bundled parts under their own licenses, e.g. FreeRTOS (MIT), TinyUSB (MIT), Bluefruit (BSD), Nordic nrfx (BSD-3-Clause) | M1, L1, T1000-E |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) with ESP-IDF | LGPL-2.1 (Arduino core), Apache-2.0 (ESP-IDF) | Station G2 |

## Third-party code inside this repository

| Code | License |
|---|---|
| [lib/ed25519](lib/ed25519) (Orson Peters) | zlib ([license.txt](lib/ed25519/license.txt)) |
| [lib/nrf52](lib/nrf52) (Nordic SoftDevice headers) | Nordic Semiconductor license, see the file headers |
| [arch/esp32/AsyncElegantOTA](arch/esp32/AsyncElegantOTA) (Ayush Sharma) | MIT ([LICENSE](arch/esp32/AsyncElegantOTA/LICENSE)) |
| Barlow Semi Condensed font, repeater dashboard and web flasher | SIL Open Font License 1.1 ([OFL.txt](examples/simple_repeater/web/OFL.txt)) |

Other upstream MeshCore files that come from third parties carry their license in the file header.

## Web flasher

The [web flasher](https://fieldmesh-radio.github.io/FieldMesh/) uses esptool-js (Apache-2.0, with pako and atob-lite), zip.js (BSD-3-Clause), the nRF52 DFU flasher by Liam Cottle (MIT) and the Barlow font (OFL 1.1). The license texts are published with the page and linked in its footer; the sources are in [docs/FieldMesh/flasher](docs/FieldMesh/flasher).
