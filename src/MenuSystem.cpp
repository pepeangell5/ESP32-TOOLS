#include "MenuSystem.h"
#include "PepeDraw.h"
#include "Icons.h"
#include "Pins.h"
#include "SoundUtils.h"

// Handlers de las tools existentes (llamadas desde el carrusel)
#include "WifiScanner.h"
#include "RadioScanner.h"
#include "RadioJammer.h"
#include "PacketMonitor.h"
#include "SettingsMenu.h"
#include "SystemInfo.h"
#include "SystemInfo.h"
#include "BLEScanner.h"
#include "BLESpam.h"
#include "BTDisruptor.h"
#include "BeaconSpam.h"
#include "Deauther.h"
#include "EvilPortal.h"
#include "Screensaver.h"
#include "ProbeSniffer.h"
#include "Karma.h"
#include "ClockWeather.h"
#include "About.h"
#include "AjoloteSprite.h"

// ═══════════════════════════════════════════════════════════════════════════
//  CARROUSEL PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

// ── Registry de categorías del menú principal ────────────────────────────
// NOTA: por ahora cada categoría lleva directo a una tool (compatibilidad
//       con lo que ya tienes). Cuando agreguemos submenús reales, el handler
//       apuntará a una función que llame a runSubMenu() con su lista.
// Submenu WIFI TOOLS · WiFi Scanner + Beacon Spam + Deauther
static void handlerWifi() {
    static const char* wifiItems[] = {
        "WiFi Scanner",
        "Beacon Spam",
        "Deauther",
        "Evil Portal",
        "Probe Sniffer",
        "KARMA Attack"
    };
    static const int wifiCount = sizeof(wifiItems) / sizeof(char*);

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("WIFI TOOLS", wifiItems, wifiCount);
        switch (choice) {
            case -1: exitSub = true;     break;
            case  0: runWifiScan();      break;
            case  1: runBeaconSpam();    break;
            case  2: runDeauther();      break;
            case  3: runEvilPortal();    break;
            case  4: runProbeSniffer();  break;
            case  5: runKarma();         break;
        }
    }
}

// Submenu BLUETOOTH · BLE Scanner + BLE Spam
// Submenu BLUETOOTH · BLE Scanner + BLE Spam + BT Disruptor
static void handlerBT() {
    static const char* btItems[] = {
        "BLE Scanner",
        "BLE Spam",
        "BT Disruptor"
    };
    static const int btCount = sizeof(btItems) / sizeof(char*);

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("BLUETOOTH", btItems, btCount);
        switch (choice) {
            case -1: exitSub = true;     break;
            case  0: runBLEScanner();    break;
            case  1: runBLESpam();       break;
            case  2: runBTDisruptor();   break;
        }
    }
}

static void handlerMonitor() { runPacketMonitor(); }

// Submenu RADIO TOOLS · abre lista con Jammer + Spectrum
static void handlerRadio() {
    static const char* radioItems[] = {
        "Jammer",
        "Spectrum"
    };
    static const int radioCount = sizeof(radioItems) / sizeof(char*);

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("RADIO TOOLS", radioItems, radioCount);
        switch (choice) {
            case -1: exitSub = true;        break;   // < BACK
            case  0: runRadioJammer();      break;
            case  1: runRadioScanner();     break;
        }
    }
}

// Submenu SYSTEM · abre lista con Settings + System Info
static void handlerSystem() {
    static const char* systemItems[] = {
        "Settings",
        "System Info",
        "Clock & Weather",
        "About"
    };
    static const int systemCount = sizeof(systemItems) / sizeof(char*);

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("SYSTEM", systemItems, systemCount);
        switch (choice) {
            case -1: exitSub = true;       break;
            case  0: runSettings();        break;
            case  1: runSystemInfo();      break;
            case  2: runClockWeather();    break;
            case  3: runAbout();           break;
        }
    }
}

// Nota: RadioScanner (el espectómetro) lo movemos a "RADIO TOOLS" cuando
//       tengamos submenús reales. Por ahora accesible desde el menú SYSTEM
//       o lo podemos dejar como categoría propia provisional.

static const MainMenuEntry MAIN_ENTRIES[] = {
    { "WIFI TOOLS",      "Scan, Deauth, ...",   ICON_WIFI,      handlerWifi    },
    { "RADIO TOOLS",     "Jammer, Scanner",     ICON_RADIO,     handlerRadio   },
    { "BLUETOOTH",       "BLE Scan, Spam",      ICON_BLUETOOTH, handlerBT      },
    { "MONITOR",         "Packet sniffer",      ICON_MONITOR,   handlerMonitor },
    { "SYSTEM",          "Settings, Info",      ICON_SYSTEM,    handlerSystem  },
};
static const int MAIN_COUNT = sizeof(MAIN_ENTRIES) / sizeof(MainMenuEntry);

static int currentEntry = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS DE DIBUJO
// ═══════════════════════════════════════════════════════════════════════════

