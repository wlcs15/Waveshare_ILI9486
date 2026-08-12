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

    struct CalRecord
    {
        uint32_t magic;
        TSConfigData cfg;
    };

    bool calLooksSane(const TSConfigData &cfg)
    {
        if (cfg.xMin >= cfg.xMax) return false;
        if (cfg.yMin >= cfg.yMax) return false;
        if (cfg.xMin < 0 || cfg.yMin < 0) return false;
        if (cfg.xMax > 1023 || cfg.yMax > 1023) return false;
        return true;
    }

    bool loadCalFromEeprom()
    {
        CalRecord rec;
        EEPROM.get(kCalEepromAddr, rec);
        if (rec.magic != kCalMagic) return false;
        if (!calLooksSane(rec.cfg)) return false;
        Waveshield.setTsConfigData(rec.cfg);
        return true;
    }

    void saveCalToEeprom()
    {
        CalRecord rec;
        rec.magic = kCalMagic;
        rec.cfg = Waveshield.getTsConfigData();
        if (!calLooksSane(rec.cfg)) return;
        EEPROM.put(kCalEepromAddr, rec);
    }
}

void setup() 
{
    SPI.begin();
    Waveshield.begin();

    Waveshield.setRotation(1);
    Waveshield.setTextSize(2);
    if (loadCalFromEeprom())
    {
        Waveshield.print("Calibration loaded from EEPROM");
    }
    else
    {
        Waveshield.print("Run stylus off each edge to calibrate!");
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

    //  Now that we have a point in screen co-ordinates, draw something there.
    Waveshield.fillCircle(p.x, p.y, 3, BLUE);

    // After ten seconds, start re-drawing the background.  Draw one line each time
    // through the loop, in a variety of colors.
    if (millis() / 1000 > 10)
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
