#include <cassert>
#include <iostream>
#include <HX711.h>
#include <Wire.h>

#include "../../src/main.cpp"

namespace {
bool connected = true;
int heartbeatCalls = 0, registrationCalls = 0, tarePosts = 0, assignments = 0;
bool tarePostOk = true, tagPresent = false;
int slotCount = 2, assignedSpool = 0, assignedPrinter = 0, assignedAms = 0, assignedTray = 0;
int registeredCore = 0;
int printerCount = 1;
long backendTare = 100000;

void tick(uint32_t elapsed = 25) {
    hostMillis += elapsed;
    loop();
}

void tap(int x, int y) {
    Wire.touch(true, x, y);
    tick();
    Wire.touch(false, x, y);
    tick();
}

void resetApp() {
    hostMillis += 1000;
    display = TouchDisplay{};
    Wire.touch(false, 0, 0);
    resetMatchedSpoolState(true);
    stateExpiresAt = 0;
    lastHeartbeatAt = hostMillis;
    lastRegistrationAttemptAt = 0;
    deviceRegistered = true;
    tareSyncPending = false;
    tarePostOk = true;
    tagPresent = false;
    printerCount = 1;
    HX711::ready = true;
    HX711::nextReading = 100000;
    scaleCalibration = 1000;
    scaleTareOffset = 100000;
    backendTare = scaleTareOffset;
    scale.begin(scaleCalibration, scaleTareOffset);
    showHomeScreen();
}

void testScale() {
    Scale subject;
    for (float factor : {1000.0f, -1000.0f}) {
        for (long delta : {-10000L, -9999L, -5000L, 0L, 5000L, 9999L, 10000L, 185000L}) {
            assert(subject.begin(factor, 100000));
            HX711::nextReading = 100000 + delta;
            int readsBefore = HX711::reads, waitsBefore = HX711::waits;
            uint32_t before = hostMillis;
            for (int i = 0; i < 3; ++i) {
                hostMillis += SCALE_SAMPLE_INTERVAL_MS;
                subject.update();
            }
            assert(hostMillis - before == 3 * SCALE_SAMPLE_INTERVAL_MS);
            assert(HX711::reads - readsBefore == 3);
            assert(HX711::waits == waitsBefore);
            float expected = abs(delta / factor) < 10 ? 0 : delta / factor;
            assert(std::fabs(subject.weightGrams() - expected) < 0.001f);
            assert(subject.rawReading() == 100000 + delta);
            assert(subject.isStable());
        }
    }
    subject.updateCalibration(1000, 100000);
    for (long delta : {30000L, 60000L, 90000L, 120000L}) {
        HX711::nextReading = 100000 + delta;
        hostMillis += SCALE_SAMPLE_INTERVAL_MS;
        subject.update();
    }
    assert(subject.weightGrams() == 90);
    assert(!subject.isStable());
    HX711::ready = false;
    hostMillis += SCALE_SAMPLE_INTERVAL_MS;
    int readsBefore = HX711::reads, waitsBefore = HX711::waits;
    uint32_t before = hostMillis;
    subject.update();
    assert(hostMillis == before && HX711::reads == readsBefore && HX711::waits == waitsBefore);
    HX711::ready = true;
    HX711::nextReading = 200000;
    assert(subject.tare());
    assert(subject.weightGrams() == 0 && !subject.isStable());
    hostMillis += SCALE_SAMPLE_INTERVAL_MS;
    subject.update();
    assert(subject.weightGrams() == 0 && subject.rawReading() == 200000);
}

void testTareSync() {
    resetApp();
    HX711::nextReading = 120000;
    tap(80, 230);
    assert(tareSyncPending && scaleTareOffset == 120000);
    applyCalibrationSettings(100000, 1.0f / 1000);
    assert(scale.tareOffset() == 120000);
    tarePostOk = false;
    lastHeartbeatAt = 0;
    int heartbeatsBefore = heartbeatCalls, postsBefore = tarePosts;
    checkForOta(hostMillis);
    assert(tarePosts == postsBefore + 1 && heartbeatCalls == heartbeatsBefore);
    assert(tareSyncPending && scale.tareOffset() == 120000);
    tareSyncPending = false;
    scaleTareOffset = 0;
    loadDeviceConfig();
    assert(tareSyncPending && scaleTareOffset == 120000);
    tarePostOk = true;
    hostMillis += HEARTBEAT_INTERVAL_MS;
    checkForOta(hostMillis);
    assert(!tareSyncPending && backendTare == 120000 && heartbeatCalls == heartbeatsBefore + 1);
    assert(Preferences::values[PREF_TARE_SYNC_PENDING] == 0);
    applyCalibrationSettings(130000, 1.0f / 1000);
    assert(scale.tareOffset() == 130000);
    HX711::ready = false;
    assert(!tareScale());
    assert(!tareSyncPending && scale.tareOffset() == 130000);
}

void testAssignment(int count, int choice) {
    resetApp();
    slotCount = count;
    tap(390, 100);
    assert(appState == AppState::TagRead);
    tagPresent = true;
    tick();
    assert(appState == AppState::TagMatched && matchedSpoolId == 42);
    tap(340, 285);
    assert(appState == AppState::PrinterList);
    int assignmentsBefore = assignments;
    tap(70, 95);
    if (count > 1) {
        assert(appState == AppState::SlotList);
        tap(choice == 0 ? 70 : 180, 95);
    }
    assert(appState == AppState::Home);
    if (count <= 0) {
        assert(assignments == assignmentsBefore);
    } else {
        assert(assignments == assignmentsBefore + 1);
        assert(assignedSpool == 42 && assignedPrinter == 7);
        assert(assignedAms == ((count == 1 || choice == 1) ? 255 : 0));
        assert(assignedTray == 0);
    }
}

void testRegistration() {
    resetApp();
    deviceRegistered = false;
    lastHeartbeatAt = 0;
    int heartbeatsBefore = heartbeatCalls, registrationsBefore = registrationCalls;
    startRegistrationFlow("AABBCCDD");
    tap(70, 100);
    assert(registerStep == RegisterStep::Brand);
    tap(70, 100);
    assert(registerStep == RegisterStep::Color);
    tap(50, 100);
    assert(registerStep == RegisterStep::Weight);
    tap(100, 200);
    assert(registerStep == RegisterStep::CoreWeight);
    display.loop();
    assert(std::find(LovyanGFX::drawnText.begin(), LovyanGFX::drawnText.end(), "Sunlu") != LovyanGFX::drawnText.end());
    assert(std::find(LovyanGFX::drawnText.begin(), LovyanGFX::drawnText.end(), "185g") != LovyanGFX::drawnText.end());
    assert(display.hitTestRegCoreWeight(240, 200) == -1);
    tap(100, 200);
    assert(registerStep == RegisterStep::Confirm && customCoreWeight == 185);
    assert(heartbeatCalls == heartbeatsBefore && registrationCalls == registrationsBefore);
    tap(350, 280);
    assert(registeredCore == 185);

    for (int choice = 0; choice < 4; ++choice) {
        resetApp();
        startRegistrationFlow("AABBCCDD");
        registerStep = RegisterStep::CoreWeight;
        showRegistrationStep();
        tap(choice % 2 ? 350 : 100, choice / 2 ? 200 : 100);
        if (choice == 3) {
            assert(registerStep == RegisterStep::CoreWeightCustom);
        } else {
            assert(registerStep == RegisterStep::Confirm);
            assert(customCoreWeight == (choice == 0 ? 250 : choice == 1 ? 216 : 185));
        }
    }
}

void testPrinterGrid() {
    resetApp();
    const char* names[16] = {
        "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9",
        "P10", "P11", "P12", "P13", "P14", "P15", "P16",
    };
    for (int count : {0, 1, 9, 10, 16}) {
        display.showPrinterList(names, count);
        for (int page = 0; page < (count > 9 ? 2 : 1); ++page) {
            LovyanGFX::textDraws.clear();
            display.loop();
            int nameCount = 0;
            for (const auto& text : LovyanGFX::textDraws) {
                if (text.text.empty() || text.y < 72 || text.y >= 276) continue;
                ++nameCount;
                assert(text.size == 2);
                assert(text.x == 88 || text.x == 240 || text.x == 392);
                assert(text.y == 102 || text.y == 174 || text.y == 246);
            }
            assert(nameCount == min(9, count - page * 9));
            for (int cell = 0; cell < 9; ++cell) {
                int index = page * 9 + cell;
                assert(display.hitTestPrinterList(88 + (cell % 3) * 152, 102 + (cell / 3) * 72)
                    == (index < count ? index : -1));
            }
            assert(display.hitTestPrinterList(165, 102) == -1);
            assert(display.hitTestPrinterList(88, 138) == -1);
            assert(display.hitTestPrinterList(88, 290) == -1);
            assert(display.hitTestPrinterList(475, 102) == -1);
            assert(!display.handlePrinterListNavigationTouch(390, 280));
            bool advanced = display.handlePrinterListNavigationTouch(390, 301);
            assert(advanced == (page == 0 && count > 9));
        }
        assert(display.handlePrinterListNavigationTouch(88, 301) == (count > 9));
        assert(display.hitTestPrinterList(88, 102) == (count > 0 ? 0 : -1));
    }

    const char* longNames[] = {"Bambu Lab X1C", "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDE"};
    display.showPrinterList(longNames, 2);
    LovyanGFX::textDraws.clear();
    display.loop();
    int lines = 0;
    for (const auto& text : LovyanGFX::textDraws) {
        if (text.text.empty() || text.y < 72) continue;
        assert(text.size == 2 && text.text.size() <= 10);
        assert(text.y == 92 || text.y == 112);
        ++lines;
    }
    assert(lines == 4);
    assert(std::find(LovyanGFX::drawnText.begin(), LovyanGFX::drawnText.end(), "Bambu Lab") != LovyanGFX::drawnText.end());
    assert(std::find(LovyanGFX::drawnText.begin(), LovyanGFX::drawnText.end(), "X1C") != LovyanGFX::drawnText.end());
    assert(std::find(LovyanGFX::drawnText.begin(), LovyanGFX::drawnText.end(), "KLMNOPQ...") != LovyanGFX::drawnText.end());

    display.showPrinterList(names, 16);
    assert(display.handlePrinterListNavigationTouch(390, 301));
    display.showPrinterList(names, 16);
    assert(display.hitTestPrinterList(88, 102) == 0);
    display.showSlotList("Printer", names, 16);
    assert(display.hitTestSlotList(420, 290) == 15);
    assert(!display.handlePrinterListNavigationTouch(390, 301));
}

void testPagedAssignment() {
    resetApp();
    printerCount = 16;
    slotCount = 1;
    tap(390, 100);
    tagPresent = true;
    tick();
    tap(340, 285);
    assert(appState == AppState::PrinterList);
    int before = assignments;
    tap(390, 301);
    assert(appState == AppState::PrinterList && assignments == before);
    tap(88, 246);
    assert(appState == AppState::Home && assignments == before + 1);
    assert(assignedPrinter == 22 && assignedSpool == 42 && assignedAms == 255);
}

void testBackgroundGating() {
    resetApp();
    for (AppState state : {AppState::Register, AppState::Calibrate, AppState::PrinterList,
                           AppState::SlotList, AppState::ConfirmDelete, AppState::ConfirmWifiReset,
                           AppState::TagUnknown, AppState::TagRead, AppState::TagMatched}) {
        appState = state;
        lastHeartbeatAt = 0;
        int before = heartbeatCalls;
        checkForOta(hostMillis);
        assert(heartbeatCalls == before);
        assert(!backgroundRequestsAllowed());
    }
    for (AppState state : {AppState::Home, AppState::Scale, AppState::Info, AppState::Settings}) {
        appState = state;
        lastHeartbeatAt = 0;
        int before = heartbeatCalls;
        checkForOta(hostMillis);
        assert(heartbeatCalls == before + 1);
    }
}
}

