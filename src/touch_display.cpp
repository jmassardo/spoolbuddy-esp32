#include "touch_display.h"

#include <Wire.h>
#include <cmath>
#include <cstring>

namespace {

constexpr const char* REG_MATERIALS[] = {
    "PLA", "PETG", "ABS", "ASA", "TPU", "PA/Nylon",
    "PC", "PVA", "HIPS", "PLA-CF", "PETG-CF", "PA-CF",
};

constexpr const char* REG_BRANDS[] = {
    "Bambu Lab", "eSUN", "Hatchbox", "Polymaker", "Prusament",
    "Overture", "SUNLU", "Inland", "Generic",
};

constexpr int REG_LABEL_WEIGHTS[] = {250, 500, 750, 1000, 2000, 3000};

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct ColorChoice {
    const char* name;
    uint32_t rgba;
    uint16_t tft;
};

constexpr ColorChoice REG_COLORS[] = {
    {"Black",       0x000000FF, rgb565(0x00, 0x00, 0x00)},
    {"White",       0xFFFFFFFF, rgb565(0xFF, 0xFF, 0xFF)},
    {"Gray",        0x808080FF, rgb565(0x80, 0x80, 0x80)},
    {"Silver",      0xC0C0C0FF, rgb565(0xC0, 0xC0, 0xC0)},
    {"Red",         0xFF0000FF, rgb565(0xFF, 0x00, 0x00)},
    {"Dark Red",    0x8B0000FF, rgb565(0x8B, 0x00, 0x00)},
    {"Orange",      0xFF8C00FF, rgb565(0xFF, 0x8C, 0x00)},
    {"Yellow",      0xFFD700FF, rgb565(0xFF, 0xD7, 0x00)},
    {"Green",       0x008000FF, rgb565(0x00, 0x80, 0x00)},
    {"Lime",        0x32CD32FF, rgb565(0x32, 0xCD, 0x32)},
    {"Teal",        0x008080FF, rgb565(0x00, 0x80, 0x80)},
    {"Olive",       0x808000FF, rgb565(0x80, 0x80, 0x00)},
    {"Blue",        0x0000FFFF, rgb565(0x00, 0x00, 0xFF)},
    {"Navy",        0x000080FF, rgb565(0x00, 0x00, 0x80)},
    {"Cyan",        0x00FFFFFF, rgb565(0x00, 0xFF, 0xFF)},
    {"Sky Blue",    0x87CEEBFF, rgb565(0x87, 0xCE, 0xEB)},
    {"Purple",      0x800080FF, rgb565(0x80, 0x00, 0x80)},
    {"Violet",      0xEE82EEFF, rgb565(0xEE, 0x82, 0xEE)},
    {"Magenta",     0xFF00FFFF, rgb565(0xFF, 0x00, 0xFF)},
    {"Pink",        0xFFB6C1FF, rgb565(0xFF, 0xB6, 0xC1)},
    {"Brown",       0x8B4513FF, rgb565(0x8B, 0x45, 0x13)},
    {"Tan",         0xD2B48CFF, rgb565(0xD2, 0xB4, 0x8C)},
    {"Gold",        0xFFD700FF, rgb565(0xFF, 0xD7, 0x00)},
    {"Transparent", 0xF0F0F0FF, rgb565(0xF0, 0xF0, 0xF0)},
};

constexpr int REG_TOTAL_STEPS = 6;
constexpr int REG_COLOR_GRID_COLS = 6;
constexpr int REG_COLOR_GRID_ROWS = 4;
constexpr int REG_COLOR_TILE_W = 72;
constexpr int REG_COLOR_TILE_H = 55;
constexpr int REG_COLOR_TILE_GAP = 4;
constexpr int REG_COLOR_GRID_X0 = (TFT_WIDTH - (REG_COLOR_GRID_COLS * (REG_COLOR_TILE_W + REG_COLOR_TILE_GAP) - REG_COLOR_TILE_GAP)) / 2;
constexpr int REG_COLOR_GRID_Y0 = 75;

constexpr int TAG_UNKNOWN_REGISTER_X = (TFT_WIDTH / 2) - 80;
constexpr int TAG_UNKNOWN_REGISTER_Y = TFT_HEIGHT - 70;
constexpr int TAG_UNKNOWN_REGISTER_W = 160;
constexpr int TAG_UNKNOWN_REGISTER_H = TOUCH_BTN_H;

constexpr int REG_CONFIRM_BTN_W = 180;
constexpr int REG_CONFIRM_BTN_H = 42;
constexpr int REG_CONFIRM_BTN_Y = TFT_HEIGHT - 56;
constexpr int REG_CONFIRM_CANCEL_X = 34;
constexpr int REG_CONFIRM_REGISTER_X = TFT_WIDTH - REG_CONFIRM_BTN_W - 34;

constexpr uint16_t C_CARD_BG = 0x2104;
constexpr uint16_t C_DELETE_BTN_BG = rgb565(0x5A, 0x18, 0x18);
constexpr uint16_t C_ASSIGN_BTN_BG = rgb565(0x14, 0x3A, 0x42);
constexpr uint16_t C_UNASSIGN_BTN_BG = rgb565(0x5A, 0x3A, 0x10);

constexpr int TAG_ACTION_BTN_W = 196;
constexpr int TAG_ACTION_BTN_H = 44;
constexpr int TAG_ACTION_BTN_Y = TFT_HEIGHT - 54;
constexpr int TAG_DELETE_BTN_X = 24;
constexpr int TAG_ASSIGN_BTN_X = TFT_WIDTH - TAG_ACTION_BTN_W - 24;

constexpr int CONFIRM_BTN_W = 180;
constexpr int CONFIRM_BTN_H = 46;
constexpr int CONFIRM_BTN_Y = TFT_HEIGHT - 64;
constexpr int CONFIRM_NO_BTN_X = 34;
constexpr int CONFIRM_YES_BTN_X = TFT_WIDTH - CONFIRM_BTN_W - 34;

constexpr int REG_MATERIAL_COLS = 4;
constexpr int REG_MATERIAL_ROWS = 3;
constexpr int REG_MATERIAL_BTN_W = 105;
constexpr int REG_MATERIAL_BTN_H = 70;
constexpr int REG_MATERIAL_GAP = 10;
constexpr int REG_MATERIAL_X0 = (TFT_WIDTH - REG_MATERIAL_COLS * REG_MATERIAL_BTN_W - (REG_MATERIAL_COLS - 1) * REG_MATERIAL_GAP) / 2;
constexpr int REG_MATERIAL_Y0 = STATUS_BAR_H + 40;

constexpr int REG_BRAND_COLS = 3;
constexpr int REG_BRAND_ROWS = 3;
constexpr int REG_BRAND_BTN_W = 140;
constexpr int REG_BRAND_BTN_H = 70;
constexpr int REG_BRAND_GAP = 12;
constexpr int REG_BRAND_X0 = (TFT_WIDTH - REG_BRAND_COLS * REG_BRAND_BTN_W - (REG_BRAND_COLS - 1) * REG_BRAND_GAP) / 2;
constexpr int REG_BRAND_Y0 = STATUS_BAR_H + 40;

constexpr int PRINTER_GRID_COLS = 4;
constexpr int PRINTER_GRID_ROWS = 4;
constexpr int PRINTER_BTN_W = 105;
constexpr int PRINTER_BTN_H = 55;
constexpr int PRINTER_GRID_GAP = 8;
constexpr int PRINTER_GRID_X0 = (TFT_WIDTH - PRINTER_GRID_COLS * PRINTER_BTN_W - (PRINTER_GRID_COLS - 1) * PRINTER_GRID_GAP) / 2;
constexpr int PRINTER_GRID_Y0 = STATUS_BAR_H + 40;

constexpr int CLEAR_GRID_COLS = 3;
constexpr int CLEAR_GRID_ROWS = 3;
constexpr int CLEAR_BTN_W = 140;
constexpr int CLEAR_BTN_H = 55;
constexpr int CLEAR_GRID_GAP = 10;
constexpr int CLEAR_GRID_X0 = (TFT_WIDTH - CLEAR_GRID_COLS * CLEAR_BTN_W - (CLEAR_GRID_COLS - 1) * CLEAR_GRID_GAP) / 2;
constexpr int CLEAR_GRID_Y0 = STATUS_BAR_H + 40;

int wrapIndex(int index, int count) {
    if (count <= 0) {
        return 0;
    }
    while (index < 0) {
        index += count;
    }
    return index % count;
}

void drawTransparentTile(LovyanGFX& gfx, int x, int y, int w, int h) {
    gfx.fillRect(x, y, w, h, rgb565(0xF0, 0xF0, 0xF0));
    for (int offset = -h; offset < w; offset += 10) {
        gfx.drawLine(x + offset, y, x + offset + h, y + h, C_TEXT_DIM);
    }
}

int hitTestGrid(int16_t x, int16_t y, int x0, int y0, int cols, int rows, int btnW, int btnH, int gap, int count) {
    if (count <= 0 || x < x0 || y < y0) {
        return -1;
    }

    const int cellW = btnW + gap;
    const int cellH = btnH + gap;
    const int relX = x - x0;
    const int relY = y - y0;
    const int col = relX / cellW;
    const int row = relY / cellH;

    if (col < 0 || col >= cols || row < 0 || row >= rows) {
        return -1;
    }
    if ((relX % cellW) >= btnW || (relY % cellH) >= btnH) {
        return -1;
    }

    const int index = row * cols + col;
    return index < count ? index : -1;
}

void drawGridButton(LovyanGFX& gfx, int x, int y, int w, int h, const char* label, uint16_t border, int textSize) {
    gfx.fillRoundRect(x, y, w, h, 10, C_CARD_BG);
    gfx.drawRoundRect(x, y, w, h, 10, border);
    gfx.setTextColor(C_TEXT);
    gfx.setTextDatum(lgfx::middle_center);
    gfx.setTextSize(textSize);
    gfx.drawString(label, x + w / 2, y + h / 2);
}

}  // namespace

