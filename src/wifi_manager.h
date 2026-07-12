#pragma once

#include <Arduino.h>

class WifiManager {
public:
    bool begin();
    void loop();

    bool connect();
    void startProvisioningPortal();
    void stopProvisioningPortal();
    void clearCredentials();

    bool hasCredentials() const;
    bool isConnected() const;
    bool isProvisioning() const { return _portalActive; }
    int8_t rssi() const;
    String ipAddress() const;
    String ssid() const { return _ssid; }
    String password() const { return _password; }
    String serverUrl() const { return _serverUrl; }
    String apiKey() const { return _apiKey; }
    String accessPointName() const { return _apName; }

private:
    String _ssid;
    String _password;
    String _serverUrl;
    String _apiKey;
    String _apName;
    bool _portalActive = false;
    bool _connectRequested = false;
    uint32_t _connectAfterMs = 0;
    uint32_t _lastConnectAttemptMs = 0;

    void _loadPreferences();
    void _savePreferences();
    void _beginStationConnect();
    void _handlePortalRoot();
    void _handlePortalSave();
    void _handlePortalRedirect();
};
