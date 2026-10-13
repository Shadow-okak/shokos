#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_NeoPixel.h>
#include "ui.h"
#include "joycal.h"

// ---------- Пины ----------
#define TFT_BL_PIN       16
#define JOY_VRX_PIN      9
#define JOY_VRY_PIN      10
#define JOY_SW_PIN       11
#define BUZZER_PIN       17
#define NEOPIXEL_PIN     48
#define I2C_SDA          18
#define I2C_SCL          8
#define BUZZER_FREQ_HZ   2000
#define BUZZER_LEDC_RES  8
#define BUZZER_CHANNEL   0

// ---------- LovyanGFX ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST; cfg.spi_mode = 0;
      cfg.freq_write = 40000000; cfg.freq_read = 16000000;
      cfg.spi_3wire = true; cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 4; cfg.pin_mosi = 5; cfg.pin_miso = -1; cfg.pin_dc = 7;
      _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs = 6; cfg.pin_rst = 15; cfg.pin_busy = -1;
      cfg.panel_width = 240; cfg.panel_height = 320;
      cfg.offset_x = 0; cfg.offset_y = 0; cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8; cfg.dummy_read_bits = 1;
      cfg.readable = false; cfg.invert = false; cfg.rgb_order = false;
      cfg.dlen_16bit = false; cfg.bus_shared = false;
      _panel_instance.config(cfg); }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
LGFX_Sprite spr(&tft);
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_BME280 bme;
bool bmeOK = false;

// ---------- Состояние UI ----------
enum Screen {
  SCREEN_HOME,
  SCREEN_CLOCK,
  SCREEN_WEATHER,
  SCREEN_CHART,
  SCREEN_SETTINGS,
  SCREEN_INFO,
  SCREEN_DEBUG,
  SCREEN_WIFI,
  SCREEN_RECOVERY_CONFIRM
};

Screen currentScreen = SCREEN_HOME;
int    focusTile = 0;
int    lastSw    = HIGH;

// ---------- Данные ----------
float temperature = 0, humidity = 0, pressure = 0;
unsigned long bootTime = 0;

// ---------- BME280 ----------
void initBME() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (bme.begin(0x76) || bme.begin(0x77)) {
    bmeOK = true;
    Serial.println("BME280 OK");
  } else {
    bmeOK = false;
    Serial.println("BME280 not found");
  }
}

// ---------- Статус-бар ----------
void drawStatusBar() {
  spr.fillRect(0, 0, 320, STATUS_H, UI_BG);

  if (WiFi.status() == WL_CONNECTED) {
    iconWifi(spr, 18, 15, UI_GREEN);
  } else {
    iconWifi(spr, 18, 15, UI_TEXT_DIM);
  }

  spr.setTextColor(UI_TEXT, UI_BG);
  spr.setTextSize(1);
  spr.setCursor(42, 6);
  if (bmeOK) {
    spr.printf("%.1f`C", temperature);
  } else {
    spr.print("--.-`C");
  }

  unsigned long uptime = (millis() - bootTime) / 1000;
  int hh = (uptime / 3600) % 24;
  int mm = (uptime / 60) % 60;
  spr.setTextColor(UI_TEXT, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(230, 6);
  spr.printf("%02d:%02d", hh, mm);

  spr.drawFastHLine(0, STATUS_H - 1, 320, UI_TILE);
}

// ---------- Плитка ----------
void drawTile(int idx, bool selected) {
  int x = tileX(idx);
  int y = tileY(idx);

  uint16_t bg = selected ? UI_TILE_SEL : UI_TILE;
  spr.fillRoundRect(x, y, TILE_W, TILE_H, 10, bg);

  if (selected) {
    spr.drawRoundRect(x - 1, y - 1, TILE_W + 2, TILE_H + 2, 11, UI_ACCENT);
    spr.drawRoundRect(x - 2, y - 2, TILE_W + 4, TILE_H + 4, 12, UI_ACCENT);
  }

  drawTileIcon(spr, idx, x + TILE_W / 2, y + 28);

  spr.setTextColor(selected ? UI_TEXT : UI_TEXT_DIM, bg);
  spr.setTextSize(1);
  const char* label = TILES[idx].label;
  int tw = spr.textWidth(label);
  spr.setCursor(x + (TILE_W - tw) / 2, y + TILE_H - 14);
  spr.print(label);
}

void drawGrid() {
  for (int i = 0; i < TILE_COUNT; i++) {
    drawTile(i, i == focusTile);
  }
}

void drawDock() {
  spr.fillRect(0, DOCK_Y, 320, DOCK_H, UI_BG);
  spr.drawFastHLine(0, DOCK_Y, 320, UI_TILE);

  const char* items[] = {"Home", "Menu", "Power"};
  for (int i = 0; i < 3; i++) {
    int cx = 60 + i * 100;
    spr.setTextColor(UI_TEXT_DIM, UI_BG);
    spr.setTextSize(1);
    int tw = spr.textWidth(items[i]);
    spr.setCursor(cx - tw / 2, DOCK_Y + 10);
    spr.print(items[i]);
  }
}

void renderHome() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawGrid();
  drawDock();
}

