#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <cmath>

#include <Adafruit_NeoPixel.h>

#include "api_client.h"
#include "config.h"
#include "nfc_reader.h"
#include "pins_v3.h"
#include "scale.h"
#include "touch_display.h"
#include "wifi_manager.h"

enum class AppState {
    Boot,
    Provisioning,
    Connecting,
    Home,
    Scale,
    Info,
    Settings,
    TagRead,
    TagMatched,
    ConfirmDelete,
    ConfirmWifiReset,
    PrinterList,
    TagUnknown,
    Register,
    Calibrate,
    Ota,
    Error,
};

enum class RegisterStep {
    Material,
    Brand,
    Color,
    Weight,
    WeightNumpad,
    CoreWeight,
    CoreWeightCustom,
    CoreWeightNumpad,
    Confirm,
};

TouchDisplay display;
WifiManager wifiManager;
ApiClient apiClient;
NfcReader nfcReader;
Scale scale;
Adafruit_NeoPixel statusPixel(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

AppState appState = AppState::Boot;
uint32_t stateExpiresAt = 0;
uint32_t lastHeartbeatAt = 0;
uint32_t lastWeightPushAt = 0;
uint32_t lastTagSeenAt = 0;
uint32_t lastRegistrationAttemptAt = 0;

String deviceId;
String currentServerUrl;
String activeTagUid;
String matchedTagUid;
int matchedSpoolId = 0;
SpoolInfo matchedSpoolInfo = {};
SpoolAssignment matchedAssignment;
bool matchedSpoolAssigned = false;
PrinterInfo availablePrinters[16];
int availablePrinterCount = 0;
int lastPendingWriteSpoolIdSeen = 0;
bool backendConnected = false;
bool deviceRegistered = false;
float lastReportedWeight = NAN;
float scaleCalibration = DEFAULT_SCALE_CALIBRATION;
long scaleTareOffset = 0;
RegisterStep registerStep = RegisterStep::Material;
int customLabelWeight = 0;  // > 0 when user entered custom weight via numpad
int customCoreWeight = 0;   // > 0 when user entered/weighed custom core weight
String registrationTagUid;

enum class CalStep { Weight, Tare, Weigh, Numpad };
CalStep calStep = CalStep::Weight;
float calKnownWeight = 1000.0f;
long calTareRaw = 0;

constexpr int REGISTER_CORE_WEIGHT_GRAMS = 250;
constexpr int MAX_PRINTER_CHOICES = 16;
constexpr int EXTERNAL_SPOOL_AMS_ID = 255;
constexpr int EXTERNAL_SPOOL_TRAY_ID = 0;

void setPixelColor(uint32_t color);
void chirp(uint16_t frequency, uint16_t durationMs);
void loadDeviceConfig();
void saveScaleConfig();
String generateDeviceId();
const char* tagTypeToString(NfcTagType type);
void applyDisplaySettings(int brightness, uint32_t blankTimeoutSeconds);
void applyCalibrationSettings(long tareOffset, float calibrationFactor);
void applyRegistrationResponse(const DeviceRegistrationResponse& response);
void attemptDeviceRegistration(uint32_t now);
void handleHeartbeatResponse(const HeartbeatResponse& response);
void showHomeScreen();
void showScaleScreen();
void showInfoScreen();
void showProvisioningScreen();
void showConnectingScreen();
void showSettingsScreen();
void showErrorScreen(const char* message);
void updateStatusBar();
void handleUiAction(bool longPress);
void handleHomeTileTap(HomeTile tile);
void handleTagLookup(const NfcTag& tag);
void startRegistrationFlow(const String& tagUid);
void showRegistrationStep();
void handleRegistrationTouch(TouchEvent touch);
void submitRegistration();
void cancelRegistrationFlow();
void startCalibration();
void handleCalibrationTouch(TouchEvent touch);
void checkForOta(uint32_t now);
void pushMatchedWeightIfNeeded(uint32_t now);
void resetMatchedSpoolState(bool clearActiveTag = false);
void showMatchedSpoolScreen(bool playTone = false);
void showActionResultThenHome(bool success, const char* message);

void otaProgressCallback(int percent, const char* version) {
    display.showOtaProgress(percent, version);
    display.loop();
}

String generateDeviceId() {
    uint64_t mac = ESP.getEfuseMac();
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s%06llX", DEVICE_ID_PREFIX, static_cast<unsigned long long>(mac & 0xFFFFFFULL));
    return String(buffer);
}

void loadDeviceConfig() {
    Preferences prefs;
    prefs.begin(DEVICE_NVS_NAMESPACE, false);

    deviceId = prefs.getString(PREF_DEVICE_ID, "");
    if (deviceId.isEmpty()) {
        deviceId = generateDeviceId();
        prefs.putString(PREF_DEVICE_ID, deviceId);
    }

    scaleCalibration = prefs.getFloat(PREF_SCALE_CALIBRATION, DEFAULT_SCALE_CALIBRATION);
    if (scaleCalibration == 0.0f) {
        scaleCalibration = DEFAULT_SCALE_CALIBRATION;
        prefs.putFloat(PREF_SCALE_CALIBRATION, scaleCalibration);
    }

    scaleTareOffset = prefs.getLong(PREF_SCALE_TARE_OFFSET, 0);
    prefs.end();
}

void saveScaleConfig() {
    Preferences prefs;
    prefs.begin(DEVICE_NVS_NAMESPACE, false);
    prefs.putFloat(PREF_SCALE_CALIBRATION, scaleCalibration);
    prefs.putLong(PREF_SCALE_TARE_OFFSET, scaleTareOffset);
    prefs.end();
}

const char* tagTypeToString(NfcTagType type) {
    switch (type) {
        case NfcTagType::Iso15693:
            return "iso15693";
        case NfcTagType::Iso14443A:
            return "iso14443a";
        case NfcTagType::None:
        default:
            return "unknown";
    }
}

void applyDisplaySettings(int brightness, uint32_t blankTimeoutSeconds) {
    display.setBrightness(constrain(brightness, 0, 100));
    display.setBlankTimeout(blankTimeoutSeconds == 0 ? 0 : blankTimeoutSeconds * 1000UL);
}

void applyCalibrationSettings(long tareOffset, float calibrationFactor) {
    bool changed = false;
    if (tareOffset != scaleTareOffset) {
        scaleTareOffset = tareOffset;
        changed = true;
    }
    // Backend stores factor as known_weight/delta (grams per count)
    // HX711 library uses scale as delta/known_weight (counts per gram)
    // Convert: local = 1/backend
    if (calibrationFactor != 0.0f) {
        float localFactor = 1.0f / calibrationFactor;
        if (localFactor != scaleCalibration) {
            scaleCalibration = localFactor;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }

    scale.updateCalibration(scaleCalibration, scaleTareOffset);
    saveScaleConfig();
}

void applyRegistrationResponse(const DeviceRegistrationResponse& response) {
    if (response.hasCalibration) {
        applyCalibrationSettings(response.tareOffset, response.calibrationFactor);
    }
    if (response.hasDisplaySettings) {
        applyDisplaySettings(response.displayBrightness, response.displayBlankTimeout);
    }
}

void attemptDeviceRegistration(uint32_t now) {
    if (!wifiManager.isConnected() || appState == AppState::Provisioning || deviceRegistered) {
        return;
    }
    if (lastRegistrationAttemptAt != 0 && (now - lastRegistrationAttemptAt) < REGISTRATION_RETRY_INTERVAL_MS) {
        return;
    }

    lastRegistrationAttemptAt = now;
    DeviceRegistrationResponse response;
    if (!apiClient.registerDevice(scaleTareOffset, scaleCalibration, response)) {
        backendConnected = false;
        return;
    }

    deviceRegistered = true;
    backendConnected = true;
    applyRegistrationResponse(response);
}

void handleHeartbeatResponse(const HeartbeatResponse& response) {
    bool skipCalibrationSync = false;

    if (response.pendingCommand == "tare") {
        if (scale.tare()) {
            scaleTareOffset = scale.tareOffset();
            saveScaleConfig();
            if (!apiClient.postTareOffset(scaleTareOffset)) {
                backendConnected = false;
            }
        }
        skipCalibrationSync = true;
    } else if (response.pendingCommand == "write_tag") {
        if (response.pendingWritePayload.present && response.pendingWritePayload.spoolId > 0 &&
            response.pendingWritePayload.spoolId != lastPendingWriteSpoolIdSeen) {
            lastPendingWriteSpoolIdSeen = response.pendingWritePayload.spoolId;
            Serial.printf("[API] write_tag requested for spool %d but NFC writing is not implemented in this firmware\n",
                          response.pendingWritePayload.spoolId);
        }
    } else if (response.pendingCommand == "reboot") {
        Serial.println("[API] reboot command received");
        delay(250);
        ESP.restart();
    } else if (!response.pendingCommand.isEmpty()) {
        Serial.printf("[API] unhandled command: %s\n", response.pendingCommand.c_str());
    } else {
        lastPendingWriteSpoolIdSeen = 0;
    }

    if (!skipCalibrationSync && response.hasCalibration) {
        applyCalibrationSettings(response.tareOffset, response.calibrationFactor);
    }
    if (response.hasDisplaySettings) {
        applyDisplaySettings(response.displayBrightness, response.displayBlankTimeout);
    }
}

void setPixelColor(uint32_t color) {
    statusPixel.setPixelColor(0, color);
    statusPixel.show();
}

void chirp(uint16_t frequency, uint16_t durationMs) {
    tone(PIN_BUZZER, frequency, durationMs);
}

void resetMatchedSpoolState(bool clearActiveTag) {
    if (clearActiveTag) {
        activeTagUid = "";
    }
    matchedTagUid = "";
    matchedSpoolId = 0;
    matchedSpoolInfo = SpoolInfo{};
    matchedAssignment = SpoolAssignment{};
    matchedSpoolAssigned = false;
    availablePrinterCount = 0;
    lastReportedWeight = NAN;
}

void showMatchedSpoolScreen(bool playTone) {
    display.showTagMatched(
        matchedSpoolInfo,
        matchedSpoolAssigned,
        matchedSpoolAssigned ? matchedAssignment.printerName.c_str() : "");
    appState = AppState::TagMatched;
    stateExpiresAt = millis() + TAG_MATCHED_DISPLAY_MS;
    setPixelColor(statusPixel.Color(0, 22, 10));
    if (playTone) {
        chirp(1760, 80);
    }
}

void showActionResultThenHome(bool success, const char* message) {
    display.showTagWriteResult(success, message);
    display.loop();
    setPixelColor(success ? statusPixel.Color(0, 24, 8) : statusPixel.Color(24, 0, 0));
    chirp(success ? 1760 : 330, success ? 80 : 120);
    delay(1200);
    resetMatchedSpoolState();
    showHomeScreen();
}

void showHomeScreen() {
    appState = AppState::Home;
    stateExpiresAt = 0;
    display.showHome(scale.weightGrams(), scale.isStable());
    setPixelColor(statusPixel.Color(0, 24, 0));
}

void showScaleScreen() {
    appState = AppState::Scale;
    stateExpiresAt = 0;
    display.showIdle(scale.weightGrams(), scale.isStable());
    setPixelColor(statusPixel.Color(0, 24, 0));
}

void showInfoScreen() {
    appState = AppState::Info;
    stateExpiresAt = 0;

    String line1 = "ID: " + deviceId;
    String line2 = wifiManager.isConnected() ? ("IP: " + wifiManager.ipAddress()) : String("IP: offline");
    String line3 = "API: " + currentServerUrl;
    display.showStatus("SpoolBuddy", line1.c_str(), line2.c_str(), line3.c_str());
    setPixelColor(statusPixel.Color(0, 18, 18));
}

void showProvisioningScreen() {
    appState = AppState::Provisioning;
    stateExpiresAt = 0;
    display.showProvisioning(wifiManager.accessPointName().c_str());
    setPixelColor(statusPixel.Color(24, 10, 0));
}

void showConnectingScreen() {
    appState = AppState::Connecting;
    stateExpiresAt = 0;
    display.showConnecting(wifiManager.ssid().c_str());
    setPixelColor(statusPixel.Color(0, 0, 24));
}

void showSettingsScreen() {
    appState = AppState::Settings;
    stateExpiresAt = 0;
    display.showSettingsGrid();
    setPixelColor(statusPixel.Color(18, 18, 0));
}

void showRegistrationStep() {
    appState = AppState::Register;
    stateExpiresAt = 0;

    switch (registerStep) {
        case RegisterStep::Material:
            display.showRegMaterial();
            break;
        case RegisterStep::Brand:
            display.showRegBrand();
            break;
        case RegisterStep::Color:
            display.showRegColor();
            break;
        case RegisterStep::Weight:
            display.showRegWeight();
            break;
        case RegisterStep::CoreWeight:
            display.showRegCoreWeight();
            break;
        case RegisterStep::Confirm: {
            int cw = (customCoreWeight > 0) ? customCoreWeight : REGISTER_CORE_WEIGHT_GRAMS;
            display.showRegConfirm(scale.weightGrams(), scale.isStable(), cw);
            break;
        }
        default:
            break;
    }
}

void startRegistrationFlow(const String& tagUid) {
    registrationTagUid = tagUid;
    registerStep = RegisterStep::Material;
    customLabelWeight = 0;
    customCoreWeight = 0;
    display.regReset();
    showRegistrationStep();
    setPixelColor(statusPixel.Color(0, 16, 24));
}

void cancelRegistrationFlow() {
    registrationTagUid = "";
    resetMatchedSpoolState(true);
    showHomeScreen();
}

void startCalibration() {
    calStep = CalStep::Weight;
    calTareRaw = 0;
    calKnownWeight = 1000.0f;
    appState = AppState::Calibrate;
    stateExpiresAt = 0;
    display.showCalWeight();
    setPixelColor(statusPixel.Color(24, 18, 0));
}

void handleCalibrationTouch(TouchEvent touch) {
    if (calStep == CalStep::Weight) {
        if (touch != TouchEvent::TAP) return;
        TouchPoint tp = display.lastTouchPoint();
        if (display.hitTestBackButton(tp.x, tp.y)) {
            appState = AppState::Settings;
            display.showSettingsGrid();
            chirp(660, 30);
            return;
        }
        int btn = display.hitTestCalWeight(tp.x, tp.y);
        if (btn == 0) { calKnownWeight = 250; }
        else if (btn == 1) { calKnownWeight = 500; }
        else if (btn == 2) { calKnownWeight = 1000; }
        else if (btn == 3) {
            calStep = CalStep::Numpad;
            display.showNumpad("Enter weight (grams)");
            chirp(880, 30);
            return;
        } else { return; }

        chirp(880, 30);
        calStep = CalStep::Tare;
        display.showStatus("Cal: Tare", "Remove ALL weight", "from scale, then tap", "anywhere to tare");

    } else if (calStep == CalStep::Numpad) {
        if (touch != TouchEvent::TAP) return;
        TouchPoint tp = display.lastTouchPoint();
        int key = display.hitTestNumpadKey(tp.x, tp.y);
        if (key >= 0 && key <= 9) {
            display.numpadAppend('0' + key);
            chirp(660, 20);
        } else if (key == 10) {
            display.numpadBackspace();
            chirp(440, 20);
        } else if (key == 11) {
            int val = display.numpadValue();
            if (val < 10 || val > 10000) {
                display.showStatus("Invalid", "Enter 10-10000g", "", "");
                delay(1000);
                display.showNumpad("Enter weight (grams)");
                return;
            }
            calKnownWeight = static_cast<float>(val);
            chirp(880, 30);
            calStep = CalStep::Tare;
            display.showStatus("Cal: Tare", "Remove ALL weight", "from scale, then tap", "anywhere to tare");
        }

    } else if (calStep == CalStep::Tare) {
        if (touch != TouchEvent::TAP) return;
        if (!scale.tare()) {
            display.showStatus("Cal: Tare", "Tare failed!", "Scale not ready", "Tap to retry");
            return;
        }
        calTareRaw = scale.tareOffset();
        scaleTareOffset = calTareRaw;
        saveScaleConfig();
        chirp(880, 60);

        calStep = CalStep::Weigh;
        char msg[48];
        snprintf(msg, sizeof(msg), "Place %.0fg weight", calKnownWeight);
        display.showStatus("Cal: Measure", msg, "on scale, then tap", "anywhere to finish");
        setPixelColor(statusPixel.Color(0, 18, 24));
    } else {
        if (touch != TouchEvent::TAP) return;
        long rawNow = scale.rawReading();
        long delta = rawNow - calTareRaw;
        if (delta == 0) {
            display.showStatus("Cal: Measure", "No weight detected!", "Place weight on scale", "Tap to retry");
            chirp(330, 120);
            return;
        }

        scaleCalibration = static_cast<float>(delta) / calKnownWeight;
        scale.updateCalibration(scaleCalibration, scaleTareOffset);
        saveScaleConfig();

        apiClient.postTareOffset(scaleTareOffset);
        apiClient.postCalibrationFactor(calKnownWeight, rawNow, calTareRaw);

        char buf[48];
        snprintf(buf, sizeof(buf), "Reading: %.1fg", scale.weightGrams());
        display.showStatus("Calibrated!", buf, "Scale is ready", "");
        setPixelColor(statusPixel.Color(0, 24, 0));
        chirp(1760, 80);
        delay(100);
        chirp(2200, 80);

        appState = AppState::Info;
        stateExpiresAt = millis() + 4000;
    }
}

void submitRegistration() {
    if (registrationTagUid.isEmpty()) {
        showErrorScreen("No tag to register");
        return;
    }

    char rgba[9];
    snprintf(rgba, sizeof(rgba), "%08lX", static_cast<unsigned long>(display.regSelectedColorRgba()));

    display.showStatus("Registering", "Saving spool...", registrationTagUid.c_str(), "");
    display.loop();
    setPixelColor(statusPixel.Color(0, 8, 24));

    int labelWeight = (customLabelWeight > 0) ? customLabelWeight : display.regSelectedLabelWeight();
    int coreWeight = (customCoreWeight > 0) ? customCoreWeight : REGISTER_CORE_WEIGHT_GRAMS;
    bool ok = apiClient.registerSpool(
        registrationTagUid,
        display.regSelectedMaterialName(),
        display.regSelectedBrandName(),
        display.regSelectedColorName(),
        rgba,
        labelWeight,
        coreWeight);
    backendConnected = ok;

    registrationTagUid = "";
    resetMatchedSpoolState(true);
    appState = AppState::Info;
    stateExpiresAt = millis() + 4000;

    if (ok) {
        display.showTagWriteResult(true, "Spool registered");
        setPixelColor(statusPixel.Color(0, 24, 8));
        chirp(1760, 80);
    } else {
        display.showTagWriteResult(false, "Registration failed");
        setPixelColor(statusPixel.Color(24, 0, 0));
        chirp(330, 120);
    }
}

void handleRegistrationTouch(TouchEvent touch) {
    // Back button — go to previous step (or cancel from first step)
    if (touch == TouchEvent::TAP) {
        TouchPoint tp = display.lastTouchPoint();
        if (display.hitTestBackButton(tp.x, tp.y)) {
            chirp(660, 30);
            switch (registerStep) {
                case RegisterStep::Material:
                    cancelRegistrationFlow();
                    return;
                case RegisterStep::Brand:
                    registerStep = RegisterStep::Material;
                    showRegistrationStep();
                    return;
                case RegisterStep::Color:
                    registerStep = RegisterStep::Brand;
                    showRegistrationStep();
                    return;
                case RegisterStep::Weight:
                    registerStep = RegisterStep::Color;
                    showRegistrationStep();
                    return;
                case RegisterStep::CoreWeight:
                    registerStep = RegisterStep::Weight;
                    showRegistrationStep();
                    return;
                case RegisterStep::CoreWeightCustom:
                    registerStep = RegisterStep::CoreWeight;
                    showRegistrationStep();
                    return;
                default:
                    break;
            }
        }
    }

    switch (registerStep) {
        case RegisterStep::Material:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int idx = display.hitTestRegMaterial(tp.x, tp.y);
                if (idx >= 0) {
                    display.regSetMaterialIdx(idx);
                    chirp(880, 30);
                    registerStep = RegisterStep::Brand;
                    showRegistrationStep();
                }
            }
            break;
        case RegisterStep::Brand:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int idx = display.hitTestRegBrand(tp.x, tp.y);
                if (idx >= 0) {
                    display.regSetBrandIdx(idx);
                    chirp(880, 30);
                    registerStep = RegisterStep::Color;
                    showRegistrationStep();
                }
            }
            break;
        case RegisterStep::Color:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int colorIdx = display.hitTestRegColorTile(tp.x, tp.y);
                if (colorIdx >= 0) {
                    display.regSelectColor(colorIdx);
                    registerStep = RegisterStep::Weight;
                    showRegistrationStep();
                }
            }
            break;
        case RegisterStep::Weight:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int btn = display.hitTestRegWeight(tp.x, tp.y);
                if (btn == 0) { display.regSetWeightIdx(0); }       // 250g
                else if (btn == 1) { display.regSetWeightIdx(1); }  // 500g
                else if (btn == 2) { display.regSetWeightIdx(3); }  // 1000g (idx 3 in array)
                else if (btn == 3) {
                    registerStep = RegisterStep::WeightNumpad;
                    display.showNumpad("Label weight (grams)");
                    chirp(880, 30);
                    break;
                } else { break; }
                chirp(880, 30);
                registerStep = RegisterStep::CoreWeight;
                showRegistrationStep();
            }
            break;
        case RegisterStep::WeightNumpad:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int key = display.hitTestNumpadKey(tp.x, tp.y);
                if (key >= 0 && key <= 9) {
                    display.numpadAppend('0' + key);
                    chirp(660, 20);
                } else if (key == 10) {
                    display.numpadBackspace();
                    chirp(440, 20);
                } else if (key == 11) {
                    int val = display.numpadValue();
                    if (val < 100 || val > 5000) {
                        display.showStatus("Invalid", "Enter 100-5000g", "", "");
                        delay(1000);
                        display.showNumpad("Label weight (grams)");
                        break;
                    }
                    customLabelWeight = val;
                    chirp(880, 30);
                    registerStep = RegisterStep::CoreWeight;
                    showRegistrationStep();
                }
            }
            break;
        case RegisterStep::CoreWeight:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int btn = display.hitTestRegCoreWeight(tp.x, tp.y);
                if (btn == 0) { customCoreWeight = 250; }       // Bambu Low Temp
                else if (btn == 1) { customCoreWeight = 216; }  // Bambu High Temp
                else if (btn == 2) {
                    // Custom — show Weigh/Type choice
                    registerStep = RegisterStep::CoreWeightCustom;
                    display.showRegCoreCustom();
                    chirp(880, 30);
                    break;
                } else { break; }
                chirp(880, 30);
                registerStep = RegisterStep::Confirm;
                showRegistrationStep();
            }
            break;
        case RegisterStep::CoreWeightCustom:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int btn = display.hitTestRegCoreCustom(tp.x, tp.y);
                if (btn == 0) {
                    // Weigh — use current scale reading
                    float w = scale.weightGrams();
                    if (w < 50.0f || w > 500.0f) {
                        display.showStatus("Invalid", "Scale: place empty spool", "", "");
                        delay(1500);
                        display.showRegCoreCustom();
                        break;
                    }
                    customCoreWeight = static_cast<int>(roundf(w));
                    chirp(880, 30);
                    registerStep = RegisterStep::Confirm;
                    showRegistrationStep();
                } else if (btn == 1) {
                    // Type — open numpad
                    registerStep = RegisterStep::CoreWeightNumpad;
                    display.showNumpad("Core weight (grams)");
                    chirp(880, 30);
                }
            }
            break;
        case RegisterStep::CoreWeightNumpad:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                int key = display.hitTestNumpadKey(tp.x, tp.y);
                if (key >= 0 && key <= 9) {
                    display.numpadAppend('0' + key);
                    chirp(660, 20);
                } else if (key == 10) {
                    display.numpadBackspace();
                    chirp(440, 20);
                } else if (key == 11) {
                    int val = display.numpadValue();
                    if (val < 50 || val > 500) {
                        display.showStatus("Invalid", "Enter 50-500g", "", "");
                        delay(1000);
                        display.showNumpad("Core weight (grams)");
                        break;
                    }
                    customCoreWeight = val;
                    chirp(880, 30);
                    registerStep = RegisterStep::Confirm;
                    showRegistrationStep();
                }
            }
            break;
        case RegisterStep::Confirm:
            if (touch == TouchEvent::TAP) {
                TouchPoint tp = display.lastTouchPoint();
                if (display.hitTestRegConfirmCancel(tp.x, tp.y)) {
                    cancelRegistrationFlow();
                } else if (display.hitTestRegConfirmRegister(tp.x, tp.y)) {
                    submitRegistration();
                }
            }
            break;
    }
}

