# SpoolBuddy ESP32 Pinout

Production firmware target: **ESP32-S3-WROOM-1-N16R8** on **ESP32-S3-DevKitC-1** with a 480×320 ST7796S TFT, FT6336U touch controller, PN5180 NFC reader, HX711 load cell ADC, buzzer, and onboard NeoPixel.

## ESP32 GPIO assignments

Solid colors are used for display/touch; all other wired signals use a striped variant of the same 10 colors. Trust **signal labels** over colors if your loom differs.

| Function | Signal | GPIO | Harness color | Notes |
| --- | --- | ---: | --- | --- |
| Power | VCC | — | Red | 5V input to TFT module |
| Power | GND | — | Black | Ground |
| TFT | LCD_CS | 7 | Orange | Dedicated TFT SPI chip select |
| TFT | LCD_RST | 4 | Brown | Shared with touch reset rail |
| TFT | LCD_RS | 40 | Yellow | Data/command (Register Select) |
| TFT | SDI (MOSI) | 41 | Blue | SPI3_HOST MOSI |
| TFT | SCK | 42 | Green | SPI3_HOST SCK |
| TFT | LED | 14 | White | Backlight PWM |
| TFT | SDO (MISO) | — | — | Unconnected; firmware uses write-only transfers |
| TFT | SD_CS | — | — | SD card slot on panel; unconnected in this firmware |
| Touch | CTP_SDA | 15 | Purple | I2C data |
| Touch | CTP_SCL | 16 | Gray | I2C clock |
| Touch | CTP_INT | 17 | Gray stripe | Optional interrupt, active low |
| Touch | CTP_RST | 4 (shared) | Brown (shared) | Physically tied to TFT reset |
| NFC | CS | 10 | Brown stripe | SPI2_HOST chip select |
| NFC | MOSI | 11 | Blue stripe | SPI2_HOST MOSI |
| NFC | SCK | 12 | Yellow stripe | SPI2_HOST SCK |
| NFC | MISO | 13 | Green stripe | SPI2_HOST MISO |
| NFC | BUSY | 9 | Orange stripe | PN5180 busy handshake |
| NFC | RST | 8 | Red stripe | PN5180 reset |
| HX711 | DOUT | 1 | White stripe | Load cell ADC data |
| HX711 | SCK | 2 | Black stripe | Load cell ADC clock |
| Buzzer | + | 21 | Purple stripe | Piezo drive pin |
| RGB LED | DIN | 48 | — | DevKitC onboard NeoPixel |
| Fallback input | BOOT | 0 | — | Use as touch substitute / long-press tare |

## SPI / I2C bus layout

| Bus | Host | Peripheral | Pins |
| --- | --- | --- | --- |
| TFT bus | `SPI3_HOST` | ST7796S display only | GPIO42 SCK, GPIO41 SDI, GPIO40 LCD_RS, GPIO7 LCD_CS |
| NFC bus | `SPI2_HOST` | PN5180 only | GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, GPIO10 CS |
| Touch bus | I2C port 0 | FT6336U | GPIO15 SDA, GPIO16 SCL |

The TFT and PN5180 do **not** share an SPI host, so display refreshes and NFC reads cannot contend on the same bus.

## Restricted GPIOs on ESP32-S3-WROOM-1-N16R8

Avoid these pins for application wiring on this module:

| GPIO range | Reason |
| --- | --- |
| 19–20 | Native USB D-/D+ |
| 26–32 | External flash signals |
| 33–37 | External PSRAM signals |

GPIO0 is safe for the BOOT button but should not be held low during normal runtime unless you intentionally want the fallback input action.

## Wiring instructions

1. **Power**
   - Feed the DevKitC from USB.
   - Feed the TFT module from the same 5V/GND rail recommended by the display vendor.
   - Power the PN5180 logic from **3V3** and follow the reader board's requirement for any separate RF / TVDD rail.
2. **Display**
   - Wire LCD_CS/LCD_RST/LCD_RS/SDI/SCK/LED to GPIO 7/4/40/41/42/14 respectively.
   - Leave SDO (MISO) and SD_CS unconnected; firmware uses write-only display transfers and does not use the panel SD slot.
3. **Touch**
   - Wire CTP_SDA/CTP_SCL to GPIO15/16.
   - Keep CTP_RST tied to the shared GPIO4 reset rail.
   - If the touch controller is absent or failed, the firmware still works using the BOOT button fallback.
4. **NFC**
   - Keep PN5180 on its own SPI bus: GPIO10/11/12/13 plus BUSY on GPIO9 and reset on GPIO8.
   - Do not move PN5180 onto the TFT bus in this firmware release.
5. **Scale**
   - Wire HX711 DOUT/SCK to GPIO1/GPIO2.
   - Typical 4-wire load cell colors are: **Red=E+**, **Black=E-**, **White=A-**, **Green=A+**.
6. **Feedback hardware**
   - Buzzer positive lead goes to GPIO21, negative to GND.
   - The onboard RGB LED is the single NeoPixel on GPIO48 and requires no external wiring.
