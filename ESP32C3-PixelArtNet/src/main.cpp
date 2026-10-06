#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <esp_system.h>
#include <ArtnetWiFi.h>
#include "config.h"
#include "config_store.h"
#include "config_server.h"

// ------------------------- Globale Zustaende -------------------------
Config g_cfg;
ConfigStore g_store;
ConfigServer g_server;

CRGB *leds = nullptr;
uint16_t currentLeds = 0;

// ArtNet: pro belegtem Universum ein DMX-Puffer
#define MAX_UNIVERSES 8
static uint8_t dmxBuffer[MAX_UNIVERSES][512];
static uint16_t subscribedUniverses[MAX_UNIVERSES];
static uint8_t numSubscribed = 0;
static bool artnetActive = false;
static uint32_t lastArtnetData = 0;
ArtnetWiFiReceiver artnet;

static uint16_t clampU16(long v, long lo, long hi) {
    return (uint16_t)constrain(v, lo, hi);
}

// ------------------------- ArtNet-Verwaltung -------------------------
void artnetSubscribe(uint16_t universe) {
    for (uint8_t i = 0; i < numSubscribed; i++) {
        if (subscribedUniverses[i] == universe) return;
    }
    if (numSubscribed >= MAX_UNIVERSES) return;
    uint8_t idx = numSubscribed;
    subscribedUniverses[idx] = universe;
    numSubscribed++;
    artnet.subscribeArtDmxUniverse(universe, [idx](const uint8_t *data, uint16_t size, const ArtDmxMetadata &metadata, const ArtNetRemoteInfo &remote) {
        (void)metadata; (void)remote;
        memcpy(dmxBuffer[idx], data, size < 512 ? size : 512);
        lastArtnetData = millis();
    });
}

void artnetUnsubscribeAll() {
    artnet.unsubscribeArtDmxUniverses();
    numSubscribed = 0;
}

void setupArtnetIfNeeded() {
    if (g_cfg.mode != MODE_ARTNET || WiFi.status() != WL_CONNECTED) {
        if (artnetActive) {
            artnetUnsubscribeAll();
            artnetActive = false;
        }
        return;
    }
    if (!artnetActive) {
        artnet.begin();
        artnetActive = true;
    }
    // Universen fuer alle benoetigten Kanaele abonnieren
    artnetUnsubscribeAll();
    uint32_t channelsNeeded = (uint32_t)g_cfg.numLeds * 3;
    uint16_t firstUni = g_cfg.artnetUniverse;
    uint16_t startOff = g_cfg.artnetAddress - 1;
    uint32_t total = startOff + channelsNeeded;
    uint16_t count = (total + 511) / 512;
    if (count > MAX_UNIVERSES) count = MAX_UNIVERSES;
    for (uint16_t u = 0; u < count; u++) {
        artnetSubscribe(firstUni + u);
    }
}

// ------------------------- Laufzeit-Config anwenden -------------------------
void applyRuntimeConfig() {
    if (leds == nullptr || currentLeds != g_cfg.numLeds) {
        if (leds) {
            FastLED.clear(true);
            free((void*)leds);
        }
        leds = (CRGB*)calloc(g_cfg.numLeds, sizeof(CRGB));
        currentLeds = g_cfg.numLeds;
        FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, g_cfg.numLeds)
            .setCorrection(TypicalLEDStrip);
    }
    FastLED.setBrightness(g_cfg.brightness);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);  // 5V-Streifen, USB-limitiert
    setupArtnetIfNeeded();
}

// ------------------------- Renderer -------------------------
void renderArtnet() {
    artnet.parse();
    uint16_t n = g_cfg.numLeds;
    uint16_t startOff = g_cfg.artnetAddress - 1;
    for (uint16_t i = 0; i < n; i++) {
        uint32_t abs = (uint32_t)startOff + (uint32_t)i * 3;
        uint16_t uniIdx = abs / 512;
        uint16_t off = abs % 512;
        uint16_t uni = g_cfg.artnetUniverse + uniIdx;
        bool found = false;
        for (uint8_t b = 0; b < numSubscribed; b++) {
            if (subscribedUniverses[b] == uni) {
                if (off + 2 < 512) {
                    leds[i] = CRGB(dmxBuffer[b][off], dmxBuffer[b][off + 1], dmxBuffer[b][off + 2]);
                }
                found = true;
                break;
            }
        }
        if (!found && off + 2 < 512) leds[i] = CRGB::Black;
    }
}