void TouchDisplay::begin() {
    _tft.init();
    _tft.setRotation(3);
    digitalWrite(PIN_TFT_BL, HIGH);  // Backlight on
    _tft.fillScreen(C_BG);

    // Create full-screen sprite in PSRAM for flicker-free double buffering
    _canvas.setPsram(true);
    _canvas.setColorDepth(16);
    void* buf = _canvas.createSprite(TFT_WIDTH, TFT_HEIGHT);
    if (buf == nullptr) {
        // PSRAM alloc failed — try without PSRAM
        _canvas.setPsram(false);
        buf = _canvas.createSprite(TFT_WIDTH, TFT_HEIGHT);
    }
    _canvas.fillSprite(C_BG);

    // Init I2C for touch (FT6336U) — bypass LovyanGFX touch driver
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
    delay(50);

    // Configure FT6336U for fast, sensitive touch detection
    // Register 0x00: device mode = 0 (normal working mode)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(5);

    // Register 0x80: touch threshold (lower = more sensitive, default ~40-70)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x80);
    Wire.write(0x16);  // 22 — fairly sensitive
    Wire.endTransmission();
    delay(5);

    // Register 0x86: CTRL = 0 (stay in active mode, don't enter monitor)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x86);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(5);

    // Register 0x88: active report rate (value * 10ms between reports)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x88);
    Wire.write(0x01);  // 10ms — fastest
    Wire.endTransmission();
    delay(5);

    _lastActivity = millis();
    _needsRedraw = true;
}

void TouchDisplay::loop() {
    uint32_t now = millis();

    if (_blankTimeout > 0 && !_blanked && (now - _lastActivity) > _blankTimeout) {
        digitalWrite(PIN_TFT_BL, LOW);  // Backlight off
        _blanked = true;
        return;
    }

    if (_msgExpiry > 0 && now > _msgExpiry) {
        _msgExpiry = 0;
        _screen = Screen::IDLE;
        _needsRedraw = true;
    }

    if (_needsRedraw) {
        _render();
        _needsRedraw = false;
        _needsScaleUpdate = false;
    } else if (_needsScaleUpdate && _screen == Screen::HOME) {
        _drawHomeScaleTile();
        _canvas.pushSprite(&_tft, 0, 0);
        _needsScaleUpdate = false;
    }
}

TouchEvent TouchDisplay::pollTouch() {
    // Throttle I2C reads
    static uint32_t lastPollMs = 0;
    uint32_t now = millis();
    if (now - lastPollMs < 20) {
        return TouchEvent::NONE;
    }
    lastPollMs = now;

    // Read FT6336U touch registers directly (INT pin not connected on this module)
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) {
        return TouchEvent::NONE;
    }

    uint8_t got = Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5);
    if (got < 5) {
        return TouchEvent::NONE;
    }

    uint8_t numTouches = Wire.read() & 0x0F;
    uint8_t xH = Wire.read();
    uint8_t xL = Wire.read();
    uint8_t yH = Wire.read();
    uint8_t yL = Wire.read();

    bool fingerDown = (numTouches > 0 && numTouches < 6);

    if (fingerDown) {
        int16_t rawX = ((xH & 0x0F) << 8) | xL;
        int16_t rawY = ((yH & 0x0F) << 8) | yL;

        // Transform for rotation 3 (landscape CCW):
        int16_t dispX = 479 - rawY;
        int16_t dispY = rawX;

        _lastTouch = {dispX, dispY};

        if (!_touching) {
            _touching = true;
            _touchStart = _lastTouch;
            _touchStartMs = millis();
        }

        if (_blanked) {
            wake();
            return TouchEvent::NONE;
        }

        _lastActivity = millis();
        return TouchEvent::NONE;
    }

    // Finger lifted
    if (_touching) {
        _touching = false;
        uint32_t duration = millis() - _touchStartMs;
        int16_t dx = _lastTouch.x - _touchStart.x;
        int16_t dy = _lastTouch.y - _touchStart.y;
        int16_t adx = abs(dx);
        int16_t ady = abs(dy);

        if (adx > TOUCH_SWIPE_MIN_PX || ady > TOUCH_SWIPE_MIN_PX) {
            if (adx > ady) {
                return dx > 0 ? TouchEvent::SWIPE_RIGHT : TouchEvent::SWIPE_LEFT;
            }
            return dy > 0 ? TouchEvent::SWIPE_DOWN : TouchEvent::SWIPE_UP;
        }
        if (duration >= TOUCH_LONG_PRESS_MS) {
            return TouchEvent::LONG_PRESS;
        }
        return TouchEvent::TAP;
    }

    return TouchEvent::NONE;
}

void TouchDisplay::showBoot(const char* version) {
    _screen = Screen::BOOT;
    _msgExpiry = 0;
    strncpy(_otaVersion, version, sizeof(_otaVersion) - 1);
    _otaVersion[sizeof(_otaVersion) - 1] = '\0';
    _needsRedraw = true;
}

void TouchDisplay::showConnecting(const char* ssid) {
    _screen = Screen::CONNECTING;
    _msgExpiry = 0;
    strncpy(_msgBuf, ssid, sizeof(_msgBuf) - 1);
    _msgBuf[sizeof(_msgBuf) - 1] = '\0';
    _needsRedraw = true;
}

void TouchDisplay::showHome(float weightGrams, bool stable) {
    bool weightChanged = (fabsf(_weightGrams - weightGrams) >= 0.1f) || (_weightStable != stable);
    bool screenChanged = (_screen != Screen::HOME);
    _msgExpiry = 0;
    _weightGrams = weightGrams;
    _weightStable = stable;
    if (screenChanged) {
        _screen = Screen::HOME;
        _needsRedraw = true;
    } else if (weightChanged) {
        _needsScaleUpdate = true;  // Only redraw scale tile, not full screen
    }
}

void TouchDisplay::showIdle(float weightGrams, bool stable) {
    bool weightChanged = (fabsf(_weightGrams - weightGrams) >= 0.1f) || (_weightStable != stable);
    bool screenChanged = (_screen != Screen::IDLE);
    _msgExpiry = 0;
    _weightGrams = weightGrams;
    _weightStable = stable;
    if (screenChanged) {
        _screen = Screen::IDLE;
        _needsRedraw = true;
    } else if (weightChanged) {
        _needsRedraw = true;  // Scale view is simple enough to full-redraw without flicker issue
    }
}

void TouchDisplay::showStatus(const char* title, const char* line1, const char* line2, const char* line3) {
    _screen = Screen::STATUS;
    _msgExpiry = 0;
    strncpy(_titleBuf, title, sizeof(_titleBuf) - 1);
    strncpy(_line1Buf, line1, sizeof(_line1Buf) - 1);
    strncpy(_line2Buf, line2, sizeof(_line2Buf) - 1);
    strncpy(_line3Buf, line3, sizeof(_line3Buf) - 1);
    _titleBuf[sizeof(_titleBuf) - 1] = '\0';
    _line1Buf[sizeof(_line1Buf) - 1] = '\0';
    _line2Buf[sizeof(_line2Buf) - 1] = '\0';
    _line3Buf[sizeof(_line3Buf) - 1] = '\0';
    _needsRedraw = true;
}

void TouchDisplay::showTagRead(const char* uid, const char* message) {
    _screen = Screen::TAG_READ;
    _msgExpiry = 0;
    strncpy(_uidBuf, uid, sizeof(_uidBuf) - 1);
    strncpy(_msgBuf, message, sizeof(_msgBuf) - 1);
    _uidBuf[sizeof(_uidBuf) - 1] = '\0';
    _msgBuf[sizeof(_msgBuf) - 1] = '\0';
    _needsRedraw = true;
}

void TouchDisplay::showTagMatched(const SpoolInfo& spool, bool assigned, const char* printerName) {
    _spoolInfo = spool;
    _spoolAssigned = assigned;
    strncpy(_assignedPrinterName, printerName != nullptr ? printerName : "", sizeof(_assignedPrinterName) - 1);
    _assignedPrinterName[sizeof(_assignedPrinterName) - 1] = '\0';
    _screen = Screen::TAG_MATCHED;
    _msgExpiry = millis() + 8000;
    _needsRedraw = true;
}