void handleHomeTileTap(HomeTile tile) {
    display.wake();
    switch (tile) {
        case HomeTile::SCALE:
            showScaleScreen();
            break;
        case HomeTile::SCAN_SPOOL:
            display.showStatus("Scan Spool", "Place tag on reader", "", "");
            appState = AppState::TagRead;
            stateExpiresAt = millis() + 30000;
            setPixelColor(statusPixel.Color(0, 12, 24));
            break;
        case HomeTile::ASSIGN:
            display.showStatus("Assign Spool", "Scan spool tag, then", "place on NFC reader", "");
            appState = AppState::Info;
            stateExpiresAt = millis() + 8000;
            break;
        case HomeTile::TARE:
            if (scale.tare()) {
                scaleTareOffset = scale.tareOffset();
                saveScaleConfig();
                display.showStatus("Scale Tared", "Empty scale stored", "Weight reset to zero", "");
                chirp(1320, 90);
            } else {
                display.showStatus("Tare Failed", "Scale not ready", "Try again", "");
            }
            appState = AppState::Info;
            stateExpiresAt = millis() + 2000;
            break;
        case HomeTile::INFO:
            showInfoScreen();
            break;
        case HomeTile::SETTINGS:
            showSettingsScreen();
            break;
        default:
            break;
    }
}

