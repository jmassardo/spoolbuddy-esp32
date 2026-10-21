#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using std::abs;
using std::max;
using std::min;

class String {
public:
    String(const char* value = "") : _value(value) {}
    String(const std::string& value) : _value(value) {}
    String(int value) : _value(std::to_string(value)) {}
    const char* c_str() const { return _value.c_str(); }
    bool isEmpty() const { return _value.empty(); }
    size_t length() const { return _value.length(); }
    friend String operator+(const String& a, const String& b) { return a._value + b._value; }
    friend bool operator==(const String& a, const String& b) { return a._value == b._value; }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
private:
    std::string _value;
};

inline uint32_t hostMillis = 1000;
inline uint32_t millis() { return hostMillis; }
inline void delay(uint32_t ms) { hostMillis += ms; }
inline constexpr int HIGH = 1, LOW = 0, OUTPUT = 1, INPUT_PULLUP = 2;
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void pinMode(int, int) {}
inline void tone(int, int, int) {}
template <typename T> T constrain(T value, T low, T high) { return std::clamp(value, low, high); }

struct HostSerial {
    void begin(int) {}
    explicit operator bool() const { return false; }
    template <typename... Args> void printf(Args...) {}
    void println(const char*) {}
};
inline HostSerial Serial;
struct HostEsp {
    uint64_t getEfuseMac() { return 123; }
    void restart() {}
};
inline HostEsp ESP;