void TouchDisplay::showConfirmDelete(const char* material, const char* color) {
    snprintf(_confirmPrompt, sizeof(_confirmPrompt), "Delete %s %s?",
             material != nullptr ? material : "",
             color != nullptr ? color : "");
    _screen = Screen::CONFIRM_DELETE;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showConfirmPrompt(const char* prompt) {
    strncpy(_confirmPrompt, prompt != nullptr ? prompt : "", sizeof(_confirmPrompt) - 1);
    _confirmPrompt[sizeof(_confirmPrompt) - 1] = '\0';
    _screen = Screen::CONFIRM_DELETE;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showPrinterList(const char** names, int count) {
    _printerListCount = constrain(count, 0, static_cast<int>(sizeof(_printerNames) / sizeof(_printerNames[0])));
    for (int i = 0; i < _printerListCount; ++i) {
        strncpy(_printerNames[i], (names != nullptr && names[i] != nullptr) ? names[i] : "", sizeof(_printerNames[i]) - 1);
        _printerNames[i][sizeof(_printerNames[i]) - 1] = '\0';
    }
    for (int i = _printerListCount; i < static_cast<int>(sizeof(_printerNames) / sizeof(_printerNames[0])); ++i) {
        _printerNames[i][0] = '\0';
    }
    _screen = Screen::PRINTER_LIST;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showSlotList(const char* printerName, const char** labels, int count) {
    strncpy(_slotPrinterName, printerName != nullptr ? printerName : "", sizeof(_slotPrinterName) - 1);
    _slotPrinterName[sizeof(_slotPrinterName) - 1] = '\0';
    _slotListCount = constrain(count, 0, static_cast<int>(sizeof(_slotLabels) / sizeof(_slotLabels[0])));
    for (int i = 0; i < _slotListCount; ++i) {
        strncpy(_slotLabels[i], (labels != nullptr && labels[i] != nullptr) ? labels[i] : "", sizeof(_slotLabels[i]) - 1);
        _slotLabels[i][sizeof(_slotLabels[i]) - 1] = '\0';
    }
    for (int i = _slotListCount; i < static_cast<int>(sizeof(_slotLabels) / sizeof(_slotLabels[0])); ++i) {
        _slotLabels[i][0] = '\0';
    }
    _screen = Screen::SLOT_LIST;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showClearPlateList(const char** names, int count) {
    _clearPlateListCount = constrain(count, 0, static_cast<int>(sizeof(_clearPlateNames) / sizeof(_clearPlateNames[0])));
    for (int i = 0; i < _clearPlateListCount; ++i) {
        strncpy(_clearPlateNames[i], (names != nullptr && names[i] != nullptr) ? names[i] : "", sizeof(_clearPlateNames[i]) - 1);
        _clearPlateNames[i][sizeof(_clearPlateNames[i]) - 1] = '\0';
    }
    for (int i = _clearPlateListCount; i < static_cast<int>(sizeof(_clearPlateNames) / sizeof(_clearPlateNames[0])); ++i) {
        _clearPlateNames[i][0] = '\0';
    }
    _screen = Screen::CLEAR_PLATE_LIST;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showTagUnknown(const char* uid) {
    strncpy(_uidBuf, uid, sizeof(_uidBuf) - 1);
    _uidBuf[sizeof(_uidBuf) - 1] = '\0';
    _screen = Screen::TAG_UNKNOWN;
    _msgExpiry = millis() + 5000;
    _needsRedraw = true;
}

void TouchDisplay::showTagWriting(int spoolId) {
    snprintf(_msgBuf, sizeof(_msgBuf), "Writing spool #%d...", spoolId);
    _screen = Screen::TAG_WRITING;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showTagWriteResult(bool success, const char* msg) {
    strncpy(_msgBuf, msg, sizeof(_msgBuf) - 1);
    _msgBuf[sizeof(_msgBuf) - 1] = '\0';
    _screen = success ? Screen::TAG_WRITE_OK : Screen::TAG_WRITE_FAIL;
    _msgExpiry = millis() + 4000;
    _needsRedraw = true;
}

void TouchDisplay::showOtaProgress(int percent, const char* version) {
    _otaPercent = percent;
    strncpy(_otaVersion, version, sizeof(_otaVersion) - 1);
    _otaVersion[sizeof(_otaVersion) - 1] = '\0';
    _screen = Screen::OTA_PROGRESS;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showError(const char* message) {
    strncpy(_msgBuf, message, sizeof(_msgBuf) - 1);
    _msgBuf[sizeof(_msgBuf) - 1] = '\0';
    _screen = Screen::ERROR;
    _msgExpiry = millis() + 6000;
    _needsRedraw = true;
}

void TouchDisplay::showProvisioning(const char* apName) {
    strncpy(_msgBuf, apName, sizeof(_msgBuf) - 1);
    _msgBuf[sizeof(_msgBuf) - 1] = '\0';
    _screen = Screen::PROVISIONING;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::regReset() {
    _regMaterialIdx = 0;
    _regBrandIdx = 0;
    _regColorIdx = 0;
    _regWeightIdx = 3;
}

void TouchDisplay::regMoveMaterial(int delta) {
    _regMaterialIdx = wrapIndex(_regMaterialIdx + delta, static_cast<int>(sizeof(REG_MATERIALS) / sizeof(REG_MATERIALS[0])));
}

void TouchDisplay::regMoveBrand(int delta) {
    _regBrandIdx = wrapIndex(_regBrandIdx + delta, static_cast<int>(sizeof(REG_BRANDS) / sizeof(REG_BRANDS[0])));
}

void TouchDisplay::regMoveWeight(int delta) {
    _regWeightIdx = wrapIndex(_regWeightIdx + delta, static_cast<int>(sizeof(REG_LABEL_WEIGHTS) / sizeof(REG_LABEL_WEIGHTS[0])));
}

void TouchDisplay::regSetMaterialIdx(int idx) {
    _regMaterialIdx = idx;
}

void TouchDisplay::regSetBrandIdx(int idx) {
    _regBrandIdx = idx;
}

void TouchDisplay::regSetWeightIdx(int idx) {
    constexpr int count = static_cast<int>(sizeof(REG_LABEL_WEIGHTS) / sizeof(REG_LABEL_WEIGHTS[0]));
    if (idx >= 0 && idx < count) {
        _regWeightIdx = idx;
    }
}

void TouchDisplay::regSelectColor(int index) {
    _regColorIdx = wrapIndex(index, static_cast<int>(sizeof(REG_COLORS) / sizeof(REG_COLORS[0])));
}

const char* TouchDisplay::regSelectedMaterialName() const {
    return REG_MATERIALS[_regMaterialIdx];
}

const char* TouchDisplay::regSelectedBrandName() const {
    return REG_BRANDS[_regBrandIdx];
}

const char* TouchDisplay::regSelectedColorName() const {
    return REG_COLORS[_regColorIdx].name;
}

uint32_t TouchDisplay::regSelectedColorRgba() const {
    return REG_COLORS[_regColorIdx].rgba;
}

uint16_t TouchDisplay::regSelectedColorTft() const {
    return REG_COLORS[_regColorIdx].tft;
}

int TouchDisplay::regSelectedLabelWeight() const {
    return REG_LABEL_WEIGHTS[_regWeightIdx];
}

void TouchDisplay::showRegMaterial() {
    _screen = Screen::REG_MATERIAL;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showRegBrand() {
    _screen = Screen::REG_BRAND;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showRegColor() {
    _screen = Screen::REG_COLOR;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showRegWeight() {
    _screen = Screen::REG_WEIGHT;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showRegCoreWeight() {
    _screen = Screen::REG_CORE_WEIGHT;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showRegCoreCustom() {
    _screen = Screen::REG_CORE_CUSTOM;
    _msgExpiry = 0;
    _needsRedraw = true;
}

void TouchDisplay::showRegConfirm(float weightGrams, bool stable, int coreWeight) {
    bool screenChanged = (_screen != Screen::REG_CONFIRM);
    bool weightChanged = (fabsf(_weightGrams - weightGrams) >= 0.1f) || (_weightStable != stable);
    _screen = Screen::REG_CONFIRM;
    _msgExpiry = 0;
    _weightGrams = weightGrams;
    _weightStable = stable;
    _regCoreWeight = coreWeight;
    if (screenChanged || weightChanged) {
        _needsRedraw = true;
    }
}

void TouchDisplay::setBrightness(uint8_t percent) {
    digitalWrite(PIN_TFT_BL, percent > 0 ? HIGH : LOW);
}

void TouchDisplay::setBlankTimeout(uint32_t ms) {
    _blankTimeout = ms;
}

void TouchDisplay::wake() {
    _blanked = false;
    digitalWrite(PIN_TFT_BL, HIGH);  // Backlight on
    _lastActivity = millis();
    _needsRedraw = true;
}

void TouchDisplay::setWifiRSSI(int8_t rssi) { if (_wifiRSSI != rssi) { _wifiRSSI = rssi; } }
void TouchDisplay::setWifiConnected(bool c) { if (_wifiConnected != c) { _wifiConnected = c; _needsRedraw = true; } }
void TouchDisplay::setBackendConnected(bool c) { if (_backendConnected != c) { _backendConnected = c; _needsRedraw = true; } }
void TouchDisplay::setNfcOk(bool ok) { if (_nfcOk != ok) { _nfcOk = ok; _needsRedraw = true; } }
void TouchDisplay::setScaleOk(bool ok) { if (_scaleOk != ok) { _scaleOk = ok; _needsRedraw = true; } }
void TouchDisplay::setServerUrl(const char* url) {
    if (strncmp(_serverUrl, url, sizeof(_serverUrl) - 1) != 0) {
        strncpy(_serverUrl, url, sizeof(_serverUrl) - 1);
        _needsRedraw = true;
    }
}
void TouchDisplay::setFirmwareVersion(const char* ver) {
    if (strncmp(_fwVersion, ver, sizeof(_fwVersion) - 1) != 0) {
        strncpy(_fwVersion, ver, sizeof(_fwVersion) - 1);
        _needsRedraw = true;
    }
}

void TouchDisplay::_render() {
    _drawStatusBar();

    switch (_screen) {
        case Screen::BOOT: _drawBoot(); break;
        case Screen::CONNECTING: _drawConnecting(); break;
        case Screen::HOME: _drawHome(); break;
        case Screen::IDLE: _drawIdle(); break;
        case Screen::STATUS: _drawStatus(); break;
        case Screen::TAG_READ: _drawTagRead(); break;
        case Screen::TAG_MATCHED: _drawTagMatched(); break;
        case Screen::CONFIRM_DELETE: _drawConfirmDelete(); break;
        case Screen::PRINTER_LIST: _drawPrinterList(); break;
        case Screen::SLOT_LIST: _drawSlotList(); break;
        case Screen::CLEAR_PLATE_LIST: _drawClearPlateList(); break;
        case Screen::TAG_UNKNOWN: _drawTagUnknown(); break;
        case Screen::TAG_WRITING: _drawTagWriting(); break;
        case Screen::TAG_WRITE_OK:
        case Screen::TAG_WRITE_FAIL: _drawTagWriteResult(); break;
        case Screen::OTA_PROGRESS: _drawOta(); break;
        case Screen::ERROR: _drawError(); break;
        case Screen::PROVISIONING: _drawProvisioning(); break;
        case Screen::REG_MATERIAL: _drawRegMaterial(); break;
        case Screen::REG_BRAND: _drawRegBrand(); break;
        case Screen::REG_COLOR: _drawRegColor(); break;
        case Screen::REG_WEIGHT: _drawRegWeight(); break;
        case Screen::REG_CORE_WEIGHT: _drawRegCoreWeight(); break;
        case Screen::REG_CORE_CUSTOM: _drawRegCoreCustom(); break;
        case Screen::REG_CONFIRM: _drawRegConfirm(); break;
        case Screen::SETTINGS_GRID: _drawSettingsGrid(); break;
        case Screen::CAL_WEIGHT: _drawCalWeight(); break;
        case Screen::NUMPAD: _drawNumpad(); break;
        default: break;
    }

    // Push entire canvas to display in one shot — no flicker
    _canvas.pushSprite(&_tft, 0, 0);
}

void TouchDisplay::_drawStatusBar() {
    _canvas.fillRect(0, 0, TFT_WIDTH, STATUS_BAR_H, C_STATUS_BG);
    _drawWifiIcon(8, 6, _wifiRSSI);

    // Server URL (truncated if needed)
    _canvas.setTextSize(1);
    _canvas.setTextColor(_backendConnected ? C_SUCCESS : C_TEXT_DIM);
    _canvas.setCursor(38, 12);
    if (_serverUrl[0]) {
        _canvas.print(_serverUrl);
    } else {
        _canvas.print("not connected");
    }

    // NFC and SCALE indicators
    _canvas.setTextColor(_nfcOk ? C_SUCCESS : C_TEXT_DIM);
    _canvas.setCursor(310, 12);
    _canvas.print("NFC");

    _canvas.setTextColor(_scaleOk ? C_SUCCESS : C_TEXT_DIM);
    _canvas.setCursor(340, 12);
    _canvas.print("SCALE");

    // Firmware version right-aligned
    if (_fwVersion[0]) {
        _canvas.setTextColor(C_TEXT_DIM);
        int16_t vw = strlen(_fwVersion) * 6; // approx width at size 1
        _canvas.setCursor(TFT_WIDTH - vw - 8, 12);
        _canvas.print("v");
        _canvas.print(_fwVersion);
    }
}

void TouchDisplay::_drawBoot() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(3);
    _canvas.drawString("SpoolBuddy", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 20);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(2);
    _canvas.drawString(_otaVersion, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 30);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawConnecting() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Connecting to WiFi", TFT_WIDTH / 2, CONTENT_Y + 48);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(3);
    _canvas.drawString(_msgBuf[0] ? _msgBuf : "Stored network", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString("Waiting for network and backend", TFT_WIDTH / 2, TFT_HEIGHT - 34);
    _canvas.setTextDatum(lgfx::top_left);
}

// Home grid: 2 rows x 3 columns of touch tiles
// Layout constants for the 480x320 landscape display
static constexpr int GRID_COLS    = 3;
static constexpr int GRID_ROWS    = 2;
static constexpr int GRID_PAD     = 12;
static constexpr int GRID_TOP     = STATUS_BAR_H + GRID_PAD;
static constexpr int GRID_AVAIL_W = TFT_WIDTH - (GRID_PAD * (GRID_COLS + 1));
static constexpr int GRID_AVAIL_H = (TFT_HEIGHT - STATUS_BAR_H) - (GRID_PAD * (GRID_ROWS + 1));
static constexpr int TILE_W       = GRID_AVAIL_W / GRID_COLS;
static constexpr int TILE_H       = GRID_AVAIL_H / GRID_ROWS;

struct TileInfo {
    const char* icon;
    const char* label;
    uint16_t color;
};

static const TileInfo TILES[6] = {
    { nullptr, "Scale",    0x34DF },  // C_ACCENT — scale gets live weight instead of icon
    { "NFC",   "Scan",     0x07E0 },  // C_SUCCESS
    { "TAG",   "Assign",   0xFD20 },  // C_WARNING
    { "0g",    "Tare",     0x34DF },  // C_ACCENT
    { "CLR",   "Clear",    0x07E0 },  // C_SUCCESS — clear plate
    { "*",     "Settings", 0x8410 },  // C_TEXT_DIM
};

static void getTileRect(int index, int& x, int& y, int& w, int& h) {
    int col = index % GRID_COLS;
    int row = index / GRID_COLS;
    x = GRID_PAD + col * (TILE_W + GRID_PAD);
    y = GRID_TOP + row * (TILE_H + GRID_PAD);
    w = TILE_W;
    h = TILE_H;
}

void TouchDisplay::_drawHome() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    for (int i = 0; i < 6; i++) {
        int tx, ty, tw, th;
        getTileRect(i, tx, ty, tw, th);

        // Tile background
        uint16_t bgColor = 0x2104;  // dark card
        _canvas.fillRoundRect(tx, ty, tw, th, 8, bgColor);
        _canvas.drawRoundRect(tx, ty, tw, th, 8, TILES[i].color);

        int cx = tx + tw / 2;
        int cy = ty + th / 2;

        _canvas.setTextDatum(lgfx::middle_center);

        if (i == 0) {
            _drawHomeScaleTile();
        } else {
            // Icon text
            _canvas.setTextColor(TILES[i].color);
            _canvas.setTextSize(3);
            _canvas.drawString(TILES[i].icon, cx, cy - 10);
        }

        // Label at bottom of tile
        _canvas.setTextColor(C_TEXT);
        _canvas.setTextSize(1);
        _canvas.drawString(TILES[i].label, cx, ty + th - 12);
    }

    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawHomeScaleTile() {
    int tx, ty, tw, th;
    getTileRect(0, tx, ty, tw, th);

    // Clear only tile interior (inset by border)
    _canvas.fillRoundRect(tx + 2, ty + 2, tw - 4, th - 18, 6, 0x2104);

    int cx = tx + tw / 2;
    int cy = ty + th / 2;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fg", _weightGrams);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(_weightStable ? C_TEXT : C_TEXT_DIM);
    _canvas.setTextSize(3);
    _canvas.drawString(buf, cx, cy - 10);

    _canvas.setTextSize(1);
    _canvas.setTextColor(_weightStable ? C_SUCCESS : C_TEXT_DIM);
    _canvas.drawString(_weightStable ? "STABLE" : "...", cx, cy + 18);
    _canvas.setTextDatum(lgfx::top_left);
}

HomeTile TouchDisplay::hitTestHomeTile(int16_t x, int16_t y) const {
    if (_screen != Screen::HOME) return HomeTile::NONE;

    for (int i = 0; i < 6; i++) {
        int tx, ty, tw, th;
        getTileRect(i, tx, ty, tw, th);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) {
            return static_cast<HomeTile>(i);
        }
    }
    return HomeTile::NONE;
}

// --- Settings Grid ---

static constexpr int SETTINGS_TILE_COUNT = 5;

static const TileInfo SETTINGS_TILES[SETTINGS_TILE_COUNT] = {
    { "CAL",  "Calibrate", 0x34DF },  // C_ACCENT
    { "OTA",  "Update",    0x07E0 },  // C_SUCCESS
    { "WiFi", "WiFi",      0xFD20 },  // C_WARNING
    { "RST",  "Reboot",    0xF800 },  // C_ERROR / red
    { "(i)",  "Info",      0x8410 },  // C_TEXT_DIM
};

static void getSettingsTileRect(int index, int& x, int& y, int& w, int& h) {
    // 3x2 grid
    static constexpr int S_COLS = 3;
    static constexpr int S_ROWS = 2;
    static constexpr int S_PAD = 12;
    static constexpr int S_TOP = STATUS_BAR_H + 30;  // room for title
    int availW = TFT_WIDTH - S_PAD * (S_COLS + 1);
    int availH = TFT_HEIGHT - S_TOP - S_PAD * (S_ROWS + 1);
    int tileW = availW / S_COLS;
    int tileH = availH / S_ROWS;
    int col = index % S_COLS;
    int row = index / S_COLS;
    x = S_PAD + col * (tileW + S_PAD);
    y = S_TOP + S_PAD + row * (tileH + S_PAD);
    w = tileW;
    h = tileH;
}

void TouchDisplay::showSettingsGrid() {
    _screen = Screen::SETTINGS_GRID;
    _needsRedraw = true;
}

void TouchDisplay::_drawSettingsGrid() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    // Title
    _canvas.setTextDatum(lgfx::top_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString("Settings", TFT_WIDTH / 2, STATUS_BAR_H + 4);
    _canvas.setTextDatum(lgfx::top_left);

    for (int i = 0; i < SETTINGS_TILE_COUNT; i++) {
        int tx, ty, tw, th;
        getSettingsTileRect(i, tx, ty, tw, th);

        uint16_t bgColor = 0x2104;
        _canvas.fillRoundRect(tx, ty, tw, th, 8, bgColor);
        _canvas.drawRoundRect(tx, ty, tw, th, 8, SETTINGS_TILES[i].color);

        int cx = tx + tw / 2;
        int cy = ty + th / 2;

        _canvas.setTextDatum(lgfx::middle_center);
        _canvas.setTextColor(SETTINGS_TILES[i].color);
        _canvas.setTextSize(3);
        _canvas.drawString(SETTINGS_TILES[i].icon, cx, cy - 10);

        _canvas.setTextColor(C_TEXT);
        _canvas.setTextSize(1);
        _canvas.drawString(SETTINGS_TILES[i].label, cx, ty + th - 12);
    }

    _canvas.setTextDatum(lgfx::top_left);
    _drawBackButton();
}

SettingsTile TouchDisplay::hitTestSettingsTile(int16_t x, int16_t y) const {
    if (_screen != Screen::SETTINGS_GRID) return SettingsTile::NONE;

    for (int i = 0; i < SETTINGS_TILE_COUNT; i++) {
        int tx, ty, tw, th;
        getSettingsTileRect(i, tx, ty, tw, th);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) {
            return static_cast<SettingsTile>(i);
        }
    }
    return SettingsTile::NONE;
}

// --- Back Button (shared by submenus) ---
static constexpr int BACK_BTN_X = 8;
static constexpr int BACK_BTN_Y = STATUS_BAR_H + 4;
static constexpr int BACK_BTN_W = 60;
static constexpr int BACK_BTN_H = 24;

void TouchDisplay::_drawBackButton() {
    _canvas.fillRoundRect(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H, 6, 0x2104);
    _canvas.drawRoundRect(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H, 6, C_TEXT_DIM);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(1);
    _canvas.drawString("< Back", BACK_BTN_X + BACK_BTN_W / 2, BACK_BTN_Y + BACK_BTN_H / 2);
    _canvas.setTextDatum(lgfx::top_left);
}

bool TouchDisplay::hitTestBackButton(int16_t x, int16_t y) const {
    return x >= BACK_BTN_X && x < BACK_BTN_X + BACK_BTN_W
        && y >= BACK_BTN_Y && y < BACK_BTN_Y + BACK_BTN_H;
}

// --- Calibration Weight Selection (4 buttons) ---

void TouchDisplay::showCalWeight() {
    _screen = Screen::CAL_WEIGHT;
    _needsRedraw = true;
}

void TouchDisplay::_drawCalWeight() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    _canvas.setTextDatum(lgfx::top_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Select Cal Weight", TFT_WIDTH / 2, STATUS_BAR_H + 8);

    // 4 buttons: 2x2 grid
    static constexpr int BTN_W = 180;
    static constexpr int BTN_H = 90;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 44;

    static const char* labels[] = {"250g", "500g", "1000g", "Custom"};
    static const uint16_t colors[] = {C_SUCCESS, C_SUCCESS, C_SUCCESS, C_ACCENT};

    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = x0 + col * (BTN_W + GAP);
        int by = y0 + row * (BTN_H + GAP);

        _canvas.fillRoundRect(bx, by, BTN_W, BTN_H, 10, 0x2104);
        _canvas.drawRoundRect(bx, by, BTN_W, BTN_H, 10, colors[i]);

        _canvas.setTextDatum(lgfx::middle_center);
        _canvas.setTextColor(C_TEXT);
        _canvas.setTextSize(3);
        _canvas.drawString(labels[i], bx + BTN_W / 2, by + BTN_H / 2);
    }

    _canvas.setTextDatum(lgfx::top_left);
    _drawBackButton();
}

int TouchDisplay::hitTestCalWeight(int16_t x, int16_t y) const {
    if (_screen != Screen::CAL_WEIGHT) return -1;

    static constexpr int BTN_W = 180;
    static constexpr int BTN_H = 90;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 44;

    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = x0 + col * (BTN_W + GAP);
        int by = y0 + row * (BTN_H + GAP);
        if (x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H) {
            return i;
        }
    }
    return -1;
}

// --- Numeric Keypad ---

void TouchDisplay::showNumpad(const char* title) {
    strncpy(_numpadTitle, title, sizeof(_numpadTitle) - 1);
    _numpadBuf[0] = '\0';
    _numpadLen = 0;
    _screen = Screen::NUMPAD;
    _needsRedraw = true;
}

void TouchDisplay::numpadAppend(char c) {
    if (_numpadLen < (int)sizeof(_numpadBuf) - 1) {
        _numpadBuf[_numpadLen++] = c;
        _numpadBuf[_numpadLen] = '\0';
        _needsRedraw = true;
    }
}

void TouchDisplay::numpadBackspace() {
    if (_numpadLen > 0) {
        _numpadBuf[--_numpadLen] = '\0';
        _needsRedraw = true;
    }
}

void TouchDisplay::numpadClear() {
    _numpadBuf[0] = '\0';
    _numpadLen = 0;
    _needsRedraw = true;
}

int TouchDisplay::numpadValue() const {
    return atoi(_numpadBuf);
}

void TouchDisplay::_drawNumpad() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    // Title
    _canvas.setTextDatum(lgfx::top_center);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString(_numpadTitle, TFT_WIDTH / 2, STATUS_BAR_H + 4);

    // Input display
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(4);
    char display[16];
    if (_numpadLen > 0) {
        snprintf(display, sizeof(display), "%s g", _numpadBuf);
    } else {
        snprintf(display, sizeof(display), "_ g");
    }
    _canvas.drawString(display, TFT_WIDTH / 2, STATUS_BAR_H + 22);

    // Keypad grid: 4 rows x 3 cols
    // [1][2][3]  [4][5][6]  [7][8][9]  [<][0][OK]
    static constexpr int KEY_W = 80;
    static constexpr int KEY_H = 50;
    static constexpr int KEY_GAP = 8;
    int kx0 = (TFT_WIDTH - 3 * KEY_W - 2 * KEY_GAP) / 2;
    int ky0 = STATUS_BAR_H + 72;

    static const char* keys[] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "<", "0", "OK"
    };

    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int kx = kx0 + col * (KEY_W + KEY_GAP);
        int ky = ky0 + row * (KEY_H + KEY_GAP);

        uint16_t bg = 0x2104;
        uint16_t border = C_TEXT_DIM;
        uint16_t textColor = C_TEXT;
        if (i == 9) { border = C_WARNING; textColor = C_WARNING; }  // backspace
        if (i == 11) { border = C_SUCCESS; textColor = C_SUCCESS; }  // OK

        _canvas.fillRoundRect(kx, ky, KEY_W, KEY_H, 8, bg);
        _canvas.drawRoundRect(kx, ky, KEY_W, KEY_H, 8, border);

        _canvas.setTextDatum(lgfx::middle_center);
        _canvas.setTextColor(textColor);
        _canvas.setTextSize(3);
        _canvas.drawString(keys[i], kx + KEY_W / 2, ky + KEY_H / 2);
    }

    _canvas.setTextDatum(lgfx::top_left);
}

int TouchDisplay::hitTestNumpadKey(int16_t x, int16_t y) const {
    if (_screen != Screen::NUMPAD) return -1;

    static constexpr int KEY_W = 80;
    static constexpr int KEY_H = 50;
    static constexpr int KEY_GAP = 8;
    int kx0 = (TFT_WIDTH - 3 * KEY_W - 2 * KEY_GAP) / 2;
    int ky0 = STATUS_BAR_H + 72;

    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int kx = kx0 + col * (KEY_W + KEY_GAP);
        int ky = ky0 + row * (KEY_H + KEY_GAP);
        if (x >= kx && x < kx + KEY_W && y >= ky && y < ky + KEY_H) {
            if (i < 9) return i + 1;     // keys 1-9
            if (i == 9) return 10;        // backspace
            if (i == 10) return 0;        // key 0
            if (i == 11) return 11;       // OK
        }
    }
    return -1;
}

bool TouchDisplay::hitTestTagUnknownRegister(int16_t x, int16_t y) const {
    return _screen == Screen::TAG_UNKNOWN
        && x >= TAG_UNKNOWN_REGISTER_X
        && x < (TAG_UNKNOWN_REGISTER_X + TAG_UNKNOWN_REGISTER_W)
        && y >= TAG_UNKNOWN_REGISTER_Y
        && y < (TAG_UNKNOWN_REGISTER_Y + TAG_UNKNOWN_REGISTER_H);
}

int TouchDisplay::hitTestRegColorTile(int16_t x, int16_t y) const {
    if (_screen != Screen::REG_COLOR) {
        return -1;
    }
    if (x < REG_COLOR_GRID_X0 || y < REG_COLOR_GRID_Y0) {
        return -1;
    }

    int relX = x - REG_COLOR_GRID_X0;
    int relY = y - REG_COLOR_GRID_Y0;
    int spanW = REG_COLOR_TILE_W + REG_COLOR_TILE_GAP;
    int spanH = REG_COLOR_TILE_H + REG_COLOR_TILE_GAP;
    int col = relX / spanW;
    int row = relY / spanH;

    if (col < 0 || col >= REG_COLOR_GRID_COLS || row < 0 || row >= REG_COLOR_GRID_ROWS) {
        return -1;
    }
    if ((relX % spanW) >= REG_COLOR_TILE_W || (relY % spanH) >= REG_COLOR_TILE_H) {
        return -1;
    }
    int index = row * REG_COLOR_GRID_COLS + col;
    return index < static_cast<int>(sizeof(REG_COLORS) / sizeof(REG_COLORS[0])) ? index : -1;
}

bool TouchDisplay::hitTestRegConfirmRegister(int16_t x, int16_t y) const {
    return _screen == Screen::REG_CONFIRM
        && x >= REG_CONFIRM_REGISTER_X
        && x < (REG_CONFIRM_REGISTER_X + REG_CONFIRM_BTN_W)
        && y >= REG_CONFIRM_BTN_Y
        && y < (REG_CONFIRM_BTN_Y + REG_CONFIRM_BTN_H);
}

bool TouchDisplay::hitTestRegConfirmCancel(int16_t x, int16_t y) const {
    return _screen == Screen::REG_CONFIRM
        && x >= REG_CONFIRM_CANCEL_X
        && x < (REG_CONFIRM_CANCEL_X + REG_CONFIRM_BTN_W)
        && y >= REG_CONFIRM_BTN_Y
        && y < (REG_CONFIRM_BTN_Y + REG_CONFIRM_BTN_H);
}

int TouchDisplay::hitTestRegWeight(int16_t x, int16_t y) const {
    if (_screen != Screen::REG_WEIGHT) return -1;

    static constexpr int BTN_W = 180;
    static constexpr int BTN_H = 90;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 44;

    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = x0 + col * (BTN_W + GAP);
        int by = y0 + row * (BTN_H + GAP);
        if (x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H) {
            return i;  // 0=250g, 1=500g, 2=1000g, 3=custom
        }
    }
    return -1;
}

int TouchDisplay::hitTestRegCoreWeight(int16_t x, int16_t y) const {
    if (_screen != Screen::REG_CORE_WEIGHT) return -1;

    static constexpr int BTN_W = 200;
    static constexpr int BTN_H = 80;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 44;

    // Row 1: buttons 0 and 1
    for (int i = 0; i < 2; i++) {
        int bx = x0 + i * (BTN_W + GAP);
        int by = y0;
        if (x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H) {
            return i;  // 0=low temp 250g, 1=high temp 216g
        }
    }

    // Row 2: custom centered
    int bx = (TFT_WIDTH - BTN_W) / 2;
    int by = y0 + BTN_H + GAP;
    if (x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H) {
        return 2;  // custom
    }

    return -1;
}

int TouchDisplay::hitTestRegCoreCustom(int16_t x, int16_t y) const {
    if (_screen != Screen::REG_CORE_CUSTOM) return -1;

    static constexpr int BTN_W = 200;
    static constexpr int BTN_H = 100;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 70;

    // Weigh button
    if (x >= x0 && x < x0 + BTN_W && y >= y0 && y < y0 + BTN_H) {
        return 0;  // weigh
    }
    // Type button
    int bx2 = x0 + BTN_W + GAP;
    if (x >= bx2 && x < bx2 + BTN_W && y >= y0 && y < y0 + BTN_H) {
        return 1;  // type
    }
    return -1;
}

bool TouchDisplay::hitTestConfirmYes(int16_t x, int16_t y) const {
    return _screen == Screen::CONFIRM_DELETE
        && x >= CONFIRM_YES_BTN_X
        && x < (CONFIRM_YES_BTN_X + CONFIRM_BTN_W)
        && y >= CONFIRM_BTN_Y
        && y < (CONFIRM_BTN_Y + CONFIRM_BTN_H);
}

bool TouchDisplay::hitTestConfirmNo(int16_t x, int16_t y) const {
    return _screen == Screen::CONFIRM_DELETE
        && x >= CONFIRM_NO_BTN_X
        && x < (CONFIRM_NO_BTN_X + CONFIRM_BTN_W)
        && y >= CONFIRM_BTN_Y
        && y < (CONFIRM_BTN_Y + CONFIRM_BTN_H);
}

int TouchDisplay::hitTestTagMatchedAction(int16_t x, int16_t y) const {
    if (_screen != Screen::TAG_MATCHED) {
        return -1;
    }
    if (x >= TAG_DELETE_BTN_X && x < (TAG_DELETE_BTN_X + TAG_ACTION_BTN_W)
        && y >= TAG_ACTION_BTN_Y && y < (TAG_ACTION_BTN_Y + TAG_ACTION_BTN_H)) {
        return 0;
    }
    if (x >= TAG_ASSIGN_BTN_X && x < (TAG_ASSIGN_BTN_X + TAG_ACTION_BTN_W)
        && y >= TAG_ACTION_BTN_Y && y < (TAG_ACTION_BTN_Y + TAG_ACTION_BTN_H)) {
        return 1;
    }
    return -1;
}

int TouchDisplay::hitTestPrinterList(int16_t x, int16_t y) const {
    if (_screen != Screen::PRINTER_LIST) {
        return -1;
    }

    return hitTestGrid(
        x, y,
        PRINTER_GRID_X0, PRINTER_GRID_Y0,
        PRINTER_GRID_COLS, PRINTER_GRID_ROWS,
        PRINTER_BTN_W, PRINTER_BTN_H, PRINTER_GRID_GAP,
        _printerListCount);
}

int TouchDisplay::hitTestSlotList(int16_t x, int16_t y) const {
    if (_screen != Screen::SLOT_LIST) {
        return -1;
    }

    return hitTestGrid(
        x, y,
        PRINTER_GRID_X0, PRINTER_GRID_Y0,
        PRINTER_GRID_COLS, PRINTER_GRID_ROWS,
        PRINTER_BTN_W, PRINTER_BTN_H, PRINTER_GRID_GAP,
        _slotListCount);
}

int TouchDisplay::hitTestClearPlateList(int16_t x, int16_t y) const {
    if (_screen != Screen::CLEAR_PLATE_LIST) {
        return -1;
    }

    return hitTestGrid(
        x, y,
        CLEAR_GRID_X0, CLEAR_GRID_Y0,
        CLEAR_GRID_COLS, CLEAR_GRID_ROWS,
        CLEAR_BTN_W, CLEAR_BTN_H, CLEAR_GRID_GAP,
        _clearPlateListCount);
}

int TouchDisplay::hitTestRegMaterial(int16_t x, int16_t y) const {
    if (_screen != Screen::REG_MATERIAL) {
        return -1;
    }

    return hitTestGrid(
        x, y,
        REG_MATERIAL_X0, REG_MATERIAL_Y0,
        REG_MATERIAL_COLS, REG_MATERIAL_ROWS,
        REG_MATERIAL_BTN_W, REG_MATERIAL_BTN_H, REG_MATERIAL_GAP,
        static_cast<int>(sizeof(REG_MATERIALS) / sizeof(REG_MATERIALS[0])));
}

int TouchDisplay::hitTestRegBrand(int16_t x, int16_t y) const {
    if (_screen != Screen::REG_BRAND) {
        return -1;
    }

    return hitTestGrid(
        x, y,
        REG_BRAND_X0, REG_BRAND_Y0,
        REG_BRAND_COLS, REG_BRAND_ROWS,
        REG_BRAND_BTN_W, REG_BRAND_BTN_H, REG_BRAND_GAP,
        static_cast<int>(sizeof(REG_BRANDS) / sizeof(REG_BRANDS[0])));
}

void TouchDisplay::_drawIdle() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f g", _weightGrams);

    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(_weightStable ? C_TEXT : C_TEXT_DIM);
    _canvas.setTextSize(5);
    _canvas.drawString(buf, TFT_WIDTH / 2, TFT_HEIGHT / 2 - 10);

    _canvas.setTextSize(2);
    _canvas.setTextColor(_weightStable ? C_SUCCESS : C_TEXT_DIM);
    _canvas.drawString(_weightStable ? "STABLE" : "MEASURING...", TFT_WIDTH / 2, TFT_HEIGHT / 2 + 50);

    _canvas.setTextSize(1);
    _canvas.setTextColor(C_ACCENT);
    _canvas.drawString("Place spool on NFC reader", TFT_WIDTH / 2, TFT_HEIGHT - 34);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawStatus() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);

    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(3);
    _canvas.drawString(_titleBuf, TFT_WIDTH / 2, CONTENT_Y + 42);

    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString(_line1Buf, TFT_WIDTH / 2, CONTENT_Y + 110);
    _canvas.drawString(_line2Buf, TFT_WIDTH / 2, CONTENT_Y + 150);

    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(2);
    _canvas.drawString(_line3Buf, TFT_WIDTH / 2, CONTENT_Y + 190);

    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString("Tap or BOOT to return", TFT_WIDTH / 2, TFT_HEIGHT - 28);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawTagRead() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString(_msgBuf, TFT_WIDTH / 2, CONTENT_Y + 56);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(1);
    _canvas.drawString(_uidBuf, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 20);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawTagMatched() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _drawColorSwatch(20, CONTENT_Y + 20, 120, 120, _spoolInfo.colorHex);

    int textX = 160;
    _canvas.setTextDatum(lgfx::top_left);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(3);
    _canvas.setCursor(textX, CONTENT_Y + 20);
    _canvas.print(_spoolInfo.material);

    _canvas.setTextSize(2);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setCursor(textX, CONTENT_Y + 55);
    _canvas.print(_spoolInfo.colorName);

    _canvas.setTextSize(2);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setCursor(textX, CONTENT_Y + 85);
    _canvas.print(_spoolInfo.brand);

    if (_spoolAssigned) {
        _canvas.setTextSize(1);
        _canvas.setTextColor(C_WARNING);
        _canvas.setCursor(textX, CONTENT_Y + 118);
        _canvas.print("Assigned:");
        _canvas.setTextColor(C_TEXT);
        _canvas.setCursor(textX, CONTENT_Y + 132);
        _canvas.print(_assignedPrinterName);
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "Used: %.0fg / %.0fg", _spoolInfo.weightUsed, _spoolInfo.labelWeight);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextSize(2);
    _canvas.setTextColor(C_TEXT);
    _canvas.drawString(buf, TFT_WIDTH / 2, TFT_HEIGHT - 110);

    int pct = (_spoolInfo.labelWeight > 0) ? (int)((_spoolInfo.weightUsed / _spoolInfo.labelWeight) * 100) : 0;
    _drawProgressBar(20, TFT_HEIGHT - 88, TFT_WIDTH - 40, 18, pct, C_ACCENT);

    _drawButton(TAG_DELETE_BTN_X, TAG_ACTION_BTN_Y, TAG_ACTION_BTN_W, TAG_ACTION_BTN_H, "Delete", C_DELETE_BTN_BG);
    _canvas.drawRoundRect(TAG_DELETE_BTN_X, TAG_ACTION_BTN_Y, TAG_ACTION_BTN_W, TAG_ACTION_BTN_H, 8, C_ERROR);

    const char* actionLabel = _spoolAssigned ? "Unassign" : "Assign";
    uint16_t actionBg = _spoolAssigned ? C_UNASSIGN_BTN_BG : C_ASSIGN_BTN_BG;
    uint16_t actionBorder = _spoolAssigned ? C_WARNING : C_ACCENT;
    _drawButton(TAG_ASSIGN_BTN_X, TAG_ACTION_BTN_Y, TAG_ACTION_BTN_W, TAG_ACTION_BTN_H, actionLabel, actionBg);
    _canvas.drawRoundRect(TAG_ASSIGN_BTN_X, TAG_ACTION_BTN_Y, TAG_ACTION_BTN_W, TAG_ACTION_BTN_H, 8, actionBorder);

    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawConfirmDelete() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ERROR);
    _canvas.setTextSize(3);
    _canvas.drawString("Delete Spool?", TFT_WIDTH / 2, CONTENT_Y + 54);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString(_confirmPrompt, TFT_WIDTH / 2, CONTENT_Y + 126);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString("This cannot be undone", TFT_WIDTH / 2, CONTENT_Y + 166);

    _drawButton(CONFIRM_NO_BTN_X, CONFIRM_BTN_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, "No", C_SUCCESS);
    _canvas.drawRoundRect(CONFIRM_NO_BTN_X, CONFIRM_BTN_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, 8, C_SUCCESS);
    _drawButton(CONFIRM_YES_BTN_X, CONFIRM_BTN_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, "Yes", C_ERROR);
    _canvas.drawRoundRect(CONFIRM_YES_BTN_X, CONFIRM_BTN_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, 8, C_ERROR);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawPrinterList() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Select Printer", TFT_WIDTH / 2, CONTENT_Y + 18);

    for (int index = 0; index < _printerListCount; ++index) {
        int col = index % PRINTER_GRID_COLS;
        int row = index / PRINTER_GRID_COLS;
        int bx = PRINTER_GRID_X0 + col * (PRINTER_BTN_W + PRINTER_GRID_GAP);
        int by = PRINTER_GRID_Y0 + row * (PRINTER_BTN_H + PRINTER_GRID_GAP);

        drawGridButton(_canvas, bx, by, PRINTER_BTN_W, PRINTER_BTN_H, _printerNames[index], C_ACCENT, 1);
    }
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawSlotList() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    char title[48];
    snprintf(title, sizeof(title), "%.20s - Select Slot", _slotPrinterName);
    _canvas.drawString(title, TFT_WIDTH / 2, CONTENT_Y + 18);

    for (int index = 0; index < _slotListCount; ++index) {
        int col = index % PRINTER_GRID_COLS;
        int row = index / PRINTER_GRID_COLS;
        int bx = PRINTER_GRID_X0 + col * (PRINTER_BTN_W + PRINTER_GRID_GAP);
        int by = PRINTER_GRID_Y0 + row * (PRINTER_BTN_H + PRINTER_GRID_GAP);

        drawGridButton(_canvas, bx, by, PRINTER_BTN_W, PRINTER_BTN_H, _slotLabels[index], C_ACCENT, 1);
    }
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawClearPlateList() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);

    if (_clearPlateListCount == 0) {
        _canvas.drawString("No plates to clear", TFT_WIDTH / 2, TFT_HEIGHT / 2);
    } else {
        _canvas.drawString("Clear Plate", TFT_WIDTH / 2, CONTENT_Y + 18);

        for (int index = 0; index < _clearPlateListCount; ++index) {
            int col = index % CLEAR_GRID_COLS;
            int row = index / CLEAR_GRID_COLS;
            int bx = CLEAR_GRID_X0 + col * (CLEAR_BTN_W + CLEAR_GRID_GAP);
            int by = CLEAR_GRID_Y0 + row * (CLEAR_BTN_H + CLEAR_GRID_GAP);

            drawGridButton(_canvas, bx, by, CLEAR_BTN_W, CLEAR_BTN_H, _clearPlateNames[index], C_SUCCESS, 1);
        }
    }
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawTagUnknown() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_WARNING);
    _canvas.setTextSize(3);
    _canvas.drawString("Unknown Tag", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 40);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString(_uidBuf, TFT_WIDTH / 2, TFT_HEIGHT / 2);
    _drawButton(TAG_UNKNOWN_REGISTER_X, TAG_UNKNOWN_REGISTER_Y, TAG_UNKNOWN_REGISTER_W, TAG_UNKNOWN_REGISTER_H, "Register", C_ACCENT);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawTagWriting() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString(_msgBuf, TFT_WIDTH / 2, TFT_HEIGHT / 2);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawTagWriteResult() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    bool ok = (_screen == Screen::TAG_WRITE_OK);
    _canvas.setTextColor(ok ? C_SUCCESS : C_ERROR);
    _canvas.setTextSize(3);
    _canvas.drawString(ok ? "SUCCESS" : "FAILED", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 20);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(2);
    _canvas.drawString(_msgBuf, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 30);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawOta() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString("Updating Firmware", TFT_WIDTH / 2, CONTENT_Y + 40);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString(_otaVersion, TFT_WIDTH / 2, CONTENT_Y + 75);
    _drawProgressBar(40, TFT_HEIGHT / 2 - 10, TFT_WIDTH - 80, 24, _otaPercent, C_SUCCESS);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _otaPercent);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString(buf, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 35);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawError() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ERROR);
    _canvas.setTextSize(3);
    _canvas.drawString("ERROR", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 20);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString(_msgBuf, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 30);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawProvisioning() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("WiFi Setup", TFT_WIDTH / 2, CONTENT_Y + 40);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString("Connect to:", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 20);
    _canvas.setTextColor(C_WARNING);
    _canvas.setTextSize(3);
    _canvas.drawString(_msgBuf, TFT_WIDTH / 2, TFT_HEIGHT / 2 + 20);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString("Then open 192.168.4.1", TFT_WIDTH / 2, TFT_HEIGHT - 30);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawRegStepIndicator(int step, int totalSteps) {
    char buf[16];
    snprintf(buf, sizeof(buf), "Step %d/%d", step, totalSteps);
    _canvas.setTextDatum(lgfx::bottom_right);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString(buf, TFT_WIDTH - 12, TFT_HEIGHT - 10);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawRegMaterial() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Select Material", TFT_WIDTH / 2, CONTENT_Y + 18);

    for (int i = 0; i < static_cast<int>(sizeof(REG_MATERIALS) / sizeof(REG_MATERIALS[0])); ++i) {
        int col = i % REG_MATERIAL_COLS;
        int row = i / REG_MATERIAL_COLS;
        int bx = REG_MATERIAL_X0 + col * (REG_MATERIAL_BTN_W + REG_MATERIAL_GAP);
        int by = REG_MATERIAL_Y0 + row * (REG_MATERIAL_BTN_H + REG_MATERIAL_GAP);
        int textSize = (strcmp(REG_MATERIALS[i], "PA/Nylon") == 0
            || strcmp(REG_MATERIALS[i], "PLA-CF") == 0
            || strcmp(REG_MATERIALS[i], "PETG-CF") == 0
            || strcmp(REG_MATERIALS[i], "PA-CF") == 0) ? 1 : 2;

        drawGridButton(_canvas, bx, by, REG_MATERIAL_BTN_W, REG_MATERIAL_BTN_H, REG_MATERIALS[i], C_SUCCESS, textSize);
    }

    _drawRegStepIndicator(1, REG_TOTAL_STEPS);
    _drawBackButton();
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawRegBrand() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Select Brand", TFT_WIDTH / 2, CONTENT_Y + 18);

    for (int i = 0; i < static_cast<int>(sizeof(REG_BRANDS) / sizeof(REG_BRANDS[0])); ++i) {
        int col = i % REG_BRAND_COLS;
        int row = i / REG_BRAND_COLS;
        int bx = REG_BRAND_X0 + col * (REG_BRAND_BTN_W + REG_BRAND_GAP);
        int by = REG_BRAND_Y0 + row * (REG_BRAND_BTN_H + REG_BRAND_GAP);
        int textSize = (strcmp(REG_BRANDS[i], "Polymaker") == 0 || strcmp(REG_BRANDS[i], "Prusament") == 0) ? 1 : 2;

        drawGridButton(_canvas, bx, by, REG_BRAND_BTN_W, REG_BRAND_BTN_H, REG_BRANDS[i], C_SUCCESS, textSize);
    }

    _drawRegStepIndicator(2, REG_TOTAL_STEPS);
    _drawBackButton();
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawRegColor() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Select Color", TFT_WIDTH / 2, 50);

    for (int i = 0; i < static_cast<int>(sizeof(REG_COLORS) / sizeof(REG_COLORS[0])); ++i) {
        int col = i % REG_COLOR_GRID_COLS;
        int row = i / REG_COLOR_GRID_COLS;
        int x = REG_COLOR_GRID_X0 + col * (REG_COLOR_TILE_W + REG_COLOR_TILE_GAP);
        int y = REG_COLOR_GRID_Y0 + row * (REG_COLOR_TILE_H + REG_COLOR_TILE_GAP);
        uint16_t border = (i == _regColorIdx) ? C_TEXT : C_TEXT_DIM;

        _canvas.fillRect(x, y, REG_COLOR_TILE_W, REG_COLOR_TILE_H, 0x2104);
        if (i == 23) {
            drawTransparentTile(_canvas, x + 4, y + 4, REG_COLOR_TILE_W - 8, REG_COLOR_TILE_H - 8);
        } else {
            _canvas.fillRect(x + 4, y + 4, REG_COLOR_TILE_W - 8, REG_COLOR_TILE_H - 8, REG_COLORS[i].tft);
        }

        _canvas.drawRect(x, y, REG_COLOR_TILE_W, REG_COLOR_TILE_H, border);
        _canvas.drawRect(x + 1, y + 1, REG_COLOR_TILE_W - 2, REG_COLOR_TILE_H - 2, border);
    }

    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(1);
    _canvas.drawString(REG_COLORS[_regColorIdx].name, TFT_WIDTH / 2, 312);
    _drawRegStepIndicator(3, REG_TOTAL_STEPS);
    _drawBackButton();
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawRegWeight() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    _drawRegStepIndicator(4, REG_TOTAL_STEPS);

    _canvas.setTextDatum(lgfx::top_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString("Label Weight", TFT_WIDTH / 2, STATUS_BAR_H + 8);

    // 4 buttons: 2x2 grid
    static constexpr int BTN_W = 180;
    static constexpr int BTN_H = 90;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 44;

    static const char* labels[] = {"250g", "500g", "1000g", "Custom"};
    static const uint16_t colors[] = {C_SUCCESS, C_SUCCESS, C_SUCCESS, C_ACCENT};

    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = x0 + col * (BTN_W + GAP);
        int by = y0 + row * (BTN_H + GAP);

        _canvas.fillRoundRect(bx, by, BTN_W, BTN_H, 10, 0x2104);
        _canvas.drawRoundRect(bx, by, BTN_W, BTN_H, 10, colors[i]);

        _canvas.setTextDatum(lgfx::middle_center);
        _canvas.setTextColor(C_TEXT);
        _canvas.setTextSize(3);
        _canvas.drawString(labels[i], bx + BTN_W / 2, by + BTN_H / 2);
    }

    _canvas.setTextDatum(lgfx::top_left);
    _drawBackButton();
}

