#pragma once
#include "Arduino.h"
#include <array>

class HostWire {
public:
    void begin(int, int, int) {}
    void beginTransmission(int) {}
    void write(int) {}
    int endTransmission(bool = true) { return 0; }
    uint8_t requestFrom(uint8_t, uint8_t) { _index = 0; return 5; }
    uint8_t read() { return _data[_index++]; }
    void touch(bool down, int x, int y) {
        int rawX = y, rawY = 479 - x;
        _data = {static_cast<uint8_t>(down), static_cast<uint8_t>(rawX >> 8),
                 static_cast<uint8_t>(rawX), static_cast<uint8_t>(rawY >> 8),
                 static_cast<uint8_t>(rawY)};
    }
private:
    std::array<uint8_t, 5> _data = {};
    size_t _index = 0;
};
inline HostWire Wire;
