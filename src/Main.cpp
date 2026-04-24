#include <Arduino.h>
#include <TFT_eSPI.h>
#include "PepeDraw.h"
#include "MenuSystem.h"
#include "Pins.h"
#include "Settings.h"
#include "NVSStore.h"
#include "SplashScreen.h"

// ═══════════════════════════════════════════════════════════════════════════
//  ESP32-TOOLS · Firmware principal
//  El main.cpp solo inicializa hardware y entrega el control al menú.
// ═══════════════════════════════════════════════════════════════════════════

TFT_eSPI tft = TFT_eSPI();

// ── Carga todas las preferencias desde NVS a las variables globales ──────
static void loadPreferences() {
    soundEnabled = nvsGetBool("sound_on",  true);
    soundVolume  = nvsGetInt ("sound_vol", 3);
    if (soundVolume < 1) soundVolume = 1;
    if (soundVolume > 5) soundVolume = 5;
}

// ── Incrementa contador de arranques (útil para System Info después) ─────
static void bumpBootCount() {
    unsigned long bc = nvsGetULong("boot_cnt", 0);
    bc++;
    nvsSetULong("boot_cnt", bc);
    Serial.printf("[NVS] Boot count: %lu\n", bc);
}

void setup() {
    Serial.begin(115200);

    // ── Botones ─────────────────────────────────────────────────────────
    pinMode(BTN_UP,   INPUT_PULLUP);
    pinMode(BTN_OK,   INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);

    // ── Buzzer ──────────────────────────────────────────────────────────
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWriteTone(0, 0);

    // ── NVS: cargar configuración guardada ──────────────────────────────
    nvsBegin();
    loadPreferences();
    bumpBootCount();

    // ── Reset pantalla ──────────────────────────────────────────────────
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);  delay(100);
    digitalWrite(4, HIGH); delay(100);

    tft.begin();
    tft.setRotation(1);

    tft.fillScreen(TFT_BLACK);


    tft.begin();
    tft.setRotation(1);

    tft.fillScreen(TFT_BLACK);

    // ── Splash screen (espera a que usuario presione OK) ────────────────
    runSplashScreen();

    // ── Menú principal (bucle infinito, nunca regresa) ──────────────────
    runMainMenu();

    // ── Menú principal (bucle infinito, nunca regresa) ──────────────────
    runMainMenu();
}

void loop() {
    delay(1000);
}