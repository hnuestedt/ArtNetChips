# ESP32-C3 Pixel-LED-Steuerung mit ArtNet

Neues, sauber strukturiertes Projekt fuer den **ESP32-C3** (RISC-V, 4 MB Flash, 400 KB SRAM, WiFi 2.4 GHz, BLE 5.0). Im Gegensatz zu den alten Sketches (`WemosD1`, `Nano+EN28J60`) ist dieses Projekt als PlatformIO-Projekt mit modularem Aufbau umgesetzt.

Ansteuerung eines Pixel-LED-Streifens (WS2812B) mit drei Betriebsmodi und Weboberflaeche zur Konfiguration. Alle Einstellungen werden in NVS (Preferences) gespeichert und ueberstehen Reboot und Powerloss.

## Features

- **WLAN**: Verbindet sich mit dem gespeicherten WLAN; wenn keins gespeichert ist oder die Verbindung fehlschlaegt, oeffnet der ESP automatisch das **Captive Portal der WiFiManager-Bibliothek** (`PixelSetup-xxxx`) zur WLAN-Einrichtung.
- **Modi**:
  - **ArtNet**: steuert den Streifen ueber Art-Net (UDP 6454), konfigurierbares Start-Universum & Start-Adresse, automatisch ueber mehrere Universen hinweg.
  - **Statisch**: 1–5 Abschnitte mit je einer RGB-Farbe (Farbwahleditor in der Weboberflaeche).
  - **Automatik**: 6 Lauflicht-Effekte (Regenbogen, Laeufer, Atmen, Funkeln, Verfolger, Stroboskop), Geschwindigkeit 1–10.
- **Persistenz**: Alle Werte werden in NVS gespeichert.
- **Reset-Taster**: 3 s gedrueckt halten (GPIO9 gegen GND) setzt auf Werkseinstellungen zurueck und startet neu.
- **Weboberflaeche**: erreichbar im AP-Modus unter `http://192.168.4.1` und im WLAN unter `http://pixelpixel.local` (mDNS).

## Hardware / Verkabelung

| Signal | GPIO |
|---|---|
| LED-Daten (WS2812B) | GPIO 2 |
| Reset-Taster (gegen GND) | GPIO 9 |
| Serielle Konsole (Debug) | USB-C (USB-Serial-JTAG, 115200 Baud) |
| Spannungsversorgung Streifen | 5 V extern, gemeinsame GND |

> Hinweis: Der ESP32-C3 liefert max. 500 mA auf 3,3 V / 5 V USB. Bei mehr als ~10–15 Pixeln sollte der Streifen ueber ein eigenes 5V-Netzteil versorgt werden, damit der ESP nicht browning out. FastLED ist auf 5V/500mA ueber `setMaxPowerInVoltsAndMilliamps` begrenzt.

## Projektstruktur

```
ESP32C3-PixelArtNet/
├── platformio.ini        # Build-Konfiguration
├── include/
│   ├── config.h          # Pins, Konstanten, Modus-Enums
│   ├── config_store.h    # Persistenz (NVS/Preferences)
│   └── config_server.h   # Webserver/AP-Konfiguration
└── src/
    ├── main.cpp          # Setup, Loop, Modus-Rendering
    └── config_server.cpp # HTML-UI, Formularverarbeitung
```

## Verwendete Bibliotheken

- [FastLED](https://github.com/FastLED/FastLED) – LED-Ansteuerung (RMT-basiert auf ESP32)
- [hideakitai/ArtNet](https://github.com/hideakitai/ArtNet) – ArtNet-Protokoll (Empfaenger mit Universe-Subscriptions, deutlich robuster als der handgebaute Parser in den alten Sketches)
- ESP-IDF `Preferences` – Persistenz in NVS

## Build & Flash

```bash
pio run                 # kompilieren
pio run -t upload       # flashen (USB-C)
pio device monitor      # serielle Konsole (115200 baud)
```

## Bedienung

1. Beim ersten Start oeffnet der ESP das WiFiManager-Captive-Portal `PixelSetup-<id>`. Mit diesem verbinden, WLAN auswaehlen und Passwort eingeben; die Konfigurationsseite des ESP ist danach im WLAN unter `http://pixelpixel.local` erreichbar.
2. WLAN-Zugangsdaten und Pixel-Konfiguration eintragen, Speichern.
3. Der ESP verbindet sich mit dem WLAN; die Weboberflaeche ist danach unter `http://pixelpixel.local` bzw. der IP aus dem seriellen Monitor erreichbar.
4. Modus wechseln und konfigurieren; Aenderungen werden sofort angewendet und dauerhaft gespeichert.

## Bluetooth-App (optional, Empfehlung)

Der ESP32-C3 unterstuetzt **BLE 5.0** (kein Classic-Bluetooth). Sinnvolle Einsatzmoeglichkeiten fuer eine App-Anbindung:

1. **BLE-Konfigurationsgatt-Server**: Die bestehende Konfiguration (SSID, Modus, Farben, Effekte) als BLE-Characteristics bereitstellen. Apps wie **nRF Connect** oder **LightBlue** koennten dann direkt Werte schreiben; eine eigene App (Flutter BluePlus / react-native-ble) waere ohne zusaetzliche Cloud moeglich. Aufwand: moderat, da die Config-Struktur bereits serialisierbar ist.
2. **BLE-Beacon / iBeacon**: Wenn nur Statusinformationen gesendet werden sollen (aktueller Modus, Farbe), koennte der ESP als BLE-Beacon senden – sehr energieeffizient, aber nur unidirektional.
3. **ESP-NOW / BLE-Mesh**: Fuer mehrere ESP32-Module, die synchron laufen sollen (z. B. mehrere Streifen mit gleicher Automatik), waere ESP-NOW (WiFi-direct, kein Router noetig) oder BLE-Mesh eine Option.

Umsetzungsempfehlung: BLE-GATT mit einer Service-UUID und Characteristic-UUIDs fuer die wichtigsten Konfigurationswerte (Modus, Farbe, Effekt, Helligkeit). Bei Bedarf kann das als Erweiterung ergaenzt werden.

## Technische Randbedingungen ESP32-C3

- RISC-V Single-Core 160 MHz: FastLED-RMT-Treiber entlastet die CPU bei der Datenausgabe, die Hauptschleife bleibt fuer Webserver/ArtNet responsiv.
- 400 KB SRAM: Der DMX-Puffer ist auf 8 Universen (8x512 Byte) begrenzt; MAX_NUM_LEDS=512 Pixel benoetigen ~1,5 KB RAM zusaetzlich – unkritisch.
- Flash 4 MB: WiFi + Webserver + FastLED passen locker in die Standard-Partition.
- 15 GPIOs: GPIO 2 (LED) und GPIO 9 (Reset) sind gewaehlt, um die USB/Flash-Pins (Strapping-Pins 8, 9, USB 18/19) nicht zu stoeren; GPIO 9 ist BOOT, per internem Pullup als Reset-Taster nutzbar.
