#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <Adafruit_NeoPixel.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// ---------- Пины ----------
#define TFT_BL_PIN       16
#define JOY_VRX_PIN      9
#define JOY_VRY_PIN      10
#define JOY_SW_PIN       11
#define BUZZER_PIN       17
#define NEOPIXEL_PIN     48
#define I2C_SDA          18
#define I2C_SCL          8
#define BOOT_BTN         0
#define BUZZER_CHANNEL   0

// ---------- Причины ----------
enum Reason {
  R_NONE = 0,
  R_USER_REQUEST,
  R_TFT_FAIL,
  R_I2C_FAIL,
  R_BME_FAIL,
  R_JOY_FAIL,
  R_BUZZER_FAIL,
  R_LITTLEFS_FAIL,
  R_NO_OTA_PARTITION
};

const char* reasonStr[] = {
  "none", "User button", "TFT fail", "I2C fail", "BME280 fail",
  "Joystick fail", "Buzzer fail", "LittleFS fail", "No OTA part"
};

bool isCritical(Reason r) {
  return (r == R_TFT_FAIL || r == R_LITTLEFS_FAIL || r == R_NO_OTA_PARTITION);
}

// ---------- LovyanGFX ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST; cfg.spi_mode = 0;
      cfg.freq_write = 20000000; cfg.freq_read = 10000000;
      cfg.spi_3wire = true; cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 4; cfg.pin_mosi = 5; cfg.pin_miso = -1; cfg.pin_dc = 7;
      _bus_instance.config(cfg); _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs = 6; cfg.pin_rst = 15; cfg.pin_busy = -1;
      cfg.panel_width = 240; cfg.panel_height = 320;
      cfg.offset_rotation = 0;
      cfg.readable = false; cfg.invert = false; cfg.rgb_order = false;
      cfg.dlen_16bit = false; cfg.bus_shared = false;
      _panel_instance.config(cfg); }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

// ---------- Лог ----------
#define LOG_LINES 20
String logLines[LOG_LINES];
int    logCount = 0;

void logAdd(const String& s) {
  Serial.println(s);
  if (logCount < LOG_LINES) {
    logLines[logCount++] = s;
  } else {
    for (int i = 1; i < LOG_LINES; i++) logLines[i-1] = logLines[i];
    logLines[LOG_LINES-1] = s;
  }
}

void showLog() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  int y = 4;
  for (int i = 0; i < logCount; i++) {
    tft.setCursor(4, y);
    tft.println(logLines[i]);
    y += 10;
    if (y > tft.height() - 20) break;
  }
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(4, tft.height() - 15);
  tft.println("SW to return");
  while (digitalRead(JOY_SW_PIN) == HIGH) delay(20);
  while (digitalRead(JOY_SW_PIN) == LOW)  delay(20);
}

// ---------- Config ----------
struct Config { String ssid, pass; int brightness = 200; } cfg;

bool loadConfig() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) { logAdd("config.json not found"); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { logAdd(String("JSON err: ") + err.c_str()); return false; }
  cfg.ssid       = doc["wifi_ssid"]  | "";
  cfg.pass       = doc["wifi_pass"]  | "";
  cfg.brightness = doc["brightness"] | 200;
  logAdd("SSID: [" + cfg.ssid + "]");
  return true;
}

// ---------- LED ----------
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b)); pixel.show();
}

// ---------- Recovery UI ----------
int recoverySelection = 0;

void drawRecoveryUI(Reason r, const String& detail, const String& ip,
                    const String& wifiStatus, bool allowSkip) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.println("RECOVERY");
  tft.drawFastHLine(0, 45, tft.width(), TFT_DARKGREY);

  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 55);
  tft.printf("Reason: %s", reasonStr[r]);

  if (detail.length()) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(10, 85);
    tft.printf("%s", detail.c_str());
  }

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 120);
  tft.printf("WiFi: %s", wifiStatus.c_str());

  tft.setCursor(10, 150);
  tft.printf("IP: %s", ip.c_str());

  int btnY = 200;
  int skipX = 20;
  int logX  = 180;
  int bw = 120, bh = 40;

  if (allowSkip) {
    uint16_t skipColor = (recoverySelection == 0) ? TFT_GREEN : TFT_DARKGREEN;
    tft.fillRoundRect(skipX, btnY, bw, bh, 8, skipColor);
    tft.setTextColor(TFT_BLACK, skipColor);
    tft.setTextSize(2);
    tft.setCursor(skipX + 30, btnY + 12);
    tft.print("SKIP");
  } else {
    tft.fillRoundRect(skipX, btnY, bw, bh, 8, TFT_DARKGREY);
    tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setCursor(skipX + 10, btnY + 15);
    tft.print("SKIP (critical)");
  }

  uint16_t logColor = (recoverySelection == 1) ? TFT_ORANGE : TFT_DARKGREY;
  tft.fillRoundRect(logX, btnY, bw, bh, 8, logColor);
  tft.setTextColor(TFT_BLACK, logColor);
  tft.setTextSize(2);
  tft.setCursor(logX + 35, btnY + 12);
  tft.print("LOG");
}

