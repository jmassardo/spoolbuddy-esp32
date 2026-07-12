#pragma once

#include <Arduino.h>
#include <SPI.h>

enum class NfcTagType {
    None,
    Iso15693,
    Iso14443A,
};

struct NfcTag {
    NfcTagType type = NfcTagType::None;
    String uid;
    uint8_t sak = 0;
};

class NfcReader {
public:
    NfcReader();

    bool begin();
    bool poll(NfcTag& tag);
    bool isHealthy() const { return _healthy; }
    const char* lastDiag() const { return _diag; }
    String dumpRegisters();

    // Exposed for diagnostic testing from main
    bool _clearIrq();
    bool _loadRfConfig(uint8_t txConf, uint8_t rxConf);
    bool _rfOn();
    bool _rfOff();
    bool _writeRegisterOr(uint8_t reg, uint32_t mask);
    bool _writeRegisterAnd(uint8_t reg, uint32_t mask);
    bool _readRegister(uint8_t reg, uint32_t& value);
    bool _sendData(const uint8_t* data, size_t length, uint8_t validBits = 0x00);
    uint32_t _lastPollMs = 0;

private:
    SPIClass _spi;
    SPISettings _spiSettings;
    bool _healthy = false;
    char _diag[48] = "idle";

    bool _hardwareReset();
    bool _waitBusyCycle(uint32_t timeoutMs = 1000);
    bool _waitForRx(uint32_t timeoutMs = 50);
    bool _command(const uint8_t* data, size_t length);
    bool _readRaw(uint8_t* data, size_t length);

    bool _writeRegister(uint8_t reg, uint32_t value);
    bool _readEeprom(uint8_t address, uint8_t* data, size_t length);
    bool _setTransceiveMode();
    bool _setIdle();
    bool _activateTransceive();
    bool _readData(uint8_t* data, size_t length);
    bool _sendIso15693Eof();

    bool _requestTypeA();
    bool _selectCascadeLevel(uint8_t selCmd, uint8_t uidPart[5], uint8_t& sak);
    bool _pollIso14443A(uint8_t* uid, size_t& uidLength, uint8_t& sak);
    bool _pollIso15693(uint8_t uid[8]);

    static String _formatUid(const uint8_t* uid, size_t length, bool reverseOrder = false);
};
