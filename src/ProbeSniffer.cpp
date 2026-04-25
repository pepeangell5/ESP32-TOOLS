#include "ProbeSniffer.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN
// ═══════════════════════════════════════════════════════════════════════════
#define MAX_PROBES         50      // máx SSIDs únicos guardados
#define VISIBLE_ROWS       6
#define CHANNEL_HOP_MS     2000    // cambiar canal cada 2s
#define UI_REFRESH_MS      300

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO
// ═══════════════════════════════════════════════════════════════════════════
static ProbeEntry      probes[MAX_PROBES];
static volatile int    probeCount = 0;
static volatile uint32_t totalProbesCaptured = 0;
static volatile int    currentChannel = 1;

static const int       hopChannels[] = {1, 6, 11};
static int             hopIdx = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  CALLBACK PROMISCUO
//  Estructura del probe request frame (802.11):
//    [0]    Frame Control byte 1: 0x40 = Management + ProbeReq
//    [1]    Frame Control byte 2
//    [2-3]  Duration
//    [4-9]  Destination address (FF:FF:FF:FF:FF:FF para probes)
//    [10-15] Source address (MAC del celular que probea)
//    [16-21] BSSID (FF:FF:FF:FF:FF:FF para probes)
//    [22-23] Sequence
//    [24]   Tag SSID = 0x00
//    [25]   SSID length
//    [26+]  SSID bytes
// ═══════════════════════════════════════════════════════════════════════════

static void probeSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;

    if (len < 28) return;

    // Frame Control byte 0: queremos type=Mgmt (00) + subtype=ProbeReq (0100)
    // En binario: 0100 0000 = 0x40
    if (payload[0] != 0x40) return;

    // Tag SSID empieza en offset 24
    uint8_t tagId = payload[24];
    uint8_t tagLen = payload[25];

    if (tagId != 0x00) return;          // no es tag SSID
    if (tagLen == 0) return;            // probe broadcast (sin SSID)
    if (tagLen > 32) return;            // SSID inválido
    if (24 + 2 + tagLen > len) return;  // out of bounds

    // Extraer SSID
    char ssid[33];
    memcpy(ssid, &payload[26], tagLen);
    ssid[tagLen] = '\0';

    // Validar que el SSID sea ASCII imprimible (filtrar basura)
    bool valid = true;
    for (int i = 0; i < tagLen; i++) {
        if ((uint8_t)ssid[i] < 32 || (uint8_t)ssid[i] > 126) {
            valid = false;
            break;
        }
    }
    if (!valid) return;

    totalProbesCaptured++;

    // Buscar si ya está en la lista
    for (int i = 0; i < probeCount; i++) {
        if (strcmp(probes[i].ssid, ssid) == 0) {
            probes[i].count++;
            probes[i].rssi = pkt->rx_ctrl.rssi;
            probes[i].lastSeenMs = millis();
            return;
        }
    }

    // Agregar nuevo si hay espacio
    if (probeCount < MAX_PROBES) {
        strncpy(probes[probeCount].ssid, ssid, 32);
        probes[probeCount].ssid[32] = '\0';
        probes[probeCount].count = 1;
        probes[probeCount].rssi = pkt->rx_ctrl.rssi;
        probes[probeCount].lastSeenMs = millis();
        probeCount++;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  API PÚBLICA
// ═══════════════════════════════════════════════════════════════════════════

int probeSnifferGetCount() {
    return probeCount;
}

bool probeSnifferGet(int idx, ProbeEntry& out) {
    if (idx < 0 || idx >= probeCount) return false;
    out = probes[idx];
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS DE UI
// ═══════════════════════════════════════════════════════════════════════════

static int rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -85) return 2;
    if (rssi >= -95) return 1;
    return 0;
}

