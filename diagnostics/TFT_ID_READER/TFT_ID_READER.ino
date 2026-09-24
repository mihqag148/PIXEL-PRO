#include <Arduino.h>
#include "USB.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error TFT ID diagnostic requires USB CDC On Boot disabled
#else
USBCDC USBSerial;
#endif

// PIXEL PRO TFT ID diagnostic
// IMPORTANT:
//   LCD_RD    -> D15   (temporary diagnostic wiring)
//   RGB DATA  -> disconnected from D15 while running this firmware
//   LCD_WR    -> D13
//   LCD_RS/DC -> D14
//   LCD_CS    -> D16
//   LCD_RST   -> EN
//   LCD_D0..D7 -> D33..D40
//
// No display driver is initialized. The sketch only reads controller registers
// over the 8-bit 8080 bus and prints the raw values over native USB CDC.

static constexpr uint8_t PIN_WR = 13;  // D13
static constexpr uint8_t PIN_DC = 14;  // D14
static constexpr uint8_t PIN_RD = 15;  // D15 (temporary)
static constexpr uint8_t PIN_CS = 16;  // D16

static constexpr uint8_t DATA_PINS[8] = {
    33,  // D33 = LCD_D0
    34,  // D34 = LCD_D1
    35,  // D35 = LCD_D2
    36,  // D36 = LCD_D3
    37,  // D37 = LCD_D4
    38,  // D38 = LCD_D5
    39,  // D39 = LCD_D6
    40   // D40 = LCD_D7
};

static void setDataOutput() {
  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(DATA_PINS[i], OUTPUT);
  }
}

static void setDataInput() {
  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(DATA_PINS[i], INPUT);
  }
}

static void writeBus8(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(
        DATA_PINS[bit],
        (value & (1U << bit)) ? HIGH : LOW);
  }
}

static uint8_t readBus8() {
  uint8_t value = 0;

  for (uint8_t bit = 0; bit < 8; ++bit) {
    if (digitalRead(DATA_PINS[bit])) {
      value |= static_cast<uint8_t>(1U << bit);
    }
  }

  return value;
}

static void pulseWrite() {
  digitalWrite(PIN_WR, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_WR, HIGH);
  delayMicroseconds(2);
}

static void writeCommand8(uint8_t command) {
  setDataOutput();
  digitalWrite(PIN_DC, LOW);
  writeBus8(command);
  pulseWrite();
  digitalWrite(PIN_DC, HIGH);
}

static void writeData8(uint8_t data) {
  setDataOutput();
  digitalWrite(PIN_DC, HIGH);
  writeBus8(data);
  pulseWrite();
}

static uint8_t readByte() {
  digitalWrite(PIN_RD, LOW);
  delayMicroseconds(3);
  const uint8_t value = readBus8();
  digitalWrite(PIN_RD, HIGH);
  delayMicroseconds(3);
  return value;
}

static void readRegister(
    uint8_t command,
    uint8_t *out,
    uint8_t count) {
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(2);

  writeCommand8(command);
  setDataInput();
  digitalWrite(PIN_DC, HIGH);
  delayMicroseconds(3);

  for (uint8_t i = 0; i < count; ++i) {
    out[i] = readByte();
  }

  digitalWrite(PIN_RD, HIGH);
  digitalWrite(PIN_CS, HIGH);
  setDataOutput();
}

static void printHex8(uint8_t value) {
  if (value < 0x10) {
    USBSerial.print('0');
  }
  USBSerial.print(value, HEX);
}

static void dumpRegister(
    uint8_t command,
    uint8_t count = 8) {
  uint8_t values[12] = {};
  if (count > sizeof(values)) {
    count = sizeof(values);
  }

  readRegister(command, values, count);

  USBSerial.print("REG|0x");
  printHex8(command);
  USBSerial.print('|');

  for (uint8_t i = 0; i < count; ++i) {
    if (i != 0) {
      USBSerial.print(' ');
    }
    printHex8(values[i]);
  }

  USBSerial.println();
}

