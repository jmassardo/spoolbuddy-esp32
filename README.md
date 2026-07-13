# SpoolBuddy ESP32

Firmware for the SpoolBuddy — an NFC-based filament spool tracking station for 3D printers. Place a spool on the scale, scan its NFC tag, and SpoolBuddy identifies the filament, shows remaining weight, and lets you assign it to a printer slot. Integrates with a [Bambuddy](https://github.com/maziggy/bambuddy) backend for inventory management.

<!-- Screenshots coming soon — see docs/ui-simulator.html for a live preview -->

## Features

- **NFC spool scanning** — Reads Bambu Lab RFID tags (ISO14443A) and generic ISO15693 NFC tags via a PN5180 reader
- **Spool identification** — Looks up scanned tags against the Bambuddy inventory; displays material, brand, color, and remaining filament percentage
- **Spool registration** — Step-by-step wizard to register unknown tags: pick material → brand → color → label weight → core weight
- **Weight tracking** — HX711 load cell provides live weight with stability detection; automatically pushes weight updates to the backend
- **Printer assignment** — Assign spools to specific printer AMS slots (A1–A4, B1–B4, etc.) or the external spool holder
- **Clear plate** — One-tap plate clearing for printers in FINISH/FAILED state, enabling the print queue to continue
- **Scale calibration** — Built-in calibration flow with known-weight reference and tare
- **OTA firmware updates** — Automatic over-the-air updates when the backend has a newer firmware version
- **WiFi provisioning** — Captive portal AP for first-time setup (SSID, password, server URL, API key)
- **Touchscreen UI** — Full 480×320 color interface on a 4" ST7796S TFT with capacitive touch (FT6336U)
- **Status indicators** — Onboard NeoPixel LED and piezo buzzer for visual/audio feedback

## Hardware

### Bill of Materials

| Component | Part | Notes |
| --- | --- | --- |
| MCU | ESP32-S3-DevKitC-1 (N16R8) | 16 MB flash, 8 MB PSRAM (PSRAM unused by firmware) |
| Display | 4.0" 480×320 SPI TFT | ST7796S controller |
| Touch | FT6336U | Capacitive, I2C @ 0x38 |
| NFC reader | PN5180 | ISO14443A + ISO15693, dedicated SPI bus |
| Load cell ADC | HX711 | Typical 5 kg load cell |
| Buzzer | Passive piezo | GPIO-driven, for UI feedback chirps |
| LED | WS2812B NeoPixel | Onboard on DevKitC (GPIO48) |

### Wiring

See [docs/pinout.md](docs/pinout.md) for the full pin assignment table and wiring instructions.

**Quick reference:**

| Bus | Peripheral | GPIOs |
| --- | --- | --- |
| SPI3 (FSPI) | ST7796S display | SCK=42, MOSI=41, CS=7, DC=40, RST=4, BL=14 |
| I2C | FT6336U touch | SDA=15, SCL=16, INT=17 |
| SPI2 (HSPI) | PN5180 NFC | SCK=12, MOSI=11, MISO=13, CS=10, BUSY=9, RST=8 |
| GPIO | HX711 scale | DOUT=1, SCK=2 |
| GPIO | Buzzer | 21 |

The TFT and PN5180 use separate SPI buses so display refreshes and NFC reads never contend.

Wiring diagrams are available in `docs/`:
- [wiring-diagram.svg](docs/wiring-diagram.svg)
- [wiring-diagram-v2.svg](docs/wiring-diagram-v2.svg)

## Building

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)

### Build

```bash
cd spoolbuddy-esp32
pio run -e esp32s3
```

The firmware binary is output to `.pio/build/esp32s3/firmware.bin`.

### Flash via USB

Put the ESP32-S3 into boot mode (hold BOOT, press RST, release BOOT), then:

```bash
pio run -e esp32s3 -t upload
```

> **Note:** The upload port is configured as `/dev/cu.usbmodem1101` in `platformio.ini`. Adjust if your device enumerates differently. The port is only visible while the board is in boot mode.

### OTA Update

To deploy firmware over-the-air via the Bambuddy backend:

1. Build the firmware: `pio run -e esp32s3`
2. Copy the binary to the server's firmware directory:
   ```bash
   docker cp .pio/build/esp32s3/firmware.bin bambuddy:/app/data/firmware/spoolbuddy/<version>.bin
   ```
