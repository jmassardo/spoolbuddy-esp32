#include "api_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <cmath>

#include "config.h"

namespace {

void fillCommonDeviceFields(const JsonVariantConst& node, DeviceRegistrationResponse& response) {
    response.hasCalibration = true;
    response.hasDisplaySettings = true;
    response.id = node["id"] | 0;
    response.deviceId = String(node["device_id"] | "");
    response.hostname = String(node["hostname"] | "");
    response.ipAddress = String(node["ip_address"] | "");
    response.firmwareVersion = String(node["firmware_version"] | "");
    response.deviceType = String(node["device_type"] | "spoolbuddy");
    response.hasNfc = node["has_nfc"] | false;
    response.hasScale = node["has_scale"] | false;
    response.tareOffset = node["tare_offset"] | 0L;
    response.calibrationFactor = node["calibration_factor"] | 0.0f;
    response.displayBrightness = node["display_brightness"] | 100;
    response.displayBlankTimeout = node["display_blank_timeout"] | 0U;
    response.hasBacklight = node["has_backlight"] | false;
    response.targetFirmware = String(node["target_firmware"] | "");
    response.otaStatus = String(node["ota_status"] | "");
    response.sshPublicKey = String(node["ssh_public_key"] | "");
}

void parsePendingWritePayload(const JsonVariantConst& node, PendingWritePayload& payload) {
    payload = PendingWritePayload{};
    if (!node.is<JsonObjectConst>()) {
        return;
    }

    payload.present = true;
    payload.spoolId = node["spool_id"] | 0;
    payload.ndefDataHex = String(node["ndef_data_hex"] | "");
    payload.dataOrigin = String(node["data_origin"] | "");
}

void parsePendingSystemPayload(const JsonVariantConst& node, PendingSystemPayload& payload) {
    payload = PendingSystemPayload{};
    if (!node.is<JsonObjectConst>()) {
        return;
    }

    payload.present = true;
    payload.backendUrl = String(node["backend_url"] | "");
    payload.apiKey = String(node["api_key"] | "");
}

}

void ApiClient::setServerUrl(const String& serverUrl) {
    _serverUrl = serverUrl;
    _serverUrl.trim();
    if (_serverUrl.endsWith("/")) {
        _serverUrl.remove(_serverUrl.length() - 1);
    }
}

void ApiClient::setDeviceId(const String& deviceId) {
    _deviceId = deviceId;
}

void ApiClient::setApiKey(const String& apiKey) {
    _apiKey = apiKey;
}

bool ApiClient::registerDevice(long tareOffset, float calibrationFactor, DeviceRegistrationResponse& response) {
    response = DeviceRegistrationResponse{};
    if (_serverUrl.isEmpty() || _deviceId.isEmpty() || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    JsonDocument doc;
    doc["device_id"] = _deviceId;
    doc["hostname"] = _deviceId;
    doc["ip_address"] = WiFi.localIP().toString();
    doc["firmware_version"] = APP_VERSION;
    doc["device_type"] = "spoolbuddy";
    doc["has_nfc"] = true;
    doc["has_scale"] = true;
    doc["nfc_reader_type"] = "PN5180";
    doc["nfc_connection"] = "SPI";
    doc["has_backlight"] = true;
    doc["tare_offset"] = tareOffset;
    doc["calibration_factor"] = (calibrationFactor != 0.0f) ? (1.0f / calibrationFactor) : 0.0f;
    doc["backend_url"] = _serverUrl;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl("/api/v1/spoolbuddy/devices/register"), payload, responseBody, statusCode)) {
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return false;
    }

    response.ok = true;
    if (responseBody.isEmpty()) {
        return true;
    }

    JsonDocument resp;
    if (deserializeJson(resp, responseBody) != DeserializationError::Ok) {
        return true;
    }

    fillCommonDeviceFields(resp.as<JsonVariantConst>(), response);
    return true;
}