bool WifiManager::begin() { return true; }
void WifiManager::loop() {}
void WifiManager::startProvisioningPortal() { _portalActive = true; }
void WifiManager::clearCredentials() {}
bool WifiManager::hasCredentials() const { return true; }
bool WifiManager::isConnected() const { return connected; }
int8_t WifiManager::rssi() const { return -50; }
String WifiManager::ipAddress() const { return "127.0.0.1"; }

NfcReader::NfcReader() = default;
bool NfcReader::begin() { return true; }
String NfcReader::dumpRegisters() { return ""; }
bool NfcReader::poll(NfcTag& tag) {
    if (!tagPresent) return false;
    tagPresent = false;
    tag.uid = "AABBCCDD";
    return true;
}

void ApiClient::setServerUrl(const String&) {}
void ApiClient::setDeviceId(const String&) {}
void ApiClient::setApiKey(const String&) {}
bool ApiClient::registerDevice(long, float, DeviceRegistrationResponse&) { ++registrationCalls; return true; }
bool ApiClient::postHeartbeat(bool, bool, uint32_t, HeartbeatResponse& response) {
    ++heartbeatCalls;
    response.hasCalibration = true;
    response.tareOffset = backendTare;
    response.calibrationFactor = 1.0f / 1000;
    return true;
}
ApiClient::LookupResult ApiClient::reportTagScanned(const String&, const String&, uint8_t, const String&, int& spoolId) {
    spoolId = 42;
    return LookupResult::Success;
}
bool ApiClient::getSpoolById(int id, SpoolRecord& record) { record.spoolId = id; return true; }
bool ApiClient::deleteSpool(int) { return true; }
int ApiClient::getPrinters(PrinterInfo* printers, int maxCount) {
    for (int i = 0; i < min(printerCount, maxCount); ++i) {
        printers[i].id = 7 + i;
        printers[i].name = "Printer " + String(i + 1);
    }
    return min(printerCount, maxCount);
}
int ApiClient::getPrintersNeedingClear(PrinterInfo*, int) { return 0; }
bool ApiClient::clearPlate(int) { return true; }
int ApiClient::getSlots(int, SlotInfo* slots, int) {
    slots[0].amsId = slotCount == 1 ? 255 : 0;
    slots[0].trayId = 0;
    strcpy(slots[0].label, slotCount == 1 ? "External" : "A1");
    slots[1].amsId = 255;
    slots[1].trayId = 0;
    strcpy(slots[1].label, "External");
    return slotCount;
}
bool ApiClient::assignSpool(int spool, int printer, int ams, int tray) {
    ++assignments;
    assignedSpool = spool;
    assignedPrinter = printer;
    assignedAms = ams;
    assignedTray = tray;
    return true;
}
bool ApiClient::unassignSpool(int, int, int) { return true; }
bool ApiClient::getSpoolAssignment(int, SpoolAssignment&) { return false; }
bool ApiClient::postSpoolWeight(int, float, bool) { return true; }
bool ApiClient::postTareOffset(long offset) {
    ++tarePosts;
    if (tarePostOk) backendTare = offset;
    return tarePostOk;
}
bool ApiClient::postCalibrationFactor(float, long, long) { return true; }
bool ApiClient::registerSpool(const String&, const char*, const char*, const char*, const char*, int, int core) {
    registeredCore = core;
    return true;
}
bool ApiClient::performOta(const String&, const String&, OtaProgressCallback) { return true; }

int main() {
    testScale();
    testTareSync();
    testAssignment(2, 0);
    testAssignment(2, 1);
    testAssignment(1, 0);
    testAssignment(0, 0);
    testRegistration();
    testBackgroundGating();
    testPrinterGrid();
    testPagedAssignment();
    std::cout << "All firmware host regressions passed\n";
}
