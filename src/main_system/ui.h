#pragma once
#include <LovyanGFX.hpp>

// ---- Цвета CarPlay dark ----
#define UI_BG         0x0841
#define UI_TILE       0x18E3
#define UI_TILE_SEL   0x31A6
#define UI_TEXT       0xFFFF
#define UI_TEXT_DIM   0x8410
#define UI_ACCENT     0x065F
#define UI_GREEN      0x07E0
#define UI_ORANGE     0xFD20
#define UI_RED        0xF800
#define UI_CYAN       0x07FF
#define UI_YELLOW     0xFFE0
#define UI_MAGENTA    0xF81F
#define UI_BLUE       0x001F
#define UI_PURPLE     0x780F

// ---- Layout ----
#define STATUS_H      30
#define GRID_TOP      36
#define TILE_W        71
#define TILE_H        78
#define TILE_GAP_X    6
#define TILE_GAP_Y    6
#define TILE_COLS     4
#define TILE_ROWS     2
#define DOCK_Y        204
#define DOCK_H        30

// ---- Плитки ----
enum TileId {
  TILE_CLOCK = 0,
  TILE_WEATHER,
  TILE_CHART,
  TILE_SETTINGS,
  TILE_INFO,
  TILE_DEBUG,
  TILE_WIFI,
  TILE_RECOVERY,
  TILE_COUNT
};

struct TileInfo {
  const char* label;
  uint16_t    color;
};

static const TileInfo TILES[TILE_COUNT] = {
  {"Clock",    UI_CYAN},
  {"Weather",  UI_ORANGE},
  {"Chart",    UI_GREEN},
  {"Settings", UI_TEXT_DIM},
  {"Info",     UI_BLUE},
  {"Debug",    UI_MAGENTA},
  {"WiFi",     UI_YELLOW},
  {"Recovery", UI_RED},
};

inline int tileX(int idx) {
  return 8 + (idx % TILE_COLS) * (TILE_W + TILE_GAP_X);
}
inline int tileY(int idx) {
  return GRID_TOP + (idx / TILE_COLS) * (TILE_H + TILE_GAP_Y);
}

// ---- Иконки ----
inline void iconClock(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.drawCircle(cx, cy, 20, col);
  g.drawCircle(cx, cy, 21, col);
  g.drawLine(cx, cy, cx, cy - 12, col);
  g.drawLine(cx, cy, cx + 9, cy + 3, col);
  g.fillCircle(cx, cy, 2, col);
}

inline void iconWeather(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.fillRoundRect(cx - 5, cy - 22, 10, 30, 5, col);
  g.fillCircle(cx, cy + 14, 10, col);
  g.fillRect(cx - 3, cy - 20, 6, 28, UI_TILE);
  g.fillCircle(cx, cy + 14, 6, col);
  g.fillRoundRect(cx - 2, cy - 18, 4, 30, 2, col);
}

inline void iconChart(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.fillRect(cx - 18, cy + 8,  8, 12, col);
  g.fillRect(cx - 6,  cy - 4,  8, 24, col);
  g.fillRect(cx + 6,  cy - 14, 8, 34, col);
  g.fillRect(cx - 20, cy + 20, 44, 2, col);
}

inline void iconSettings(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.drawCircle(cx, cy, 18, col);
  g.drawCircle(cx, cy, 17, col);
  g.fillCircle(cx, cy, 6, col);
  for (int i = 0; i < 6; i++) {
    float a = i * 60 * 3.14159 / 180;
    int x = cx + cos(a) * 18;
    int y = cy + sin(a) * 18;
    g.fillRect(x - 3, y - 3, 6, 6, col);
  }
}

inline void iconInfo(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.drawCircle(cx, cy, 20, col);
  g.drawCircle(cx, cy, 19, col);
  g.fillRect(cx - 2, cy - 12, 4, 6, col);
  g.fillRect(cx - 2, cy - 3, 4, 15, col);
}

inline void iconDebug(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.drawRoundRect(cx - 20, cy - 16, 40, 32, 4, col);
  g.drawRoundRect(cx - 19, cy - 15, 38, 30, 4, col);
  g.drawLine(cx - 12, cy - 4, cx - 4, cy + 3, col);
  g.drawLine(cx - 12, cy + 10, cx - 4, cy + 3, col);
  g.fillRect(cx + 2, cy + 10, 10, 2, col);
}

inline void iconWifi(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.fillCircle(cx, cy + 12, 3, col);
  g.drawArc(cx, cy + 12, 8, 10, 220, 320, col);
  g.drawArc(cx, cy + 12, 14, 16, 220, 320, col);
  g.drawArc(cx, cy + 12, 20, 22, 220, 320, col);
}

inline void iconPower(LGFX_Sprite& g, int cx, int cy, uint16_t col) {
  g.drawCircle(cx, cy, 20, col);
  g.drawCircle(cx, cy, 19, col);
  g.fillRect(cx - 2, cy - 22, 4, 14, UI_BG);
  g.fillRect(cx - 2, cy - 20, 4, 12, col);
  g.fillArc(cx, cy, 10, 14, 300, 240, col);
}

inline void drawTileIcon(LGFX_Sprite& g, int tileId, int cx, int cy) {
  switch (tileId) {
    case TILE_CLOCK:    iconClock(g, cx, cy, TILES[tileId].color); break;
    case TILE_WEATHER:  iconWeather(g, cx, cy, TILES[tileId].color); break;
    case TILE_CHART:    iconChart(g, cx, cy, TILES[tileId].color); break;
    case TILE_SETTINGS: iconSettings(g, cx, cy, TILES[tileId].color); break;
    case TILE_INFO:     iconInfo(g, cx, cy, TILES[tileId].color); break;
    case TILE_DEBUG:    iconDebug(g, cx, cy, TILES[tileId].color); break;
    case TILE_WIFI:     iconWifi(g, cx, cy, TILES[tileId].color); break;
    case TILE_RECOVERY: iconPower(g, cx, cy, TILES[tileId].color); break;
  }
}