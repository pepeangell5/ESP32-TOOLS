#include "PepeDraw.h"
#include "Settings.h"
#include "Pins.h"
#include "NVSStore.h"

static int cursor = 0;

void drawSettings() {
    tft.fillScreen(TFT_BLACK);

    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    drawStringCustom(30, 10, "SETTINGS", TFT_WHITE, 3);
    tft.drawFastHLine(0, 45, 320, TFT_WHITE);

    String soundStr = soundEnabled ? "ON" : "OFF";

    for (int i = 0; i < 3; i++) {
        int y = 70 + (i * 40);

        if (i == cursor) {
            tft.fillRect(10, y - 5, 300, 30, TFT_WHITE);
        }

        if (i == 0) {
            drawStringCustom(20, y, "SOUND: " + soundStr,
                i == cursor ? TFT_BLACK : TFT_WHITE, 2);
        }

        if (i == 1) {
            drawStringCustom(20, y, "VOLUME: " + String(soundVolume),
                i == cursor ? TFT_BLACK : TFT_WHITE, 2);
        }

        if (i == 2) {
            drawStringCustom(20, y, "BACK",
                i == cursor ? TFT_BLACK : TFT_WHITE, 2);
        }
    }
}

void runSettings() {

    cursor = 0;

    // Evitar doble OK
    while (digitalRead(BTN_OK) == LOW);
    delay(150);

    bool exitMenu = false;

    drawSettings();

    while (!exitMenu) {

        if (digitalRead(BTN_DOWN) == LOW) {
            cursor = (cursor + 1) % 3;
            drawSettings();
            delay(200);
        }

        if (digitalRead(BTN_UP) == LOW) {
            cursor = (cursor - 1 + 3) % 3;
            drawSettings();
            delay(200);
        }

        if (digitalRead(BTN_OK) == LOW) {

            if (cursor == 0) {
                soundEnabled = !soundEnabled;
                nvsSetBool("sound_on", soundEnabled);    // 💾 persistir
            }
            else if (cursor == 1) {
                soundVolume++;
                if (soundVolume > 5) soundVolume = 1;
                nvsSetInt("sound_vol", soundVolume);     // 💾 persistir
            }
            else if (cursor == 2) {
                exitMenu = true;
            }

            drawSettings();

            while (digitalRead(BTN_OK) == LOW);
            delay(150);
        }

        delay(10);
    }
}