bool ApiClient::postHeartbeat(bool nfcOk, bool scaleOk, uint32_t uptimeSeconds, HeartbeatResponse& response) {
    response = HeartbeatResponse{};
    if (_serverUrl.isEmpty() || _deviceId.isEmpty()) {
        return false;
    }

    JsonDocument doc;
    doc["nfc_ok"] = nfcOk;
    doc["scale_ok"] = scaleOk;
    doc["uptime_s"] = uptimeSeconds;
    doc["firmware_version"] = APP_VERSION;
    doc["ip_address"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
    doc["device_type"] = "spoolbuddy";
    doc["nfc_reader_type"] = "PN5180";
    doc["nfc_connection"] = "SPI";
    doc["backend_url"] = _serverUrl;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl(String("/api/v1/spoolbuddy/devices/") + _deviceId + "/heartbeat"), payload, responseBody, statusCode)) {
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return false;
    }

    response.ok = true;
    if (responseBody.isEmpty()) {
        return true;
    }

    JsonDocument resp;
    if (deserializeJson(resp, responseBody) != DeserializationError::Ok) {
        return true;
    }

    response.pendingCommand = String(resp["pending_command"] | "");
    parsePendingWritePayload(resp["pending_write_payload"].as<JsonVariantConst>(), response.pendingWritePayload);
    parsePendingSystemPayload(resp["pending_system_payload"].as<JsonVariantConst>(), response.pendingSystemPayload);
    response.hasCalibration = true;
    response.tareOffset = resp["tare_offset"] | 0L;
    response.calibrationFactor = resp["calibration_factor"] | 0.0f;
    response.hasDisplaySettings = true;
    response.displayBrightness = resp["display_brightness"] | 100;
    response.displayBlankTimeout = resp["display_blank_timeout"] | 0U;
    response.sshPublicKey = String(resp["ssh_public_key"] | "");
    if (resp["config_update"].is<JsonObjectConst>() || resp["config_update"].is<JsonArrayConst>()) {
        serializeJson(resp["config_update"], response.configUpdateJson);
    }
    response.otaVersion = String(resp["ota_version"] | "");
    // Construct OTA URL from version (avoids linker optimization stripping the key)
    if (response.otaVersion.length() > 0 && response.otaVersion != APP_VERSION) {
        response.otaUrl = _buildUrl(String("/firmware/spoolbuddy/") + response.otaVersion + ".bin");
    }
    return true;
}

ApiClient::LookupResult ApiClient::reportTagScanned(const String& tagUid, const String& trayUuid, uint8_t sak, const String& tagType, int& spoolId) {
    spoolId = 0;
    if (_serverUrl.isEmpty() || _deviceId.isEmpty() || tagUid.isEmpty()) {
        return LookupResult::Error;
    }

    JsonDocument doc;
    doc["device_id"] = _deviceId;
    doc["tag_uid"] = tagUid;
    if (trayUuid.isEmpty()) {
        doc["tray_uuid"] = nullptr;
    } else {
        doc["tray_uuid"] = trayUuid;
    }
    doc["sak"] = sak;
    doc["tag_type"] = tagType;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl("/api/v1/spoolbuddy/nfc/tag-scanned"), payload, responseBody, statusCode)) {
        return LookupResult::Error;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return LookupResult::Error;
    }

    JsonDocument resp;
    if (deserializeJson(resp, responseBody) != DeserializationError::Ok) {
        return LookupResult::Error;
    }

    if (!(resp["matched"] | false)) {
        return LookupResult::NotFound;
    }

    spoolId = resp["spool_id"] | 0;
    return spoolId > 0 ? LookupResult::Success : LookupResult::Error;
}

bool ApiClient::getSpoolById(int spoolId, SpoolRecord& record) {
    if (_serverUrl.isEmpty() || spoolId <= 0) {
        return false;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpGet(_buildUrl(String("/api/v1/inventory/spools/") + spoolId), responseBody, statusCode)) {
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, responseBody) != DeserializationError::Ok) {
        return false;
    }

    _fillSpoolRecord(doc.as<JsonVariantConst>(), String(), record);
    return true;
}

