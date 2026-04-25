#include "Screensaver.h"
#include <TFT_eSPI.h>
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
//  AJOLOTE 48x40 · versión reducida del bitmap del splash
//  Generado tomando el bitmap original 96x80 y haciendo downsample 2x2
//  (cada 2x2 píxeles del original se reducen a 1: si 2 o más están encendidos,
//  se enciende, si no, apagado)
// ═══════════════════════════════════════════════════════════════════════════
#define AJ_W  48
#define AJ_H  40

static const uint8_t AJOLOTE_SMALL[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0x00, 0x00, 0x00,
    0x00, 0x01, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x03, 0xFF, 0xC0, 0x00, 0x00,
    0x00, 0x07, 0xE7, 0xE0, 0x00, 0x00,
    0x00, 0x0F, 0xC3, 0xF0, 0x00, 0x00,
    0x00, 0x0F, 0x81, 0xF0, 0x00, 0x00,
    0x00, 0x1F, 0x99, 0xF8, 0x00, 0x00,
    0x00, 0x3F, 0xFF, 0xFC, 0x00, 0x00,
    0x00, 0x3F, 0xFF, 0xFC, 0x00, 0x00,
    0x00, 0x7F, 0xFF, 0xFE, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x01, 0xFF, 0xFF, 0xFF, 0x80, 0x00,
    0x03, 0xFF, 0xFF, 0xFF, 0xC0, 0x00,
    0x07, 0xC0, 0xFF, 0x03, 0xE0, 0x00,
    0x0F, 0x80, 0x7E, 0x01, 0xF0, 0x00,
    0x1F, 0x00, 0x3C, 0x00, 0xF8, 0x00,
    0x3E, 0x00, 0x18, 0x00, 0x7C, 0x00,
    0x7C, 0x07, 0xFF, 0xE0, 0x3E, 0x00,
    0x78, 0x07, 0xFF, 0xE0, 0x1E, 0x00,
    0x70, 0x0F, 0xFF, 0xF0, 0x0E, 0x00,
    0x60, 0x0F, 0xFF, 0xF0, 0x06, 0x00,
    0x40, 0x0F, 0xFF, 0xF0, 0x02, 0x00,
    0x00, 0x07, 0xFF, 0xE0, 0x00, 0x00,
    0x00, 0x03, 0xFF, 0xC0, 0x00, 0x00,
    0x00, 0x01, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0x07, 0xFF, 0xE0, 0x00, 0x00,
    0x00, 0x0F, 0xFF, 0xF0, 0x00, 0x00,
    0x00, 0x07, 0xFF, 0xE0, 0x00, 0x00,
    0x00, 0x03, 0xC3, 0xC0, 0x00, 0x00,
    0x00, 0x07, 0x81, 0xE0, 0x00, 0x00,
    0x00, 0x07, 0x81, 0xE0, 0x00, 0x00,
    0x00, 0x0F, 0x00, 0xF0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO DE LA ANIMACIÓN
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_STARS  20

struct Star {
    int16_t x, y;
    uint8_t brightness;   // 0..255
    int8_t  fadeDir;      // +1 o -1
};

static Star stars[MAX_STARS];

// Posición del ajolote (con sub-pixel precision via fixed point x10)
static int16_t ajX = 100;
static int16_t ajY = 80;
static int8_t  ajVX = 1;
static int8_t  ajVY = 1;

// Para el texto rotativo
static const char* TEXTS[] = {
    "ESP32-TOOLS",
    "by PepeAngell",
    "ZzZ...",
    "Press any key"
};
static const int TEXT_COUNT = sizeof(TEXTS) / sizeof(char*);
static int currentTextIdx = 0;
static unsigned long lastTextChange = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS DE DIBUJO
// ═══════════════════════════════════════════════════════════════════════════

// Dibuja el ajolote con color en (x, y)
static void drawAjolote(int x, int y, uint16_t color) {
    int bytesPerRow = AJ_W / 8;   // = 6
    for (int r = 0; r < AJ_H; r++) {
        for (int byteIdx = 0; byteIdx < bytesPerRow; byteIdx++) {
            uint8_t bits = pgm_read_byte(&AJOLOTE_SMALL[r * bytesPerRow + byteIdx]);
            if (bits == 0) continue;
            for (int bit = 0; bit < 8; bit++) {
                if (bits & (0x80 >> bit)) {
                    tft.drawPixel(x + byteIdx * 8 + bit, y + r, color);
                }
            }
        }
    }
}

// Borra el ajolote pintándolo en negro
static void eraseAjolote(int x, int y) {
    drawAjolote(x, y, TFT_BLACK);
}

// Inicializa estrellitas en posiciones aleatorias
static void initStars() {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = random(5, 315);
        stars[i].y = random(5, 235);
        stars[i].brightness = random(50, 255);
        stars[i].fadeDir = random(2) ? 1 : -1;
    }
}

// Actualiza brillo de cada estrella y la dibuja
static void updateStars() {
    for (int i = 0; i < MAX_STARS; i++) {
        // Borrar posición anterior
        tft.drawPixel(stars[i].x, stars[i].y, TFT_BLACK);

        // Actualizar brillo
        int b = stars[i].brightness + stars[i].fadeDir * 8;
        if (b >= 250) {
            b = 255;
            stars[i].fadeDir = -1;
        }
        if (b <= 30) {
            b = 30;
            stars[i].fadeDir = 1;
            // Reposicionar de vez en cuando para variar
            if (random(100) < 30) {
                stars[i].x = random(5, 315);
                stars[i].y = random(5, 235);
            }
        }
        stars[i].brightness = b;

        // Convertir brillo a color RGB565 (gris azulado)
        uint16_t r5 = (b >> 3) & 0x1F;
        uint16_t g6 = (b >> 2) & 0x3F;
        uint16_t b5 = (b >> 3) & 0x1F;
        uint16_t color = (r5 << 11) | (g6 << 5) | b5;

        tft.drawPixel(stars[i].x, stars[i].y, color);
    }
}

