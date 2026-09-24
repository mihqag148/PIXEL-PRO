#include <Arduino.h>

// PIXEL PRO HX8357-B power/VCOM/brightness sweep.
//
// Normal LCD wiring:
//   WR  -> D13
//   DC  -> D14
//   CS  -> D16
//   D0..D7 -> D33..D40
//   RD  -> 3V3
//   RST -> EN
//
// Keep RGB DATA physically disconnected from D15.
//
// The test repeats three 8-second phases:
//   RED top bar   = minimal MCUFRIEND-style init + INVON
//   GREEN top bar = full Adafruit HX8357-B analog/panel init + INVON
//   BLUE top bar  = same full init + force brightness registers to max
//
// Each phase fills the rest of the screen 0xFFFF white so perceived panel /
// backlight brightness can be compared directly.

static constexpr uint8_t PIN_WR = 13;
static constexpr uint8_t PIN_DC = 14;
static constexpr uint8_t PIN_CS = 16;

static constexpr uint8_t DATA_PINS[8] = {
    33, 34, 35, 36, 37, 38, 39, 40
};

static inline void busSet(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(
        DATA_PINS[bit],
        (value & (1U << bit)) ? HIGH : LOW);
  }
}

static inline void wrPulse() {
  digitalWrite(PIN_WR, LOW);
  digitalWrite(PIN_WR, HIGH);
}

static inline void selectLcd() {
  digitalWrite(PIN_CS, LOW);
}

static inline void deselectLcd() {
  digitalWrite(PIN_CS, HIGH);
}

static void writeCommandRaw(uint8_t command) {
  digitalWrite(PIN_DC, LOW);
  busSet(command);
  wrPulse();
  digitalWrite(PIN_DC, HIGH);
}

static void writeDataRaw(uint8_t data) {
  digitalWrite(PIN_DC, HIGH);
  busSet(data);
  wrPulse();
}

static void command0(uint8_t command) {
  selectLcd();
  writeCommandRaw(command);
  deselectLcd();
}

static void command1(
    uint8_t command,
    uint8_t d0) {
  selectLcd();
  writeCommandRaw(command);
  writeDataRaw(d0);
  deselectLcd();
}

static void command2(
    uint8_t command,
    uint8_t d0,
    uint8_t d1) {
  selectLcd();
  writeCommandRaw(command);
  writeDataRaw(d0);
  writeDataRaw(d1);
  deselectLcd();
}

static void command3(
    uint8_t command,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2) {
  selectLcd();
  writeCommandRaw(command);
  writeDataRaw(d0);
  writeDataRaw(d1);
  writeDataRaw(d2);
  deselectLcd();
}

static void command4(
    uint8_t command,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2,
    uint8_t d3) {
  selectLcd();
  writeCommandRaw(command);
  writeDataRaw(d0);
  writeDataRaw(d1);
  writeDataRaw(d2);
  writeDataRaw(d3);
  deselectLcd();
}

static void command5(
    uint8_t command,
    uint8_t d0,
    uint8_t d1,
    uint8_t d2,
    uint8_t d3,
    uint8_t d4) {
  selectLcd();
  writeCommandRaw(command);
  writeDataRaw(d0);
  writeDataRaw(d1);
  writeDataRaw(d2);
  writeDataRaw(d3);
  writeDataRaw(d4);
  deselectLcd();
}

static void command12(
    uint8_t command,
    const uint8_t *data) {
  selectLcd();
  writeCommandRaw(command);
  for (uint8_t i = 0; i < 12; ++i) {
    writeDataRaw(data[i]);
  }
  deselectLcd();
}

static void softwareReset() {
  command0(0x01);
  delay(150);
}

static void setLandscapeAndInvOn() {
  // Rotation 1 and BGR, matching the working raw diagnostic / MCUFRIEND path.
  command1(0x36, 0x28);

  // ID 0x8357 shield needs inversion enabled for normal polarity.
  command0(0x21);
  delay(20);
}

static void minimalInit() {
  softwareReset();

  // Keep the known-good minimal sequence from the previous static test.
  command2(0xB0, 0x00, 0x00);
  command0(0x28);
  command1(0x3A, 0x55);
  delay(1);
  command0(0x11);
  delay(150);
  command0(0x29);
  delay(50);

  setLandscapeAndInvOn();
}

