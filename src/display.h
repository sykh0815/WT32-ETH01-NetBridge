// Kleine Display-Treiber ohne externe Bibliothek:
//  - OledGfx: 128x64-OLED mit SSD1306/SSD1315 an I2C (1 Bit pro Pixel, 1 KB)
//  - TftGfx:  Farb-TFT mit ST7735 an SPI (160x128, 128x128 oder 160x80; 4 Bit pro Pixel mit Farbpalette, max. 10 KB)
// Beide zeichnen in einen Bildspeicher im RAM; show() uebertraegt ihn. Schrift: proportional 5x7 (oled_font.h).
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "oled_font.h"

// Farben als Rolle; das OLED kennt nur an/aus, das TFT setzt sie ueber eine Palette um
enum GfxColor : uint8_t { C_BLACK = 0, C_WHITE, C_GREY, C_MODE, C_DOWN, C_UP, C_RED, C_GREEN, C_HEADTEXT, C_COUNT };

class Gfx {
 public:
  virtual ~Gfx() {}
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void clear() = 0;
  virtual void show() = 0;
  virtual void power(bool on) = 0;
  virtual void dim(bool dimmed) = 0;
  virtual void setBrightness(uint8_t percent) = 0;  // 10..100 %; "abgedunkelt" ist ein Viertel davon
  virtual bool isColor() const { return false; }

  void setOffset(int x, int y) { offsetX_ = x; offsetY_ = y; }  // Einbrennschutz (OLED) bzw. Zentrierung

  void pixel(int x, int y, uint8_t color) {
    x += offsetX_;
    y += offsetY_;
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    put(x, y, color);
  }
  void fillRect(int x, int y, int w, int h, uint8_t color) {
    for (int yy = y; yy < y + h; ++yy)
      for (int xx = x; xx < x + w; ++xx) pixel(xx, yy, color);
  }
  void rect(int x, int y, int w, int h, uint8_t color) {
    for (int xx = x; xx < x + w; ++xx) { pixel(xx, y, color); pixel(xx, y + h - 1, color); }
    for (int yy = y; yy < y + h; ++yy) { pixel(x, yy, color); pixel(x + w - 1, yy, color); }
  }
  void hline(int x, int y, int w, uint8_t color, bool dotted = false) {
    for (int xx = x; xx < x + w; ++xx)
      if (!dotted || (xx & 1) == 0) pixel(xx, y, color);
  }

  // Text ab (x, y) = linke obere Ecke; scale 1 = 7 Pixel hoch. Liefert die x-Position danach.
  int text(int x, int y, const char *s, int scale, uint8_t color) {
    while (*s) {
      const int index = nextGlyph(s);
      if (index < 0) continue;
      const uint8_t *glyph = OLED_FONT[index];
      const int w = glyph[5];
      for (int col = 0; col < w; ++col)
        for (int row = 0; row < 8; ++row)
          if (glyph[col] & (1 << row)) fillRect(x + col * scale, y + row * scale, scale, scale, color);
      x += (w + 1) * scale;
    }
    return x;
  }
  static int textWidth(const char *s, int scale = 1) {
    int w = 0;
    while (*s) {
      const int index = nextGlyph(s);
      if (index >= 0) w += (OLED_FONT[index][5] + 1) * scale;
    }
    return w > 0 ? w - scale : 0;
  }

 protected:
  virtual void put(int x, int y, uint8_t color) = 0;

  // Liest ein UTF-8-Zeichen und liefert den Index in OLED_FONT (-1 = nicht darstellbar)
  static int nextGlyph(const char *&s) {
    uint32_t code = static_cast<uint8_t>(*s++);
    if (code >= 0xE0 && s[0] && s[1]) {
      code = ((code & 0x0F) << 12) | ((static_cast<uint8_t>(s[0]) & 0x3F) << 6) | (static_cast<uint8_t>(s[1]) & 0x3F);
      s += 2;
    } else if (code >= 0xC0 && s[0]) {
      code = ((code & 0x1F) << 6) | (static_cast<uint8_t>(*s++) & 0x3F);
    }
    if (code >= OLED_FONT_FIRST && code <= 126) return code - OLED_FONT_FIRST;
    switch (code) {
      case 0xE4: return 95;     // ä
      case 0xF6: return 96;     // ö
      case 0xFC: return 97;     // ü
      case 0xC4: return 98;     // Ä
      case 0xD6: return 99;     // Ö
      case 0xDC: return 100;    // Ü
      case 0xDF: return 101;    // ß
      case 0x2193: return 102;  // ↓
      case 0x2191: return 103;  // ↑
      case 0xB7: return 104;    // ·
      default: return code >= 0x80 ? '?' - OLED_FONT_FIRST : -1;
    }
  }