3. The device picks up the update on its next heartbeat (every ~10 seconds) when the server reports a newer `ota_version`.

The version string is defined in `src/config.h` as `APP_VERSION`. Bump it before building a release.

## First-Time Setup

1. **Power on** — The device boots and starts a WiFi access point named `SpoolBuddy-XXXXXX`.
2. **Connect** — Join the AP from your phone/laptop. A captive portal opens automatically.
3. **Configure** — Enter your WiFi SSID, password, Bambuddy server URL (e.g. `http://192.168.1.100:8000`), and API key.
4. **Save** — The device reboots, connects to your WiFi, and registers with the backend.

To reconfigure WiFi later, go to **Settings → WiFi** on the touchscreen and confirm the reset.

## User Interface

The touchscreen shows a **6-tile home screen**:

| Tile | Function |
| --- | --- |
| **Scale** | Live weight display with stability indicator |
| **Scan** | Activate NFC reader to scan a spool tag |
| **Assign** | Scan a spool then assign it to a printer/slot |
| **Tare** | Zero the scale (saves to NVS) |
| **Clear** | Clear build plate on printers that finished printing |
| **Settings** | Calibration, OTA check, WiFi reset, reboot, device info |

### Typical Workflow

1. Place a spool on the scale
2. Tap **Scan** — the NFC reader identifies the tag
3. SpoolBuddy shows filament info (material, color, brand, remaining %)
4. Tap **Assign** to assign the spool to a printer's AMS slot
5. Weight is automatically pushed to the backend

### Spool Registration

When an unrecognized tag is scanned, tap **Register** to walk through:
1. Select **material** (PLA, PETG, ABS, TPU, etc.)
2. Select **brand** (Bambu Lab, eSUN, Polymaker, etc.)
3. Select **color** from a palette
4. Enter **label weight** (250g, 500g, 1000g, or custom)
5. Enter **core weight** (weigh the empty spool or enter manually)
6. Confirm — the spool is registered and the tag is written

## Backend API

SpoolBuddy communicates with a Bambuddy server over HTTP/JSON. Key endpoints:

| Method | Endpoint | Purpose |
| --- | --- | --- |
| POST | `/api/v1/spoolbuddy/devices/register` | Device registration |
| POST | `/api/v1/spoolbuddy/devices/{id}/heartbeat` | Periodic heartbeat + OTA check |
| POST | `/api/v1/spoolbuddy/nfc/tag-scanned` | Tag lookup |
| GET | `/api/v1/inventory/spools/{id}` | Fetch spool details |
| POST | `/api/v1/inventory/spools` | Register new spool |
| DELETE | `/api/v1/inventory/spools/{id}` | Delete spool |
| GET | `/api/v1/printers/` | List printers |
| GET | `/api/v1/printers/{id}/status` | Printer status (AMS slot discovery) |
| GET | `/api/v1/inventory/assignments` | List spool assignments |
| POST | `/api/v1/inventory/assignments` | Assign spool to printer slot |
| DELETE | `/api/v1/inventory/assignments/{printer}/{ams}/{tray}` | Unassign spool |
| POST | `/api/v1/printers/{id}/clear-plate` | Clear build plate |
| POST | `/api/v1/spoolbuddy/scale/update-spool-weight` | Push weight reading |

## Project Structure

```
spoolbuddy-esp32/
├── platformio.ini          # Build configuration
├── src/
│   ├── main.cpp            # App state machine, UI event handling
│   ├── config.h            # Version, timing constants, defaults
│   ├── pins_v3.h           # GPIO pin assignments
│   ├── tft_config.h        # LovyanGFX display/bus configuration
│   ├── touch_display.cpp/h # Touchscreen UI rendering and hit testing
│   ├── api_client.cpp/h    # HTTP client for Bambuddy backend
│   ├── nfc_reader.cpp/h    # PN5180 NFC reader driver
│   ├── scale.cpp/h         # HX711 load cell interface
│   └── wifi_manager.cpp/h  # WiFi provisioning and connection management
└── docs/
    ├── pinout.md           # Detailed pin assignment table
    ├── wiring-diagram.svg  # Wiring schematic
    ├── panel-layout.svg    # Enclosure panel layout
    ├── ui-simulator.html   # Browser-based UI preview
    └── screens/            # UI screenshots
```

## License

This project is not currently licensed. All rights reserved.
