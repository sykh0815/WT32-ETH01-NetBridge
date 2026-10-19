// Kleiner QR-Code-Generator (Byte-Modus, Fehlerkorrektur L, Version 1-6 = bis 134 Zeichen).
// Portiert und gekuerzt nach "QR Code generator library" von Project Nayuki (MIT-Lizenz),
// https://www.nayuki.io/page/qr-code-generator-library
#pragma once
#include <stdint.h>
#include <string.h>

class QrCode {
 public:
  static constexpr int MAX_SIZE = 41;  // Version 6

  // Erzeugt den Code fuer text. false = zu lang.
  bool encode(const char *text) {
    const int length = static_cast<int>(strlen(text));
    version_ = 0;
    for (int v = 1; v <= 6; ++v) {
      if (dataCodewords(v) * 8 >= 4 + 8 + length * 8) { version_ = v; break; }
    }
    if (version_ == 0) return false;
    size_ = version_ * 4 + 17;
    // Datenbits: Modus "Byte" (0100), Laenge (8 Bit), Daten, Abschluss, Fuellbytes
    const int capacity = dataCodewords(version_);
    uint8_t data[136] = {};
    int bit = 0;
    auto append = [&](uint32_t value, int count) {
      for (int i = count - 1; i >= 0; --i, ++bit)
        if ((value >> i) & 1) data[bit >> 3] |= 0x80 >> (bit & 7);
    };
    append(4, 4);
    append(static_cast<uint32_t>(length), 8);
    for (int i = 0; i < length; ++i) append(static_cast<uint8_t>(text[i]), 8);
    const int capacityBits = capacity * 8;
    append(0, capacityBits - bit < 4 ? capacityBits - bit : 4);
    append(0, (8 - bit % 8) % 8);
    for (uint8_t pad = 0xEC; bit < capacityBits; pad ^= 0xEC ^ 0x11) append(pad, 8);

    memset(modules_, 0, sizeof(modules_));
    memset(function_, 0, sizeof(function_));
    drawFunctionPatterns();
    uint8_t all[172];
    addEccAndInterleave(data, all);
    drawCodewords(all, rawModules(version_) / 8);
    // beste Maske waehlen
    int best = 0;
    long bestPenalty = 0x7FFFFFFF;
    for (int mask = 0; mask < 8; ++mask) {
      applyMask(mask);
      drawFormatBits(mask);
      const long penalty = penaltyScore();
      if (penalty < bestPenalty) { best = mask; bestPenalty = penalty; }
      applyMask(mask);  // XOR: hebt die Maske wieder auf
    }
    applyMask(best);
    drawFormatBits(best);
    return true;
  }

  int size() const { return size_; }
  bool module(int x, int y) const { return x >= 0 && y >= 0 && x < size_ && y < size_ && modules_[y][x]; }