void TouchDisplay::_drawRegCoreWeight() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    _drawRegStepIndicator(5, REG_TOTAL_STEPS);

    _canvas.setTextDatum(lgfx::top_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString("Empty Spool Weight", TFT_WIDTH / 2, STATUS_BAR_H + 8);

    // 3 buttons: 1 row of 2 + 1 centered below
    static constexpr int BTN_W = 200;
    static constexpr int BTN_H = 80;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 44;

    // Row 1: Bambu Low Temp, Bambu High Temp
    static const char* labels[] = {"Low Temp", "High Temp", "Custom"};
    static const char* sublabels[] = {"250g", "216g", ""};
    static const uint16_t colors[] = {C_SUCCESS, C_SUCCESS, C_ACCENT};

    for (int i = 0; i < 2; i++) {
        int bx = x0 + i * (BTN_W + GAP);
        int by = y0;

        _canvas.fillRoundRect(bx, by, BTN_W, BTN_H, 10, 0x2104);
        _canvas.drawRoundRect(bx, by, BTN_W, BTN_H, 10, colors[i]);

        _canvas.setTextDatum(lgfx::middle_center);
        _canvas.setTextColor(C_TEXT);
        _canvas.setTextSize(2);
        _canvas.drawString(labels[i], bx + BTN_W / 2, by + BTN_H / 2 - 10);
        _canvas.setTextColor(C_TEXT_DIM);
        _canvas.setTextSize(2);
        _canvas.drawString(sublabels[i], bx + BTN_W / 2, by + BTN_H / 2 + 14);
    }

    // Row 2: Custom centered
    int bx = (TFT_WIDTH - BTN_W) / 2;
    int by = y0 + BTN_H + GAP;
    _canvas.fillRoundRect(bx, by, BTN_W, BTN_H, 10, 0x2104);
    _canvas.drawRoundRect(bx, by, BTN_W, BTN_H, 10, C_ACCENT);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(3);
    _canvas.drawString("Custom", bx + BTN_W / 2, by + BTN_H / 2);

    _canvas.setTextDatum(lgfx::top_left);
    _drawBackButton();
}