void showErrorScreen(const char* message) {
    appState = AppState::Error;
    stateExpiresAt = millis() + ERROR_DISPLAY_MS;
    display.showError(message);
    setPixelColor(statusPixel.Color(24, 0, 0));
    chirp(330, 120);
}

void updateStatusBar() {
    display.setWifiConnected(wifiManager.isConnected());
    display.setWifiRSSI(wifiManager.rssi());
    display.setBackendConnected(backendConnected);
    display.setNfcOk(nfcReader.isHealthy());
    display.setScaleOk(scale.isHealthy());
    display.setServerUrl(currentServerUrl.c_str());
    display.setFirmwareVersion(APP_VERSION);
}

void handleUiAction(bool longPress) {
    display.wake();

    if (longPress) {
        // Long press on Settings = go home
        if (appState == AppState::Settings) {
            showHomeScreen();
            return;
        }
        // Long press on Scale = tare
        if ((appState == AppState::Home || appState == AppState::Scale) && scale.tare()) {
            scaleTareOffset = scale.tareOffset();
            saveScaleConfig();
            appState = AppState::Info;
            stateExpiresAt = millis() + 2000;
            display.showStatus("Scale Tared", "Empty scale stored", "Weight reset to zero", "");
            setPixelColor(statusPixel.Color(0, 24, 8));
            chirp(1320, 90);
        }
        return;
    }

    switch (appState) {
        case AppState::Scale:
        case AppState::Info:
        case AppState::TagMatched:
        case AppState::TagUnknown:
        case AppState::Error:
            showHomeScreen();
            break;
        default:
            break;
    }
}