 private:
  static int rawModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
      const int numAlign = ver / 7 + 2;
      result -= (25 * numAlign - 10) * numAlign - 55;
    }
    return result;
  }
  static int eccPerBlock(int ver) { static const uint8_t t[] = {0, 7, 10, 15, 20, 26, 18}; return t[ver]; }
  static int numBlocks(int ver) { return ver == 6 ? 2 : 1; }
  static int dataCodewords(int ver) { return rawModules(ver) / 8 - eccPerBlock(ver) * numBlocks(ver); }

  void setFunction(int x, int y, bool dark) { modules_[y][x] = dark; function_[y][x] = true; }

  void drawFunctionPatterns() {
    for (int i = 0; i < size_; ++i) {
      setFunction(6, i, i % 2 == 0);
      setFunction(i, 6, i % 2 == 0);
    }
    drawFinder(3, 3);
    drawFinder(size_ - 4, 3);
    drawFinder(3, size_ - 4);
    if (version_ >= 2) {  // Version 2-6: ein Ausrichtungsmuster unten rechts
      const int p = size_ - 7;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
          const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
          setFunction(p + dx, p + dy, (adx > ady ? adx : ady) != 1);
        }
    }
    drawFormatBits(0);
  }

  void drawFinder(int x, int y) {
    for (int dy = -4; dy <= 4; ++dy) {
      for (int dx = -4; dx <= 4; ++dx) {
        const int xx = x + dx, yy = y + dy;
        if (xx < 0 || yy < 0 || xx >= size_ || yy >= size_) continue;
        const int d = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        setFunction(xx, yy, d != 2 && d != 4);
      }
    }
  }

  void drawFormatBits(int mask) {
    const int data = 1 << 3 | mask;  // Fehlerkorrektur L = 01
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int bits = (data << 10 | rem) ^ 0x5412;
    auto bitAt = [&](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) setFunction(8, i, bitAt(i));
    setFunction(8, 7, bitAt(6));
    setFunction(8, 8, bitAt(7));
    setFunction(7, 8, bitAt(8));
    for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, bitAt(i));
    for (int i = 0; i < 8; ++i) setFunction(size_ - 1 - i, 8, bitAt(i));
    for (int i = 8; i < 15; ++i) setFunction(8, size_ - 15 + i, bitAt(i));
    setFunction(8, size_ - 8, true);
  }

  static uint8_t gfMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
      z = (z << 1) ^ ((z >> 7) * 0x11D);
      z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
  }

  void addEccAndInterleave(const uint8_t *data, uint8_t *result) {
    const int blocks = numBlocks(version_);
    const int ecc = eccPerBlock(version_);
    const int raw = rawModules(version_) / 8;
    const int blockLen = raw / blocks;          // Versionen 1-6 (L): alle Bloecke gleich lang
    const int dataLen = blockLen - ecc;
    uint8_t divisor[30] = {};
    divisor[ecc - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < ecc; ++i) {
      for (int j = 0; j < ecc; ++j) {
        divisor[j] = gfMultiply(divisor[j], root);
        if (j + 1 < ecc) divisor[j] ^= divisor[j + 1];
      }
      root = gfMultiply(root, 0x02);
    }
    uint8_t blockData[2][172];
    for (int b = 0; b < blocks; ++b) {
      const uint8_t *d = data + b * dataLen;
      uint8_t remainder[30] = {};
      for (int i = 0; i < dataLen; ++i) {
        const uint8_t factor = d[i] ^ remainder[0];
        memmove(remainder, remainder + 1, ecc - 1);
        remainder[ecc - 1] = 0;
        for (int j = 0; j < ecc; ++j) remainder[j] ^= gfMultiply(divisor[j], factor);
      }
      memcpy(blockData[b], d, dataLen);
      memcpy(blockData[b] + dataLen, remainder, ecc);
    }
    int k = 0;
    for (int i = 0; i < blockLen; ++i)
      for (int b = 0; b < blocks; ++b) result[k++] = blockData[b][i];
  }

  void drawCodewords(const uint8_t *data, int length) {
    int i = 0;
    for (int right = size_ - 1; right >= 1; right -= 2) {
      if (right == 6) right = 5;
      for (int vert = 0; vert < size_; ++vert) {
        for (int j = 0; j < 2; ++j) {
          const int x = right - j;
          const bool upward = ((right + 1) & 2) == 0;
          const int y = upward ? size_ - 1 - vert : vert;
          if (!function_[y][x] && i < length * 8) {
            modules_[y][x] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
            ++i;
          }
        }
      }
    }
  }

  void applyMask(int mask) {
    for (int y = 0; y < size_; ++y) {
      for (int x = 0; x < size_; ++x) {
        bool invert;
        switch (mask) {
          case 0: invert = (x + y) % 2 == 0; break;
          case 1: invert = y % 2 == 0; break;
          case 2: invert = x % 3 == 0; break;
          case 3: invert = (x + y) % 3 == 0; break;
          case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
          case 5: invert = x * y % 2 + x * y % 3 == 0; break;
          case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
          default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
        }
        if (invert && !function_[y][x]) modules_[y][x] = !modules_[y][x];
      }
    }
  }

  // Strafpunkte nach QR-Standard (zur Wahl der Maske)
  struct RunHistory {
    int v[7] = {};
    void push(int run, int size) {
      if (v[0] == 0) run += size;
      for (int i = 6; i > 0; --i) v[i] = v[i - 1];
      v[0] = run;
    }
    int count() const {
      const int n = v[1];
      const bool core = n > 0 && v[2] == n && v[3] == n * 3 && v[4] == n && v[5] == n;
      return (core && v[0] >= n * 4 && v[6] >= n ? 1 : 0) + (core && v[6] >= n * 4 && v[0] >= n ? 1 : 0);
    }
  };

  long penaltyScore() const {
    long result = 0;
    for (int pass = 0; pass < 2; ++pass) {  // 0 = Zeilen, 1 = Spalten
      for (int a = 0; a < size_; ++a) {
        bool runColor = false;
        int run = 0;
        RunHistory history;
        for (int b = 0; b < size_; ++b) {
          const bool cell = pass == 0 ? modules_[a][b] : modules_[b][a];
          if (cell == runColor) {
            ++run;
            if (run == 5) result += 3;
            else if (run > 5) result += 1;
          } else {
            history.push(run, size_);
            if (!runColor) result += history.count() * 40;
            runColor = cell;
            run = 1;
          }
        }
        if (runColor) { history.push(run, size_); run = 0; }
        run += size_;
        history.push(run, size_);
        result += history.count() * 40;
      }
    }
    for (int y = 0; y < size_ - 1; ++y)
      for (int x = 0; x < size_ - 1; ++x) {
        const bool c = modules_[y][x];
        if (c == modules_[y][x + 1] && c == modules_[y + 1][x] && c == modules_[y + 1][x + 1]) result += 3;
      }
    int dark = 0;
    for (int y = 0; y < size_; ++y)
      for (int x = 0; x < size_; ++x) dark += modules_[y][x] ? 1 : 0;
    const int total = size_ * size_;
    const int k = ((dark * 20 - total * 10 < 0 ? total * 10 - dark * 20 : dark * 20 - total * 10) + total - 1) / total - 1;
    result += k * 10;
    return result;
  }

  int version_ = 0;
  int size_ = 0;
  bool modules_[MAX_SIZE][MAX_SIZE];
  bool function_[MAX_SIZE][MAX_SIZE];
};
