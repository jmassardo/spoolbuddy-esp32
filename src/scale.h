#pragma once

#include <Arduino.h>

class Scale {
public:
    bool begin(float calibrationFactor, long tareOffset = 0);
    void update();
    bool tare(uint8_t samples = 15);
    void updateCalibration(float calibrationFactor, long tareOffset);

    bool isHealthy() const { return _healthy; }
    bool isStable() const { return _stable; }
    float weightGrams() const { return _weightGrams; }
    long rawReading() const { return _rawReading; }
    long tareOffset() const;

private:
    class HX711* _driverPtr = nullptr;
    bool _healthy = false;
    bool _stable = false;
    float _weightGrams = 0.0f;
    long _rawReading = 0;
    float _history[3] = {0.0f, 0.0f, 0.0f};
    uint8_t _historyCount = 0;
    uint8_t _historyIndex = 0;
    uint32_t _lastSampleMs = 0;

    void _resetHistory();
    void _pushHistory(float weight);
};
