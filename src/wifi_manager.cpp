#include "wifi_manager.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"

namespace {
DNSServer dnsServer;
WebServer portalServer(PROVISION_HTTP_PORT);

const char PORTAL_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SpoolBuddy Setup</title>
  <style>
    body{font-family:system-ui,-apple-system,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;background:#111827;color:#f3f4f6}
    h1{font-size:1.4rem;margin-bottom:0.4rem;color:#22d3ee}
    p{color:#9ca3af}
    label{display:block;margin-top:14px;margin-bottom:4px;font-size:0.9rem}
    input{width:100%;padding:10px 12px;border-radius:8px;border:1px solid #374151;background:#1f2937;color:#f9fafb;box-sizing:border-box}
    button{width:100%;margin-top:18px;padding:12px;border:none;border-radius:8px;background:#0891b2;color:#fff;font-size:1rem}
    small{display:block;margin-top:10px;color:#6b7280}
  </style>
</head>
<body>
  <h1>SpoolBuddy Provisioning</h1>
  <p>Join your WiFi and point this station at the Bambuddy backend.</p>
  <form method="POST" action="/save">
    <label for="ssid">WiFi SSID</label>
    <input id="ssid" name="ssid" autocapitalize="off" autocorrect="off" spellcheck="false" required>
    <label for="pass">WiFi Password</label>
    <input id="pass" name="pass" type="password" autocapitalize="off" autocorrect="off">
    <label for="server">Server URL</label>
    <input id="server" name="server" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="http://bambuddy.local" required>
    <label for="apikey">API Key</label>
    <input id="apikey" name="apikey" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="bb_...">
    <button type="submit">Save &amp; Connect</button>
  </form>
  <small>BOOT button works as a touch fallback after provisioning.</small>
</body>
</html>
)html";

const char PORTAL_SAVED_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SpoolBuddy Saved</title>
  <style>
    body{font-family:system-ui,-apple-system,sans-serif;max-width:420px;margin:48px auto;padding:0 16px;background:#111827;color:#f3f4f6;text-align:center}
    h1{color:#22c55e}
    p{color:#9ca3af}
  </style>
</head>
<body>
  <h1>Configuration saved</h1>
  <p>The station is joining your WiFi now.</p>
</body>
</html>
)html";
}

bool WifiManager::begin() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    _loadPreferences();
    if (hasCredentials()) {
        return connect();
    }
    return false;
}

void WifiManager::loop() {
    if (_portalActive) {
        dnsServer.processNextRequest();
        portalServer.handleClient();
        if (_connectRequested && millis() >= _connectAfterMs) {
            _connectRequested = false;
            stopProvisioningPortal();
            connect();
        }
        return;
    }

    if (!hasCredentials()) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    uint32_t now = millis();
    if ((now - _lastConnectAttemptMs) >= WIFI_RECONNECT_INTERVAL_MS) {
        _beginStationConnect();
    }
}

bool WifiManager::connect() {
    if (!hasCredentials()) {
        return false;
    }

    _beginStationConnect();
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

void WifiManager::startProvisioningPortal() {
    if (_portalActive) {
        return;
    }

    uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
    char apName[32];
    snprintf(apName, sizeof(apName), "%s%06lX", PROVISION_AP_PREFIX, static_cast<unsigned long>(suffix));
    _apName = String(apName);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apName.c_str());
    delay(200);

    dnsServer.start(PROVISION_DNS_PORT, "*", WiFi.softAPIP());

    portalServer.on("/", HTTP_GET, [this]() { _handlePortalRoot(); });
    portalServer.on("/save", HTTP_POST, [this]() { _handlePortalSave(); });
    portalServer.on("/generate_204", HTTP_GET, [this]() { _handlePortalRedirect(); });
    portalServer.on("/fwlink", HTTP_GET, [this]() { _handlePortalRedirect(); });
    portalServer.onNotFound([this]() { _handlePortalRoot(); });
    portalServer.begin();

    _portalActive = true;
    _connectRequested = false;
}

void WifiManager::stopProvisioningPortal() {
    if (!_portalActive) {
        return;
    }

    portalServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _portalActive = false;
}

void WifiManager::clearCredentials() {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();

    _ssid = "";
    _password = "";
    _serverUrl = DEFAULT_SERVER_URL;
    stopProvisioningPortal();
    WiFi.disconnect(true, true);
}

bool WifiManager::hasCredentials() const {
    return !_ssid.isEmpty();
}

bool WifiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

int8_t WifiManager::rssi() const {
    return isConnected() ? static_cast<int8_t>(WiFi.RSSI()) : -100;
}

String WifiManager::ipAddress() const {
    return isConnected() ? WiFi.localIP().toString() : String();
}

void WifiManager::_loadPreferences() {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, true);
    _ssid = prefs.getString(PREF_WIFI_SSID, "");
    _password = prefs.getString(PREF_WIFI_PASS, "");
    _serverUrl = prefs.getString(PREF_SERVER_URL, DEFAULT_SERVER_URL);
    _apiKey = prefs.getString(PREF_API_KEY, "");
    prefs.end();

    if (_serverUrl.isEmpty()) {
        _serverUrl = DEFAULT_SERVER_URL;
    }
}

void WifiManager::_savePreferences() {
    Preferences prefs;
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    prefs.putString(PREF_WIFI_SSID, _ssid);
    prefs.putString(PREF_WIFI_PASS, _password);
    prefs.putString(PREF_SERVER_URL, _serverUrl);
    prefs.putString(PREF_API_KEY, _apiKey);
    prefs.end();
}

void WifiManager::_beginStationConnect() {
    _lastConnectAttemptMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _password.c_str());
}

void WifiManager::_handlePortalRoot() {
    portalServer.send(200, "text/html", FPSTR(PORTAL_HTML));
}

void WifiManager::_handlePortalSave() {
    String ssid = portalServer.arg("ssid");
    String pass = portalServer.arg("pass");
    String server = portalServer.arg("server");
    String apikey = portalServer.arg("apikey");
    ssid.trim();
    server.trim();
    apikey.trim();

    if (ssid.isEmpty() || server.isEmpty()) {
        portalServer.send(400, "text/plain", "SSID and server URL are required.");
        return;
    }

    _ssid = ssid;
    _password = pass;
    _serverUrl = server;
    _apiKey = apikey;
    _savePreferences();

    portalServer.send(200, "text/html", FPSTR(PORTAL_SAVED_HTML));
    _connectRequested = true;
    _connectAfterMs = millis() + 800;
}

void WifiManager::_handlePortalRedirect() {
    portalServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    portalServer.send(302, "text/plain", "");
}
