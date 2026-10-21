#pragma once
#include "Arduino.h"
#include <map>

class Preferences {
public:
    inline static std::map<std::string, double> values;
    bool begin(const char*, bool) { return true; }
    void end() {}
    String getString(const char*, const char* fallback) { return fallback; }
    void putString(const char*, const String&) {}
    float getFloat(const char* key, float fallback) { return get(key, fallback); }
    long getLong(const char* key, long fallback) { return get(key, fallback); }
    bool getBool(const char* key, bool fallback) { return get(key, fallback) != 0; }
    void putFloat(const char* key, float value) { values[key] = value; }
    void putLong(const char* key, long value) { values[key] = value; }
    void putBool(const char* key, bool value) { values[key] = value; }
private:
    double get(const char* key, double fallback) {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
};
