// Touch test for Waveshare 4" ILI9486 on TTgo D1 R32.
// EEPROM record layout matches the Uno R3 TouchTest (magic TSC1 + TSConfigData).

#include <Arduino.h>
#include <SPI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Waveshare_ILI9486.h>

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

namespace
{
    Waveshare_ILI9486 Waveshield;
    Adafruit_GFX &tft = Waveshield;

    const int BACKLIGHT_PIN = 13;

    // Same on-disk layout as the Uno R3 TouchTest.
    constexpr uint32_t kCalMagic = 0x54534331UL; // 'TSC1'
    constexpr int kCalEepromAddr = 0;
    constexpr unsigned long kCalSaveSettleMs = 1000;

    struct CalRecord
    {
        uint32_t magic;
        TSConfigData cfg;
    };

    bool calLoadedFromEeprom = false;
    bool calSavedThisSession = false;
    CalRecord eepromRaw = {};

    void eepromBegin()
    {
#if defined(ESP32)
        EEPROM.begin(64);
#endif
    }

    void eepromCommit()
    {
#if defined(ESP32)
        EEPROM.commit();
#endif
    }

    bool calLooksSane(const TSConfigData &cfg)
    {
        if (cfg.xMin >= cfg.xMax) return false;
        if (cfg.yMin >= cfg.yMax) return false;
        if (cfg.xMin < 0 || cfg.yMin < 0) return false;
        if (cfg.xMax > 1023 || cfg.yMax > 1023) return false;
        return true;
    }

    void logCfg(const char *label, const TSConfigData &cfg)
    {
        Serial.print(F("[cal] "));
        Serial.print(label);
        Serial.print(F(" xMin="));
        Serial.print(cfg.xMin);
        Serial.print(F(" xMax="));
        Serial.print(cfg.xMax);
        Serial.print(F(" yMin="));
        Serial.print(cfg.yMin);
        Serial.print(F(" yMax="));
        Serial.println(cfg.yMax);
    }

    void printCfgLine(const TSConfigData &cfg)
    {
        tft.print(F("x "));
        tft.print(cfg.xMin);
        tft.print(F(".."));
        tft.println(cfg.xMax);
        tft.print(F("y "));
        tft.print(cfg.yMin);
        tft.print(F(".."));
        tft.println(cfg.yMax);
    }

    bool loadCalFromEeprom()
    {
        EEPROM.get(kCalEepromAddr, eepromRaw);
        Serial.print(F("[cal] EEPROM magic=0x"));
        Serial.print(eepromRaw.magic, HEX);
        Serial.print(F(" expected=0x"));
        Serial.println(kCalMagic, HEX);
        logCfg("EEPROM record", eepromRaw.cfg);

        if (eepromRaw.magic != kCalMagic)
        {
            Serial.println(F("[cal] REJECTED: magic mismatch — no prior session"));
            return false;
        }
        if (!calLooksSane(eepromRaw.cfg))
        {
            Serial.println(F("[cal] REJECTED: limits failed sanity check"));
            return false;
        }

        Waveshield.setTsConfigData(eepromRaw.cfg);
        Serial.println(F("[cal] ACCEPTED: prior session calibration is valid and is now active"));
        logCfg("active after load", Waveshield.getTsConfigData());
        return true;
    }

    void saveCalToEeprom()
    {
        CalRecord rec;
        rec.magic = kCalMagic;
        rec.cfg = Waveshield.getTsConfigData();
        if (!calLooksSane(rec.cfg))
        {
            Serial.println(F("[cal] SAVE skipped: live limits not sane"));
            logCfg("live", rec.cfg);
            return;
        }
        EEPROM.put(kCalEepromAddr, rec);
        eepromCommit();
        calSavedThisSession = true;
        Serial.println(F("[cal] SAVE wrote live calibration to EEPROM"));
        logCfg("saved", rec.cfg);

        tft.setCursor(10, 8);
        tft.setTextColor(GREEN, BLACK);
        tft.setTextSize(1);
        tft.print(F("Calibration Saved"));
    }

    void clearCalInEeprom()
    {
        CalRecord rec = {};
        EEPROM.put(kCalEepromAddr, rec);
        eepromCommit();
        Waveshield.resetTsConfigData();
        Serial.println(F("[cal] EEPROM cleared (BOOT held)"));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println();
    Serial.println(F("[cal] TouchTest boot (D1 R32, EEPROM TSC1)"));

    SPI.begin();
    SPI.setFrequency(8000000);
    Serial.println(F("SPI 8 MHz"));

    Waveshield.begin();
    Serial.println(F("Display initialized"));

    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);
    Serial.println(F("Backlight ON"));

    eepromBegin();

    pinMode(0, INPUT_PULLUP);
    const bool forceCal = (digitalRead(0) == LOW);
    if (forceCal)
    {
        clearCalInEeprom();
        calLoadedFromEeprom = false;
    }
    else
    {
        calLoadedFromEeprom = loadCalFromEeprom();
    }

    tft.setRotation(1);
    tft.fillScreen(BLACK);
    tft.setCursor(0, 0);
    tft.setTextSize(2);
    tft.setTextColor(WHITE, BLACK);
    if (calLoadedFromEeprom)
    {
        tft.println(F("EEPROM cal VALID"));
        tft.println(F("Using prior session"));
        printCfgLine(Waveshield.getTsConfigData());
        tft.setTextSize(1);
        tft.println();
        tft.println(F("Hold BOOT + Reset to recalibrate"));
    }
    else
    {
        tft.println(F("No saved calibration"));
        tft.println(F("Run stylus off each"));
        tft.println(F("edge to calibrate!"));
    }

    delay(3000);
    tft.fillScreen(BLACK);
    Serial.println(F("Ready - Start drawing!"));
}

uint32_t lastDraw = 0;
bool calDirty = false;
unsigned long calDirtyAt = 0;

void loop()
{
    TSPoint p = Waveshield.getPoint();

    if (p.z > 80)
    {
        if (Waveshield.normalizeTsPoint(p))
        {
            calDirty = true;
            calDirtyAt = millis();
        }

        if (millis() - lastDraw > 8)
        {
            tft.fillCircle(p.x, p.y, 4, BLUE);
            lastDraw = millis();
        }
    }

    if (calDirty && (millis() - calDirtyAt >= kCalSaveSettleMs))
    {
        saveCalToEeprom();
        calDirty = false;
    }

    delay(1);
}
