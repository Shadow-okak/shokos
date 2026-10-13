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

// ★ Главная настройка — процент мёртвой зоны
#define JOY_DEADZONE_PERCENT  40

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

// ---------- Нормализация с 40% deadzone ----------
inline int joyXNorm() {
  int x = analogRead(JOY_VRX_PIN);
  int half = min(joyCal.xCenter - joyCal.xMin, joyCal.xMax - joyCal.xCenter);
  if (half < 100) half = 100;   // защита от некорректной калибровки
  int dz = half * joyCal.deadzonePercent / 100;
  int lowEdge  = joyCal.xCenter - dz;
  int highEdge = joyCal.xCenter + dz;

  if (x < lowEdge)  return map(x, joyCal.xMin, lowEdge, -100, 0);
  if (x > highEdge) return map(x, highEdge, joyCal.xMax, 0, 100);
  return 0;
}

inline int joyYNorm() {
  int y = analogRead(JOY_VRY_PIN);
  int half = min(joyCal.yCenter - joyCal.yMin, joyCal.yMax - joyCal.yCenter);
  if (half < 100) half = 100;
  int dz = half * joyCal.deadzonePercent / 100;
  int lowEdge  = joyCal.yCenter - dz;
  int highEdge = joyCal.yCenter + dz;

  if (y < lowEdge)  return map(y, joyCal.yMin, lowEdge, -100, 0);
  if (y > highEdge) return map(y, highEdge, joyCal.yMax, 0, 100);
  return 0;
}

// ---------- Wizard калибровки ----------
inline void runCalibration(LGFX_Sprite& spr) {
  // === Фаза 1: центр (0.5 сек) ===
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

  long sx = 0, sy = 0;
  int  n  = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 500) {
    sx += analogRead(JOY_VRX_PIN);
    sy += analogRead(JOY_VRY_PIN);
    n++;
    delay(5);
  }
  joyCal.xCenter = sx / n;
  joyCal.yCenter = sy / n;

  // Замер шума (для лога, не для deadzone)
  long noiseSum = 0;
  n = 0;
  t0 = millis();
  while (millis() - t0 < 300) {
    noiseSum += abs(analogRead(JOY_VRX_PIN) - joyCal.xCenter);
    noiseSum += abs(analogRead(JOY_VRY_PIN) - joyCal.yCenter);
    n += 2;
    delay(5);
  }
  int avgNoise = noiseSum / n;

  Serial.printf("Center: X=%d Y=%d, noise=%d\n",
                joyCal.xCenter, joyCal.yCenter, avgNoise);

  // === Фаза 2: полный ход (1.5 сек) ===
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

  t0 = millis();
  const unsigned long PHASE2_MS = 1500;
  while (millis() - t0 < PHASE2_MS) {
    int x = analogRead(JOY_VRX_PIN);
    int y = analogRead(JOY_VRY_PIN);
    if (x < joyCal.xMin) joyCal.xMin = x;
    if (x > joyCal.xMax) joyCal.xMax = x;
    if (y < joyCal.yMin) joyCal.yMin = y;
    if (y > joyCal.yMax) joyCal.yMax = y;

    int progress = (millis() - t0) * 280 / PHASE2_MS;
    spr.drawRect(20, 110, 280, 20, UI_TEXT_DIM);
    spr.fillRect(21, 111, progress, 18, UI_ACCENT);
    spr.pushSprite(0, 0);
    delay(10);
  }

  joyCal.deadzonePercent = JOY_DEADZONE_PERCENT;
  joyCal.valid = true;
  saveJoyCal();

  Serial.printf("Range X: %d..%d  Y: %d..%d  deadzone=%d%%\n",
                joyCal.xMin, joyCal.xMax, joyCal.yMin, joyCal.yMax,
                joyCal.deadzonePercent);

  // === Итог ===
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
  spr.printf("Deadzone: %d%% of half-range", joyCal.deadzonePercent);

  spr.pushSprite(0, 0);
  delay(1200);
}