// Verifica si alguna estrella está dentro del rectángulo del ajolote
// (para no dibujar encima de él)
static bool starInsideAjolote(int sx, int sy, int ax, int ay) {
    return (sx >= ax && sx < ax + AJ_W &&
            sy >= ay && sy < ay + AJ_H);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEXTO ROTATIVO
// ═══════════════════════════════════════════════════════════════════════════

static int prevTextX = -1, prevTextY = -1, prevTextW = 0, prevTextH = 0;

static void updateText(unsigned long startMs) {
    // Cambiar texto cada 4 segundos
    if (millis() - lastTextChange > 4000) {
        // Borrar texto anterior
        if (prevTextX >= 0) {
            tft.fillRect(prevTextX, prevTextY, prevTextW, prevTextH, TFT_BLACK);
        }

        currentTextIdx = (currentTextIdx + 1) % TEXT_COUNT;
        lastTextChange = millis();

        // Posición aleatoria nueva (evitando bordes y centro donde puede
        // estar el ajolote)
        String text;
        if (currentTextIdx == 2) {
            // ZzZ... cerca del ajolote
            text = String(TEXTS[currentTextIdx]);
        } else if (currentTextIdx == 3) {
            // Press any key abajo
            text = String(TEXTS[currentTextIdx]);
        } else {
            text = String(TEXTS[currentTextIdx]);
        }

        int textW = getTextWidth(text, 2, FONT_BIG);
        int textH = 24;

        int x, y;
        // 4 zonas: arriba-izq, arriba-der, abajo-izq, abajo-der
        int zone = random(4);
        switch (zone) {
            case 0: x = 20;          y = 20;  break;
            case 1: x = 320 - textW - 20; y = 20;  break;
            case 2: x = 20;          y = 200; break;
            default: x = 320 - textW - 20; y = 200; break;
        }

        prevTextX = x;
        prevTextY = y;
        prevTextW = textW;
        prevTextH = textH;

        // Color que cambia con el texto
        uint16_t colors[] = {UI_MAIN, UI_SELECT, TFT_CYAN, TFT_GREEN};
        drawStringBig(x, y, text, colors[currentTextIdx % 4], 2);
    }

    // Mostrar uptime en una zona fija (esquina inferior izquierda pequeño)
    static unsigned long lastUptimeUpdate = 0;
    if (millis() - lastUptimeUpdate > 1000) {
        unsigned long sec = (millis() - startMs) / 1000;
        unsigned long m = sec / 60;
        sec = sec % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "Idle: %lum %lus", m, sec);

        tft.fillRect(5, 230, 110, 8, TFT_BLACK);
        drawStringCustom(5, 230, String(buf), UI_ACCENT, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  ANIMACIÓN PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

static void updateAjolote() {
    // Borrar posición anterior
    eraseAjolote(ajX, ajY);

    // Actualizar posición
    ajX += ajVX;
    ajY += ajVY;

    // Rebotar en bordes
    bool bounced = false;
    if (ajX < 5) {
        ajX = 5;
        ajVX = -ajVX;
        bounced = true;
    }
    if (ajX + AJ_W > 315) {
        ajX = 315 - AJ_W;
        ajVX = -ajVX;
        bounced = true;
    }
    if (ajY < 30) {
        ajY = 30;
        ajVY = -ajVY;
        bounced = true;
    }
    if (ajY + AJ_H > 220) {
        ajY = 220 - AJ_H;
        ajVY = -ajVY;
        bounced = true;
    }

    // Cuando rebota, beep sutil
    if (bounced) {
        beep(2400, 8);
    }

    // Color del ajolote: alterna entre blanco y cian para efecto sutil
    uint16_t color = ((millis() / 600) % 2) ? UI_MAIN : TFT_CYAN;
    drawAjolote(ajX, ajY, color);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

void runScreensaver() {
    // Esperar liberación de cualquier botón presionado
    while (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW ||
           digitalRead(BTN_OK) == LOW) delay(5);
    delay(50);

    // Setup inicial
    tft.fillScreen(TFT_BLACK);
    initStars();

    // Posición inicial aleatoria del ajolote (centrada-ish)
    ajX = random(50, 270 - AJ_W);
    ajY = random(50, 200 - AJ_H);

    // Dirección aleatoria
    ajVX = random(2) ? 1 : -1;
    ajVY = random(2) ? 1 : -1;

    currentTextIdx = 0;
    lastTextChange = millis() - 4000;   // forzar primer texto inmediato
    prevTextX = -1;

    unsigned long startMs = millis();
    unsigned long lastFrame = millis();
    const unsigned long FRAME_MS = 50;   // ~20 FPS

    while (true) {
        // Detectar cualquier botón → salir
        if (digitalRead(BTN_UP) == LOW ||
            digitalRead(BTN_DOWN) == LOW ||
            digitalRead(BTN_OK) == LOW) {
            beep(1800, 30);
            // Esperar liberación de TODOS los botones
            while (digitalRead(BTN_UP) == LOW ||
                   digitalRead(BTN_DOWN) == LOW ||
                   digitalRead(BTN_OK) == LOW) delay(5);
            delay(100);
            return;
        }

        // Frame update
        if (millis() - lastFrame >= FRAME_MS) {
            updateStars();
            updateAjolote();
            updateText(startMs);
            lastFrame = millis();
        }

        delay(2);
    }
}