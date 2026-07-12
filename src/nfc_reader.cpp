#include "nfc_reader.h"

#include "config.h"
#include "pins_v3.h"

namespace {
constexpr uint8_t CMD_WRITE_REGISTER = 0x00;
constexpr uint8_t CMD_WRITE_REGISTER_OR_MASK = 0x01;
constexpr uint8_t CMD_WRITE_REGISTER_AND_MASK = 0x02;
constexpr uint8_t CMD_READ_REGISTER = 0x04;
constexpr uint8_t CMD_READ_EEPROM = 0x07;
constexpr uint8_t CMD_SEND_DATA = 0x09;
constexpr uint8_t CMD_READ_DATA = 0x0A;
constexpr uint8_t CMD_LOAD_RF_CONFIG = 0x11;
constexpr uint8_t CMD_RF_ON = 0x16;
constexpr uint8_t CMD_RF_OFF = 0x17;

constexpr uint8_t REG_SYSTEM_CONFIG = 0x00;
constexpr uint8_t REG_IRQ_STATUS = 0x02;
constexpr uint8_t REG_IRQ_CLEAR = 0x03;
constexpr uint8_t REG_RX_CRC_CONFIG = 0x12;
constexpr uint8_t REG_RX_STATUS = 0x13;
constexpr uint8_t REG_RF_STATUS = 0x0D;
constexpr uint8_t REG_TX_CONFIG = 0x19;
}

NfcReader::NfcReader() : _spi(FSPI), _spiSettings(4000000, MSBFIRST, SPI_MODE0) {}

bool NfcReader::begin() {
    Serial.println("[NFC] begin()");

    pinMode(PIN_NFC_CS, OUTPUT);
    digitalWrite(PIN_NFC_CS, HIGH);
    pinMode(PIN_NFC_RST, OUTPUT);
    digitalWrite(PIN_NFC_RST, HIGH);
    pinMode(PIN_NFC_BUSY, INPUT);

    Serial.printf("[NFC] pins: CS=%d MOSI=%d SCK=%d MISO=%d BUSY=%d RST=%d\n",
                  PIN_NFC_CS, PIN_NFC_MOSI, PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_BUSY, PIN_NFC_RST);
    Serial.printf("[NFC] BUSY pin reads: %d\n", digitalRead(PIN_NFC_BUSY));

    _spi.begin(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_CS);
    Serial.println("[NFC] SPI.begin() done");

    if (!_hardwareReset()) {
        Serial.println("[NFC] hardware reset FAILED");
        _healthy = false;
        return false;
    }
    Serial.println("[NFC] hardware reset OK");

    uint8_t version[2] = {0, 0};
    _healthy = _readEeprom(0x10, version, sizeof(version));
    Serial.printf("[NFC] EEPROM version read: %s (0x%02X 0x%02X)\n",
                  _healthy ? "OK" : "FAILED", version[0], version[1]);
    return _healthy;
}

String NfcReader::dumpRegisters() {
    uint32_t irq = 0, rfStatus = 0, rxStatus = 0;
    _readRegister(REG_IRQ_STATUS, irq);
    _readRegister(REG_RF_STATUS, rfStatus);
    _readRegister(REG_RX_STATUS, rxStatus);
    char buf[64];
    snprintf(buf, sizeof(buf), "IRQ:%08lX RF:%08lX RX:%04lX",
             (unsigned long)irq, (unsigned long)rfStatus,
             (unsigned long)(rxStatus & 0xFFFF));
    return String(buf);
}