void renderStatic() {
    uint16_t n = g_cfg.numLeds;
    uint8_t segs = g_cfg.staticSegments;
    uint16_t segLen = n / segs;
    uint16_t extra = n % segs;
    uint16_t idx = 0;
    for (uint8_t s = 0; s < segs; s++) {
        uint16_t len = segLen + (s < extra ? 1 : 0);
        CRGB c = CRGB(g_cfg.staticColors[s] & 0xFFFFFF);
        for (uint16_t i = 0; i < len && idx < n; i++, idx++) leds[idx] = c;
    }
    if (idx < n) {
        CRGB c = CRGB(g_cfg.staticColors[segs - 1] & 0xFFFFFF);
        while (idx < n) leds[idx++] = c;
    }
}

void autoRainbow(uint8_t speed) {
    static uint16_t hue = 0;
    for (uint16_t i = 0; i < g_cfg.numLeds; i++) {
        leds[i] = CHSV(hue + (i * 256 / g_cfg.numLeds), 255, 255);
    }
    hue += speed * 4;
}

void autoRunner(uint8_t speed) {
    static uint16_t pos = 0;
    fadeToBlackBy(leds, g_cfg.numLeds, max(1, 60 - speed * 5));
    leds[pos % g_cfg.numLeds] = CRGB::White;
    pos += speed;
}

void autoBreathe(uint8_t speed) {
    static uint16_t phase = 0;
    static int8_t dir = 1;
    fill_solid(leds, g_cfg.numLeds, CHSV(160, 255, phase));
    if (phase >= 255 - speed) dir = -1;
    if (phase <= speed) dir = 1;
    phase += dir * speed;
}

void autoTwinkle(uint8_t speed) {
    fadeToBlackBy(leds, g_cfg.numLeds, 30);
    if (random8() < 60 + speed * 10) {
        leds[random16(g_cfg.numLeds)] = CHSV(random8(), 200, 255);
    }
}

void autoChase(uint8_t speed) {
    static uint16_t pos = 0;
    fadeToBlackBy(leds, g_cfg.numLeds, 80);
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t p = (pos + i * 2) % g_cfg.numLeds;
        leds[p] = CHSV((pos * 4) & 255, 255, 255);
    }
    pos += speed;
}

void autoStrobe(uint8_t speed) {
    static bool on = false;
    static uint8_t count = 0;
    if (++count >= (11 - speed)) {
        count = 0;
        on = !on;
        fill_solid(leds, g_cfg.numLeds, on ? CRGB::White : CRGB::Black);
    }
}

void renderAuto() {
    switch (g_cfg.autoPattern) {
        case AUTO_RAINBOW: autoRainbow(g_cfg.autoSpeed); break;
        case AUTO_RUNNING: autoRunner(g_cfg.autoSpeed); break;
        case AUTO_BREATHE: autoBreathe(g_cfg.autoSpeed); break;
        case AUTO_TWINKLE: autoTwinkle(g_cfg.autoSpeed); break;
        case AUTO_CHASE:   autoChase(g_cfg.autoSpeed);   break;
        case AUTO_STROBE:  autoStrobe(g_cfg.autoSpeed);  break;
    }
}

// ------------------------- WiFi / Setup -------------------------
// STA-Credentials liegen im eigenen ConfigStore (NVS-Namespace "pixcfg"),
// nicht im WiFi-NVS: esp_wifi_get_config() liefert vor dem ersten
// mode()/begin() ESP_ERR_WIFI_NOT_INIT und der WiFi-NVS-Inhalt ist bei
// Soft-Resets nicht zuverlaessig verfuegbar.
static bool hasStoredCredentials() {
    String ssid = g_store.wifiSsid();
    if (ssid.length() == 0) {
        Serial.println("[WiFi] Keine Credentials gespeichert -> kein STA-Versuch");
        return false;
    }
    Serial.printf("[WiFi] Gespeicherte SSID: '%s'\n", ssid.c_str());
    return true;
}

// STA neu verbinden: aus dem Main-Task heraus (nicht im Event-Callback).
// Nach Trennung (z. B. ASSOC_LEAVE durch Mesh/Band-Steering) mit gecacheter
// BSSID ist der Auto-Reconnect des Cores unzuverlaessig -> aktiver Reconnect
// mit frischem Scan (bssid = nullptr) und begrenzter Fehlerzahl.
static uint32_t staReconnectTries = 0;
static const uint32_t STA_RECONNECT_MAX = 8;
static bool staReconnectPending = false;
static uint32_t staReconnectNextMs = 0;



void requestStaReconnect() {
    staReconnectPending = true;
    staReconnectNextMs = millis() + 2000;
}

