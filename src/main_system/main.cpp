#include <Arduino.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_NeoPixel.h>

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
      cfg.freq_write = 20000000; cfg.freq_read = 10000000;
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
LGFX_Sprite sprite(&tft);
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_BME280 bme;

int joyX = 0, joyY = 0;
bool joyBtn = false, lastJoyBtn = false;
float temperature = 0, humidity = 0, pressure = 0;
unsigned long lastUpdate = 0;
const unsigned long UPDATE_MS = 100;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== MAIN SYSTEM ===");

  // ★ Следующий reset -> снова preloader (factory)
  const esp_partition_t* factory = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (factory) {
    esp_ota_set_boot_partition(factory);
    Serial.println("Next boot -> factory (preloader)");
  } else {
    Serial.println("WARNING: factory partition not found!");
  }

  pixel.begin(); pixel.setBrightness(64);
  pixel.setPixelColor(0, pixel.Color(0, 0, 255)); pixel.show();

  pinMode(TFT_BL_PIN, OUTPUT); digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  sprite.setPsram(true);
  sprite.setColorDepth(16);
  sprite.createSprite(320, 200);
  sprite.fillSprite(TFT_BLACK);

  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ_HZ, BUZZER_LEDC_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, 0);

  Wire.begin(I2C_SDA, I2C_SCL);
  bme.begin(0x76);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("BME280 + Joystick");
  tft.drawFastHLine(0, 35, tft.width(), TFT_DARKGREY);
}

void loop() {
  joyX = analogRead(JOY_VRX_PIN);
  joyY = analogRead(JOY_VRY_PIN);
  joyBtn = (digitalRead(JOY_SW_PIN) == LOW);

  if (joyBtn && !lastJoyBtn) {
    ledcWriteTone(BUZZER_CHANNEL, 1000); delay(80);
    ledcWriteTone(BUZZER_CHANNEL, 0);
  }
  lastJoyBtn = joyBtn;

  temperature = bme.readTemperature();
  humidity    = bme.readHumidity();
  pressure    = bme.readPressure() / 100.0F;

  if (millis() - lastUpdate >= UPDATE_MS) {
    lastUpdate = millis();
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextColor(TFT_CYAN, TFT_BLACK);
    sprite.setTextSize(2);
    sprite.setCursor(10, 20);  sprite.printf("Temp: %.1f C", temperature);
    sprite.setCursor(10, 50);  sprite.printf("Hum:  %.1f %%", humidity);
    sprite.setCursor(10, 80);  sprite.printf("Press: %.1f hPa", pressure);
    sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    sprite.setTextSize(3);
    sprite.setCursor(10, 120); sprite.printf("X: %4d", joyX);
    sprite.setCursor(10, 160); sprite.printf("Y: %4d", joyY);
    sprite.setTextSize(2);
    sprite.setTextColor(joyBtn ? TFT_GREEN : TFT_RED, TFT_BLACK);
    sprite.setCursor(10, 200);
    sprite.printf("SW: %-8s", joyBtn ? "PRESSED" : "released");
    sprite.pushSprite(0, 40);
  }
  delay(20);
}