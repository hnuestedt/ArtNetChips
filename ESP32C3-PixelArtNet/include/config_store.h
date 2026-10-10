#pragma once
#include <Preferences.h>
#include "config.h"

// Persistent config handling with NVS Preferences.
class ConfigStore {
public:
    static const uint16_t NVS_VERSION = 1;

    void begin() {
        prefs.begin("pixcfg", false);
    }

    void load(Config &cfg) {
        cfg = defaults();
        uint16_t ver = prefs.getUShort("version", 0);
        if (ver != NVS_VERSION) {
            save(cfg);
            return;
        }
        cfg.numLeds = prefs.getUShort("numLeds", cfg.numLeds);
        if (cfg.numLeds == 0 || cfg.numLeds > MAX_NUM_LEDS) cfg.numLeds = DEFAULT_NUM_LEDS;

        cfg.brightness = prefs.getUChar("bright", cfg.brightness);
        cfg.mode = prefs.getUChar("mode", cfg.mode);
        if (cfg.mode > MODE_AUTO) cfg.mode = MODE_ARTNET;

        cfg.artnetUniverse = prefs.getUShort("univ", cfg.artnetUniverse);
        cfg.artnetAddress = prefs.getUShort("addr", cfg.artnetAddress);
        if (cfg.artnetAddress < 1 || cfg.artnetAddress > 512) cfg.artnetAddress = 1;

        cfg.staticSegments = prefs.getUChar("segs", cfg.staticSegments);
        if (cfg.staticSegments < 1) cfg.staticSegments = 1;
        if (cfg.staticSegments > 5) cfg.staticSegments = 5;
        for (int i = 0; i < 5; i++) {
            char key[8];
            snprintf(key, sizeof(key), "col%d", i);
            cfg.staticColors[i] = prefs.getUInt(key, cfg.staticColors[i]);
        }

        cfg.autoPattern = prefs.getUChar("pattern", cfg.autoPattern);
        if (cfg.autoPattern > AUTO_STROBE) cfg.autoPattern = AUTO_RAINBOW;
        cfg.autoSpeed = prefs.getUChar("speed", cfg.autoSpeed);
        if (cfg.autoSpeed < 1) cfg.autoSpeed = 5;
        if (cfg.autoSpeed > 10) cfg.autoSpeed = 5;
    }

    void save(const Config &cfg) {
        prefs.putUShort("version", NVS_VERSION);
        prefs.putUShort("numLeds", cfg.numLeds);
        prefs.putUChar("bright", cfg.brightness);
        prefs.putUChar("mode", cfg.mode);
        prefs.putUShort("univ", cfg.artnetUniverse);
        prefs.putUShort("addr", cfg.artnetAddress);
        prefs.putUChar("segs", cfg.staticSegments);
        for (int i = 0; i < 5; i++) {
            char key[8];
            snprintf(key, sizeof(key), "col%d", i);
            prefs.putUInt(key, cfg.staticColors[i]);
        }
        prefs.putUChar("pattern", cfg.autoPattern);
        prefs.putUChar("speed", cfg.autoSpeed);
    }

    void factoryReset() {
        Config cfg = defaults();
        save(cfg);
    }

    Config defaults() const {
        Config cfg = {};
        cfg.numLeds = DEFAULT_NUM_LEDS;
        cfg.brightness = 128;
        cfg.mode = MODE_ARTNET;
        cfg.artnetUniverse = 0;
        cfg.artnetAddress = 1;
        cfg.staticSegments = 1;
        for (int i = 0; i < 5; i++) cfg.staticColors[i] = 0x00101010;
        cfg.autoPattern = AUTO_RAINBOW;
        cfg.autoSpeed = 5;
        return cfg;
    }

private:
    Preferences prefs;
};