void serviceStaReconnect() {
    if (!staReconnectPending) return;
    if ((int32_t)(millis() - staReconnectNextMs) < 0) return;
    if (WiFi.status() == WL_CONNECTED) {
        staReconnectPending = false;
        staReconnectTries = 0;
        return;
    }
    if (staReconnectTries >= STA_RECONNECT_MAX) {
        staReconnectPending = false;
        Serial.printf("[WiFi] %u Reconnect-Versuche -> aufgeben\n",
                      (unsigned)staReconnectTries);
        return;
    }
    staReconnectTries++;
    uint32_t backoffMs = 2000u << (staReconnectTries > 4 ? 2 : staReconnectTries - 1);
    staReconnectNextMs = millis() + backoffMs;
    Serial.printf("[WiFi] Reconnect-Versuch %u/%u (Driver-Reset)\n",
                  (unsigned)staReconnectTries, (unsigned)STA_RECONNECT_MAX);
    String ssid = g_store.wifiSsid();
    String pass = g_store.wifiPass();
    if (ssid.length() == 0) return;
    // Auto-Reconnect des Cores aus: Er haelt den Supplicant nach einem
    // gescheiterten 4-Wege-Handshake im defekten Zustand fest und triggert
    // endlos weitere fehlgeschlagene Handshakes (parallel zu eigenen
    // Versuchen). Wir steuern die Verbindung ausschliesslich von hier.
    WiFi.setAutoReconnect(false);
    // Kompletter Driver-Reset: Der Supplicant hängt nach Handshake-Fehlern
    // fest; ein einfaches begin() wird ignoriert. WIFI_OFF reisst alles
    // ab, danach frischer Start ohne gecachte BSSID.
    WiFi.mode(WIFI_OFF);
    delay(150);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

bool connectWiFi() {
    if (!hasStoredCredentials()) return false;
    String ssid = g_store.wifiSsid();
    String pass = g_store.wifiPass();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[WiFi] Verbinde mit gespeichertem WLAN (Timeout 15 s)\n");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (!ok) {
        Serial.printf("[WiFi] STA-Verbindung fehlgeschlagen (Status %d)", (int)WiFi.status());
        Serial.println(" -> WiFi.stop() fuer sauberen AP-Start");
        // Wichtig: WiFi sauber stoppen, sonst startet der AP-Mode mit
        // defektem TCP/IP-Interface (bekannter Core-Bug, arduino-esp32 #7232).
        // ABER: disconnect(false, ...) nutzen -- der zweite Parameter 'true'
        // haette die gespeicherten Credentials aus NVS geloescht!
        WiFi.persistent(true);
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);
        delay(500);
    }
    return ok;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.printf("[Boot] Reset-Ursache: %d (1=Power, 3=Software, 4=Watchdog, 9=panic)\n",
                  (int)esp_reset_reason());

    pinMode(PIN_RESET_BTN, INPUT_PULLUP);

    g_store.begin();
    g_store.load(g_cfg);

    // WiFi zuerst starten (lwIP initialisieren), erst danach ArtNet/UDP.
    // Ohne gespeicherte Credentials direkt in den AP-Modus (Core-Bug #7232 umgehen).
    bool staOk = connectWiFi();

    applyRuntimeConfig();

    if (staOk) {
        Serial.printf("WLAN verbunden: %s  IP: %s\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str());
        g_server.begin(false);
    } else {
        Serial.println("Kein WLAN verfuegbar -> AP-Modus mit Captive Portal");
        g_server.begin(true);
    }

    Serial.println("Setup fertig.");
}

// ------------------------- Hauptschleife -------------------------
void loop() {
    static uint32_t lastFrame = 0;
    uint32_t now = millis();

    // Reset-Taster: 3 s gedrueckt halten = Werkseinstellungen + Neustart
    static uint32_t btnDownSince = 0;
    if (digitalRead(PIN_RESET_BTN) == LOW) {
        if (btnDownSince == 0) btnDownSince = now;
        if (now - btnDownSince > 3000) {
            Serial.println("Reset-Taster erkannt -> Werkseinstellungen, Neustart");
            g_store.factoryReset();
            delay(200);
            ESP.restart();
        }
    } else {
        btnDownSince = 0;
    }

    g_server.handleClient();
    serviceStaReconnect();

    if (WiFi.status() == WL_CONNECTED && !artnetActive && g_cfg.mode == MODE_ARTNET) {
        setupArtnetIfNeeded();
    }

    // Frame-Rate: ca. 40 fps
    if (now - lastFrame >= 25) {
        lastFrame = now;
        switch (g_cfg.mode) {
            case MODE_ARTNET:
                if (artnetActive) renderArtnet();
                break;
            case MODE_STATIC: renderStatic(); break;
            case MODE_AUTO:   renderAuto();   break;
        }
        FastLED.show();
    }
}