bool NfcReader::poll(NfcTag& tag) {
    tag = NfcTag{};
    if (!_healthy) {
        snprintf(_diag, sizeof(_diag), "not healthy");
        return false;
    }

    uint32_t now = millis();
    if ((now - _lastPollMs) < NFC_POLL_INTERVAL_MS) {
        return false;
    }
    _lastPollMs = now;

    // ISO 14443A attempt
    _clearIrq();
    if (!_loadRfConfig(0x00, 0x80) || !_rfOn()) {
        snprintf(_diag, sizeof(_diag), "rfcfg/on fail");
        return false;
    }
    delay(50);

    uint8_t uid14443[10] = {};
    size_t uid14443Len = 0;
    uint8_t sak = 0;
    if (_pollIso14443A(uid14443, uid14443Len, sak)) {
        tag.type = NfcTagType::Iso14443A;
        tag.uid = _formatUid(uid14443, uid14443Len, false);
        tag.sak = sak;
        _rfOff();
        snprintf(_diag, sizeof(_diag), "14443A uid=%s", tag.uid.c_str());
        return true;
    }

    // Read IRQ after failed 14443A attempt
    uint32_t irqA = 0, rxA = 0;
    _readRegister(REG_IRQ_STATUS, irqA);
    _readRegister(REG_RX_STATUS, rxA);
    _rfOff();

    // ISO 15693 attempt
    _clearIrq();
    uint8_t uid15693[8] = {};
    if (_loadRfConfig(0x0D, 0x8D) && _rfOn()) {
        delay(10);
        if (_pollIso15693(uid15693)) {
            tag.type = NfcTagType::Iso15693;
            tag.uid = _formatUid(uid15693, sizeof(uid15693), true);
            _rfOff();
            snprintf(_diag, sizeof(_diag), "15693 uid=%s", tag.uid.c_str());
            return true;
        }
        _rfOff();
    }

    // Report IRQ from 14443A attempt (most useful diagnostic)
    snprintf(_diag, sizeof(_diag), "noTag IRQ=%lX RX=%lX",
             (unsigned long)irqA, (unsigned long)(rxA & 0xFFFF));
    return false;
}

bool NfcReader::_hardwareReset() {
    Serial.printf("[NFC] RST LOW, BUSY=%d\n", digitalRead(PIN_NFC_BUSY));
    digitalWrite(PIN_NFC_RST, LOW);
    delay(50);
    Serial.printf("[NFC] RST HIGH, BUSY=%d\n", digitalRead(PIN_NFC_BUSY));
    digitalWrite(PIN_NFC_RST, HIGH);
    delay(100);
    Serial.printf("[NFC] post-reset BUSY=%d\n", digitalRead(PIN_NFC_BUSY));
    bool ok = _waitBusyCycle(2000);
    Serial.printf("[NFC] waitBusyCycle=%d, BUSY=%d\n", ok, digitalRead(PIN_NFC_BUSY));
    return ok;
}

bool NfcReader::_waitBusyCycle(uint32_t timeoutMs) {
    uint32_t waitHighUntil = millis() + min<uint32_t>(timeoutMs, 10);
    while (digitalRead(PIN_NFC_BUSY) == LOW && millis() < waitHighUntil) {
        delayMicroseconds(10);
    }

    uint32_t waitLowUntil = millis() + timeoutMs;
    while (digitalRead(PIN_NFC_BUSY) == HIGH) {
        if (millis() > waitLowUntil) {
            return false;
        }
        delayMicroseconds(100);
    }
    return true;
}

bool NfcReader::_waitForRx(uint32_t timeoutMs) {
    // Poll IRQ for RX_IRQ (bit 0) or TIMER_IRQ (bit 17)
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        uint32_t irq = 0;
        if (!_readRegister(REG_IRQ_STATUS, irq)) {
            return false;
        }
        if (irq & 0x00000001UL) return true;  // RX_IRQ — data received
        if (irq & 0x00020000UL) return false;  // TIMER_IRQ — timeout
        delayMicroseconds(500);
    }
    return false;
}

bool NfcReader::_command(const uint8_t* data, size_t length) {
    // Wait for BUSY LOW (PN5180 ready to accept command)
    while (digitalRead(PIN_NFC_BUSY) == HIGH) {
        if (!_waitBusyCycle()) {
            return false;
        }
    }

    // CS LOW, transfer command
    digitalWrite(PIN_NFC_CS, LOW);
    delayMicroseconds(2000);
    _spi.beginTransaction(_spiSettings);
    for (size_t i = 0; i < length; ++i) {
        _spi.transfer(data[i]);
    }
    _spi.endTransaction();

    // Wait for BUSY HIGH (PN5180 acknowledges command) BEFORE releasing CS
    uint32_t busyWait = millis() + 1000;
    while (digitalRead(PIN_NFC_BUSY) == LOW) {
        if (millis() > busyWait) {
            digitalWrite(PIN_NFC_CS, HIGH);
            return false;
        }
        delayMicroseconds(10);
    }

    // NOW release CS
    digitalWrite(PIN_NFC_CS, HIGH);
    delay(1);

    // Wait for BUSY LOW (command complete)
    uint32_t doneWait = millis() + 1000;
    while (digitalRead(PIN_NFC_BUSY) == HIGH) {
        if (millis() > doneWait) {
            return false;
        }
        delayMicroseconds(100);
    }
    return true;
}

