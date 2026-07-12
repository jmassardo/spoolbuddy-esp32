#pragma once
// LovyanGFX configuration for Hosyond 4.0" ST7796S + FT6336U
// ESP32-S3, TFT on SPI3_HOST, touch on I2C, no PSRAM allocations.

#include <LovyanGFX.hpp>
#include "pins_v3.h"

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Panel_ST7796  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;

    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = PIN_TFT_SCK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = PIN_TFT_MISO;
            cfg.pin_dc = PIN_TFT_DC;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = PIN_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.readable = false;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel_instance.config(cfg);
        }

        // Backlight is managed manually via digitalWrite in main.cpp
        // (LovyanGFX LEDC/PWM conflicts with ESP32-S3 on some boards)

        // Touch is handled directly via Wire in touch_display.cpp
        // (LovyanGFX Touch_FT5x06 driver doesn't work with this module)

        setPanel(&_panel_instance);
    }
};