// Dibuja header con contador "< X/N >" a la derecha
static void drawMainHeader() {
    tft.fillRect(1, 1, 318, 28, TFT_BLACK);
    drawStringBig(10, 8, "ESP32-TOOLS", UI_MAIN, 1);

    String counter = "< " + String(currentEntry + 1) + "/" +
                     String(MAIN_COUNT) + " >";
    int w = getTextWidth(counter, 1);
    drawStringCustom(315 - w, 12, counter, UI_ACCENT, 1);

    tft.drawFastHLine(0, 30, 320, UI_ACCENT);
}

// Dibuja footer con hints de botones
static void drawMainFooter() {
    tft.drawFastHLine(0, 215, 320, UI_ACCENT);
    tft.fillRect(1, 217, 318, 22, TFT_BLACK);
    drawStringCustom(10, 223, "UP/DN: NAVEGAR", UI_ACCENT, 1);
    drawStringCustom(230, 223, "OK: ENTRAR", UI_ACCENT, 1);
}

// Dibuja el ícono + texto de la tarjeta centrada en pantalla.
// `yOffset` permite desplazar verticalmente para la animación slide.
// `highlighted` = true cuando el usuario acaba de apretar OK (flash naranja)
static void drawCard(int entryIdx, int yOffset, bool highlighted) {
    if (entryIdx < 0 || entryIdx >= MAIN_COUNT) return;

    const MainMenuEntry& e = MAIN_ENTRIES[entryIdx];
    uint16_t iconColor  = highlighted ? UI_SELECT : UI_MAIN;
    uint16_t titleColor = highlighted ? UI_SELECT : UI_MAIN;

    // Ícono centrado horizontalmente, un poco arriba del centro vertical
    int iconCx = 160;
    int iconCy = 95 + yOffset;
    // Clipping al área central (entre header y=30 y footer y=215)
    drawIcon(iconCx, iconCy, e.icon, iconColor, 31, 214);

    // Título bajo el ícono con fuente BIG
    int titleY = 150 + yOffset;
    String title = e.title;
    int tw = getTextWidth(title, 2, FONT_BIG);
    drawStringBig((320 - tw) / 2, titleY, title, titleColor, 2);

    // Subtítulo con fuente SMALL
    int subY = 180 + yOffset;
    String sub = e.subtitle;
    int sw = getTextWidth(sub, 1);
    drawStringCustom((320 - sw) / 2, subY, sub, UI_ACCENT, 1);
}