void drawScreenHeader(const char* title) {
  spr.fillRect(0, STATUS_H, 320, 26, UI_BG);
  spr.drawFastHLine(0, STATUS_H + 26, 320, UI_TILE);

  spr.setTextColor(UI_ACCENT, UI_BG);
  spr.setTextSize(1);
  spr.setCursor(10, STATUS_H + 10);
  spr.print("SW: back");

  spr.setTextColor(UI_TEXT, UI_BG);
  spr.setTextSize(2);
  int tw = spr.textWidth(title);
  spr.setCursor((320 - tw) / 2, STATUS_H + 8);
  spr.print(title);
}

void renderClock() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Clock");

  unsigned long uptime = (millis() - bootTime) / 1000;
  int hh = (uptime / 3600) % 24;
  int mm = (uptime / 60) % 60;
  int ss = uptime % 60;

  spr.setTextColor(UI_CYAN, UI_BG);
  spr.setTextSize(7);
  char buf[16];
  sprintf(buf, "%02d:%02d", hh, mm);
  int tw = spr.textWidth(buf);
  spr.setCursor((320 - tw) / 2, 100);
  spr.print(buf);

  spr.setTextSize(2);
  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  sprintf(buf, ":%02d", ss);
  tw = spr.textWidth(buf);
  spr.setCursor((320 - tw) / 2, 175);
  spr.print(buf);
}

void renderWeather() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Weather");

  if (!bmeOK) {
    spr.setTextSize(2);
    spr.setTextColor(UI_RED, UI_BG);
    int tw = spr.textWidth("BME280 offline");
    spr.setCursor((320 - tw) / 2, 110);
    spr.print("BME280 offline");
    return;
  }

  spr.setTextColor(UI_ORANGE, UI_BG);
  spr.setTextSize(5);
  char buf[16];
  sprintf(buf, "%.1f", temperature);
  spr.setCursor(20, 90);
  spr.print(buf);
  spr.setTextSize(2);
  spr.print(" C");

  spr.setTextColor(UI_CYAN, UI_BG);
  spr.setTextSize(3);
  spr.setCursor(20, 155);
  spr.printf("H %.1f %%", humidity);

  spr.setTextColor(UI_GREEN, UI_BG);
  spr.setCursor(20, 190);
  spr.printf("P %.1f hP", pressure);
}

void renderChart() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Chart");

  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(50, 110);
  spr.print("Coming soon");
}

void renderSettings() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Settings");

  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(20, 90);
  spr.print("Recalibrate joystick?");
  spr.setTextSize(1);
  spr.setCursor(20, 130);
  spr.print("Press SW to recalibrate");
}