// Ordena la lista por count descendente (mediante bubble sort simple)
static void sortByCount() {
    for (int i = 0; i < probeCount - 1; i++) {
        for (int j = 0; j < probeCount - 1 - i; j++) {
            if (probes[j].count < probes[j + 1].count) {
                ProbeEntry t = probes[j];
                probes[j] = probes[j + 1];
                probes[j + 1] = t;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA DE CAPTURA EN VIVO
// ═══════════════════════════════════════════════════════════════════════════

static int g_cursor = 0;
static int g_scrollOffset = 0;

static void drawHeader() {
    tft.fillRect(0, 0, 320, 30, TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, UI_MAIN);

    drawStringBig(8, 8, "PROBE SNIFFER", UI_MAIN, 1);

    char chBuf[16];
    snprintf(chBuf, sizeof(chBuf), "CH:%d", currentChannel);
    drawStringCustom(225, 6, String(chBuf), TFT_GREEN, 1);

    char totBuf[20];
    snprintf(totBuf, sizeof(totBuf), "TOT:%lu",
             (unsigned long)totalProbesCaptured);
    drawStringCustom(225, 18, String(totBuf), UI_ACCENT, 1);

    tft.drawFastHLine(0, 30, 320, UI_ACCENT);
}

static void drawList() {
    const int rowH = 28;
    const int listY = 36;

    // Limpiar área de lista
    tft.fillRect(2, listY, 316, rowH * VISIBLE_ROWS, TFT_BLACK);

    int total = probeCount;
    if (total == 0) {
        drawStringCustom(60, 100, "Esperando probe requests...", UI_ACCENT, 1);
        drawStringCustom(50, 115, "Asegurate que haya celulares cerca",
                         UI_ACCENT, 1);
        drawStringCustom(70, 130, "con WiFi activo y desconectados.",
                         UI_ACCENT, 1);
        return;
    }

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = i + g_scrollOffset;
        if (idx >= total) break;
        int y = listY + i * rowH;

        bool sel = (idx == g_cursor);
        if (sel) tft.fillRect(5, y, 310, rowH - 2, UI_SELECT);

        uint16_t col1 = sel ? UI_BG : UI_MAIN;
        uint16_t col2 = sel ? UI_BG : UI_ACCENT;

        // SSID truncado si es largo
        String s = String(probes[idx].ssid);
        if (s.length() > 22) s = s.substring(0, 20) + "..";
        drawStringCustom(8, y + 4, s, col1, 1);

        // Línea inferior: count + RSSI + tiempo
        unsigned long ago = (millis() - probes[idx].lastSeenMs) / 1000;
        char meta[40];
        snprintf(meta, sizeof(meta), "x%d  %ddBm  %lus",
                 probes[idx].count, probes[idx].rssi, ago);
        drawStringCustom(8, y + 16, String(meta), col2, 1);

        // Barras de señal a la derecha
        int bars = rssiBars(probes[idx].rssi);
        int bx = 285, by = 22;
        for (int b = 0; b < 4; b++) {
            int bh = 3 + b * 2;
            uint16_t c = (b < bars)
                ? (sel ? UI_BG : (bars >= 3 ? TFT_GREEN :
                                  bars >= 2 ? TFT_YELLOW : TFT_ORANGE))
                : (sel ? UI_BG : UI_ACCENT);
            if (b < bars) tft.fillRect(bx + b*5, by - bh, 3, bh, c);
            else          tft.drawRect(bx + b*5, by - bh, 3, bh, c);
        }
    }

    // Scroll bar
    if (total > VISIBLE_ROWS) {
        int barH = (VISIBLE_ROWS * rowH * VISIBLE_ROWS) / total;
        if (barH < 8) barH = 8;
        int trackH = rowH * VISIBLE_ROWS;
        int barY = listY + (g_scrollOffset * (trackH - barH)) /
                            (total - VISIBLE_ROWS);
        tft.fillRect(314, barY, 4, barH, UI_ACCENT);
    }
}

static void drawFooter() {
    tft.drawFastHLine(0, 215, 320, UI_ACCENT);
    char info[40];
    snprintf(info, sizeof(info), "Unicos: %d/%d", probeCount, MAX_PROBES);
    drawStringCustom(8, 220, String(info), UI_MAIN, 1);
    drawStringCustom(140, 220, "OK(HOLD):EXIT", UI_ACCENT, 1);
    drawStringCustom(245, 220, "OK:SORT", UI_ACCENT, 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

static void runSnifferLoop() {
    drawHeader();
    drawList();
    drawFooter();

    unsigned long lastUI = millis();
    unsigned long lastHop = millis();
    int lastTotalDrawn = 0;
    int lastCountDrawn = 0;

    bool stop = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;

    while (!stop) {
        // Channel hopping
        if (millis() - lastHop > CHANNEL_HOP_MS) {
            hopIdx = (hopIdx + 1) % 3;
            currentChannel = hopChannels[hopIdx];
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            lastHop = millis();
        }

        // Refresh UI
        if (millis() - lastUI > UI_REFRESH_MS) {
            drawHeader();
            // Solo redibujar lista si hubo cambios (más fluido)
            if (probeCount != lastCountDrawn ||
                totalProbesCaptured != (uint32_t)lastTotalDrawn) {
                drawList();
                lastCountDrawn = probeCount;
                lastTotalDrawn = totalProbesCaptured;
                // Beep sutil cuando aparece un SSID nuevo
                if (probeCount > lastCountDrawn) {
                    beep(2800, 15);
                }
            } else {
                drawList();   // refresh igual para que actualice el "ago"
            }
            lastUI = millis();
        }

        // ── UP: cursor up ──────────────────────────────────────────────
        static unsigned long lastBtn = 0;
        if (digitalRead(BTN_UP) == LOW && millis() - lastBtn > 200) {
            if (probeCount > 0) {
                g_cursor = (g_cursor + probeCount - 1) % probeCount;
                if (g_cursor < g_scrollOffset) g_scrollOffset = g_cursor;
                if (g_cursor >= g_scrollOffset + VISIBLE_ROWS)
                    g_scrollOffset = g_cursor - VISIBLE_ROWS + 1;
                beep(2100, 15);
                drawList();
            }
            lastBtn = millis();
        }

        // ── DOWN: cursor down ──────────────────────────────────────────
        if (digitalRead(BTN_DOWN) == LOW && millis() - lastBtn > 200) {
            if (probeCount > 0) {
                g_cursor = (g_cursor + 1) % probeCount;
                if (g_cursor < g_scrollOffset) g_scrollOffset = g_cursor;
                if (g_cursor >= g_scrollOffset + VISIBLE_ROWS)
                    g_scrollOffset = g_cursor - VISIBLE_ROWS + 1;
                beep(2100, 15);
                drawList();
            }
            lastBtn = millis();
        }

        // ── OK click corto: re-ordenar por count ──────────────────────
        // ── OK hold: salir ────────────────────────────────────────────
        if (digitalRead(BTN_OK) == LOW) {
            if (!okHeld) {
                okPressStart = millis();
                okHeld = true;
            } else if (millis() - okPressStart > 500) {
                stop = true;
            }
        } else {
            if (okHeld && millis() - okPressStart < 500) {
                // Click corto: reordenar
                sortByCount();
                g_cursor = 0;
                g_scrollOffset = 0;
                beep(2400, 30);
                drawList();
            }
            okHeld = false;
        }

        delay(15);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

void runProbeSniffer() {
    // Esperar liberación de OK
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    // Reset estado
    probeCount = 0;
    totalProbesCaptured = 0;
    g_cursor = 0;
    g_scrollOffset = 0;
    hopIdx = 0;
    currentChannel = hopChannels[0];

    // Pantalla de "preparando"
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, UI_MAIN);
    drawStringBig(60, 90, "INICIANDO", UI_SELECT, 2);
    drawStringCustom(80, 130, "Activando modo promiscuo...", UI_ACCENT, 1);
    delay(500);

    // ── Setup WiFi promiscuo ────────────────────────────────────────────
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&probeSnifferCallback);

    // Filtro: solo management frames
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filter);

    beep(2000, 40); delay(20);
    beep(2400, 60);

    // Loop principal
    runSnifferLoop();

    // ── Cleanup ─────────────────────────────────────────────────────────
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(100);

    beep(1800, 40); delay(20);
    beep(1200, 60);

    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(150);
}