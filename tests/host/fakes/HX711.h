#pragma once
#include "Arduino.h"

class HX711 {
public:
    inline static bool ready = true;
    inline static long nextReading = 100000;
    inline static int reads = 0;
    inline static int waits = 0;
    void begin(int, int) {}
    void set_scale(float factor) { _factor = factor; }
    float get_scale() const { return _factor; }
    void set_offset(long offset) { _offset = offset; }
    long get_offset() const { return _offset; }
    bool is_ready() const { return ready; }
    bool wait_ready_timeout(unsigned long) { ++waits; return ready; }
    long read() { ++reads; return nextReading; }
    long read_average(uint8_t samples) {
        delay(samples * 100);
        long total = 0;
        for (uint8_t i = 0; i < samples; ++i) total += read();
        return total / samples;
    }
    void tare(uint8_t samples) { _offset = read_average(samples); }
private:
    float _factor = 1;
    long _offset = 0;
};