void handleTagLookup(const NfcTag& tag) {
    activeTagUid = tag.uid;
    registrationTagUid = "";
    appState = AppState::TagRead;
    stateExpiresAt = millis() + TAG_READ_TIMEOUT_MS;
    display.showTagRead(tag.uid.c_str(), "Looking up spool...");
    setPixelColor(statusPixel.Color(0, 12, 24));
    chirp(1040, 60);
    display.loop();

    int spoolId = 0;
    ApiClient::LookupResult result = apiClient.reportTagScanned(tag.uid, String(), tag.sak, tagTypeToString(tag.type), spoolId);
    if (result == ApiClient::LookupResult::Success) {
        backendConnected = true;
        matchedTagUid = tag.uid;
        matchedSpoolId = spoolId;
        matchedAssignment = SpoolAssignment{};
        matchedSpoolAssigned = false;

        SpoolRecord record;
        if (!apiClient.getSpoolById(spoolId, record)) {
            record = SpoolRecord{};
            record.spoolId = spoolId;
            record.uid = tag.uid;
            record.material = "Filament";
            record.colorName = "Spool #" + String(spoolId);
            record.brand = "BamBuddy";
            record.colorHex = 0x4B5563;
        }

        SpoolInfo spool = {};
        strncpy(spool.material, record.material.c_str(), sizeof(spool.material) - 1);
        strncpy(spool.colorName, record.colorName.c_str(), sizeof(spool.colorName) - 1);
        strncpy(spool.brand, record.brand.c_str(), sizeof(spool.brand) - 1);
        spool.colorHex = record.colorHex == 0 ? 0x4B5563 : record.colorHex;
        spool.weightUsed = record.weightUsed;
        spool.labelWeight = record.labelWeight;
        matchedSpoolInfo = spool;

        if (apiClient.getSpoolAssignment(spoolId, matchedAssignment)) {
            matchedSpoolAssigned = true;
            if (matchedAssignment.printerName.isEmpty()) {
                matchedAssignment.printerName = "Printer #" + String(matchedAssignment.printerId);
            }
        }

        showMatchedSpoolScreen(true);

        if (scale.isStable() && apiClient.postSpoolWeight(spoolId, scale.weightGrams(), true)) {
            lastReportedWeight = scale.weightGrams();
            lastWeightPushAt = millis();
        }
        return;
    }

    if (result == ApiClient::LookupResult::NotFound) {
        backendConnected = true;
        resetMatchedSpoolState();
        display.showTagUnknown(tag.uid.c_str());
        appState = AppState::TagUnknown;
        stateExpiresAt = millis() + TAG_UNKNOWN_DISPLAY_MS;
        setPixelColor(statusPixel.Color(24, 14, 0));
        chirp(620, 120);
        return;
    }

    backendConnected = false;
    resetMatchedSpoolState();
    showErrorScreen("Spool lookup failed");
}

