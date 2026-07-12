#pragma once
// SpoolBuddy v3 — Touch Display Driver

#include <Arduino.h>
#include "tft_config.h"

#define TFT_WIDTH          480
#define TFT_HEIGHT         320
#define STATUS_BAR_H        32
#define CONTENT_Y           32
#define CONTENT_H          (TFT_HEIGHT - STATUS_BAR_H)
#define TOUCH_BTN_H         50
#define TOUCH_BTN_MARGIN     8
#define SPRITE_BAND_H       40

#define C_BG           0x1082
#define C_STATUS_BG    0x2104
#define C_TEXT         0xFFFF
#define C_TEXT_DIM     0x8410
#define C_ACCENT       0x34DF
#define C_SUCCESS      0x07E0
#define C_WARNING      0xFD20
#define C_ERROR        0xF800

#define DISPLAY_BLANK_MS  60000
#define TOUCH_LONG_PRESS_MS  2500
#define TOUCH_SWIPE_MIN_PX    40

enum class Screen {
    BOOT,
    CONNECTING,
    HOME,
    IDLE,
    STATUS,
    TAG_READ,
    TAG_MATCHED,
    CONFIRM_DELETE,
    PRINTER_LIST,
    SLOT_LIST,
    TAG_UNKNOWN,
    TAG_WRITING,
    TAG_WRITE_OK,
    TAG_WRITE_FAIL,
    OTA_PROGRESS,
    ERROR,
    PROVISIONING,
    REG_MATERIAL,
    REG_BRAND,
    REG_COLOR,
    REG_WEIGHT,
    REG_CORE_WEIGHT,
    REG_CORE_CUSTOM,
    REG_CONFIRM,
    MENU,
    SETTINGS_GRID,
    CAL_WEIGHT,
    NUMPAD,
};

enum class TouchEvent {
    NONE,
    TAP,
    LONG_PRESS,
    SWIPE_UP,
    SWIPE_DOWN,
    SWIPE_LEFT,
    SWIPE_RIGHT,
};

enum class HomeTile {
    NONE = -1,
    SCALE = 0,
    SCAN_SPOOL,
    ASSIGN,
    TARE,
    INFO,
    SETTINGS,
};

enum class SettingsTile {
    NONE = -1,
    CALIBRATE = 0,
    CHECK_UPDATE,
    WIFI,
    REBOOT,
};

struct TouchPoint {
    int16_t x;
    int16_t y;
};

struct SpoolInfo {
    char material[16];
    char colorName[20];
    char brand[20];
    uint32_t colorHex;
    float weightUsed;
    float labelWeight;
};

class TouchDisplay {
public:
    void begin();
    void loop();

    void setWifiRSSI(int8_t rssi);
    void setWifiConnected(bool connected);
    void setBackendConnected(bool connected);
    void setServerUrl(const char* url);
    void setFirmwareVersion(const char* ver);
    void setNfcOk(bool ok);
    void setScaleOk(bool ok);

    void showBoot(const char* version);
    void showConnecting(const char* ssid);
    void showHome(float weightGrams, bool stable);
    void showIdle(float weightGrams, bool stable);
    void showStatus(const char* title, const char* line1, const char* line2, const char* line3 = "");
    void showTagRead(const char* uid, const char* message = "Looking up spool...");
    void showTagMatched(const SpoolInfo& spool, bool assigned = false, const char* printerName = "");
    void showConfirmDelete(const char* material, const char* color);
    void showConfirmPrompt(const char* prompt);
    void showPrinterList(const char** names, int count);
    void showSlotList(const char* printerName, const char** labels, int count);
    void showTagUnknown(const char* uid);
    void showTagWriting(int spoolId);
    void showTagWriteResult(bool success, const char* msg);
    void showOtaProgress(int percent, const char* version);
    void showError(const char* message);
    void showProvisioning(const char* apName);
    void showRegMaterial();
    void showRegBrand();
    void showRegColor();
    void showRegWeight();
    void showRegCoreWeight();
    void showRegCoreCustom();
    void showRegConfirm(float weightGrams, bool stable, int coreWeight);
    void regReset();
    void regMoveMaterial(int delta);
    void regMoveBrand(int delta);
    void regMoveWeight(int delta);
    void regSetMaterialIdx(int idx);
    void regSetBrandIdx(int idx);
    void regSetWeightIdx(int idx);
    void regSelectColor(int index);
    int regSelectedMaterial() const { return _regMaterialIdx; }
    int regSelectedBrand() const { return _regBrandIdx; }
    int regSelectedColor() const { return _regColorIdx; }
    int regSelectedWeight() const { return _regWeightIdx; }
    const char* regSelectedMaterialName() const;
    const char* regSelectedBrandName() const;
    const char* regSelectedColorName() const;
    uint32_t regSelectedColorRgba() const;
    uint16_t regSelectedColorTft() const;
    int regSelectedLabelWeight() const;