  int offsetX_ = 0;
  int offsetY_ = 0;
};

// ---------------------------------------------------------------------------
// OLED 128x64, SSD1306/SSD1315, I2C
// ---------------------------------------------------------------------------
class OledGfx : public Gfx {
 public:
  // Sucht das Display an Adresse 0x3C bzw. 0x3D und initialisiert es. false = kein Display gefunden.
  bool begin(int sdaPin, int sclPin) {
    Wire.begin(sdaPin, sclPin, 400000);
    Wire.setTimeOut(20);
    const uint8_t candidates[] = {0x3C, 0x3D};
    for (uint8_t candidate : candidates) {
      Wire.beginTransmission(candidate);
      if (Wire.endTransmission() == 0) {
        address_ = candidate;
        break;
      }
    }
    if (address_ == 0) return false;
    static const uint8_t init[] = {
      0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
      0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF,
    };
    for (uint8_t c : init) command(c);
    clear();
    show();
    return true;
  }
  bool present() const { return address_ != 0; }
  uint8_t address() const { return address_; }
  void rotate180(bool rotated) { command(rotated ? 0xA0 : 0xA1); command(rotated ? 0xC0 : 0xC8); }
  const uint8_t *data() const { return buffer_; }

  int width() const override { return 128; }
  int height() const override { return 64; }
  void clear() override { memset(buffer_, 0, sizeof(buffer_)); }
  void power(bool on) override { command(on ? 0xAF : 0xAE); }
  void dim(bool dimmed) override { dimmed_ = dimmed; applyContrast(); }
  void setBrightness(uint8_t percent) override { brightness_ = percent; applyContrast(); }
  void show() override {
    command(0x20); command(0x00);                    // Adressierung bei jedem Bild erneut setzen (robust gegen Stoerungen)
    command(0x21); command(0); command(127);
    command(0x22); command(0); command(7);
    for (size_t pos = 0; pos < sizeof(buffer_); pos += 16) {
      Wire.beginTransmission(address_);
      Wire.write(0x40);
      Wire.write(buffer_ + pos, 16);
      Wire.endTransmission();
    }
  }

 protected:
  void put(int x, int y, uint8_t color) override {
    uint8_t &cell = buffer_[x + (y / 8) * 128];
    if (color != C_BLACK && color != C_HEADTEXT) cell |= 1 << (y & 7);  // Kopfzeile: dunkle Schrift auf hellem Balken
    else cell &= ~(1 << (y & 7));
  }

 private:
  void applyContrast() {  // Kontrast 1..255 (das OLED regelt damit den Strom der Pixel)
    int value = brightness_ * 255 / 100;
    if (dimmed_) value /= 4;
    command(0x81);
    command(static_cast<uint8_t>(value < 1 ? 1 : value > 255 ? 255 : value));
  }
  void command(uint8_t value) {
    if (address_ == 0) return;
    Wire.beginTransmission(address_);
    Wire.write(0x00);
    Wire.write(value);
    Wire.endTransmission();
  }
  uint8_t address_ = 0;
  uint8_t brightness_ = 100;
  bool dimmed_ = false;
  uint8_t buffer_[128 * 64 / 8] = {};
};

// ---------------------------------------------------------------------------
// TFT mit ST7735, SPI (nur Schreiben). Querformat.
// ---------------------------------------------------------------------------
struct TftConfig {
  int width = 160;       // sichtbare Breite im Querformat
  int height = 128;
  int offsetX = 0;       // Versatz im Speicher des Controllers (je nach Modul)
  int offsetY = 0;
  bool bgr = false;      // Rot und Blau vertauscht
  bool invert = false;   // Farben invertiert (viele 0,96-Zoll-IPS-Module)
  bool rotate180 = false;
};

class TftGfx : public Gfx {
 public:
  static constexpr uint32_t SPI_HZ = 8000000;  // 8 MHz: sicher auch mit Steckbrett-Kabeln