bool NfcReader::_readRaw(uint8_t* data, size_t length) {
    // Wait for BUSY LOW (ready)
    while (digitalRead(PIN_NFC_BUSY) == HIGH) {
        if (!_waitBusyCycle()) {
            return false;
        }
    }

    // CS LOW, read data
    digitalWrite(PIN_NFC_CS, LOW);
    delayMicroseconds(2000);
    _spi.beginTransaction(_spiSettings);
    for (size_t i = 0; i < length; ++i) {
        data[i] = _spi.transfer(0xFF);
    }
    _spi.endTransaction();

    // Wait for BUSY HIGH before releasing CS
    uint32_t busyWait = millis() + 1000;
    while (digitalRead(PIN_NFC_BUSY) == LOW) {
        if (millis() > busyWait) {
            digitalWrite(PIN_NFC_CS, HIGH);
            return false;
        }
        delayMicroseconds(10);
    }

    // Release CS
    digitalWrite(PIN_NFC_CS, HIGH);
    delay(1);

    // Wait for BUSY LOW (done)
    uint32_t doneWait = millis() + 1000;
    while (digitalRead(PIN_NFC_BUSY) == HIGH) {
        if (millis() > doneWait) {
            return false;
        }
        delayMicroseconds(100);
    }
    return true;
}

bool NfcReader::_writeRegister(uint8_t reg, uint32_t value) {
    uint8_t cmd[6] = {
        CMD_WRITE_REGISTER,
        reg,
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF),
    };
    return _command(cmd, sizeof(cmd));
}

bool NfcReader::_writeRegisterOr(uint8_t reg, uint32_t mask) {
    uint8_t cmd[6] = {
        CMD_WRITE_REGISTER_OR_MASK,
        reg,
        static_cast<uint8_t>(mask & 0xFF),
        static_cast<uint8_t>((mask >> 8) & 0xFF),
        static_cast<uint8_t>((mask >> 16) & 0xFF),
        static_cast<uint8_t>((mask >> 24) & 0xFF),
    };
    return _command(cmd, sizeof(cmd));
}

bool NfcReader::_writeRegisterAnd(uint8_t reg, uint32_t mask) {
    uint8_t cmd[6] = {
        CMD_WRITE_REGISTER_AND_MASK,
        reg,
        static_cast<uint8_t>(mask & 0xFF),
        static_cast<uint8_t>((mask >> 8) & 0xFF),
        static_cast<uint8_t>((mask >> 16) & 0xFF),
        static_cast<uint8_t>((mask >> 24) & 0xFF),
    };
    return _command(cmd, sizeof(cmd));
}

bool NfcReader::_readRegister(uint8_t reg, uint32_t& value) {
    uint8_t cmd[2] = {CMD_READ_REGISTER, reg};
    if (!_command(cmd, sizeof(cmd))) {
        return false;
    }
    delayMicroseconds(100);

    uint8_t data[4] = {};
    if (!_readRaw(data, sizeof(data))) {
        return false;
    }
    value = static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24);
    return true;
}

bool NfcReader::_readEeprom(uint8_t address, uint8_t* data, size_t length) {
    uint8_t cmd[3] = {CMD_READ_EEPROM, address, static_cast<uint8_t>(length)};
    if (!_command(cmd, sizeof(cmd))) {
        return false;
    }
    delayMicroseconds(100);
    return _readRaw(data, length);
}

bool NfcReader::_loadRfConfig(uint8_t txConf, uint8_t rxConf) {
    _clearIrq();
    uint8_t cmd[3] = {CMD_LOAD_RF_CONFIG, txConf, rxConf};
    return _command(cmd, sizeof(cmd));
}

bool NfcReader::_rfOn() {
    uint8_t cmd[2] = {CMD_RF_ON, 0x00};
    return _command(cmd, sizeof(cmd));
}