void checkForOta(uint32_t now) {
    if (!wifiManager.isConnected()) {
        backendConnected = false;
        deviceRegistered = false;
        return;
    }

    if ((lastHeartbeatAt != 0 && (now - lastHeartbeatAt) < HEARTBEAT_INTERVAL_MS) || appState == AppState::Provisioning || appState == AppState::Ota) {
        return;
    }
    lastHeartbeatAt = now;

    currentServerUrl = wifiManager.serverUrl();
    apiClient.setServerUrl(currentServerUrl);
    apiClient.setDeviceId(deviceId);

    HeartbeatResponse response;
    backendConnected = apiClient.postHeartbeat(nfcReader.isHealthy(), scale.isHealthy(), now / 1000U, response);
    if (!backendConnected) {
        deviceRegistered = false;
        return;
    }

    handleHeartbeatResponse(response);

    if (!response.otaUrl.isEmpty() && response.otaVersion.length() > 0 && response.otaVersion != APP_VERSION) {
        appState = AppState::Ota;
        stateExpiresAt = 0;
        display.showOtaProgress(0, response.otaVersion.c_str());
        setPixelColor(statusPixel.Color(18, 0, 18));
        apiClient.performOta(response.otaUrl, response.otaVersion, otaProgressCallback);
        showErrorScreen("OTA failed");
    }
}

