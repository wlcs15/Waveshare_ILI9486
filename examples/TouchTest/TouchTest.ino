// the setup function runs once when you press reset or power the board

#include <Arduino.h>

#include <SPI.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Waveshare_ILI9486.h>

// Assign human-readable names to some common 16-bit color values:
#define	BLACK   0x0000
#define	BLUE    0x001F
#define	RED     0xF800
#define	GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

namespace
{
    Waveshare_ILI9486 Waveshield;

    // EEPROM layout: magic + TSConfigData. Written only after calibration settles.
    constexpr uint32_t kCalMagic = 0x54534331UL; // 'TSC1'
    constexpr int kCalEepromAddr = 0;
    constexpr unsigned long kCalSaveSettleMs = 1000;
    constexpr unsigned long kRainbowAfterMs = 10000;
    constexpr unsigned long kValidMsgHoldMs = 3000;

    struct CalRecord
    {
        uint32_t magic;
        TSConfigData cfg;
    };

    bool calLoadedFromEeprom = false;
    bool calSavedThisSession = false;
    bool rainbowStarted = false;
    CalRecord eepromRaw = {};

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
        Waveshield.print(F("x "));
        Waveshield.print(cfg.xMin);
        Waveshield.print(F(".."));
        Waveshield.println(cfg.xMax);
        Waveshield.print(F("y "));
        Waveshield.print(cfg.yMin);
        Waveshield.print(F(".."));
        Waveshield.println(cfg.yMax);
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
        calSavedThisSession = true;
        Serial.println(F("[cal] SAVE wrote live calibration to EEPROM"));
        logCfg("saved", rec.cfg);
    }

    void showValidCalibrationScreen()
    {
        const TSConfigData &cfg = Waveshield.getTsConfigData();

        Waveshield.setRotation(1);
        Waveshield.fillScreen(BLACK);
        Waveshield.setCursor(0, 0);
        Waveshield.setTextSize(2);
        Waveshield.setTextColor(GREEN, BLACK);

        if (calLoadedFromEeprom)
        {
            Waveshield.println(F("Calibration VALID"));
            Waveshield.println(F("Prior EEPROM used"));
            Serial.println(F("[cal] pre-rainbow: showing VALID — prior EEPROM session used"));
        }
        else if (calSavedThisSession)
        {
            Waveshield.println(F("Calibration VALID"));
            Waveshield.println(F("Saved this session"));
            Serial.println(F("[cal] pre-rainbow: showing VALID — saved this session"));
        }
        else
        {
            Waveshield.setTextColor(YELLOW, BLACK);
            Waveshield.println(F("Calibration DEFAULT"));
            Waveshield.println(F("No EEPROM record yet"));
            Serial.println(F("[cal] pre-rainbow: defaults still in use (not yet saved)"));
        }

        Waveshield.setTextColor(WHITE, BLACK);
        Waveshield.println();
        printCfgLine(cfg);
        Waveshield.println();
        Waveshield.setTextSize(1);
        Waveshield.println(F("Rainbow lines start after this screen"));

        logCfg("pre-rainbow active", cfg);
        delay(kValidMsgHoldMs);
        Waveshield.setRotation(0);
        Waveshield.fillScreen(BLACK);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println(F("[cal] TouchTest boot"));

    SPI.begin();
    Waveshield.begin();

    calLoadedFromEeprom = loadCalFromEeprom();

    Waveshield.setRotation(1);
    Waveshield.fillScreen(BLACK);
    Waveshield.setCursor(0, 0);
    Waveshield.setTextSize(2);
    Waveshield.setTextColor(WHITE, BLACK);
    if (calLoadedFromEeprom)
    {
        Waveshield.println(F("EEPROM cal VALID"));
        Waveshield.println(F("Using prior session"));
        printCfgLine(Waveshield.getTsConfigData());
    }
    else
    {
        Waveshield.println(F("No saved calibration"));
        Waveshield.println(F("Run stylus off each"));
        Waveshield.println(F("edge to calibrate!"));
    }

    Waveshield.setRotation(0);
}

int i = 0;
bool calDirty = false;
unsigned long calDirtyAt = 0;

// the loop function runs over and over again until power down or reset
void loop()
{
    //  Get raw touchscreen values.
    TSPoint p = Waveshield.getPoint();

    //  Remaps raw touchscreen values to screen co-ordinates.  Automatically handles
    //  rotation!  Returns true when the live calibration limits change.
    if (Waveshield.normalizeTsPoint(p))
    {
        calDirty = true;
        calDirtyAt = millis();
    }

    //  Persist after the stylus stops moving the limits, to avoid EEPROM wear.
    if (calDirty && (millis() - calDirtyAt >= kCalSaveSettleMs))
    {
        saveCalToEeprom();
        calDirty = false;
    }

    //  Confirm EEPROM/session calibration on screen before the rainbow phase.
    if (!rainbowStarted && millis() >= kRainbowAfterMs)
    {
        showValidCalibrationScreen();
        rainbowStarted = true;
        i = 0;
    }

    //  Now that we have a point in screen co-ordinates, draw something there.
    Waveshield.fillCircle(p.x, p.y, 3, BLUE);

    if (rainbowStarted)
    {
        uint16_t color = i << 7 ^ i;
        Waveshield.drawFastHLine(0, i, Waveshield.width() - 1, color);

        i++;
        if (i >= Waveshield.height())
        {
            i = 0;
        }
    }
}
