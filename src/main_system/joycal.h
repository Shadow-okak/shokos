#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "ui.h"

#ifndef JOY_VRX_PIN
#define JOY_VRX_PIN      9
#endif
#ifndef JOY_VRY_PIN
#define JOY_VRY_PIN      10
#endif

// ---------- Настройки ----------
#define JOY_DEADZONE_PERCENT  40
#define JOY_EMA_ALPHA         0.4f
#define JOY_CURVE_POW         2.0f
#define JOY_DRIFT_TIMEOUT_MS  5000
#define JOY_DRIFT_ALPHA       0.002f
#define JOY_MOVE_THRESHOLD    60

struct JoyCal {
  int xCenter  = 2048;
  int yCenter  = 2048;
  int xMin     = 0;
  int xMax     = 4095;
  int yMin     = 0;
  int yMax     = 4095;
  int deadzonePercent = JOY_DEADZONE_PERCENT;
  bool valid   = false;
};

static JoyCal joyCal;
static float emaX = 0, emaY = 0;
static bool  emaInited = false;

// ---------- NVS ----------
inline void loadJoyCal() {
  Preferences prefs;
  prefs.begin("joycal", true);
  joyCal.valid = prefs.getBool("valid", false);
  if (joyCal.valid) {
    joyCal.xCenter = prefs.getInt("xc",    2048);
    joyCal.yCenter = prefs.getInt("yc",    2048);
    joyCal.xMin    = prefs.getInt("xmin",  0);
    joyCal.xMax    = prefs.getInt("xmax",  4095);
    joyCal.yMin    = prefs.getInt("ymin",  0);
    joyCal.yMax    = prefs.getInt("ymax",  4095);
    joyCal.deadzonePercent = prefs.getInt("dzp", JOY_DEADZONE_PERCENT);
  }
  prefs.end();
}

inline void saveJoyCal() {
  Preferences prefs;
  prefs.begin("joycal", false);
  prefs.putInt("xc",   joyCal.xCenter);
  prefs.putInt("yc",   joyCal.yCenter);
  prefs.putInt("xmin", joyCal.xMin);
  prefs.putInt("xmax", joyCal.xMax);
  prefs.putInt("ymin", joyCal.yMin);
  prefs.putInt("ymax", joyCal.yMax);
  prefs.putInt("dzp",  joyCal.deadzonePercent);
  prefs.putBool("valid", true);
  prefs.end();
}

// ---------- Фильтрация ----------
inline int median7(const int* v) {
  int t[7];
  for (int i = 0; i < 7; i++) t[i] = v[i];
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6 - i; j++)
      if (t[j] > t[j+1]) { int x = t[j]; t[j] = t[j+1]; t[j+1] = x; }
  return t[3];
}

inline void readFiltered(int& outX, int& outY) {
  int rx[7], ry[7];
  for (int i = 0; i < 7; i++) {
    rx[i] = analogRead(JOY_VRX_PIN);
    ry[i] = analogRead(JOY_VRY_PIN);
    delayMicroseconds(80);
  }
  int mx = median7(rx);
  int my = median7(ry);
  if (!emaInited) { emaX = mx; emaY = my; emaInited = true; }
  emaX = JOY_EMA_ALPHA * mx + (1 - JOY_EMA_ALPHA) * emaX;
  emaY = JOY_EMA_ALPHA * my + (1 - JOY_EMA_ALPHA) * emaY;
  outX = (int)emaX;
  outY = (int)emaY;
}

// ---------- Авто-центр ----------
inline void updateAutoCenter(int x, int y) {
  static int lastX = 0, lastY = 0;
  static unsigned long lastMove = 0;

  if (abs(x - lastX) > JOY_MOVE_THRESHOLD || abs(y - lastY) > JOY_MOVE_THRESHOLD) {
    lastMove = millis();
  }
  if (millis() - lastMove > JOY_DRIFT_TIMEOUT_MS) {
    joyCal.xCenter = joyCal.xCenter * (1 - JOY_DRIFT_ALPHA) + x * JOY_DRIFT_ALPHA;
    joyCal.yCenter = joyCal.yCenter * (1 - JOY_DRIFT_ALPHA) + y * JOY_DRIFT_ALPHA;
  }
  lastX = x; lastY = y;
}