// ---------- Recovery Trigger (5-секундный таймаут) ----------
bool checkRecoveryTrigger() {
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  delay(50);

  // Звуковой сигнал старта
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 15);
  tft.println("PRELOADER");
  tft.drawFastHLine(0, 55, tft.width(), TFT_DARKGREY);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 75);
  tft.println("Press JOY SW for");
  tft.setCursor(10, 105);
  tft.println("RECOVERY mode");

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 150);
  tft.println("Auto-boot in:");

  for (int i = 5; i > 0; i--) {
    tft.fillRect(20, 180, 120, 55, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(6);
    tft.setCursor(40, 180);
    tft.printf("%d", i);

    // Один короткий писк на каждой секунде
    ledcWriteTone(BUZZER_CHANNEL, 1500);
    delay(30);
    ledcWriteTone(BUZZER_CHANNEL, 0);

    unsigned long t0 = millis();
    while (millis() - t0 < 1000) {
      if (digitalRead(JOY_SW_PIN) == LOW) {
        Serial.println("JOY_SW -> RECOVERY");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(3);
        tft.setCursor(30, 100);
        tft.println("RECOVERY!");
        ledcWriteTone(BUZZER_CHANNEL, 2500);
        delay(200);
        ledcWriteTone(BUZZER_CHANNEL, 0);
        delay(300);
        return true;
      }
      if (digitalRead(BOOT_BTN) == LOW) {
        Serial.println("BOOT -> RECOVERY");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(3);
        tft.setCursor(30, 100);
        tft.println("RECOVERY!");
        ledcWriteTone(BUZZER_CHANNEL, 2500);
        delay(200);
        ledcWriteTone(BUZZER_CHANNEL, 0);
        delay(300);
        return true;
      }
      delay(20);
    }
  }

  Serial.println("Timeout -> boot main");
  return false;
}

// ---------- Проверки ----------
Reason runHealthChecks(String& detail) {
  logAdd("[CHECK] TFT...");
  tft.init();
  if (tft.width() == 0 || tft.height() == 0) {
    detail = "TFT not responding";
    return R_TFT_FAIL;
  }
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  logAdd("  TFT OK");

  logAdd("[CHECK] I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setTimeOut(50);
  byte count = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      logAdd(String("  device @ 0x") + String(a, HEX));
      count++;
    }
  }
  if (count == 0) { detail = "No I2C devices"; return R_I2C_FAIL; }

  logAdd("[CHECK] BME280...");
  bool bmeOk = false;
  Wire.beginTransmission(0x76);
  if (Wire.endTransmission() == 0) bmeOk = true;
  Wire.beginTransmission(0x77);
  if (Wire.endTransmission() == 0) bmeOk = true;
  if (!bmeOk) { detail = "BME280 not found"; return R_BME_FAIL; }
  logAdd("  BME280 OK");

  logAdd("[CHECK] Joystick...");
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  delay(20);
  int x = analogRead(JOY_VRX_PIN);
  int y = analogRead(JOY_VRY_PIN);
  logAdd(String("  X=") + x + " Y=" + y);
  if ((x < 100 && y < 100) || (x > 3995 && y > 3995)) {
    detail = "Joystick stuck";
    return R_JOY_FAIL;
  }

  logAdd("[CHECK] Buzzer...");
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, 0);

  logAdd("[CHECK] LittleFS...");
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    detail = "LittleFS mount fail";
    return R_LITTLEFS_FAIL;
  }

  return R_NONE;
}

// ---------- bootMain ----------
void bootMain() {
  const esp_partition_t* p = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  if (!p) { logAdd("ota_0 not found!"); esp_restart(); }
  logAdd("Booting main...");
  esp_ota_set_boot_partition(p);
  delay(200);
  esp_restart();
}

