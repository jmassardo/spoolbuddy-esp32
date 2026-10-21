#pragma once
#include "Arduino.h"
#include "SPI.h"
#include <vector>

class LovyanGFX {
public:
    inline static std::vector<std::string> drawnText;
    struct TextDraw {
        std::string text;
        int x, y, size;
    };
    inline static std::vector<TextDraw> textDraws;
    int textSize = 1;
    template <typename... T> void fillRect(T...) {}
    template <typename... T> void drawRect(T...) {}
    template <typename... T> void fillRoundRect(T...) {}
    template <typename... T> void drawRoundRect(T...) {}
    template <typename... T> void fillCircle(T...) {}
    template <typename... T> void drawCircle(T...) {}
    template <typename... T> void drawLine(T...) {}
    template <typename... T> void drawArc(T...) {}
    template <typename... T> void setTextDatum(T...) {}
    template <typename... T> void setTextColor(T...) {}
    void setTextSize(int size) { textSize = size; }
    template <typename... T> void setCursor(T...) {}
    template <typename... T> void print(T...) {}
    void drawString(const char* text, int x, int y) {
        drawnText.emplace_back(text);
        textDraws.push_back({text, x, y, textSize});
    }
    void init() {}
    void setRotation(int) {}
    void fillScreen(int) {}
    void fillSprite(int) {}
    void setPsram(bool) {}
    void setColorDepth(int) {}
    void* createSprite(int, int) { return this; }
    void pushSprite(LovyanGFX*, int, int) {}
    uint16_t color565(int r, int g, int b) { return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3); }
};
using LGFX_Sprite = LovyanGFX;

namespace lgfx {
inline constexpr int middle_center = 0, top_left = 1, top_center = 2, bottom_right = 3, middle_right = 4;
struct HostConfig {
    int spi_host, spi_mode, freq_write, freq_read, pin_sclk, pin_mosi, pin_miso, pin_dc;
    bool spi_3wire, use_lock;
    int dma_channel, pin_cs, pin_rst, pin_busy, panel_width, panel_height;
    int offset_x, offset_y, offset_rotation;
    bool readable, invert, rgb_order, dlen_16bit, bus_shared;
};
class Bus_SPI {
public:
    HostConfig config() { return {}; }
    void config(const HostConfig&) {}
};
class Panel_ST7796 : public Bus_SPI {
public:
    void setBus(Bus_SPI*) {}
};
class LGFX_Device : public LovyanGFX {
public:
    void setPanel(Panel_ST7796*) {}
};
}
