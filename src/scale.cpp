#include "scale.h"

#include <HX711.h>

#include "config.h"
#include "pins_v3.h"

namespace {
HX711 gScaleDriver;
}

bool Scale::begin(float calibrationFactor, long tareOffset) {
    _driverPtr = &gScaleDriver;
    _driverPtr->begin(PIN_HX711_DOUT, PIN_HX711_SCK);
    _driverPtr->set_scale(calibrationFactor == 0.0f ? DEFAULT_SCALE_CALIBRATION : calibrationFactor);

    if (!_driverPtr->wait_ready_timeout(1000)) {
        _healthy = false;
        return false;
    }

    if (tareOffset != 0) {
        _driverPtr->set_offset(tareOffset);
    } else {
        _driverPtr->tare(15);
    }

    _healthy = true;
    _resetHistory();
    return true;
}

void Scale::update() {
    if (!_healthy || _driverPtr == nullptr) {
        return;
    }

    uint32_t now = millis();
    if ((now - _lastSampleMs) < SCALE_SAMPLE_INTERVAL_MS) {
        return;
    }
    _lastSampleMs = now;

    if (!_driverPtr->wait_ready_timeout(10)) {
        return;
    }

    _rawReading = _driverPtr->read_average(SCALE_READ_AVERAGE_SAMPLES);
    float scaleFactor = _driverPtr->get_scale();
    if (scaleFactor == 0.0f) {
        scaleFactor = DEFAULT_SCALE_CALIBRATION;
    }

    _weightGrams = (_rawReading - _driverPtr->get_offset()) / scaleFactor;
    if (abs(_weightGrams) < 0.25f) {
        _weightGrams = 0.0f;
    }

    _pushHistory(_weightGrams);
    if (_historyCount >= SCALE_STABILITY_SAMPLES) {
        float minValue = _history[0];
        float maxValue = _history[0];
        for (uint8_t i = 1; i < SCALE_STABILITY_SAMPLES; ++i) {
            minValue = min(minValue, _history[i]);
            maxValue = max(maxValue, _history[i]);
        }
        _stable = (maxValue - minValue) <= SCALE_STABILITY_TOLERANCE_G;
    } else {
        _stable = false;
    }
}

bool Scale::tare(uint8_t samples) {
    if (!_healthy || _driverPtr == nullptr || !_driverPtr->wait_ready_timeout(1000)) {
        return false;
    }
    _driverPtr->tare(samples);
    _weightGrams = 0.0f;
    _rawReading = _driverPtr->get_offset();
    _resetHistory();
    return true;
}

void Scale::updateCalibration(float calibrationFactor, long tareOffset) {
    if (_driverPtr == nullptr) {
        return;
    }

    _driverPtr->set_scale(calibrationFactor == 0.0f ? DEFAULT_SCALE_CALIBRATION : calibrationFactor);
    _driverPtr->set_offset(tareOffset);
    _rawReading = tareOffset;
    _weightGrams = 0.0f;
    _resetHistory();
}

long Scale::tareOffset() const {
    return (_driverPtr != nullptr) ? _driverPtr->get_offset() : 0;
}

void Scale::_resetHistory() {
    _historyCount = 0;
    _historyIndex = 0;
    _stable = false;
    for (float& sample : _history) {
        sample = 0.0f;
    }
}

void Scale::_pushHistory(float weight) {
    _history[_historyIndex] = weight;
    _historyIndex = (_historyIndex + 1) % SCALE_STABILITY_SAMPLES;
    if (_historyCount < SCALE_STABILITY_SAMPLES) {
        ++_historyCount;
    }
}
