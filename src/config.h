#pragma once

#include <Arduino.h>

inline constexpr char APP_VERSION[] = "0.8.0";
inline constexpr char DEFAULT_SERVER_URL[] = "http://bambuddy.local";

inline constexpr char WIFI_NVS_NAMESPACE[] = "sbwifi";
inline constexpr char DEVICE_NVS_NAMESPACE[] = "sbcfg";

inline constexpr char PREF_WIFI_SSID[] = "ssid";
inline constexpr char PREF_WIFI_PASS[] = "pass";
inline constexpr char PREF_SERVER_URL[] = "server";
inline constexpr char PREF_API_KEY[] = "apikey";
inline constexpr char PREF_DEVICE_ID[] = "devid";
inline constexpr char PREF_SCALE_CALIBRATION[] = "scale_cal";
inline constexpr char PREF_SCALE_TARE_OFFSET[] = "scale_tare";

inline constexpr char DEVICE_ID_PREFIX[] = "spoolbuddy-";
inline constexpr char PROVISION_AP_PREFIX[] = "SpoolBuddy-";

inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
inline constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
inline constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;
inline constexpr uint32_t REGISTRATION_RETRY_INTERVAL_MS = 30000;
inline constexpr uint16_t HTTP_TIMEOUT_MS = 10000;
inline constexpr uint16_t OTA_HTTP_TIMEOUT_MS = 30000;
inline constexpr uint32_t DISPLAY_BLANK_TIMEOUT_MS = 60000;
inline constexpr uint32_t NFC_POLL_INTERVAL_MS = 250;
inline constexpr uint32_t TAG_REARM_MS = 2500;
inline constexpr uint32_t TAG_READ_TIMEOUT_MS = 1500;
inline constexpr uint32_t TAG_MATCHED_DISPLAY_MS = 8000;
inline constexpr uint32_t TAG_UNKNOWN_DISPLAY_MS = 6000;
inline constexpr uint32_t ERROR_DISPLAY_MS = 4000;
inline constexpr uint32_t TOUCH_FALLBACK_LONG_PRESS_MS = 1500;
inline constexpr uint32_t SCALE_SAMPLE_INTERVAL_MS = 150;
inline constexpr uint8_t SCALE_READ_AVERAGE_SAMPLES = 3;
inline constexpr float SCALE_STABILITY_TOLERANCE_G = 0.5f;
inline constexpr uint8_t SCALE_STABILITY_SAMPLES = 3;
inline constexpr float DEFAULT_SCALE_CALIBRATION = 2280.0f;

inline constexpr uint16_t PROVISION_DNS_PORT = 53;
inline constexpr uint16_t PROVISION_HTTP_PORT = 80;
