#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>

struct PendingWritePayload {
    bool present = false;
    int spoolId = 0;
    String ndefDataHex;
    String dataOrigin;
};

struct PendingSystemPayload {
    bool present = false;
    String backendUrl;
    String apiKey;
};

struct HeartbeatResponse {
    bool ok = false;
    bool hasCalibration = false;
    bool hasDisplaySettings = false;
    String pendingCommand;
    PendingWritePayload pendingWritePayload;
    PendingSystemPayload pendingSystemPayload;
    long tareOffset = 0;
    float calibrationFactor = 0.0f;
    int displayBrightness = 100;
    uint32_t displayBlankTimeout = 0;
    String sshPublicKey;
    String configUpdateJson;
    String otaUrl;
    String otaVersion;
};

struct DeviceRegistrationResponse {
    bool ok = false;
    bool hasCalibration = false;
    bool hasDisplaySettings = false;
    int id = 0;
    String deviceId;
    String hostname;
    String ipAddress;
    String firmwareVersion;
    String deviceType;
    bool hasNfc = false;
    bool hasScale = false;
    long tareOffset = 0;
    float calibrationFactor = 0.0f;
    int displayBrightness = 100;
    uint32_t displayBlankTimeout = 0;
    bool hasBacklight = false;
    String targetFirmware;
    String otaStatus;
    String sshPublicKey;
};

struct SpoolRecord {
    int spoolId = 0;
    String uid;
    String material;
    String colorName;
    String brand;
    uint32_t colorHex = 0;
    float weightUsed = 0.0f;
    float labelWeight = 0.0f;
};

struct PrinterInfo {
    int id = 0;
    String name;
};

struct SpoolAssignment {
    int printerId = 0;
    int amsId = 0;
    int trayId = 0;
    String printerName;
};

using OtaProgressCallback = void (*)(int percent, const char* targetVersion);

class ApiClient {
public:
    enum class LookupResult {
        Success,
        NotFound,
        Error,
    };

    void setServerUrl(const String& serverUrl);
    void setDeviceId(const String& deviceId);
    void setApiKey(const String& apiKey);

    bool registerDevice(long tareOffset, float calibrationFactor, DeviceRegistrationResponse& response);
    bool postHeartbeat(bool nfcOk, bool scaleOk, uint32_t uptimeSeconds, HeartbeatResponse& response);
    LookupResult reportTagScanned(const String& tagUid, const String& trayUuid, uint8_t sak, const String& tagType, int& spoolId);
    bool getSpoolById(int spoolId, SpoolRecord& record);
    bool deleteSpool(int spoolId);
    int getPrinters(PrinterInfo* printers, int maxCount);
    bool assignSpool(int spoolId, int printerId, int amsId, int trayId);
    bool unassignSpool(int printerId, int amsId, int trayId);
    bool getSpoolAssignment(int spoolId, SpoolAssignment& assignment);
    bool postSpoolWeight(int spoolId, float weightGrams, bool stable = true);
    bool postTareOffset(long tareOffset);
    bool postCalibrationFactor(float knownWeightGrams, long rawAdc, long tareRawAdc);
    bool registerSpool(const String& tagUid, const char* material, const char* brand, const char* colorName, const char* rgba, int labelWeight, int coreWeight);
    bool performOta(const String& otaUrl, const String& targetVersion, OtaProgressCallback callback = nullptr);

private:
    String _serverUrl;
    String _deviceId;
    String _apiKey;

    String _buildUrl(const String& path) const;
    String _resolveUrl(const String& urlOrPath) const;
    bool _beginRequest(class HTTPClient& http, NetworkClient& plainClient, NetworkClientSecure& secureClient, const String& url) const;
    bool _httpGet(const String& url, String& responseBody, int& statusCode) const;
    bool _httpPost(const String& url, const String& payload, String& responseBody, int& statusCode) const;
    bool _httpDelete(const String& url, String& responseBody, int& statusCode) const;

    static uint32_t _parseColorHex(const JsonVariantConst& value);
    static void _fillSpoolRecord(const JsonVariantConst& node, const String& uid, SpoolRecord& record);
};