  // backlight: Pin fuer die Hintergrundbeleuchtung (PWM), -1 = fest an 3V3 (dann Helligkeit ueber die Farben)
  bool begin(const TftConfig &config, int sck, int mosi, int dc, int cs, int rst, int backlight = -1) {
    config_ = config;
    if (config_.width < 16 || config_.height < 16 || config_.width > 160 || config_.height > 160) return false;
    buffer_ = static_cast<uint8_t *>(malloc(config_.width * config_.height / 2));
    if (buffer_ == nullptr) return false;
    dc_ = dc;
    cs_ = cs;
    rst_ = rst;
    pinMode(dc_, OUTPUT);
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    if (rst_ >= 0) pinMode(rst_, OUTPUT);
    backlight_ = backlight;
    if (backlight_ >= 0) {
      ledcAttach(backlight_, 5000, 8);  // 5 kHz, 8 Bit
      applyBrightness();
    }
    spi_ = new SPIClass(HSPI);
    spi_->begin(sck, -1, mosi, -1);
    setPalette(rgb565(0x1263A6));
    initDisplay();
    return true;
  }

  // Neue Einstellungen (Drehung, Versatz, Farben) ohne Neustart uebernehmen; die Groesse bleibt gleich
  void reconfigure(const TftConfig &config) {
    if (!buffer_) return;
    const int w = config_.width, h = config_.height;
    config_ = config;
    config_.width = w;
    config_.height = h;
    initDisplay();
  }

  // Regelmaessig aufrufen: setzt die wichtigsten Register erneut, falls das Display durch eine Stoerung
  // auf der Leitung (lange Kabel, Spannungseinbruch) in einen falschen Zustand geraten ist
  void keepAlive() {
    command(0x11);                                   // Schlafmodus aus
    command(config_.invert ? 0x21 : 0x20);
    command(0x13);                                   // Normalbetrieb
    command(0x29);                                   // Anzeige an
  }

  bool ready() const { return buffer_ != nullptr; }
  const uint8_t *data() const { return buffer_; }
  bool isColor() const override { return true; }
  int width() const override { return config_.width; }
  int height() const override { return config_.height; }
  void clear() override { if (buffer_) memset(buffer_, 0, config_.width * config_.height / 2); }
  void power(bool on) override { command(on ? 0x29 : 0x28); }
  void dim(bool dimmed) override { dimmed_ = dimmed; applyBrightness(); }
  void setBrightness(uint8_t percent) override { brightness_ = percent; applyBrightness(); }
  bool hasBacklight() const { return backlight_ >= 0; }
  uint16_t paletteValue(int index) const { return index >= 0 && index < C_COUNT ? scaled_[index] : 0; }

  static uint16_t rgb565(uint32_t rgb) {
    return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
  }
  // Farbe des Kopfbalkens (je Betriebsart)
  void setPalette(uint16_t modeColor) {
    palette_[C_BLACK] = 0x0000;
    palette_[C_WHITE] = 0xFFFF;
    palette_[C_GREY] = rgb565(0x8A96A3);
    palette_[C_MODE] = modeColor;
    palette_[C_DOWN] = rgb565(0x4AA8FF);
    palette_[C_UP] = rgb565(0xF5A524);
    palette_[C_RED] = rgb565(0xF0504A);
    palette_[C_GREEN] = rgb565(0x3DDC84);
    palette_[C_HEADTEXT] = 0xFFFF;
    applyBrightness();
  }

  void show() override {
    if (!buffer_) return;
    const uint16_t x0 = config_.offsetX, y0 = config_.offsetY;
    const uint8_t caset[] = {0, static_cast<uint8_t>(x0), 0, static_cast<uint8_t>(x0 + config_.width - 1)};
    const uint8_t raset[] = {0, static_cast<uint8_t>(y0), 0, static_cast<uint8_t>(y0 + config_.height - 1)};
    const uint8_t orientation[] = {madctl()};
    const uint8_t colmod[] = {0x05};
    command(0x36, orientation, 1);  // Ausrichtung und Farbformat bei jedem Bild erneut setzen (robust gegen Stoerungen)
    command(0x3A, colmod, 1);
    command(0x2A, caset, 4);
    command(0x2B, raset, 4);
    command(0x2C);
    uint8_t line[320];
    spi_->beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    for (int y = 0; y < config_.height; ++y) {
      for (int x = 0; x < config_.width; ++x) {
        const int index = y * config_.width + x;
        const uint8_t color = (buffer_[index / 2] >> ((index & 1) * 4)) & 0x0F;
        uint16_t value = scaled_[color < C_COUNT ? color : 0];
        line[x * 2] = value >> 8;
        line[x * 2 + 1] = value & 0xFF;
      }
      spi_->writeBytes(line, config_.width * 2);
    }
    digitalWrite(cs_, HIGH);
    spi_->endTransaction();
  }

