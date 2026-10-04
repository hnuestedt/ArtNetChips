#pragma once
#include <Arduino.h>
#include <FastLED.h>

// ------------------------- Hardware-Pins (ESP32-C3) -------------------------
// Der ESP32-C3 hat kein Classic-Bluetooth und nur RMT-Kanaele fuer Neopixel.
// Wir nutzen FastLEDs RMT-Treiber, der auf dem C3 stabil laeuft.

#define PIN_LED_DATA     2        // Daten-Pin fuer den Pixelstreifen (GPIO2)
#define PIN_RESET_BTN    9        // Reset-Taster gegen GND (GPIO9 = BOOT)

#define LED_TYPE         WS2812B  // pixel type, GRB
#define COLOR_ORDER      GRB
#define MAX_NUM_LEDS     512      // Obergrenze, um RAM-Overflow zu vermeiden (RAM ist begrenzt)
#define DEFAULT_NUM_LEDS 30

// ------------------------- Betriebsmodi -------------------------
enum Mode {
    MODE_ARTNET = 0,
    MODE_STATIC = 1,
    MODE_AUTO   = 2
};

// 6 vorgefertigte Automatik-Lauflichter
enum AutoPattern {
    AUTO_RAINBOW = 0,
    AUTO_RUNNING = 1,
    AUTO_BREATHE = 2,
    AUTO_TWINKLE = 3,
    AUTO_CHASE   = 4,
    AUTO_STROBE  = 5
};

// ------------------------- Persistente Konfiguration -------------------------
// Wird in NVS (Preferences) gespeichert und uebersteht Reboot/Powerloss.
struct Config {
    // Streifen
    uint16_t numLeds;
    uint8_t  brightness;

    // Modus
    uint8_t  mode;             // Mode enum

    // ArtNet
    uint16_t artnetUniverse;   // 0..32767 (15 Bit, Uni-1 Darstellung: hier 0-basiert)
    uint16_t artnetAddress;    // 1..512

    // Statisch: 1..5 Abschnitte, je eine RGB-Farbe
    uint8_t  staticSegments;
    uint32_t staticColors[5];  // 0x00RRGGBB

    // Automatik
    uint8_t  autoPattern;      // AutoPattern enum
    uint8_t  autoSpeed;       // 1..10
};

// ------------------------- Debug -------------------------
#define LOGI(...) Serial.printf(__VA_ARGS__)