void TouchDisplay::_drawRegCoreCustom() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);

    _canvas.setTextDatum(lgfx::top_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(2);
    _canvas.drawString("Custom Core Weight", TFT_WIDTH / 2, STATUS_BAR_H + 16);
    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.drawString("Place empty spool on scale, or type weight", TFT_WIDTH / 2, STATUS_BAR_H + 44);

    // Two buttons side by side
    static constexpr int BTN_W = 200;
    static constexpr int BTN_H = 100;
    static constexpr int GAP = 16;
    int x0 = (TFT_WIDTH - 2 * BTN_W - GAP) / 2;
    int y0 = STATUS_BAR_H + 70;

    _canvas.fillRoundRect(x0, y0, BTN_W, BTN_H, 10, 0x2104);
    _canvas.drawRoundRect(x0, y0, BTN_W, BTN_H, 10, C_SUCCESS);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(3);
    _canvas.drawString("Weigh", x0 + BTN_W / 2, y0 + BTN_H / 2);

    int bx2 = x0 + BTN_W + GAP;
    _canvas.fillRoundRect(bx2, y0, BTN_W, BTN_H, 10, 0x2104);
    _canvas.drawRoundRect(bx2, y0, BTN_W, BTN_H, 10, C_ACCENT);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextSize(3);
    _canvas.drawString("Type", bx2 + BTN_W / 2, y0 + BTN_H / 2);

    _canvas.setTextDatum(lgfx::top_left);
    _drawBackButton();
}