 protected:
  void put(int x, int y, uint8_t color) override {
    const int index = y * config_.width + x;
    uint8_t &cell = buffer_[index / 2];
    if (index & 1) cell = (cell & 0x0F) | (color << 4);
    else cell = (cell & 0xF0) | (color & 0x0F);
  }

 private:
  // Mit PWM-Pin: Hintergrundbeleuchtung regeln. Ohne: Farben abdunkeln (Palette skalieren).
  void applyBrightness() {
    int level = brightness_;
    if (dimmed_) level /= 4;
    if (level < 2) level = 2;
    const int colorLevel = backlight_ >= 0 ? 100 : level;
    for (int i = 0; i < C_COUNT; ++i) {
      const uint16_t v = palette_[i];
      const uint16_t r = ((v >> 11) & 0x1F) * colorLevel / 100;
      const uint16_t g = ((v >> 5) & 0x3F) * colorLevel / 100;
      const uint16_t b = (v & 0x1F) * colorLevel / 100;
      scaled_[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
    if (backlight_ >= 0) ledcWrite(backlight_, static_cast<uint32_t>(level * 255 / 100));
  }

  uint8_t madctl() const {  // Querformat: MV + MX (bzw. MV + MY bei 180 Grad), dazu RGB/BGR
    return static_cast<uint8_t>((config_.rotate180 ? 0xA0 : 0x60) | (config_.bgr ? 0x08 : 0x00));
  }

  // Hardware-Reset und komplette Initialisierung
  void initDisplay() {
    if (rst_ >= 0) {
      digitalWrite(rst_, HIGH); delay(10);
      digitalWrite(rst_, LOW); delay(50);
      digitalWrite(rst_, HIGH); delay(150);
    }
    command(0x01); delay(150);                       // Software-Reset
    command(0x11); delay(200);                       // Schlafmodus aus
    const uint8_t frame[] = {0x01, 0x2C, 0x2D};
    command(0xB1, frame, 3);
    command(0xB2, frame, 3);
    const uint8_t frame3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    command(0xB3, frame3, 6);
    const uint8_t inv[] = {0x07};            command(0xB4, inv, 1);
    const uint8_t pw1[] = {0xA2, 0x02, 0x84}; command(0xC0, pw1, 3);
    const uint8_t pw2[] = {0xC5};             command(0xC1, pw2, 1);
    const uint8_t pw3[] = {0x0A, 0x00};       command(0xC2, pw3, 2);
    const uint8_t pw4[] = {0x8A, 0x2A};       command(0xC3, pw4, 2);
    const uint8_t pw5[] = {0x8A, 0xEE};       command(0xC4, pw5, 2);
    const uint8_t vm[] = {0x0E};              command(0xC5, vm, 1);
    command(config_.invert ? 0x21 : 0x20);
    const uint8_t mad[] = {madctl()};         command(0x36, mad, 1);
    const uint8_t colmod[] = {0x05};          command(0x3A, colmod, 1);  // 16 Bit pro Pixel
    const uint8_t gp[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    command(0xE0, gp, 16);
    const uint8_t gn[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
    command(0xE1, gn, 16);
    command(0x13); delay(10);                        // Normalbetrieb
    // erst ein schwarzes Bild schreiben, dann einschalten (sonst kurz weiss bzw. Bildreste)
    clear();
    show();
    command(0x29); delay(20);                        // Anzeige an
  }

  void command(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0) {
    if (!spi_) return;
    spi_->beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    digitalWrite(dc_, LOW);
    spi_->write(cmd);
    if (len) {
      digitalWrite(dc_, HIGH);
      spi_->writeBytes(data, len);
    }
    digitalWrite(cs_, HIGH);
    spi_->endTransaction();
  }

  TftConfig config_;
  SPIClass *spi_ = nullptr;
  uint8_t *buffer_ = nullptr;
  int dc_ = -1;
  int cs_ = -1;
  int rst_ = -1;
  bool dimmed_ = false;
  uint8_t brightness_ = 100;
  int backlight_ = -1;
  uint16_t palette_[C_COUNT] = {};
  uint16_t scaled_[C_COUNT] = {};
};