void renderInfo() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Info");

  spr.setTextSize(1);
  spr.setTextColor(UI_TEXT, UI_BG);

  int y = 75;
  int line = 14;
  spr.setCursor(15, y);          spr.printf("Chip:    ESP32-S3 N16R8");
  spr.setCursor(15, y += line);  spr.printf("Flash:   16 MB");
  spr.setCursor(15, y += line);  spr.printf("PSRAM:   %u KB free", ESP.getFreePsram() / 1024);
  spr.setCursor(15, y += line);  spr.printf("Heap:    %u KB free", ESP.getFreeHeap() / 1024);
  spr.setCursor(15, y += line);  spr.printf("Uptime:  %lu sec", (millis() - bootTime) / 1000);
  spr.setCursor(15, y += line);  spr.printf("IP:      %s",
    WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "--");
  spr.setCursor(15, y += line);  spr.printf("Build:   %s %s", __DATE__, __TIME__);
}

void renderDebug() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Debug");

  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(30, 110);
  spr.print("Serial: on");
}

void renderWiFi() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("WiFi");

  spr.setTextSize(1);
  spr.setTextColor(UI_TEXT, UI_BG);
  int y = 80;
  if (WiFi.status() == WL_CONNECTED) {
    spr.setTextColor(UI_GREEN, UI_BG);
    spr.setCursor(15, y); spr.print("Connected");
    spr.setTextColor(UI_TEXT, UI_BG);
    spr.setCursor(15, y += 20); spr.printf("IP:   %s", WiFi.localIP().toString().c_str());
    spr.setCursor(15, y += 15); spr.printf("RSSI: %d dBm", WiFi.RSSI());
    spr.setCursor(15, y += 15); spr.printf("SSID: %s", WiFi.SSID().c_str());
  } else {
    spr.setTextColor(UI_RED, UI_BG);
    spr.setCursor(15, y); spr.print("Not connected");
  }
}

void renderRecoveryConfirm() {
  spr.fillSprite(UI_BG);
  drawStatusBar();
  drawScreenHeader("Recovery");

  spr.setTextColor(UI_YELLOW, UI_BG);
  spr.setTextSize(2);
  spr.setCursor(30, 80);
  spr.print("Reboot to");
  spr.setCursor(30, 105);
  spr.print("PRELOADER?");

  spr.setTextColor(UI_TEXT_DIM, UI_BG);
  spr.setTextSize(1);
  spr.setCursor(30, 140);
  spr.print("SW: confirm");

  spr.fillRoundRect(200, 150, 100, 40, 8, UI_RED);
  spr.setTextColor(UI_TEXT, UI_RED);
  spr.setTextSize(2);
  int tw = spr.textWidth("GO");
  spr.setCursor(200 + (100 - tw) / 2, 160);
  spr.print("GO");
}

void renderScreen() {
  switch (currentScreen) {
    case SCREEN_HOME:             renderHome(); break;
    case SCREEN_CLOCK:            renderClock(); break;
    case SCREEN_WEATHER:          renderWeather(); break;
    case SCREEN_CHART:            renderChart(); break;
    case SCREEN_SETTINGS:         renderSettings(); break;
    case SCREEN_INFO:             renderInfo(); break;
    case SCREEN_DEBUG:            renderDebug(); break;
    case SCREEN_WIFI:             renderWiFi(); break;
    case SCREEN_RECOVERY_CONFIRM: renderRecoveryConfirm(); break;
  }
  spr.pushSprite(0, 0);
}

void updateSensors() {
  if (!bmeOK) return;
  temperature = bme.readTemperature();
  humidity    = bme.readHumidity();
  pressure    = bme.readPressure() / 100.0F;
}