static void adafruitStyleInit(bool forceBrightness) {
  softwareReset();

  // Adafruit HX8357B initb[] analog/panel values.
  command3(0xD0, 0x44, 0x41, 0x06);       // SETPOWER
  command2(0xD1, 0x40, 0x10);             // SETVCOM
  command2(0xD2, 0x05, 0x12);             // SETPWRNORMAL
  command5(0xC0, 0x14, 0x3B, 0x00, 0x02, 0x11); // PANEL DRIVING
  command1(0xC5, 0x0C);                   // DISPLAY FRAME
  command1(0xE9, 0x01);                   // PANEL RELATED
  command3(0xEA, 0x03, 0x00, 0x00);

  selectLcd();
  writeCommandRaw(0xEB);
  writeDataRaw(0x40);
  writeDataRaw(0x54);
  writeDataRaw(0x26);
  writeDataRaw(0xDB);
  deselectLcd();

  static const uint8_t GAMMA[12] = {
      0x00, 0x15, 0x00, 0x22,
      0x00, 0x08, 0x77, 0x26,
      0x66, 0x22, 0x04, 0x00
  };
  command12(0xC8, GAMMA);

  // CPU DBI mode / internal oscillator.
  command1(0xB4, 0x00);

  // RGB565.
  command1(0x3A, 0x55);

  command0(0x11);
  delay(120);
  command0(0x29);
  delay(30);

  setLandscapeAndInvOn();

  if (forceBrightness) {
    // HX8357-B implements standard DCS display-brightness / control / CABC.
    // Force maximum requested brightness, enable brightness control, and keep
    // content-adaptive brightness disabled so this phase is deterministic.
    command1(0x51, 0xFF);
    command1(0x53, 0x2C);
    command1(0x55, 0x00);
    delay(20);
  }
}

static void setWindow(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1) {
  command4(
      0x2A,
      static_cast<uint8_t>(x0 >> 8),
      static_cast<uint8_t>(x0),
      static_cast<uint8_t>(x1 >> 8),
      static_cast<uint8_t>(x1));

  command4(
      0x2B,
      static_cast<uint8_t>(y0 >> 8),
      static_cast<uint8_t>(y0),
      static_cast<uint8_t>(y1 >> 8),
      static_cast<uint8_t>(y1));
}

static void fillRect565(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t color) {
  if (w == 0 || h == 0) {
    return;
  }

  setWindow(
      x,
      y,
      static_cast<uint16_t>(x + w - 1),
      static_cast<uint16_t>(y + h - 1));

  const uint8_t hi =
      static_cast<uint8_t>(color >> 8);
  const uint8_t lo =
      static_cast<uint8_t>(color);

  selectLcd();
  writeCommandRaw(0x2C);
  digitalWrite(PIN_DC, HIGH);

  const uint32_t pixels =
      static_cast<uint32_t>(w) * h;

  for (uint32_t i = 0; i < pixels; ++i) {
    busSet(hi);
    wrPulse();
    busSet(lo);
    wrPulse();
  }

  deselectLcd();
}

static void drawPhaseFrame(uint16_t barColor) {
  fillRect565(0, 0, 480, 320, 0xFFFF);
  fillRect565(0, 0, 480, 20, barColor);

  // Leave bus electrically quiet.
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  busSet(0xFF);
}

static void runPhase(
    uint8_t phase,
    uint32_t holdMs) {
  if (phase == 0) {
    minimalInit();
    drawPhaseFrame(0xF800); // red
  } else if (phase == 1) {
    adafruitStyleInit(false);
    drawPhaseFrame(0x07E0); // green
  } else {
    adafruitStyleInit(true);
    drawPhaseFrame(0x001F); // blue
  }

  delay(holdMs);
}

void setup() {
  // Keep D15 completely out of this test.
  pinMode(15, INPUT);

  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_WR, OUTPUT);

  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(DATA_PINS[i], OUTPUT);
  }

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  busSet(0x00);

  // RST is tied to EN, so let the shared hardware reset settle once.
  delay(1000);
}

void loop() {
  runPhase(0, 8000); // red bar
  runPhase(1, 8000); // green bar
  runPhase(2, 8000); // blue bar
}