    HomeTile hitTestHomeTile(int16_t x, int16_t y) const;
    SettingsTile hitTestSettingsTile(int16_t x, int16_t y) const;
    void showSettingsGrid();
    bool hitTestBackButton(int16_t x, int16_t y) const;
    void showCalWeight();
    int hitTestCalWeight(int16_t x, int16_t y) const;  // returns 0-3 (250/500/1000/custom) or -1
    void showNumpad(const char* title);
    void numpadAppend(char c);
    void numpadBackspace();
    void numpadClear();
    int numpadValue() const;
    int hitTestNumpadKey(int16_t x, int16_t y) const;  // 0-9, 10=backspace, 11=OK, -1=none
    bool hitTestTagUnknownRegister(int16_t x, int16_t y) const;
    bool hitTestConfirmYes(int16_t x, int16_t y) const;
    bool hitTestConfirmNo(int16_t x, int16_t y) const;
    int hitTestTagMatchedAction(int16_t x, int16_t y) const;
    int hitTestPrinterList(int16_t x, int16_t y) const;
    int hitTestSlotList(int16_t x, int16_t y) const;
    int hitTestRegMaterial(int16_t x, int16_t y) const;  // 0-11 or -1
    int hitTestRegBrand(int16_t x, int16_t y) const;     // 0-8 or -1
    int hitTestRegColorTile(int16_t x, int16_t y) const;
    bool hitTestRegConfirmRegister(int16_t x, int16_t y) const;
    bool hitTestRegConfirmCancel(int16_t x, int16_t y) const;
    int hitTestRegWeight(int16_t x, int16_t y) const;  // 0=250, 1=500, 2=1000, 3=custom, -1=none
    int hitTestRegCoreWeight(int16_t x, int16_t y) const; // 0=low temp 250g, 1=high temp 216g, 2=custom, -1=none
    int hitTestRegCoreCustom(int16_t x, int16_t y) const; // 0=weigh, 1=type, -1=none
    void showDebugCoords(const char* text);

    void setBrightness(uint8_t percent);
    void setBlankTimeout(uint32_t ms);
    void wake();

    TouchEvent pollTouch();
    TouchPoint lastTouchPoint() const { return _lastTouch; }

private:
    LGFX _tft;
    LGFX_Sprite _canvas;  // full-screen double buffer in PSRAM

    Screen _screen = Screen::BOOT;
    Screen _prevScreen = Screen::BOOT;
    uint32_t _msgExpiry = 0;
    uint32_t _lastActivity = 0;
    uint32_t _blankTimeout = DISPLAY_BLANK_MS;
    bool _blanked = false;
    bool _needsRedraw = true;
    bool _needsScaleUpdate = false;

    int8_t _wifiRSSI = -100;
    bool _wifiConnected = false;
    bool _backendConnected = false;
    bool _nfcOk = false;
    bool _scaleOk = false;
    char _serverUrl[64] = {};
    char _fwVersion[16] = {};

    float _weightGrams = 0.0f;
    bool _weightStable = false;
    int _regCoreWeight = 250;
    SpoolInfo _spoolInfo = {};
    bool _spoolAssigned = false;
    char _assignedPrinterName[32] = {};
    char _msgBuf[64] = {};
    char _uidBuf[24] = {};
    char _confirmPrompt[48] = {};
    int _otaPercent = 0;
    char _otaVersion[16] = {};
    char _titleBuf[32] = {};
    char _line1Buf[48] = {};
    char _line2Buf[48] = {};
    char _line3Buf[48] = {};
    int _regMaterialIdx = 0;
    int _regBrandIdx = 0;
    int _regColorIdx = 0;
    int _regWeightIdx = 3;
    int _printerListCount = 0;
    char _printerNames[16][32] = {};
    int _slotListCount = 0;
    char _slotLabels[16][12] = {};
    char _slotPrinterName[32] = {};
    char _numpadBuf[8] = {};
    int _numpadLen = 0;
    char _numpadTitle[32] = {};

    TouchPoint _lastTouch = {0, 0};
    TouchPoint _touchStart = {0, 0};
    uint32_t _touchStartMs = 0;
    bool _touching = false;

    void _render();
    void _drawStatusBar();
    void _drawBoot();
    void _drawConnecting();
    void _drawHome();
    void _drawHomeScaleTile();
    void _drawIdle();
    void _drawStatus();
    void _drawTagRead();
    void _drawTagMatched();
    void _drawConfirmDelete();
    void _drawPrinterList();
    void _drawSlotList();
    void _drawTagUnknown();
    void _drawTagWriting();
    void _drawTagWriteResult();
    void _drawOta();
    void _drawError();
    void _drawProvisioning();
    void _drawRegMaterial();
    void _drawRegBrand();
    void _drawRegColor();
    void _drawRegWeight();
    void _drawRegCoreWeight();
    void _drawRegCoreCustom();
    void _drawRegConfirm();
    void _drawRegStepIndicator(int step, int totalSteps);
    void _drawSettingsGrid();
    void _drawCalWeight();
    void _drawNumpad();

    void _drawColorSwatch(int x, int y, int w, int h, uint32_t color);
    void _drawProgressBar(int x, int y, int w, int h, int percent, uint16_t color);
    void _drawButton(int x, int y, int w, int h, const char* label, uint16_t bg);
    void _drawBackButton();
    void _drawWifiIcon(int x, int y, int8_t rssi);
};
