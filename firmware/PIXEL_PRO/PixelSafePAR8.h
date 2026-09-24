#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Diagnostic-only conservative 8-bit i8080 bus for PIXEL PRO.
//
// Arduino_ESP32PAR8 uses direct GPIO set/clear registers and immediately
// toggles WR. On this shield, the bit-banged static-white diagnostic is stable
// while the production direct-register bus produces dim/flickering output.
// This class deliberately adds generous setup/hold time to prove or disprove
// a bus-timing problem before optimizing for speed.
class PixelSafePAR8 : public Arduino_DataBus {
 public:
  PixelSafePAR8(
      int8_t dc,
      int8_t cs,
      int8_t wr,
      int8_t rd,
      int8_t d0,
      int8_t d1,
      int8_t d2,
      int8_t d3,
      int8_t d4,
      int8_t d5,
      int8_t d6,
      int8_t d7)
      : _dc(dc),
        _cs(cs),
        _wr(wr),
        _rd(rd) {
    _data[0] = d0;
    _data[1] = d1;
    _data[2] = d2;
    _data[3] = d3;
    _data[4] = d4;
    _data[5] = d5;
    _data[6] = d6;
    _data[7] = d7;
  }

  bool begin(
      int32_t speed = GFX_NOT_DEFINED,
      int8_t dataMode = GFX_NOT_DEFINED) override {
    (void)speed;
    (void)dataMode;

    pinMode(_dc, OUTPUT);
    digitalWrite(_dc, HIGH);

    if (_cs != GFX_NOT_DEFINED) {
      pinMode(_cs, OUTPUT);
      digitalWrite(_cs, HIGH);
    }

    pinMode(_wr, OUTPUT);
    digitalWrite(_wr, HIGH);

    if (_rd != GFX_NOT_DEFINED) {
      pinMode(_rd, OUTPUT);
      digitalWrite(_rd, HIGH);
    }

    for (uint8_t i = 0; i < 8; ++i) {
      pinMode(_data[i], OUTPUT);
      digitalWrite(_data[i], LOW);
    }

    return true;
  }

  void beginWrite() override {
    digitalWrite(_dc, HIGH);
    if (_cs != GFX_NOT_DEFINED) {
      digitalWrite(_cs, LOW);
    }
    delayMicroseconds(1);
  }

  void endWrite() override {
    delayMicroseconds(1);
    if (_cs != GFX_NOT_DEFINED) {
      digitalWrite(_cs, HIGH);
    }
  }

  void writeCommand(uint8_t c) override {
    digitalWrite(_dc, LOW);
    writeByte(c);
    digitalWrite(_dc, HIGH);
    delayMicroseconds(1);
  }

  void writeCommand16(uint16_t c) override {
    digitalWrite(_dc, LOW);
    writeByte(static_cast<uint8_t>(c >> 8));
    writeByte(static_cast<uint8_t>(c));
    digitalWrite(_dc, HIGH);
    delayMicroseconds(1);
  }

  void writeCommandBytes(
      uint8_t *data,
      uint32_t len) override {
    digitalWrite(_dc, LOW);
    while (len-- > 0) {
      writeByte(*data++);
    }
    digitalWrite(_dc, HIGH);
    delayMicroseconds(1);
  }

  void write(uint8_t d) override {
    writeByte(d);
  }

  void write16(uint16_t d) override {
    writeByte(static_cast<uint8_t>(d >> 8));
    writeByte(static_cast<uint8_t>(d));
  }

  void writeRepeat(
      uint16_t p,
      uint32_t len) override {
    const uint8_t hi =
        static_cast<uint8_t>(p >> 8);
    const uint8_t lo =
        static_cast<uint8_t>(p);

    while (len-- > 0) {
      writeByte(hi);
      writeByte(lo);
    }
  }

  void writeBytes(
      uint8_t *data,
      uint32_t len) override {
    while (len-- > 0) {
      writeByte(*data++);
    }
  }

  void writePixels(
      uint16_t *data,
      uint32_t len) override {
    while (len-- > 0) {
      const uint16_t pixel = *data++;
      writeByte(
          static_cast<uint8_t>(
              pixel >> 8));
      writeByte(
          static_cast<uint8_t>(
              pixel));
    }
  }

  void writeC8D8(
      uint8_t c,
      uint8_t d) override {
    digitalWrite(_dc, LOW);
    writeByte(c);
    digitalWrite(_dc, HIGH);
    writeByte(d);
  }

  void writeC8D16(
      uint8_t c,
      uint16_t d) override {
    digitalWrite(_dc, LOW);
    writeByte(c);
    digitalWrite(_dc, HIGH);
    writeByte(
        static_cast<uint8_t>(
            d >> 8));
    writeByte(
        static_cast<uint8_t>(
            d));
  }

  void writeC8D16D16(
      uint8_t c,
      uint16_t d1,
      uint16_t d2) override {
    digitalWrite(_dc, LOW);
    writeByte(c);
    digitalWrite(_dc, HIGH);

    writeByte(
        static_cast<uint8_t>(
            d1 >> 8));
    writeByte(
        static_cast<uint8_t>(
            d1));
    writeByte(
        static_cast<uint8_t>(
            d2 >> 8));
    writeByte(
        static_cast<uint8_t>(
            d2));
  }

  void writeC8D16D16Split(
      uint8_t c,
      uint16_t d1,
      uint16_t d2) override {
    writeC8D16D16(
        c,
        d1,
        d2);
  }

 private:
  void setData(uint8_t value) {
    for (uint8_t bit = 0;
         bit < 8;
         ++bit) {
      digitalWrite(
          _data[bit],
          (value &
           static_cast<uint8_t>(
               1U << bit))
              ? HIGH
              : LOW);
    }
  }

  void writeByte(uint8_t value) {
    setData(value);

    // Deliberately conservative timing:
    // data setup -> WR low width -> data hold / recovery.
    delayMicroseconds(1);
    digitalWrite(_wr, LOW);
    delayMicroseconds(1);
    digitalWrite(_wr, HIGH);
    delayMicroseconds(1);
  }

  int8_t _dc;
  int8_t _cs;
  int8_t _wr;
  int8_t _rd;
  int8_t _data[8];
};
