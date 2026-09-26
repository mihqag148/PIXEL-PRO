#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// PIXEL PRO production 8-bit i8080 bus.
//
// The stock Arduino_ESP32PAR8 updates D33-D40 and immediately toggles WR.
// On the user's MCUFRIEND HX8357-B shield that path has repeatedly shown
// washed-out / dim / flickering output, while slower static diagnostics are
// electrically stable.
//
// PIXEL PRO has a useful layout advantage: LCD D0..D7 are exactly D33..D40,
// i.e. GPIO33..GPIO40, contiguous on ESP32-S2 GPIO_OUT1 bits 1..8. We can
// therefore keep direct-register throughput while adding deterministic
// data-setup, WR-low, and data-hold time instead of falling back to the very
// slow digitalWrite-based diagnostic bus.
class PixelStablePAR8 : public Arduino_DataBus {
 public:
  PixelStablePAR8(
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
        _rd(rd),
        _d0(d0),
        _d1(d1),
        _d2(d2),
        _d3(d3),
        _d4(d4),
        _d5(d5),
        _d6(d6),
        _d7(d7) {}

  bool begin(
      int32_t speed = GFX_NOT_DEFINED,
      int8_t dataMode = GFX_NOT_DEFINED) override {
    (void)speed;
    (void)dataMode;

    // This fast path is intentionally specific to the final PIXEL PRO wiring.
    // Refuse an accidental pin-map change rather than silently corrupt pixels.
    if (_d0 != 33 ||
        _d1 != 34 ||
        _d2 != 35 ||
        _d3 != 36 ||
        _d4 != 37 ||
        _d5 != 38 ||
        _d6 != 39 ||
        _d7 != 40) {
      return false;
    }

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

    for (int8_t pin = 33; pin <= 40; ++pin) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }

    _dcPinMask = digitalPinToBitMask(_dc);
    _csPinMask =
        _cs != GFX_NOT_DEFINED
            ? digitalPinToBitMask(_cs)
            : 0;
    _wrPinMask = digitalPinToBitMask(_wr);

    _dcPortSet = (PORTreg_t)GPIO_OUT_W1TS_REG;
    _dcPortClr = (PORTreg_t)GPIO_OUT_W1TC_REG;
    _csPortSet = (PORTreg_t)GPIO_OUT_W1TS_REG;
    _csPortClr = (PORTreg_t)GPIO_OUT_W1TC_REG;
    _wrPortSet = (PORTreg_t)GPIO_OUT_W1TS_REG;
    _wrPortClr = (PORTreg_t)GPIO_OUT_W1TC_REG;

    _dataPortSet = (PORTreg_t)GPIO_OUT1_W1TS_REG;
    _dataPortClr = (PORTreg_t)GPIO_OUT1_W1TC_REG;

    // GPIO33..40 correspond to OUT1 bits 1..8.
    _dataClrMask = 0x000001FEUL;
    *_dataPortClr = _dataClrMask;

    return true;
  }

  void beginWrite() override {
    dcHigh();
    csLow();
    busGuard();
  }

  void endWrite() override {
    busGuard();
    csHigh();
  }

  void writeCommand(uint8_t c) override {
    dcLow();
    writeByte(c);
    dcHigh();
    busGuard();
  }

  void writeCommand16(uint16_t c) override {
    dcLow();
    writeByte(static_cast<uint8_t>(c >> 8));
    writeByte(static_cast<uint8_t>(c));
    dcHigh();
    busGuard();
  }

  void writeCommandBytes(
      uint8_t *data,
      uint32_t len) override {
    dcLow();
    while (len-- > 0) {
      writeByte(*data++);
    }
    dcHigh();
    busGuard();
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

    if (hi == lo) {
      setData(hi);
      dataSetup();
      while (len-- > 0) {
        pulseWrite();
        pulseWrite();
      }
      return;
    }

    while (len-- > 0) {
      writeByte(hi);
      writeByte(lo);
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
    dcLow();
    writeByte(c);
    dcHigh();
    writeByte(d);
  }

  void writeC8D16(
      uint8_t c,
      uint16_t d) override {
    dcLow();
    writeByte(c);
    dcHigh();
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
    dcLow();
    writeByte(c);
    dcHigh();

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

  void writeBytes(
      uint8_t *data,
      uint32_t len) override {
    while (len-- > 0) {
      writeByte(*data++);
    }
  }

  void writeIndexedPixels(
      uint8_t *data,
      uint16_t *idx,
      uint32_t len) override {
    while (len-- > 0) {
      const uint16_t pixel =
          idx[*data++];
      writeByte(
          static_cast<uint8_t>(
              pixel >> 8));
      writeByte(
          static_cast<uint8_t>(
              pixel));
    }
  }

  void writeIndexedPixelsDouble(
      uint8_t *data,
      uint16_t *idx,
      uint32_t len) override {
    while (len-- > 0) {
      const uint16_t pixel =
          idx[*data++];
      const uint8_t hi =
          static_cast<uint8_t>(
              pixel >> 8);
      const uint8_t lo =
          static_cast<uint8_t>(
              pixel);

      writeByte(hi);
      writeByte(lo);
      writeByte(hi);
      writeByte(lo);
    }
  }

 private:
  GFX_INLINE void setData(
      uint8_t value) {
    *_dataPortClr = _dataClrMask;
    *_dataPortSet =
        static_cast<uint32_t>(
            value) <<
        1;
  }

  // ESP32-S2 normally runs at 240 MHz here. These NOP groups intentionally
  // provide tens of nanoseconds of deterministic margin without the ~1 us per
  // edge penalty of the diagnostic bus.
  GFX_INLINE void dataSetup() {
    asm volatile(
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n");
  }

  GFX_INLINE void writeLowHold() {
    asm volatile(
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n");
  }

  GFX_INLINE void dataHold() {
    asm volatile(
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n");
  }

  GFX_INLINE void busGuard() {
    asm volatile(
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n");
  }

  GFX_INLINE void pulseWrite() {
    *_wrPortClr = _wrPinMask;
    writeLowHold();
    *_wrPortSet = _wrPinMask;
    dataHold();
  }

  GFX_INLINE void writeByte(
      uint8_t value) {
    setData(value);
    dataSetup();
    pulseWrite();
  }

  GFX_INLINE void dcHigh() {
    *_dcPortSet = _dcPinMask;
  }

  GFX_INLINE void dcLow() {
    *_dcPortClr = _dcPinMask;
  }

  GFX_INLINE void csHigh() {
    if (_cs != GFX_NOT_DEFINED) {
      *_csPortSet = _csPinMask;
    }
  }

  GFX_INLINE void csLow() {
    if (_cs != GFX_NOT_DEFINED) {
      *_csPortClr = _csPinMask;
    }
  }

  int8_t _dc;
  int8_t _cs;
  int8_t _wr;
  int8_t _rd;
  int8_t _d0;
  int8_t _d1;
  int8_t _d2;
  int8_t _d3;
  int8_t _d4;
  int8_t _d5;
  int8_t _d6;
  int8_t _d7;

  PORTreg_t _dcPortSet;
  PORTreg_t _dcPortClr;
  PORTreg_t _csPortSet;
  PORTreg_t _csPortClr;
  PORTreg_t _wrPortSet;
  PORTreg_t _wrPortClr;
  PORTreg_t _dataPortSet;
  PORTreg_t _dataPortClr;

  uint32_t _dcPinMask = 0;
  uint32_t _csPinMask = 0;
  uint32_t _wrPinMask = 0;
  uint32_t _dataClrMask = 0;
};