void pushMatchedWeightIfNeeded(uint32_t now) {
    if (appState != AppState::TagMatched || matchedSpoolId <= 0 || !scale.isStable()) {
        return;
    }
    if ((now - lastWeightPushAt) < 3000) {
        return;
    }
    if (!std::isnan(lastReportedWeight) && fabsf(scale.weightGrams() - lastReportedWeight) < 1.0f) {
        return;
    }

    if (apiClient.postSpoolWeight(matchedSpoolId, scale.weightGrams(), true)) {
        lastReportedWeight = scale.weightGrams();
        lastWeightPushAt = now;
        backendConnected = true;
    } else {
        backendConnected = false;
    }
}

void setup() {
    esp_ota_mark_app_valid_cancel_rollback();

    // Backlight ON immediately (before LovyanGFX init)
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    Serial.begin(115200);
    // Wait up to 2s for USB CDC host to connect (debug only)
    uint32_t serialWait = millis();
    while (!Serial && (millis() - serialWait) < 2000) { delay(10); }
    Serial.println("[BOOT] SpoolBuddy starting...");

    pinMode(PIN_TFT_RST, OUTPUT);
    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    digitalWrite(PIN_TFT_RST, HIGH);
    delay(10);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(50);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(300);

    statusPixel.begin();
    statusPixel.setBrightness(32);
    setPixelColor(statusPixel.Color(0, 0, 18));

    display.begin();
    Serial.println("[BOOT] Display initialized");
    display.setBlankTimeout(DISPLAY_BLANK_TIMEOUT_MS);
    display.showBoot(APP_VERSION);
    display.loop();
    delay(800);

    loadDeviceConfig();
    Serial.printf("[BOOT] Config loaded: cal=%.6f tare=%ld\n", scaleCalibration, scaleTareOffset);
    currentServerUrl = DEFAULT_SERVER_URL;

    wifiManager.begin();
    Serial.printf("[BOOT] WiFi: connected=%d\n", wifiManager.isConnected());
    currentServerUrl = wifiManager.serverUrl();
    apiClient.setServerUrl(currentServerUrl);
    apiClient.setApiKey(wifiManager.apiKey());
    apiClient.setDeviceId(deviceId);

    scale.begin(scaleCalibration, scaleTareOffset);
    Serial.printf("[BOOT] Scale: healthy=%d\n", scale.isHealthy());
    bool nfcOk = nfcReader.begin();
    Serial.printf("[BOOT] NFC: ok=%d\n", nfcOk);

    // Show hardware init results on display
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "NFC:%s  Scale:%s",
                 nfcOk ? "OK" : "FAIL",
                 scale.isHealthy() ? "OK" : "FAIL");
        display.showStatus("Hardware Init", buf, "", "");
        display.loop();
        delay(2000);
    }

    if (!wifiManager.hasCredentials()) {
        wifiManager.startProvisioningPortal();
        showProvisioningScreen();
    } else if (wifiManager.isConnected()) {
        showHomeScreen();
    } else {
        showConnectingScreen();
    }

    updateStatusBar();
    display.loop();
}

