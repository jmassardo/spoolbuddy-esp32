#pragma once
#include "Arduino.h"
inline constexpr int NEO_GRB = 0, NEO_KHZ800 = 0;
class Adafruit_NeoPixel {
public:
    Adafruit_NeoPixel(int, int, int) {}
    void begin() {}
    void setBrightness(int) {}
    void setPixelColor(int, uint32_t) {}
    void show() {}
    uint32_t Color(int r, int g, int b) { return (r << 16) | (g << 8) | b; }
};