// ---------- WiFi ----------
String connectWiFi() {
  logAdd("=== WiFi CONNECT ===");
  logAdd("SSID: [" + cfg.ssid + "]");

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true);
  delay(200);

  logAdd("Scanning...");
  int n = WiFi.scanNetworks();
  logAdd("Found " + String(n) + " networks");
  bool found = false;
  for (int i = 0; i < n; i++) {
    String s = "  " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)";
    if (WiFi.SSID(i) == cfg.ssid) { s += " <-- TARGET"; found = true; }
    logAdd(s);
  }
  if (!found) logAdd("WARNING: target SSID not found!");

  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    logAdd("Connected! IP: " + WiFi.localIP().toString());
    return "OK";
  }

  int st = WiFi.status();
  if (st == WL_NO_SSID_AVAIL)  return "NO_SSID";
  if (st == WL_CONNECT_FAILED) return "BAD_PASS";
  return "FAIL(" + String(st) + ")";
}

// ---------- OTA ----------
void setupOTA() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
      "<h2>Recovery OTA</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='firmware'><input type='submit' value='Upload'>"
      "</form>");
  });
  server.on("/update", HTTP_POST,
    []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      delay(1000); esp_restart();
    },
    []() {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
      else if (up.status == UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
      else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          const esp_partition_t* p = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
          esp_ota_set_boot_partition(p);
        }
      }
    });
  server.begin();
  logAdd("OTA: http://" + WiFi.localIP().toString());
}

// ---------- Recovery ----------
void gotoRecovery(Reason r, const String& detail) {
  setLed(255, 100, 0);

  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    logAdd("LittleFS failed in recovery");
  }
  loadConfig();

  bool allowSkip = !isCritical(r);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  String ipStr = "-";
  String wifiStatus = "connecting";
  bool wifiOK = false;

  recoverySelection = 0;
  drawRecoveryUI(r, detail, ipStr, wifiStatus, allowSkip);

  wifiStatus = connectWiFi();
  wifiOK = (wifiStatus == "OK");
  if (wifiOK) {
    ipStr = WiFi.localIP().toString();
    setLed(0, 255, 0);
    setupOTA();
  } else {
    setLed(255, 0, 0);
  }
  drawRecoveryUI(r, detail, ipStr, wifiStatus, allowSkip);

  int lastX = analogRead(JOY_VRX_PIN);
  int lastSW = HIGH;
  int lastBoot = HIGH;

  while (true) {
    if (wifiOK) server.handleClient();

    int x = analogRead(JOY_VRX_PIN);
    int sw = digitalRead(JOY_SW_PIN);
    int bootBtn = digitalRead(BOOT_BTN);

    // Влево/вправо — выбор
    if (lastX >= 2000 && x < 1000) {
      recoverySelection = 0;
      drawRecoveryUI(r, detail, ipStr, wifiStatus, allowSkip);
    }
    if (lastX <= 2000 && x > 3000) {
      recoverySelection = 1;
      drawRecoveryUI(r, detail, ipStr, wifiStatus, allowSkip);
    }
    lastX = x;

    // BOOT = пропуск = boot main
    if (lastBoot == HIGH && bootBtn == LOW) {
      logAdd("BOOT -> boot main");
      delay(50);
      if (digitalRead(BOOT_BTN) == LOW) {
        bootMain();
      }
    }
    lastBoot = bootBtn;

    // SW — навигация
    if (lastSW == HIGH && sw == LOW) {
      delay(20);
      if (digitalRead(JOY_SW_PIN) == LOW) {
        if (recoverySelection == 1) {
          showLog();
          drawRecoveryUI(r, detail, ipStr, wifiStatus, allowSkip);
        } else if (recoverySelection == 0 && allowSkip) {
          logAdd("User pressed SKIP");
          bootMain();
        }
        while (digitalRead(JOY_SW_PIN) == LOW) delay(10);
      }
    }
    lastSW = sw;

    delay(20);
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== PRELOADER ===");
  logAdd("=== PRELOADER ===");

  pixel.begin(); pixel.setBrightness(64); setLed(0, 0, 255);

  pinMode(TFT_BL_PIN, OUTPUT); digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(1);

  // ★ 5-секундный таймаут: SW или BOOT → recovery
  if (checkRecoveryTrigger()) {
    logAdd("User requested RECOVERY");
    gotoRecovery(R_USER_REQUEST, "User requested recovery");
  }

  String detail;
  Reason r = runHealthChecks(detail);

  if (r == R_NONE) {
    logAdd("All checks OK -> boot main");
    setLed(0, 255, 0);
    delay(300);
    bootMain();
  }

  logAdd(String("Health fail: ") + reasonStr[r] + " (" + detail + ")");
  gotoRecovery(r, detail);
}

void loop() {
  delay(1000);
}