void loop() {
    static bool bootButtonDown = false;
    static uint32_t bootButtonPressedAt = 0;
    uint32_t now = millis();

    scale.update();
    wifiManager.loop();
    currentServerUrl = wifiManager.serverUrl();
    apiClient.setServerUrl(currentServerUrl);
    apiClient.setApiKey(wifiManager.apiKey());
    apiClient.setDeviceId(deviceId);

    if (wifiManager.isProvisioning()) {
        deviceRegistered = false;
        if (appState != AppState::Provisioning) {
            showProvisioningScreen();
        }
    } else if (!wifiManager.hasCredentials()) {
        deviceRegistered = false;
        wifiManager.startProvisioningPortal();
        showProvisioningScreen();
    } else if (!wifiManager.isConnected()) {
        deviceRegistered = false;
        if (appState != AppState::Connecting && appState != AppState::Provisioning) {
            showConnectingScreen();
        }
    } else if (appState == AppState::Connecting || appState == AppState::Provisioning) {
        showHomeScreen();
    }

    if (wifiManager.isConnected()) {
        attemptDeviceRegistration(now);
    }

    updateStatusBar();

    TouchEvent touch = display.pollTouch();
    bool shortAction = false;
    bool longAction = false;

    if (appState == AppState::Register && touch == TouchEvent::TAP) {
        handleRegistrationTouch(touch);
    } else if (appState == AppState::Calibrate && (touch == TouchEvent::TAP || touch == TouchEvent::SWIPE_UP || touch == TouchEvent::SWIPE_DOWN)) {
        handleCalibrationTouch(touch);
    } else if (touch == TouchEvent::TAP) {
        if (appState == AppState::Home) {
            TouchPoint tp = display.lastTouchPoint();
            HomeTile tile = display.hitTestHomeTile(tp.x, tp.y);
            if (tile != HomeTile::NONE) {
                handleHomeTileTap(tile);
                chirp(880, 30);
            }
        } else if (appState == AppState::Settings) {
            TouchPoint tp = display.lastTouchPoint();
            if (display.hitTestBackButton(tp.x, tp.y)) {
                showHomeScreen();
                chirp(660, 30);
            } else {
                SettingsTile stile = display.hitTestSettingsTile(tp.x, tp.y);
                if (stile == SettingsTile::CALIBRATE) {
                    startCalibration();
                    chirp(880, 30);
                } else if (stile == SettingsTile::CHECK_UPDATE) {
                    lastHeartbeatAt = 0; // force immediate heartbeat/OTA check
                    display.showStatus("Checking...", "Checking for updates", "", "");
                    appState = AppState::Info;
                    stateExpiresAt = millis() + 3000;
                    chirp(880, 30);
                } else if (stile == SettingsTile::WIFI) {
                    // Confirm before resetting WiFi
                    display.showConfirmPrompt("Reset WiFi?");
                    appState = AppState::ConfirmWifiReset;
                    chirp(880, 30);
                } else if (stile == SettingsTile::REBOOT) {
                    display.showStatus("Rebooting...", "", "", "");
                    display.loop();
                    delay(500);
                    ESP.restart();
                }
            }
        } else if (appState == AppState::TagMatched) {
            TouchPoint tp = display.lastTouchPoint();
            int action = display.hitTestTagMatchedAction(tp.x, tp.y);
            if (action == 0) {
                display.showConfirmDelete(matchedSpoolInfo.material, matchedSpoolInfo.colorName);
                appState = AppState::ConfirmDelete;
                stateExpiresAt = 0;
                chirp(880, 30);
            } else if (action == 1) {
                if (matchedSpoolAssigned) {
                    bool ok = apiClient.unassignSpool(
                        matchedAssignment.printerId,
                        matchedAssignment.amsId,
                        matchedAssignment.trayId);
                    backendConnected = ok;
                    char msg[64];
                    if (ok) {
                        snprintf(msg, sizeof(msg), "Unassigned from %.28s", matchedAssignment.printerName.c_str());
                    } else {
                        snprintf(msg, sizeof(msg), "Unassign failed");
                    }
                    showActionResultThenHome(ok, msg);
                } else {
                    availablePrinterCount = apiClient.getPrinters(availablePrinters, MAX_PRINTER_CHOICES);
                    if (availablePrinterCount <= 0) {
                        showActionResultThenHome(false, "No printers found");
                    } else {
                        const char* printerNames[MAX_PRINTER_CHOICES] = {};
                        for (int i = 0; i < availablePrinterCount; ++i) {
                            printerNames[i] = availablePrinters[i].name.c_str();
                        }
                        display.showPrinterList(printerNames, availablePrinterCount);
                        appState = AppState::PrinterList;
                        stateExpiresAt = 0;
                        chirp(880, 30);
                    }
                }
            } else {
                shortAction = true;
            }
        } else if (appState == AppState::ConfirmDelete) {
            TouchPoint tp = display.lastTouchPoint();
            if (display.hitTestConfirmYes(tp.x, tp.y)) {
                bool ok = apiClient.deleteSpool(matchedSpoolId);
                backendConnected = ok;
                showActionResultThenHome(ok, ok ? "Spool deleted" : "Delete failed");
            } else if (display.hitTestConfirmNo(tp.x, tp.y)) {
                showMatchedSpoolScreen();
                chirp(660, 30);
            }
        } else if (appState == AppState::ConfirmWifiReset) {
            TouchPoint tp = display.lastTouchPoint();
            if (display.hitTestConfirmYes(tp.x, tp.y)) {
                wifiManager.clearCredentials();
                wifiManager.startProvisioningPortal();
                showProvisioningScreen();
                chirp(880, 30);
            } else if (display.hitTestConfirmNo(tp.x, tp.y)) {
                appState = AppState::Settings;
                display.showSettingsGrid();
                chirp(660, 30);
            }
        } else if (appState == AppState::PrinterList) {
            TouchPoint tp = display.lastTouchPoint();
            int index = display.hitTestPrinterList(tp.x, tp.y);
            if (index >= 0 && index < availablePrinterCount) {
                bool ok = apiClient.assignSpool(
                    matchedSpoolId,
                    availablePrinters[index].id,
                    EXTERNAL_SPOOL_AMS_ID,
                    EXTERNAL_SPOOL_TRAY_ID);
                backendConnected = ok;
                char msg[64];
                if (ok) {
                    snprintf(msg, sizeof(msg), "Assigned to %.30s", availablePrinters[index].name.c_str());
                } else {
                    snprintf(msg, sizeof(msg), "Assignment failed");
                }
                showActionResultThenHome(ok, msg);
            }
        } else if (appState == AppState::TagUnknown) {
            TouchPoint tp = display.lastTouchPoint();
            if (display.hitTestTagUnknownRegister(tp.x, tp.y)) {
                startRegistrationFlow(activeTagUid);
                chirp(880, 30);
            } else {
                shortAction = true;
            }
        } else {
            shortAction = true;
        }
    } else if (touch == TouchEvent::SWIPE_LEFT || touch == TouchEvent::SWIPE_RIGHT) {
        if (appState == AppState::Settings) {
            showHomeScreen();
        } else {
            shortAction = true;
        }
    } else if (touch == TouchEvent::LONG_PRESS) {
        longAction = true;
    }

    bool bootPressed = digitalRead(PIN_BOOT_BUTTON) == LOW;
    if (bootPressed && !bootButtonDown) {
        bootButtonDown = true;
        bootButtonPressedAt = millis();
        display.wake();
    } else if (!bootPressed && bootButtonDown) {
        bootButtonDown = false;
        if ((millis() - bootButtonPressedAt) >= TOUCH_FALLBACK_LONG_PRESS_MS) {
            longAction = true;
        } else {
            shortAction = true;
        }
    }

    if (appState == AppState::Register) {
        shortAction = false;
        longAction = false;
    }

    if (longAction) {
        handleUiAction(true);
    } else if (shortAction) {
        handleUiAction(false);
    }

    if (appState == AppState::Home) {
        display.showHome(scale.weightGrams(), scale.isStable());
    } else if (appState == AppState::Scale) {
        display.showIdle(scale.weightGrams(), scale.isStable());
    } else if (appState == AppState::Register && registerStep == RegisterStep::Confirm) {
        int cw = (customCoreWeight > 0) ? customCoreWeight : REGISTER_CORE_WEIGHT_GRAMS;
        display.showRegConfirm(scale.weightGrams(), scale.isStable(), cw);
    }

    if (stateExpiresAt != 0 && now >= stateExpiresAt) {
        showHomeScreen();
        resetMatchedSpoolState();
    }

    checkForOta(now);

    if (wifiManager.isConnected() && (appState == AppState::Home || appState == AppState::Scale || appState == AppState::TagMatched || appState == AppState::TagRead)) {
        NfcTag tag;
        if (nfcReader.poll(tag)) {
            lastTagSeenAt = now;
            if (appState == AppState::TagRead) {
                // Show found tag on scan screen
                handleTagLookup(tag);
            } else if (tag.uid != activeTagUid) {
                handleTagLookup(tag);
            }
        } else if (appState == AppState::TagRead) {
            // Update diagnostic display in real-time on scan screen
            static uint32_t lastDiagUpdate = 0;
            if ((now - lastDiagUpdate) > 500) {
                lastDiagUpdate = now;
                String regs = nfcReader.dumpRegisters();
                display.showStatus("NFC Scan", nfcReader.lastDiag(), regs.c_str(), "Hold tag on antenna");
            }
        } else if (!activeTagUid.isEmpty() && (now - lastTagSeenAt) > TAG_REARM_MS) {
            resetMatchedSpoolState(true);
            if (appState == AppState::TagMatched || appState == AppState::TagUnknown || appState == AppState::TagRead) {
                showHomeScreen();
            }
        }
    }

    pushMatchedWeightIfNeeded(now);
    display.loop();

    // Periodic status print (every 5s) for serial debug
    static uint32_t lastStatusPrint = 0;
    if (Serial && (now - lastStatusPrint) > 5000) {
        lastStatusPrint = now;
        Serial.printf("[STATUS] NFC=%s Scale=%s(%.1fg) WiFi=%s\n",
                      nfcReader.isHealthy() ? "OK" : "FAIL",
                      scale.isHealthy() ? "OK" : "FAIL", scale.weightGrams(),
                      wifiManager.isConnected() ? "UP" : "DOWN");
    }

    delay(5);
}