void TouchDisplay::_drawRegConfirm() {
    _canvas.fillRect(0, STATUS_BAR_H, TFT_WIDTH, CONTENT_H, C_BG);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextColor(C_ACCENT);
    _canvas.setTextSize(2);
    _canvas.drawString("Confirm Spool", TFT_WIDTH / 2, 52);
    _canvas.setTextDatum(lgfx::top_left);

    const int labelX = 26;
    const int valueX = 175;
    const int rowGap = 26;
    int y = 74;

    auto drawRow = [&](const char* label, const char* value, uint16_t color = C_TEXT) {
        _canvas.setTextColor(C_TEXT_DIM);
        _canvas.setTextSize(1);
        _canvas.setCursor(labelX, y);
        _canvas.print(label);
        _canvas.setTextColor(color);
        _canvas.setCursor(valueX, y);
        _canvas.print(value);
        y += rowGap;
    };

    drawRow("Material", REG_MATERIALS[_regMaterialIdx]);
    drawRow("Brand", REG_BRANDS[_regBrandIdx]);

    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.setCursor(labelX, y);
    _canvas.print("Color");
    _canvas.fillCircle(valueX + 8, y + 7, 8, REG_COLORS[_regColorIdx].tft);
    _canvas.drawCircle(valueX + 8, y + 7, 8, C_TEXT);
    if (_regColorIdx == 23) {
        _canvas.drawLine(valueX + 1, y + 12, valueX + 15, y + 2, C_TEXT_DIM);
        _canvas.drawLine(valueX + 2, y + 13, valueX + 16, y + 3, C_TEXT_DIM);
    }
    _canvas.setTextColor(C_TEXT);
    _canvas.setCursor(valueX + 24, y);
    _canvas.print(REG_COLORS[_regColorIdx].name);
    y += rowGap;

    char buf[24];
    snprintf(buf, sizeof(buf), "%dg", REG_LABEL_WEIGHTS[_regWeightIdx]);
    drawRow("Label Weight", buf);
    snprintf(buf, sizeof(buf), "%dg", _regCoreWeight);
    drawRow("Core Weight", buf);

    snprintf(buf, sizeof(buf), "%.0fg%s", _weightGrams, _weightStable ? "" : " *");
    drawRow("Scale", buf, _weightStable ? C_TEXT : C_WARNING);

    float netFilament = _weightGrams - static_cast<float>(_regCoreWeight);
    if (netFilament < 0.0f) {
        netFilament = 0.0f;
    }
    snprintf(buf, sizeof(buf), "%.0fg", netFilament);
    drawRow("Net Filament", buf, C_SUCCESS);

    _canvas.setTextColor(C_TEXT_DIM);
    _canvas.setTextSize(1);
    _canvas.setCursor(labelX, 252);
    _canvas.print("* scale settling");

    _drawButton(REG_CONFIRM_CANCEL_X, REG_CONFIRM_BTN_Y, REG_CONFIRM_BTN_W, REG_CONFIRM_BTN_H, "Cancel", C_TEXT_DIM);
    _drawButton(REG_CONFIRM_REGISTER_X, REG_CONFIRM_BTN_Y, REG_CONFIRM_BTN_W, REG_CONFIRM_BTN_H, "Register", C_ACCENT);
    _drawRegStepIndicator(6, REG_TOTAL_STEPS);
}

