#include <Arduino.h>
#include <esp_wifi.h>
#include "config_server.h"

const char* ConfigServer::AP_SSID_BASE = "PixelSetup-";
const char* ConfigServer::AP_PASSWORD = nullptr;

// Einfaches, kompaktes Inline-UI (kein SPA-Framework, keine externen Assets).
static const char PAGE_HEADER[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pixel-Konfiguration</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:480px}
h1{font-size:1.3em} fieldset{border:1px solid #444;border-radius:8px;margin-bottom:14px}
legend{padding:0 6px} label{display:block;margin:8px 0 2px;font-size:.9em}
input[type=number],input[type=text],input[type=password],select{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #555;background:#222;color:#eee}
input[type=color]{width:100%;height:40px;border:1px solid #555;border-radius:6px;background:#222}
button{margin-top:14px;width:100%;padding:12px;font-size:1em;border:0;border-radius:8px;background:#4a7;padding:12px;color:#fff;cursor:pointer}
small{color:#999} .seg{display:flex;gap:8px;align-items:center;margin:4px 0} .seg input{width:60px;height:36px;padding:2px}
.hide{display:none} #saved{color:#7d7;display:none}
</style></head><body><h1>LED-Pixel-Konfiguration</h1>)html";

static const char PAGE_FOOTER[] PROGMEM = R"html(<button onclick="save()">Speichern</button>
<div id="saved">Gespeichert.</div>
<script>
function save(){
  const f=document.getElementById('cfg');
  fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(f))})
   .then(r=>{document.getElementById('saved').style.display='block';setTimeout(()=>location.reload(),600);});
}
function modeChange(){
  const m=document.getElementById('mode').value;
  ['fs_artnet','fs_static','fs_auto'].forEach(id=>document.getElementById(id).classList.add('hide'));
  if(m=='0')document.getElementById('fs_artnet').classList.remove('hide');
  if(m=='1')document.getElementById('fs_static').classList.remove('hide');
  if(m=='2')document.getElementById('fs_auto').classList.remove('hide');
}
function segChange(){
  const n=+document.getElementById('segs').value;
  for(let i=1;i<=5;i++)document.getElementById('segrow'+i).style.display=(i<=n)?'flex':'none';
}
document.addEventListener('DOMContentLoaded',()=>{modeChange();segChange();});
</script></body></html>)html";

void ConfigServer::beginPortal() { begin(true); }