void rebootToPreloader() {
  Serial.println("Reboot to preloader...");
  const esp_partition_t* factory = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (factory) {
    esp_ota_set_boot_partition(factory);
    delay(200);
    esp_restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== MAIN SYSTEM (UI) ===");
  bootTime = millis();

  // SW удержание >3 сек → preloader
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  delay(50);
  if (digitalRead(JOY_SW_PIN) == LOW) {
    unsigned long t0 = millis();
    bool held = true;
    while (millis() - t0 < 3000) {
      if (digitalRead(JOY_SW_PIN) == HIGH) { held = false; break; }
      delay(20);
    }
    if (held) {
      Serial.println("SW held 3s -> PRELOADER");
      rebootToPreloader();
    }
  }

  pixel.begin(); pixel.setBrightness(64);
  pixel.setPixelColor(0, pixel.Color(0, 0, 255)); pixel.show();

  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(UI_BG);

  spr.setPsram(true);
  spr.setColorDepth(16);
  spr.createSprite(320, 240);

  // Загрузка калибровки джойстика
  loadJoyCal();
  if (!joyCal.valid) {
    Serial.println("No joystick calibration -> running wizard");
    runCalibration(spr);
  } else {
    Serial.printf("JoyCal loaded: X c=%d r=%d..%d, Y c=%d r=%d..%d, dz=%d%%\n",
                  joyCal.xCenter, joyCal.xMin, joyCal.xMax,
                  joyCal.yCenter, joyCal.yMin, joyCal.yMax,
                  joyCal.deadzonePercent);
  }

  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ_HZ, BUZZER_LEDC_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, 0);

  initBME();

  WiFi.mode(WIFI_STA);

  renderScreen();
  Serial.println("UI ready");
}

void loop() {
  int jx = joyXNorm();
  int jy = joyYNorm();
  int sw = digitalRead(JOY_SW_PIN);

  bool redraw = false;
  const int TRIG = 40;

  // ---- Навигация ----
  if (currentScreen == SCREEN_HOME) {
    if (jx < -TRIG) {
      int col = focusTile % TILE_COLS;
      if (col > 0) { focusTile--; redraw = true; }
      delay(200);
    } else if (jx > TRIG) {
      int col = focusTile % TILE_COLS;
      if (col < TILE_COLS - 1) { focusTile++; redraw = true; }
      delay(200);
    }
    if (jy < -TRIG) {
      if (focusTile >= TILE_COLS) { focusTile -= TILE_COLS; redraw = true; }
      delay(200);
    } else if (jy > TRIG) {
      if (focusTile < TILE_COUNT - TILE_COLS) { focusTile += TILE_COLS; redraw = true; }
      delay(200);
    }
  }

  // ---- SW ----
  if (lastSw == HIGH && sw == LOW) {
    delay(30);
    if (digitalRead(JOY_SW_PIN) == LOW) {
      ledcWriteTone(BUZZER_CHANNEL, 2000);
      delay(30);
      ledcWriteTone(BUZZER_CHANNEL, 0);

      if (currentScreen == SCREEN_HOME) {
        switch (focusTile) {
          case TILE_CLOCK:    currentScreen = SCREEN_CLOCK; break;
          case TILE_WEATHER:  currentScreen = SCREEN_WEATHER; break;
          case TILE_CHART:    currentScreen = SCREEN_CHART; break;
          case TILE_SETTINGS: currentScreen = SCREEN_SETTINGS; break;
          case TILE_INFO:     currentScreen = SCREEN_INFO; break;
          case TILE_DEBUG:    currentScreen = SCREEN_DEBUG; break;
          case TILE_WIFI:     currentScreen = SCREEN_WIFI; break;
          case TILE_RECOVERY: currentScreen = SCREEN_RECOVERY_CONFIRM; break;
        }
      } else if (currentScreen == SCREEN_RECOVERY_CONFIRM) {
        rebootToPreloader();
      } else if (currentScreen == SCREEN_SETTINGS) {
        runCalibration(spr);
        currentScreen = SCREEN_HOME;
      } else {
        currentScreen = SCREEN_HOME;
      }
      redraw = true;
      while (digitalRead(JOY_SW_PIN) == LOW) delay(10);
    }
  }
  lastSw = sw;

  // ---- Обновление датчиков ----
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 250) {
    lastUpdate = millis();
    updateSensors();
    if (currentScreen == SCREEN_HOME ||
        currentScreen == SCREEN_WEATHER ||
        currentScreen == SCREEN_CLOCK ||
        currentScreen == SCREEN_INFO ||
        currentScreen == SCREEN_WIFI) {
      redraw = true;
    }
  }

  if (redraw) {
    renderScreen();
  }

  delay(20);
}