void TouchDisplay::_drawColorSwatch(int x, int y, int w, int h, uint32_t color) {
    uint16_t c565 = _canvas.color565((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    _canvas.fillRoundRect(x, y, w, h, 8, c565);
    _canvas.drawRoundRect(x, y, w, h, 8, C_TEXT_DIM);
}

void TouchDisplay::_drawProgressBar(int x, int y, int w, int h, int percent, uint16_t color) {
    percent = constrain(percent, 0, 100);
    int fill = (w * percent) / 100;

    _canvas.drawRoundRect(x, y, w, h, 4, C_TEXT_DIM);
    if (fill > 4) {
        _canvas.fillRoundRect(x + 2, y + 2, fill - 4, h - 4, 3, color);
    } else if (fill > 0) {
        _canvas.fillRect(x + 2, y + 2, fill, h - 4, color);
    }
}

void TouchDisplay::_drawButton(int x, int y, int w, int h, const char* label, uint16_t bg) {
    _canvas.fillRoundRect(x, y, w, h, 8, bg);
    _canvas.setTextColor(C_TEXT);
    _canvas.setTextDatum(lgfx::middle_center);
    _canvas.setTextSize(2);
    _canvas.drawString(label, x + w / 2, y + h / 2);
    _canvas.setTextDatum(lgfx::top_left);
}

void TouchDisplay::_drawWifiIcon(int x, int y, int8_t rssi) {
    uint16_t color;
    if (!_wifiConnected) {
        color = C_ERROR;
    } else if (rssi > -50) {
        color = C_SUCCESS;
    } else if (rssi > -70) {
        color = C_WARNING;
    } else {
        color = C_ERROR;
    }

    _canvas.fillCircle(x + 10, y + 18, 3, color);
    if (rssi > -80) _canvas.drawArc(x + 10, y + 18, 8, 6, 225, 315, color);
    if (rssi > -65) _canvas.drawArc(x + 10, y + 18, 13, 11, 225, 315, color);
    if (rssi > -50) _canvas.drawArc(x + 10, y + 18, 18, 16, 225, 315, color);
}

void TouchDisplay::showDebugCoords(const char* text) {
    // Show in right side of status bar
    _canvas.fillRect(200, 0, 280, STATUS_BAR_H, C_STATUS_BG);
    _canvas.setTextColor(C_WARNING);
    _canvas.setTextSize(2);
    _canvas.setTextDatum(lgfx::middle_right);
    _canvas.drawString(text, TFT_WIDTH - 8, STATUS_BAR_H / 2);
    _canvas.setTextDatum(lgfx::top_left);
    _canvas.pushSprite(&_tft, 0, 0);
}