void ConfigServer::begin(bool startAP) {
    if (startAP) {
        apSsid = String(AP_SSID_BASE) + String((uint32_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
        delay(100);
        dns.start(53, "*", WiFi.softAPIP());
        apActive = true;
        Serial.printf("AP aktiv: %s  IP: %s\n", apSsid.c_str(),
                      WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.mode(WIFI_STA);
        apActive = false;
    }
    if (!MDNS.begin(mdnsName.c_str())) Serial.println("mDNS Fehler");
    registerRoutes();
    server.begin();
}

void ConfigServer::stop() {
    server.stop();
    if (apActive) {
        dns.stop();
        WiFi.softAPdisconnect(true);
        apActive = false;
    }
}

bool ConfigServer::isAPActive() { return apActive; }
void ConfigServer::handleClient() {
    if (apActive) dns.processNextRequest();
    server.handleClient();
}

void ConfigServer::registerRoutes() {
    server.on("/", HTTP_GET, [this]() {
        server.send_P(200, "text/html", PAGE_HEADER);
        server.sendContent(renderForm());
        server.sendContent_P(PAGE_FOOTER);
    });
    // Captive-Portal-Umleitungen
    server.on("/generate_204", HTTP_GET, [this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.on("/fwlink", HTTP_GET, [this]() { server.sendHeader("Location", "/", true); server.send(302); });
    server.on("/hotspot-detect.html", HTTP_GET, [this]() { server.sendHeader("Location", "/", true); server.send(302); });

    server.on("/save", HTTP_POST, [this]() {
        if (applyConfigFromForm()) {
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "BAD REQUEST");
        }
    });

    server.on("/reset", HTTP_POST, [this]() {
        g_store.factoryReset();
        server.send(200, "text/plain", "OK, reboot");
        delay(500);
        ESP.restart();
    });

    server.onNotFound([this]() {
        if (apActive) { server.sendHeader("Location", "/", true); server.send(302); }
        else server.send(404, "text/plain", "Not Found");
    });
}

String ConfigServer::renderForm() {
    wifi_config_t conf;
    String savedSsid;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        savedSsid = String(reinterpret_cast<const char*>(conf.sta.ssid));
    }

    String s;
    s.reserve(2048);
    s += "<form id='cfg'><fieldset><legend>WLAN</legend>";
    s += "<label for='ssid'>WLAN-Netzwerk (SSID)</label>";
    s += "<input name='ssid' type='text' value='" + savedSsid + "' placeholder='SSID'>";
    s += "<label for='pass'>Passwort</label><input name='pass' type='password' placeholder='unveraendert lassen'>";
    s += "<small>Aktuell verbunden: " + (savedSsid.isEmpty() ? String("kein") : savedSsid) + "</small>";
    s += "</fieldset>";

    s += "<fieldset><legend>Pixel-Streifen</legend>";
    s += "<label for='numLeds'>Anzahl Pixel (1.." + String(MAX_NUM_LEDS) + ")</label>";
    s += "<input name='numLeds' id='numLeds' type='number' min='1' max='" + String(MAX_NUM_LEDS) + "' value='" + String(g_cfg.numLeds) + "'>";
    s += "<label for='bright'>Helligkeit (0-255)</label>";
    s += "<input name='bright' id='bright' type='number' min='0' max='255' value='" + String(g_cfg.brightness) + "'>";
    s += "</fieldset>";

    s += "<fieldset><legend>Modus</legend>";
    s += "<label for='mode'>Betriebsmodus</label><select name='mode' id='mode' onchange='modeChange()'>";
    const char *modes[] = {"ArtNet", "Statisch", "Automatik"};
    for (int i = 0; i <= 2; i++) {
        s += "<option value='" + String(i) + "'" + (g_cfg.mode == i ? " selected" : "") + ">" + modes[i] + "</option>";
    }
    s += "</select></fieldset>";

    // ArtNet
    s += "<fieldset id='fs_artnet' class='hide'><legend>ArtNet</legend>";
    s += "<label for='univ'>Start-Universum (0-basiert, Net/SubNet=0)</label>";
    s += "<input name='univ' id='univ' type='number' min='0' max='32767' value='" + String(g_cfg.artnetUniverse) + "'>";
    s += "<label for='addr'>Start-Adresse (1-512)</label>";
    s += "<input name='addr' id='addr' type='number' min='1' max='512' value='" + String(g_cfg.artnetAddress) + "'>";
    s += "</fieldset>";

    // Statisch
    s += "<fieldset id='fs_static' class='hide'><legend>Statisch</legend>";
    s += "<label for='segs'>Anzahl Abschnitte (1-5)</label>";
    s += "<input name='segs' id='segs' type='number' min='1' max='5' value='" + String(g_cfg.staticSegments) + "' onchange='segChange()'>";
    for (int i = 0; i < 5; i++) {
        uint32_t c = g_cfg.staticColors[i];
        char col[10];
        snprintf(col, sizeof(col), "#%06X", c & 0xFFFFFF);
        s += "<div class='seg' id='segrow" + String(i + 1) + "'><small>Abschnitt " + String(i + 1) + "</small>";
        s += "<input type='color' name='col" + String(i) + "' value='" + String(col) + "'></div>";
    }
    s += "</fieldset>";

    // Automatik
    s += "<fieldset id='fs_auto' class='hide'><legend>Automatik</legend>";
    s += "<label for='pattern'>Lauflicht</label><select name='pattern' id='pattern'>";
    const char *patterns[] = {"Regenbogen", "Läufer", "Atmen", "Funkeln", "Verfolger", "Stroboskop"};
    for (int i = 0; i <= 5; i++) {
        s += "<option value='" + String(i) + "'" + (g_cfg.autoPattern == i ? " selected" : "") + ">" + patterns[i] + "</option>";
    }
    s += "</select>";
    s += "<label for='speed'>Geschwindigkeit (1-10)</label>";
    s += "<input name='speed' id='speed' type='number' min='1' max='10' value='" + String(g_cfg.autoSpeed) + "'>";
    s += "</fieldset>";
    s += "</form>";
    s += "<button style='background:#a44' onclick=\"if(confirm('Werkseinstellungen laden und Neustart?'))fetch('/reset',{method:'POST'}).then(()=>alert('Gestartet'))\">Werkseinstellungen</button>";
    return s;
}

bool ConfigServer::parseColor(const String &value, uint32_t &out) {
    if (value.length() != 7 || value[0] != '#') return false;
    out = strtoul(value.c_str() + 1, nullptr, 16) & 0xFFFFFF;
    return true;
}

bool ConfigServer::applyConfigFromForm() {
    Config c = g_cfg;
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    c.numLeds = constrain((uint16_t)server.arg("numLeds").toInt(), (uint16_t)1, (uint16_t)MAX_NUM_LEDS);
    c.brightness = constrain((uint16_t)server.arg("bright").toInt(), (uint16_t)0, (uint16_t)255);
    c.mode = constrain((uint16_t)server.arg("mode").toInt(), (uint16_t)MODE_ARTNET, (uint16_t)MODE_AUTO);
    c.artnetUniverse = constrain((uint16_t)server.arg("univ").toInt(), (uint16_t)0, (uint16_t)32767);
    c.artnetAddress = constrain((uint16_t)server.arg("addr").toInt(), (uint16_t)1, (uint16_t)512);
    c.staticSegments = constrain((uint16_t)server.arg("segs").toInt(), (uint16_t)1, (uint16_t)5);
    for (int i = 0; i < 5; i++) {
        uint32_t col;
        if (parseColor(server.arg("col" + String(i)), col)) c.staticColors[i] = col;
    }
    c.autoPattern = constrain((uint16_t)server.arg("pattern").toInt(), (uint16_t)AUTO_RAINBOW, (uint16_t)AUTO_STROBE);
    c.autoSpeed = constrain((uint16_t)server.arg("speed").toInt(), (uint16_t)1, (uint16_t)10);

    g_cfg = c;
    g_store.save(g_cfg);
    applyRuntimeConfig();

    if (!ssid.isEmpty() && ssid.length() < 33) {
        WiFi.begin(ssid.c_str(), pass.isEmpty() ? nullptr : pass.c_str());
    }
    return true;
}