bool NfcReader::_rfOff() {
    uint8_t cmd[2] = {CMD_RF_OFF, 0x00};
    return _command(cmd, sizeof(cmd));
}

bool NfcReader::_setTransceiveMode() {
    uint32_t sysCfg = 0;
    if (!_readRegister(REG_SYSTEM_CONFIG, sysCfg)) {
        return false;
    }
    sysCfg = (sysCfg & 0xFFFFFFF8UL) | 0x03UL;
    return _writeRegister(REG_SYSTEM_CONFIG, sysCfg);
}

bool NfcReader::_setIdle() {
    return _writeRegisterAnd(REG_SYSTEM_CONFIG, 0xFFFFFFF8UL);
}

bool NfcReader::_activateTransceive() {
    return _writeRegisterOr(REG_SYSTEM_CONFIG, 0x00000003UL);
}

bool NfcReader::_clearIrq() {
    return _writeRegister(REG_IRQ_CLEAR, 0xFFFFFFFFUL);
}

bool NfcReader::_sendData(const uint8_t* data, size_t length, uint8_t validBits) {
    uint8_t buffer[34] = {CMD_SEND_DATA, validBits};
    if (length > (sizeof(buffer) - 2)) {
        return false;
    }
    memcpy(&buffer[2], data, length);
    return _command(buffer, length + 2);
}

bool NfcReader::_readData(uint8_t* data, size_t length) {
    uint8_t cmd[2] = {CMD_READ_DATA, 0x00};
    if (!_command(cmd, sizeof(cmd))) {
        return false;
    }
    return _readRaw(data, length);
}

bool NfcReader::_sendIso15693Eof() {
    const uint8_t maskCmd[6] = {CMD_WRITE_REGISTER_AND_MASK, REG_TX_CONFIG, 0x3F, 0xFB, 0xFF, 0xFF};
    const uint8_t eofCmd[2] = {CMD_SEND_DATA, 0x00};
    return _command(maskCmd, sizeof(maskCmd)) && _command(eofCmd, sizeof(eofCmd));
}

bool NfcReader::_requestTypeA() {
    // Disable collision detection, TX CRC, RX CRC
    _writeRegisterAnd(REG_SYSTEM_CONFIG, 0xFFFFFFBFUL);
    _writeRegisterAnd(REG_RX_CRC_CONFIG, 0xFFFFFFFEUL);
    _writeRegisterAnd(REG_TX_CONFIG, 0xFFFFFFFEUL);
    _clearIrq();

    // Set idle → transceive using AND/OR masks (matches library approach)
    _writeRegisterAnd(REG_SYSTEM_CONFIG, 0xFFFFFFF8UL);
    _writeRegisterOr(REG_SYSTEM_CONFIG, 0x00000003UL);

    // Send WUPA (0x52, 7 valid bits)
    const uint8_t wupa = 0x52;
    if (!_sendData(&wupa, 1, 0x07)) {
        return false;
    }

    // Poll IRQ for RX_IRQ (bit 0) or TIMER_IRQ (bit 17) — response takes ~6ms
    if (!_waitForRx(50)) {
        return false;
    }

    uint32_t rxStatus = 0;
    if (!_readRegister(REG_RX_STATUS, rxStatus)) {
        return false;
    }
    uint16_t rxLength = rxStatus & 0x01FFU;
    if (rxLength < 2 || rxLength > 64) {
        return false;
    }

    uint8_t atqa[2] = {};
    return _readData(atqa, sizeof(atqa)) && atqa[0] != 0x00 && atqa[0] != 0xFF;
}

