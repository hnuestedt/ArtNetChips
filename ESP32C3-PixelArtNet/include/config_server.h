#pragma once
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "config_store.h"

extern Config g_cfg;
extern ConfigStore g_store;
extern class ConfigServer g_server;
extern void applyRuntimeConfig();

// AP-Mode mit Captive-Portal + Konfig-Weboberflaeche.
// Ansatz: Wenn WLAN-Credentials gespeichert sind und connect fehlschlaegt,
// oder keine Credentials vorhanden sind, wird ein AP aufgemacht.
// Konfig-UI wird immer angeboten (AP und Station-Modus).
class ConfigServer {
public:
    static const char* AP_SSID_BASE;
    static const char* AP_PASSWORD;  // nullptr = offenes Netz, sonst min. 8 Zeichen

    void begin(bool startAP);
    void beginPortal();
    void handleClient();
    bool isAPActive();
    void stop();
    String getMDNSName() const { return mdnsName; }

private:
    void registerRoutes();
    String buildPage();
    String renderForm();
    bool parseColor(const String &value, uint32_t &out);
    bool applyConfigFromForm();

    WebServer server{80};
    DNSServer dns;
    bool apActive = false;
    String mdnsName = "pixelpixel";
    String apSsid;
};