// Limpia solo el área interior (entre header y footer) para redibujar
static void clearCardArea() {
    tft.fillRect(1, 31, 318, 183, TFT_BLACK);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ANIMACIÓN DE SLIDE
//  La tarjeta actual sale en una dirección, la nueva entra desde la opuesta.
//  direction = +1 → nueva entra desde abajo (navegamos DOWN)
//  direction = -1 → nueva entra desde arriba (navegamos UP)
// ═══════════════════════════════════════════════════════════════════════════
static void slideAnimation(int oldIdx, int newIdx, int direction) {
    const int STEPS   = 6;
    const int TRAVEL  = 120;

    for (int step = 1; step <= STEPS; step++) {
        int offset = (TRAVEL * step) / STEPS;
        int oldOff = -direction * offset;
        int newOff =  direction * (TRAVEL - offset);

        clearCardArea();
        drawCard(oldIdx, oldOff, false);
        drawCard(newIdx, newOff, false);

        // Tapar cualquier pixel que se haya salido al footer
        tft.fillRect(1, 215, 318, 24, TFT_BLACK);
        tft.drawFastHLine(0, 215, 320, UI_ACCENT);
        drawStringCustom(10, 223, "UP/DN: NAVEGAR", UI_ACCENT, 1);
        drawStringCustom(230, 223, "OK: ENTRAR", UI_ACCENT, 1);

        delay(12);
    }

    clearCardArea();
    drawCard(newIdx, 0, false);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN MENU · carrusel vertical
// ═══════════════════════════════════════════════════════════════════════════
void runMainMenu() {
    // Dibujar marco completo inicial
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, UI_MAIN);
    drawMainHeader();
    drawMainFooter();
    drawCard(currentEntry, 0, false);

    unsigned long lastPress = 0;
    unsigned long lastActivity = millis();   // ← NUEVO: tracking de idle

    while (true) {

        // ── UP: tarjeta anterior (wrap-around) ──────────────────────────
        if (digitalRead(BTN_UP) == LOW && (millis() - lastPress > 200)) {
            int prev = (currentEntry - 1 + MAIN_COUNT) % MAIN_COUNT;
            beep(2200, 25);
            slideAnimation(currentEntry, prev, -1);
            currentEntry = prev;
            drawMainHeader();
            lastPress = millis();
            lastActivity = millis();              // ← NUEVO
        }

        // ── DOWN: tarjeta siguiente (wrap-around) ───────────────────────
        if (digitalRead(BTN_DOWN) == LOW && (millis() - lastPress > 200)) {
            int next = (currentEntry + 1) % MAIN_COUNT;
            beep(2200, 25);
            slideAnimation(currentEntry, next, +1);
            currentEntry = next;
            drawMainHeader();
            lastPress = millis();
            lastActivity = millis();              // ← NUEVO
        }

        // ── OK: flash naranja + llamar handler ──────────────────────────
        if (digitalRead(BTN_OK) == LOW && (millis() - lastPress > 350)) {
            // Flash de selección: 3 pulsos rápidos de color
            for (int i = 0; i < 3; i++) {
                clearCardArea();
                drawCard(currentEntry, 0, true);
                beep(1500 + i * 300, 40);
                delay(60);
                clearCardArea();
                drawCard(currentEntry, 0, false);
                delay(40);
            }

            // Ejecutar la tool
            MAIN_ENTRIES[currentEntry].handler();

            // Al regresar, redibujar el menú entero
            tft.fillScreen(TFT_BLACK);
            tft.drawRect(0, 0, 320, 240, UI_MAIN);
            drawMainHeader();
            drawMainFooter();
            drawCard(currentEntry, 0, false);
            lastPress = millis();
            lastActivity = millis();              // ← NUEVO
        }

        // ── SCREENSAVER: si hay 30s sin actividad, lanzar ───────────────
        if (millis() - lastActivity > SCREENSAVER_IDLE_MS) {
            runScreensaver();   // bloquea hasta que el usuario presione un botón

            // Al regresar, redibujar el menú entero
            tft.fillScreen(TFT_BLACK);
            tft.drawRect(0, 0, 320, 240, UI_MAIN);
            drawMainHeader();
            drawMainFooter();
            drawCard(currentEntry, 0, false);
            lastActivity = millis();              // resetear idle
            lastPress = millis();
        }

        delay(10);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SUB MENU · lista vertical scrolleable con BACK al inicio
//  · Devuelve índice 0..count-1 del item elegido
//  · Devuelve -1 si usuario eligió "< BACK" o mantuvo OK presionado
// ═══════════════════════════════════════════════════════════════════════════
int runSubMenu(const char* title, const char* items[], int count) {
    const int VISIBLE       = 5;
    const int LINE_HEIGHT   = 30;
    const int LIST_Y_START  = 50;

    int totalItems   = count + 1;   // +1 por BACK
    int cursor       = 0;
    int scrollOffset = 0;
    bool needsRedraw = true;
    int result       = -2;

    unsigned long lastPress = 0;

    // Esperar que suelten OK del menú anterior
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    while (result == -2) {

        if (needsRedraw) {
            tft.fillScreen(TFT_BLACK);
            tft.drawRect(0, 0, 320, 240, UI_MAIN);

            // Header
            tft.fillRect(1, 1, 318, 28, TFT_BLACK);
            drawStringBig(10, 8, title, UI_MAIN, 1);
            tft.drawFastHLine(0, 30, 320, UI_ACCENT);

            // Items
            for (int i = 0; i < VISIBLE; i++) {
                int idx = i + scrollOffset;
                if (idx >= totalItems) break;

                int y = LIST_Y_START + i * LINE_HEIGHT;
                bool selected = (idx == cursor);

                if (selected) {
                    tft.fillRect(8, y - 4, 304, LINE_HEIGHT - 4, UI_SELECT);
                }

                uint16_t textColor = selected ? UI_BG : UI_MAIN;

                if (idx == 0) {
                    drawStringCustom(20, y + 2, "< BACK", textColor, 2);
                } else {
                    drawStringCustom(20, y + 2, items[idx - 1], textColor, 2);
                }
            }

            // Scroll bar lateral
            if (totalItems > VISIBLE) {
                int barH = (VISIBLE * 170) / totalItems;
                int barY = 40 + (scrollOffset * (170 - barH)) / (totalItems - VISIBLE);
                tft.fillRect(313, barY, 4, barH, UI_ACCENT);
            }

            // Footer
            tft.drawFastHLine(0, 215, 320, UI_ACCENT);
            drawStringCustom(10, 223, "UP/DN: NAVEGAR   OK: SELECC",
                             UI_ACCENT, 1);

            needsRedraw = false;
        }

        // UP
        if (digitalRead(BTN_UP) == LOW && (millis() - lastPress > 180)) {
            cursor = (cursor - 1 + totalItems) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE)
                scrollOffset = cursor - VISIBLE + 1;
            beep(2200, 25);
            needsRedraw = true;
            lastPress = millis();
        }

        // DOWN
        if (digitalRead(BTN_DOWN) == LOW && (millis() - lastPress > 180)) {
            cursor = (cursor + 1) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE)
                scrollOffset = cursor - VISIBLE + 1;
            beep(2200, 25);
            needsRedraw = true;
            lastPress = millis();
        }

        // OK
        if (digitalRead(BTN_OK) == LOW && (millis() - lastPress > 350)) {
            beep(1500, 60);
            if (cursor == 0) result = -1;       // BACK
            else             result = cursor - 1;
        }

        delay(10);
    }

    // Esperar liberación del OK
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    return result;
}