bool NfcReader::_selectCascadeLevel(uint8_t selCmd, uint8_t uidPart[5], uint8_t& sak) {
    // Anti-collision: no CRC
    _writeRegisterAnd(REG_RX_CRC_CONFIG, 0xFFFFFFFEUL);
    _writeRegisterAnd(REG_TX_CONFIG, 0xFFFFFFFEUL);
    _clearIrq();
    _writeRegisterAnd(REG_SYSTEM_CONFIG, 0xFFFFFFF8UL);
    _writeRegisterOr(REG_SYSTEM_CONFIG, 0x00000003UL);

    const uint8_t anticollisionCmd[2] = {selCmd, 0x20};
    if (!_sendData(anticollisionCmd, sizeof(anticollisionCmd))) {
        return false;
    }

    if (!_waitForRx(50)) {
        return false;
    }

    uint32_t rxStatus = 0;
    if (!_readRegister(REG_RX_STATUS, rxStatus)) {
        return false;
    }
    uint16_t rxLength = rxStatus & 0x01FFU;
    if (rxLength < 5 || rxLength > 64) {
        return false;
    }
    if (!_readData(uidPart, 5)) {
        return false;
    }

    uint8_t bcc = uidPart[0] ^ uidPart[1] ^ uidPart[2] ^ uidPart[3];
    if (bcc != uidPart[4]) {
        return false;
    }

    // SELECT: enable CRC for this command
    _clearIrq();
    _writeRegisterAnd(REG_SYSTEM_CONFIG, 0xFFFFFFF8UL);
    _writeRegisterOr(REG_SYSTEM_CONFIG, 0x00000003UL);
    _writeRegisterOr(REG_TX_CONFIG, 0x00000001UL);
    _writeRegisterOr(REG_RX_CRC_CONFIG, 0x00000001UL);

    uint8_t selectCmd[7] = {selCmd, 0x70, uidPart[0], uidPart[1], uidPart[2], uidPart[3], uidPart[4]};
    if (!_sendData(selectCmd, sizeof(selectCmd))) {
        return false;
    }

    if (!_waitForRx(50)) {
        return false;
    }

    if (!_readRegister(REG_RX_STATUS, rxStatus)) {
        return false;
    }
    rxLength = rxStatus & 0x01FFU;
    if (rxLength < 1) {
        return false;
    }

    uint8_t response[3] = {};
    if (!_readData(response, min<uint16_t>(rxLength, 3))) {
        return false;
    }
    sak = response[0];
    return true;
}

bool NfcReader::_pollIso14443A(uint8_t* uid, size_t& uidLength, uint8_t& sak) {
    uidLength = 0;
    sak = 0;

    if (!_requestTypeA()) {
        return false;
    }

    const uint8_t cascadeLevels[3] = {0x93, 0x95, 0x97};
    for (uint8_t level = 0; level < 3; ++level) {
        uint8_t uidPart[5] = {};
        if (!_selectCascadeLevel(cascadeLevels[level], uidPart, sak)) {
            return false;
        }

        if (uidPart[0] == 0x88) {
            memcpy(uid + uidLength, &uidPart[1], 3);
            uidLength += 3;
        } else {
            memcpy(uid + uidLength, uidPart, 4);
            uidLength += 4;
        }

        if ((sak & 0x04U) == 0) {
            return true;
        }
    }

    return uidLength > 0;
}

bool NfcReader::_pollIso15693(uint8_t uid[8]) {
    const uint8_t inventoryCmd[3] = {0x26, 0x01, 0x00}; // 1-slot, high data rate

    _clearIrq();
    _writeRegisterAnd(REG_SYSTEM_CONFIG, 0xFFFFFFF8UL);
    _writeRegisterOr(REG_SYSTEM_CONFIG, 0x00000003UL);

    if (!_sendData(inventoryCmd, sizeof(inventoryCmd))) {
        return false;
    }

    if (!_waitForRx(50)) {
        return false;
    }

    uint32_t rxStatus = 0;
    if (!_readRegister(REG_RX_STATUS, rxStatus)) {
        return false;
    }
    uint16_t rxLength = rxStatus & 0x01FFU;
    if (rxLength >= 10 && rxLength <= 32) {
        uint8_t buffer[32] = {};
        if (!_readData(buffer, rxLength)) {
            return false;
        }
        if (buffer[0] == 0x00) {
            memcpy(uid, &buffer[2], 8);
            return true;
        }
    }

    return false;
}

String NfcReader::_formatUid(const uint8_t* uid, size_t length, bool reverseOrder) {
    String out;
    out.reserve(length * 2);
    char buf[3];

    if (reverseOrder) {
        for (size_t i = 0; i < length; ++i) {
            snprintf(buf, sizeof(buf), "%02X", uid[length - 1 - i]);
            out += buf;
        }
    } else {
        for (size_t i = 0; i < length; ++i) {
            snprintf(buf, sizeof(buf), "%02X", uid[i]);
            out += buf;
        }
    }
    return out;
}