static bool pairMatches(
    const uint8_t *data,
    uint8_t count,
    uint8_t hi,
    uint8_t lo) {
  for (uint8_t i = 0; i + 1 < count; ++i) {
    if (data[i] == hi &&
        data[i + 1] == lo) {
      return true;
    }
  }

  return false;
}

static uint16_t inferIdFromD3() {
  uint8_t d3[8] = {};
  readRegister(0xD3, d3, sizeof(d3));

  static constexpr uint16_t ids[] = {
      0x9340,
      0x9341,
      0x9481,
      0x9486,
      0x9488,
      0x7796,
      0x9163,
      0x6814};

  for (uint16_t id : ids) {
    if (pairMatches(
            d3,
            sizeof(d3),
            static_cast<uint8_t>(id >> 8),
            static_cast<uint8_t>(id))) {
      return id;
    }
  }

  return 0;
}

static uint16_t inferIdFromBf() {
  uint8_t bf[10] = {};
  readRegister(0xBF, bf, sizeof(bf));

  static constexpr uint16_t ids[] = {
      0x1581,
      0x9481,
      0x8357,
      0x1520,
      0x1526,
      0x1511,
      0x6814};

  for (uint16_t id : ids) {
    if (pairMatches(
            bf,
            sizeof(bf),
            static_cast<uint8_t>(id >> 8),
            static_cast<uint8_t>(id))) {
      return id;
    }
  }

  return 0;
}

static void dumpAll() {
  USBSerial.println();
  USBSerial.println("PIXEL_PRO_TFT_ID_DIAG|1");
  USBSerial.println("WIRING|RD=D15|WR=D13|DC=D14|CS=D16|D0-D7=D33-D40|RST=EN");

  // Read the same register families used by MCUFRIEND_kbv::readID().
  dumpRegister(0x00);
  dumpRegister(0x04);
  dumpRegister(0x09);
  dumpRegister(0x67);
  dumpRegister(0xA1);
  dumpRegister(0xBF, 10);
  dumpRegister(0xD3);
  dumpRegister(0xD4);
  dumpRegister(0xD7);
  dumpRegister(0xEF);
  dumpRegister(0xFE);

  const uint16_t d3Id = inferIdFromD3();
  const uint16_t bfId = inferIdFromBf();

  USBSerial.print("AUTO_ID_D3|0x");
  USBSerial.println(d3Id, HEX);
  USBSerial.print("AUTO_ID_BF|0x");
  USBSerial.println(bfId, HEX);

  if (d3Id != 0) {
    USBSerial.print("BEST_GUESS|0x");
    USBSerial.println(d3Id, HEX);
  } else if (bfId != 0) {
    USBSerial.print("BEST_GUESS|0x");
    USBSerial.println(bfId, HEX);
  } else {
    USBSerial.println("BEST_GUESS|UNKNOWN");
  }

  USBSerial.println("END_TFT_ID_DIAG");
}

void setup() {
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_WR, OUTPUT);
  pinMode(PIN_RD, OUTPUT);

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  digitalWrite(PIN_RD, HIGH);

  setDataOutput();
  writeBus8(0x00);

  USB.VID(0x303A);
  USB.PID(0x80C2);
  USB.productName("PIXEL PRO TFT ID");
  USB.manufacturerName("Lumi3D");
  USB.serialNumber("PIXELPRO-TFT-ID");

  USBSerial.begin();
  USB.begin();

  // LCD_RST is tied to EN, so the panel has already received the board reset.
  // Give the controller time to settle, then unlock the common R61520 family
  // read path in the same spirit as MCUFRIEND_kbv::reset().
  delay(1200);

  digitalWrite(PIN_CS, LOW);
  writeCommand8(0xB0);
  writeData8(0x00);
  digitalWrite(PIN_CS, HIGH);

  delay(20);
  dumpAll();
}

void loop() {
  static uint32_t lastDumpAt = 0;

  if (USBSerial.available()) {
    while (USBSerial.available()) {
      (void)USBSerial.read();
    }
    dumpAll();
    lastDumpAt = millis();
  }

  if (millis() - lastDumpAt >= 3000UL) {
    lastDumpAt = millis();
    dumpAll();
  }

  delay(10);
}