// ---------- Нормализация + кривая ----------
inline int normAxis(int value, int center, int vmin, int vmax) {
  int half = min(center - vmin, vmax - center);
  if (half < 100) half = 100;
  int dz = half * joyCal.deadzonePercent / 100;
  int low  = center - dz;
  int high = center + dz;

  float t = 0.0f;
  if (value < low)       t = (float)(value - low) / (low - vmin);
  else if (value > high) t = (float)(value - high) / (vmax - high);
  else return 0;

  float s = powf(fabsf(t), JOY_CURVE_POW) * (t < 0 ? -1.0f : 1.0f);
  return (int)(s * 100.0f);
}

inline int joyXNorm() {
  int x, y;
  readFiltered(x, y);
  updateAutoCenter(x, y);
  return normAxis(x, joyCal.xCenter, joyCal.xMin, joyCal.xMax);
}

inline int joyYNorm() {
  int x, y;
  readFiltered(x, y);
  updateAutoCenter(x, y);
  return normAxis(y, joyCal.yCenter, joyCal.yMin, joyCal.yMax);
}

// ---------- Калибровка ----------
inline int median21(int* v) {
  for (int i = 1; i < 21; i++) {
    int key = v[i], j = i - 1;
    while (j >= 0 && v[j] > key) { v[j+1] = v[j]; j--; }
    v[j+1] = key;
  }
  return v[10];
}

inline void runCalibration(LGFX_Sprite& spr) {
  spr.fillSprite(UI_BG);
  spr.setTextColor(UI_TEXT, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(20, 30);
  spr.print("Calibration");
  spr.setTextSize(1);
  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  spr.setCursor(20, 65);
  spr.print("Phase 1/2: leave stick at rest");
  spr.setTextColor(UI_GREEN, UI_BG);
  spr.setCursor(20, 80);
  spr.print(">>> DO NOT TOUCH <<<");
  spr.pushSprite(0, 0);

  int sx[21], sy[21];
  for (int i = 0; i < 21; i++) {
    sx[i] = analogRead(JOY_VRX_PIN);
    sy[i] = analogRead(JOY_VRY_PIN);
    delay(10);
  }
  joyCal.xCenter = median21(sx);
  joyCal.yCenter = median21(sy);

  Serial.printf("Center: X=%d Y=%d\n", joyCal.xCenter, joyCal.yCenter);

  spr.fillRect(0, 60, 320, 100, UI_BG);
  spr.setTextColor(UI_TEXT, UI_BG);
  spr.setTextSize(1);
  spr.setCursor(20, 65);
  spr.print("Phase 2/2: circle the stick");
  spr.setTextColor(UI_YELLOW, UI_BG);
  spr.setCursor(20, 80);
  spr.print(">>> FULL RANGE <<<");
  spr.pushSprite(0, 0);

  joyCal.xMin = 4095; joyCal.xMax = 0;
  joyCal.yMin = 4095; joyCal.yMax = 0;

  unsigned long t0 = millis();
  const unsigned long DURATION = 2000;
  while (millis() - t0 < DURATION) {
    int x = analogRead(JOY_VRX_PIN);
    int y = analogRead(JOY_VRY_PIN);
    if (x < joyCal.xMin) joyCal.xMin = x;
    if (x > joyCal.xMax) joyCal.xMax = x;
    if (y < joyCal.yMin) joyCal.yMin = y;
    if (y > joyCal.yMax) joyCal.yMax = y;

    int progress = (millis() - t0) * 280 / DURATION;
    spr.drawRect(20, 110, 280, 20, UI_TEXT_DIM);
    spr.fillRect(21, 111, progress, 18, UI_ACCENT);
    spr.pushSprite(0, 0);
    delay(10);
  }

  joyCal.deadzonePercent = JOY_DEADZONE_PERCENT;
  joyCal.valid = true;
  saveJoyCal();

  Serial.printf("Range: X %d..%d  Y %d..%d  dz=%d%%\n",
                joyCal.xMin, joyCal.xMax, joyCal.yMin, joyCal.yMax,
                joyCal.deadzonePercent);

  spr.fillRect(0, 140, 320, 80, UI_BG);
  spr.setTextColor(UI_GREEN, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(20, 145);
  spr.print("Done!");

  spr.setTextSize(1);
  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  spr.setCursor(20, 175);
  spr.printf("X %d..%d  Y %d..%d",
             joyCal.xMin, joyCal.xMax, joyCal.yMin, joyCal.yMax);
  spr.setCursor(20, 192);
  spr.printf("Curve ^%.1f  Deadzone %d%%",
             JOY_CURVE_POW, joyCal.deadzonePercent);

  spr.pushSprite(0, 0);
  delay(1500);
}