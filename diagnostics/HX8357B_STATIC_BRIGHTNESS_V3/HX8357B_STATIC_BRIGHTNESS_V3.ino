#include <Arduino.h>

// PIXEL PRO HX8357-B static stage v3: BRIGHTNESS
// Controller ID measured from the real shield: 0x8357 (HX8357-B).
//
// FINAL NORMAL WIRING:
//   LCD_RD    -> 3V3
//   LCD_WR    -> D13
//   LCD_RS/DC -> D14
//   LCD_CS    -> D16
//   LCD_RST   -> EN
//   LCD_D0..D7 -> D33..D40
//   TFT 5V    -> 5V
//   TFT GND   -> GND
//
// IMPORTANT:
//   - RGB DATA on D15 must stay DISCONNECTED for this diagnostic.
//   - No touch polling.
//   - No RGB.
//   - No SD.
//   - No modules.
//   - No GIF/menu/sleep/runtime refresh.
//   - The display is initialized once, filled with 0xFFFF once, then the bus
//     is left idle forever.
//
// This build uses INVON (0x21) after init.
// v3 rule: one cold init, one full-screen white fill, then permanent idle.

static constexpr uint8_t PIN_WR = 13;  // D13
static constexpr uint8_t PIN_DC = 14;  // D14
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

static void writeBus8(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(
        DATA_PINS[bit],
        (value & (1U << bit)) ? HIGH : LOW);
  }
}

static inline void pulseWrite() {
  digitalWrite(PIN_WR, LOW);
  digitalWrite(PIN_WR, HIGH);
}

static void beginTransaction() {
  digitalWrite(PIN_CS, LOW);
}

static void endTransaction() {
  digitalWrite(PIN_CS, HIGH);
}

static void writeCommand8(uint8_t command) {
  digitalWrite(PIN_DC, LOW);
  writeBus8(command);
  pulseWrite();
  digitalWrite(PIN_DC, HIGH);
}

static void writeData8(uint8_t data) {
  digitalWrite(PIN_DC, HIGH);
  writeBus8(data);
  pulseWrite();
}

static void command0(uint8_t command) {
  beginTransaction();
  writeCommand8(command);
  endTransaction();
}

static void command1(uint8_t command, uint8_t data) {
  beginTransaction();
  writeCommand8(command);
  writeData8(data);
  endTransaction();
}

static void command2(
    uint8_t command,
    uint8_t d0,
    uint8_t d1) {
  beginTransaction();
  writeCommand8(command);
  writeData8(d0);
  writeData8(d1);
  endTransaction();
}

static void command4(
    uint8_t command,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2,
    uint8_t d3) {
  beginTransaction();
  writeCommand8(command);
  writeData8(d0);
  writeData8(d1);
  writeData8(d2);
  writeData8(d3);
  endTransaction();
}


static void command3(
    uint8_t command,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2) {
  beginTransaction();
  writeCommand8(command);
  writeData8(d0);
  writeData8(d1);
  writeData8(d2);
  endTransaction();
}

static void command5(
    uint8_t command,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2,
    uint8_t d3,
    uint8_t d4) {
  beginTransaction();
  writeCommand8(command);
  writeData8(d0);
  writeData8(d1);
  writeData8(d2);
  writeData8(d3);
  writeData8(d4);
  endTransaction();
}

static void commandN(
    uint8_t command,
    const uint8_t *data,
    uint8_t count) {
  beginTransaction();
  writeCommand8(command);
  for (uint8_t i = 0; i < count; ++i) {
    writeData8(data[i]);
  }
  endTransaction();
}

static void applyBrightnessStage() {
  command0(0x28);  // display off

  // POWER / VCOM group.
  command3(0xD0, 0x44, 0x41, 0x06);
  command2(0xD1, 0x40, 0x10);
  command2(0xD2, 0x05, 0x12);

  // Panel drive / gamma group.
  command5(0xC0, 0x14, 0x3B, 0x00, 0x02, 0x11);
  command1(0xC5, 0x0C);
  command1(0xE9, 0x01);
  command3(0xEA, 0x03, 0x00, 0x00);

  static const uint8_t EB[] = {0x40, 0x54, 0x26, 0xDB};
  commandN(0xEB, EB, sizeof(EB));

  static const uint8_t GAMMA[] = {
      0x00, 0x15, 0x00, 0x22,
      0x00, 0x08, 0x77, 0x26,
      0x66, 0x22, 0x04, 0x00
  };
  commandN(0xC8, GAMMA, sizeof(GAMMA));
  command1(0xB4, 0x00);

  command1(0x3A, 0x55);
  command1(0x36, 0x28);
  command0(0x21);
  command0(0x29);
  delay(50);

  // Standard DCS display brightness control.
  command1(0x51, 0xFF);
  command1(0x53, 0x2C);
  command1(0x55, 0x00);
  delay(20);
}

static void mcufriend8357Init() {
  // MCUFRIEND_kbv::reset() writes B0 with 16-bit 0x0000 on its 8-bit bus.
  command2(0xB0, 0x00, 0x00);

  // MCUFRIEND generic reset_off[] for ID 0x8357.
  command0(0x01);  // software reset
  delay(150);

  command0(0x28);  // display off
  command1(0x3A, 0x55);  // RGB565

  // For ID 0x8357 MCUFRIEND has no extra power/timing table.
  // It goes directly to generic wake_on[].
  command0(0x11);  // sleep out
  delay(150);
  command0(0x29);  // display on
  delay(50);

  // Landscape rotation 1 from MCUFRIEND_kbv::setRotation().
  command1(0x36, 0x28);

  // Separate polarity test.
  command0(0x21);  // INVON
  delay(20);
}

static void fillWhite480x320Once() {
  // Landscape address window: X=0..479, Y=0..319.
  command4(0x2A, 0x00, 0x00, 0x01, 0xDF);
  command4(0x2B, 0x00, 0x00, 0x01, 0x3F);

  beginTransaction();
  writeCommand8(0x2C);
  digitalWrite(PIN_DC, HIGH);

  // 0xFFFF white: set D0..D7 high once, then clock two FF bytes per pixel.
  writeBus8(0xFF);

  constexpr uint32_t PIXELS = 480UL * 320UL;
  for (uint32_t i = 0; i < PIXELS; ++i) {
    pulseWrite();
    pulseWrite();
  }

  endTransaction();

  // Leave control bus in an electrically quiet idle state.
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  writeBus8(0xFF);
}

void setup() {
  // Explicitly keep D15 out of the diagnostic.
  pinMode(15, INPUT);

  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_WR, OUTPUT);
  setDataOutput();

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  writeBus8(0x00);

  // LCD_RST is wired to EN, so the panel receives the same board reset as the
  // ESP32-S2. Let the panel fully settle before issuing any command.
  delay(1000);

  mcufriend8357Init();
  applyBrightnessStage();
  fillWhite480x320Once();
}

void loop() {
  // Intentionally do nothing forever. No touch scans, no refresh, no RGB.
  delay(1000);
}