bool ApiClient::deleteSpool(int spoolId) {
    if (_serverUrl.isEmpty() || spoolId <= 0) {
        return false;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpDelete(_buildUrl(String("/api/v1/inventory/spools/") + spoolId), responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

int ApiClient::getPrinters(PrinterInfo* printers, int maxCount) {
    if (_serverUrl.isEmpty() || printers == nullptr || maxCount <= 0) {
        return 0;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpGet(_buildUrl("/api/v1/printers/"), responseBody, statusCode)) {
        return 0;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return 0;
    }

    JsonDocument doc;
    if (deserializeJson(doc, responseBody) != DeserializationError::Ok || !doc.is<JsonArrayConst>()) {
        return 0;
    }

    int count = 0;
    for (JsonVariantConst printerNode : doc.as<JsonArrayConst>()) {
        if (count >= maxCount) {
            break;
        }
        printers[count].id = printerNode["id"] | 0;
        printers[count].name = String(printerNode["name"] | "");
        if (printers[count].id <= 0 || printers[count].name.isEmpty()) {
            continue;
        }
        ++count;
    }
    return count;
}

int ApiClient::getPrintersNeedingClear(PrinterInfo* printers, int maxCount) {
    if (_serverUrl.isEmpty() || printers == nullptr || maxCount <= 0) {
        return 0;
    }

    // First get all printers
    PrinterInfo allPrinters[16];
    int totalPrinters = getPrinters(allPrinters, 16);
    if (totalPrinters <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < totalPrinters && count < maxCount; ++i) {
        String responseBody;
        int statusCode = 0;
        if (!_httpGet(_buildUrl(String("/api/v1/printers/") + allPrinters[i].id + "/status"),
                      responseBody, statusCode)) {
            continue;
        }
        if (statusCode < 200 || statusCode >= 300) {
            continue;
        }

        JsonDocument doc;
        if (deserializeJson(doc, responseBody) != DeserializationError::Ok) {
            continue;
        }

        const char* state = doc["state"] | "";
        if (strcmp(state, "FINISH") == 0 || strcmp(state, "FAILED") == 0) {
            printers[count] = allPrinters[i];
            ++count;
        }
    }

    return count;
}

bool ApiClient::clearPlate(int printerId) {
    if (_serverUrl.isEmpty() || printerId <= 0) {
        return false;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl(String("/api/v1/printers/") + printerId + "/clear-plate"),
                   "{}", responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

int ApiClient::getSlots(int printerId, SlotInfo* slots, int maxCount) {
    if (_serverUrl.isEmpty() || slots == nullptr || maxCount <= 0 || printerId <= 0) {
        return 0;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpGet(_buildUrl(String("/api/v1/printers/") + printerId + "/status"), responseBody, statusCode)) {
        return 0;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return 0;
    }

    JsonDocument doc;
    if (deserializeJson(doc, responseBody) != DeserializationError::Ok) {
        return 0;
    }

    int count = 0;
    JsonArrayConst amsUnits = doc["ams"].as<JsonArrayConst>();
    if (!amsUnits.isNull()) {
        for (JsonVariantConst unit : amsUnits) {
            int amsId = unit["id"] | -1;
            if (amsId < 0) continue;
            // AMS unit letter: 0→A, 1→B, etc.
            char unitLetter = 'A' + constrain(amsId, 0, 25);
            JsonArrayConst trays = unit["tray"].as<JsonArrayConst>();
            if (trays.isNull()) continue;
            for (JsonVariantConst tray : trays) {
                if (count >= maxCount - 1) break;  // reserve last slot for external
                int trayId = tray["id"] | -1;
                if (trayId < 0) continue;
                slots[count].amsId = amsId;
                slots[count].trayId = trayId;
                snprintf(slots[count].label, sizeof(slots[count].label), "%c%d", unitLetter, trayId + 1);
                ++count;
            }
        }
    }

    // Always append the external spool slot
    if (count < maxCount) {
        slots[count].amsId = 255;
        slots[count].trayId = 0;
        strncpy(slots[count].label, "External", sizeof(slots[count].label) - 1);
        slots[count].label[sizeof(slots[count].label) - 1] = '\0';
        ++count;
    }

    return count;
}

bool ApiClient::assignSpool(int spoolId, int printerId, int amsId, int trayId) {
    if (_serverUrl.isEmpty() || spoolId <= 0 || printerId <= 0) {
        return false;
    }

    JsonDocument doc;
    doc["spool_id"] = spoolId;
    doc["printer_id"] = printerId;
    doc["ams_id"] = amsId;
    doc["tray_id"] = trayId;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl("/api/v1/inventory/assignments"), payload, responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

bool ApiClient::unassignSpool(int printerId, int amsId, int trayId) {
    if (_serverUrl.isEmpty() || printerId <= 0) {
        return false;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpDelete(_buildUrl(String("/api/v1/inventory/assignments/") + printerId + "/" + amsId + "/" + trayId),
                     responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

bool ApiClient::getSpoolAssignment(int spoolId, SpoolAssignment& assignment) {
    assignment = SpoolAssignment{};
    if (_serverUrl.isEmpty() || spoolId <= 0) {
        return false;
    }

    String responseBody;
    int statusCode = 0;
    if (!_httpGet(_buildUrl("/api/v1/inventory/assignments"), responseBody, statusCode)) {
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, responseBody) != DeserializationError::Ok || !doc.is<JsonArrayConst>()) {
        return false;
    }

    for (JsonVariantConst assignmentNode : doc.as<JsonArrayConst>()) {
        if ((assignmentNode["spool_id"] | 0) != spoolId) {
            continue;
        }
        assignment.printerId = assignmentNode["printer_id"] | 0;
        assignment.amsId = assignmentNode["ams_id"] | 0;
        assignment.trayId = assignmentNode["tray_id"] | 0;
        assignment.printerName = String(assignmentNode["printer_name"] | "");
        return assignment.printerId > 0;
    }

    return false;
}

bool ApiClient::postSpoolWeight(int spoolId, float weightGrams, bool stable) {
    if (_serverUrl.isEmpty() || _deviceId.isEmpty() || spoolId <= 0) {
        return false;
    }

    JsonDocument doc;
    doc["device_id"] = _deviceId;
    doc["spool_id"] = spoolId;
    doc["weight_grams"] = static_cast<int>(roundf(weightGrams));
    doc["stable"] = stable;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl("/api/v1/spoolbuddy/scale/update-spool-weight"), payload, responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

bool ApiClient::postTareOffset(long tareOffset) {
    if (_serverUrl.isEmpty() || _deviceId.isEmpty()) {
        return false;
    }

    JsonDocument doc;
    doc["tare_offset"] = tareOffset;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl(String("/api/v1/spoolbuddy/devices/") + _deviceId + "/calibration/set-tare"), payload, responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

bool ApiClient::postCalibrationFactor(float knownWeightGrams, long rawAdc, long tareRawAdc) {
    if (_serverUrl.isEmpty() || _deviceId.isEmpty()) {
        return false;
    }

    JsonDocument doc;
    doc["known_weight_grams"] = knownWeightGrams;
    doc["raw_adc"] = rawAdc;
    doc["tare_raw_adc"] = tareRawAdc;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl(String("/api/v1/spoolbuddy/devices/") + _deviceId + "/calibration/set-factor"), payload, responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

bool ApiClient::registerSpool(const String& tagUid, const char* material, const char* brand, const char* colorName, const char* rgba, int labelWeight, int coreWeight) {
    if (_serverUrl.isEmpty() || tagUid.isEmpty() || material == nullptr || brand == nullptr || colorName == nullptr || rgba == nullptr) {
        return false;
    }

    JsonDocument doc;
    doc["material"] = material;
    doc["brand"] = brand;
    doc["color_name"] = colorName;
    doc["rgba"] = rgba;
    doc["tag_uid"] = tagUid;
    doc["label_weight"] = labelWeight;
    doc["core_weight"] = coreWeight;

    String payload;
    serializeJson(doc, payload);

    String responseBody;
    int statusCode = 0;
    if (!_httpPost(_buildUrl("/api/v1/inventory/spools"), payload, responseBody, statusCode)) {
        return false;
    }
    return statusCode >= 200 && statusCode < 300;
}

bool ApiClient::performOta(const String& otaUrl, const String& targetVersion, OtaProgressCallback callback) {
    String url = _resolveUrl(otaUrl);
    if (url.isEmpty()) {
        return false;
    }

    NetworkClient plainClient;
    NetworkClientSecure secureClient;
    HTTPClient http;
    if (!_beginRequest(http, plainClient, secureClient, url)) {
        return false;
    }

    http.setTimeout(OTA_HTTP_TIMEOUT_MS);
    http.addHeader("X-Device-Id", _deviceId);

    int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int32_t contentLength = http.getSize();
    if (!Update.begin(contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN)) {
        http.end();
        return false;
    }

    NetworkClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t writtenTotal = 0;
    int32_t remaining = contentLength;
    int lastPercent = -1;

    while (http.connected() && (remaining > 0 || remaining == -1)) {
        size_t availableBytes = stream->available();
        if (availableBytes == 0) {
            delay(1);
            continue;
        }

        size_t chunkSize = min(availableBytes, sizeof(buffer));
        int bytesRead = stream->readBytes(buffer, chunkSize);
        if (bytesRead <= 0) {
            delay(1);
            continue;
        }

        size_t written = Update.write(buffer, static_cast<size_t>(bytesRead));
        if (written != static_cast<size_t>(bytesRead)) {
            Update.abort();
            http.end();
            return false;
        }

        writtenTotal += written;
        if (remaining > 0) {
            remaining -= written;
            int percent = static_cast<int>((writtenTotal * 100U) / static_cast<size_t>(contentLength));
            if (callback && percent != lastPercent) {
                callback(percent, targetVersion.c_str());
                lastPercent = percent;
            }
        }
        delay(1);
    }

    http.end();

    if (!Update.end(true)) {
        Update.abort();
        return false;
    }

    if (callback) {
        callback(100, targetVersion.c_str());
    }
    delay(250);
    ESP.restart();
    return true;
}

String ApiClient::_buildUrl(const String& path) const {
    if (_serverUrl.isEmpty()) {
        return String();
    }
    if (path.startsWith("/")) {
        return _serverUrl + path;
    }
    return _serverUrl + "/" + path;
}

String ApiClient::_resolveUrl(const String& urlOrPath) const {
    if (urlOrPath.startsWith("http://") || urlOrPath.startsWith("https://")) {
        return urlOrPath;
    }
    return _buildUrl(urlOrPath);
}

bool ApiClient::_beginRequest(HTTPClient& http, NetworkClient& plainClient, NetworkClientSecure& secureClient, const String& url) const {
    if (url.startsWith("https://")) {
        secureClient.setInsecure();
        return http.begin(secureClient, url);
    }
    return http.begin(plainClient, url);
}

bool ApiClient::_httpGet(const String& url, String& responseBody, int& statusCode) const {
    NetworkClient plainClient;
    NetworkClientSecure secureClient;
    HTTPClient http;
    statusCode = -1;

    if (!_beginRequest(http, plainClient, secureClient, url)) {
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!_apiKey.isEmpty()) {
        http.addHeader("X-API-Key", _apiKey);
    }
    statusCode = http.GET();
    if (statusCode > 0) {
        responseBody = http.getString();
    }
    http.end();
    return statusCode > 0;
}

bool ApiClient::_httpPost(const String& url, const String& payload, String& responseBody, int& statusCode) const {
    NetworkClient plainClient;
    NetworkClientSecure secureClient;
    HTTPClient http;
    statusCode = -1;

    if (!_beginRequest(http, plainClient, secureClient, url)) {
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    if (!_apiKey.isEmpty()) {
        http.addHeader("X-API-Key", _apiKey);
    }
    statusCode = http.POST(payload);
    if (statusCode > 0) {
        responseBody = http.getString();
    }
    http.end();
    return statusCode > 0;
}

bool ApiClient::_httpDelete(const String& url, String& responseBody, int& statusCode) const {
    NetworkClient plainClient;
    NetworkClientSecure secureClient;
    HTTPClient http;
    statusCode = -1;

    if (!_beginRequest(http, plainClient, secureClient, url)) {
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!_apiKey.isEmpty()) {
        http.addHeader("X-API-Key", _apiKey);
    }
    statusCode = http.sendRequest("DELETE");
    if (statusCode > 0) {
        responseBody = http.getString();
    }
    http.end();
    return statusCode > 0;
}

uint32_t ApiClient::_parseColorHex(const JsonVariantConst& value) {
    if (value.is<uint32_t>()) {
        return value.as<uint32_t>();
    }
    if (value.is<const char*>()) {
        String text = String(value.as<const char*>());
        text.trim();
        if (text.startsWith("#")) {
            text.remove(0, 1);
        }
        if (text.length() == 8) {
            text.remove(6);
        }
        return static_cast<uint32_t>(strtoul(text.c_str(), nullptr, 16));
    }
    return 0;
}

void ApiClient::_fillSpoolRecord(const JsonVariantConst& node, const String& uid, SpoolRecord& record) {
    record = SpoolRecord{};
    record.uid = uid;
    if (record.uid.isEmpty() && node["tag_uid"].is<const char*>()) {
        record.uid = String(node["tag_uid"].as<const char*>());
    }

    if (node["id"].is<int>()) {
        record.spoolId = node["id"].as<int>();
    } else if (node["spool_id"].is<int>()) {
        record.spoolId = node["spool_id"].as<int>();
    }

    if (node["material"].is<const char*>()) {
        record.material = String(node["material"].as<const char*>());
    } else if (node["filament_material"].is<const char*>()) {
        record.material = String(node["filament_material"].as<const char*>());
    }

    if (node["color_name"].is<const char*>()) {
        record.colorName = String(node["color_name"].as<const char*>());
    } else if (node["colorName"].is<const char*>()) {
        record.colorName = String(node["colorName"].as<const char*>());
    }

    if (node["brand"].is<const char*>()) {
        record.brand = String(node["brand"].as<const char*>());
    } else if (node["manufacturer"].is<const char*>()) {
        record.brand = String(node["manufacturer"].as<const char*>());
    }

    if (node["rgba"].is<const char*>() || node["rgba"].is<uint32_t>() || node["rgba"].is<int>()) {
        record.colorHex = _parseColorHex(node["rgba"].as<JsonVariantConst>());
    } else if (node["color_hex"].is<const char*>() || node["color_hex"].is<uint32_t>() || node["color_hex"].is<int>()) {
        record.colorHex = _parseColorHex(node["color_hex"].as<JsonVariantConst>());
    } else if (node["colorHex"].is<const char*>() || node["colorHex"].is<uint32_t>() || node["colorHex"].is<int>()) {
        record.colorHex = _parseColorHex(node["colorHex"].as<JsonVariantConst>());
    } else if (node["color"].is<JsonObjectConst>()) {
        JsonObjectConst color = node["color"].as<JsonObjectConst>();
        if (record.colorName.isEmpty() && color["name"].is<const char*>()) {
            record.colorName = String(color["name"].as<const char*>());
        }
        if (record.colorHex == 0 && (color["hex"].is<const char*>() || color["hex"].is<uint32_t>() || color["hex"].is<int>())) {
            record.colorHex = _parseColorHex(color["hex"].as<JsonVariantConst>());
        }
    }

    if (node["weight_used"].is<float>() || node["weight_used"].is<int>()) {
        record.weightUsed = node["weight_used"].as<float>();
    } else if (node["weightUsed"].is<float>() || node["weightUsed"].is<int>()) {
        record.weightUsed = node["weightUsed"].as<float>();
    }

    if (node["label_weight"].is<float>() || node["label_weight"].is<int>()) {
        record.labelWeight = node["label_weight"].as<float>();
    } else if (node["labelWeight"].is<float>() || node["labelWeight"].is<int>()) {
        record.labelWeight = node["labelWeight"].as<float>();
    } else if (node["nominal_weight_grams"].is<float>() || node["nominal_weight_grams"].is<int>()) {
        record.labelWeight = node["nominal_weight_grams"].as<float>();
    }

    if (record.material.isEmpty()) {
        record.material = "Filament";
    }
    if (record.colorName.isEmpty()) {
        record.colorName = "Unknown color";
    }
    if (record.brand.isEmpty()) {
        record.brand = "Unbranded";
    }
}
