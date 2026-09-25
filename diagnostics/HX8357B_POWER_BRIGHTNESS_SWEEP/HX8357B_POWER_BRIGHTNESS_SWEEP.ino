#include <Arduino.h>

// PIXEL PRO HX8357-B cumulative power/VCOM/brightness diagnostic v2.
//
// This v2 deliberately starts from the exact known-good static-white INVON
// sequence that previously produced a bright, stable full-screen frame.
// No software reset or full re-init is performed between phases.
//
// Normal LCD wiring:
//   WR  -> D13
//   DC  -> D14
//   CS  -> D16
//   D0..D7 -> D33..D40
//   RD  -> 3V3
//   RST -> EN
//
// D15 is intentionally unused by this diagnostic.
//
// Repeating cumulative phases (10 seconds each):
//   RED bar    = exact known-good MCUFRIEND-style baseline + INVON
//   GREEN bar  = baseline + POWER/VCOM registers D0/D1/D2
//   CYAN bar   = previous + PANEL/GAMMA registers
//   BLUE bar   = previous + DCS brightness max / CTRL Display / CABC off
//
// The white background is drawn with the same constant-0xFF bus pattern used
// by the proven static-white test. Only the small marker bar uses per-byte data.

static constexpr uint8_t PIN_WR = 13;
static constexpr uint8_t PIN_DC = 14;
static constexpr uint8_t PIN_CS = 16;

static constexpr uint8_t DATA_PINS[8] = {
    33, 34, 35, 36, 37, 38, 39, 40
};

static inline void setDataOutput() {
  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(DATA_PINS[i], OUTPUT);
  }
}

static inline void writeBus8(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(
        DATA_PINS[bit],
        (value & static_cast<uint8_t>(1U << bit)) ? HIGH : LOW);
  }
}

static inline void pulseWrite() {
  digitalWrite(PIN_WR, LOW);
  digitalWrite(PIN_WR, HIGH);
}

static inline void beginTransaction() {
  digitalWrite(PIN_CS, LOW);
}

static inline void endTransaction() {
  digitalWrite(PIN_CS, HIGH);
}

static inline void writeCommand8(uint8_t command) {
  digitalWrite(PIN_DC, LOW);
  writeBus8(command);
  pulseWrite();
  digitalWrite(PIN_DC, HIGH);
}

static inline void writeData8(uint8_t data) {
  digitalWrite(PIN_DC, HIGH);
  writeBus8(data);
  pulseWrite();
}

static void command0(uint8_t command) {
  beginTransaction();
  writeCommand8(command);
  endTransaction();
}

static void command1(uint8_t command, uint8_t d0) {
  beginTransaction();
  writeCommand8(command);
  writeData8(d0);
  endTransaction();
}

static void command2(uint8_t command, uint8_t d0, uint8_t d1) {
  beginTransaction();
  writeCommand8(command);
  writeData8(d0);
  writeData8(d1);
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

static void knownGoodBaselineInit() {
  // IMPORTANT: exact order from the proven TFT_WHITE_INVON diagnostic.
  // MCUFRIEND reset() first writes B0=0x0000, then issues SWRESET.
  command2(0xB0, 0x00, 0x00);

  command0(0x01);  // SWRESET
  delay(150);

  command0(0x28);  // display off
  command1(0x3A, 0x55);  // RGB565

  command0(0x11);  // sleep out
  delay(150);
  command0(0x29);  // display on
  delay(50);

  command1(0x36, 0x28);  // landscape rotation 1 + BGR
  command0(0x21);         // INVON (known-good polarity for this shield)
  delay(20);
}

static void restoreVisibleMode() {
  command1(0x3A, 0x55);
  command1(0x36, 0x28);
  command0(0x21);
  command0(0x29);
  delay(40);
}

static void applyPowerVcomGroup() {
  // Do not reset. Temporarily blank while changing analog power registers.
  command0(0x28);
  delay(20);

  command3(0xD0, 0x44, 0x41, 0x06);  // SETPOWER
  command2(0xD1, 0x40, 0x10);        // SETVCOM
  command2(0xD2, 0x05, 0x12);        // SETPWRNORMAL

  restoreVisibleMode();
}

static void applyPanelGammaGroup() {
  // Cumulative: POWER/VCOM remains active. Still no reset.
  command0(0x28);
  delay(20);

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

  restoreVisibleMode();
}

static void applyBrightnessGroup() {
  // Standard DCS brightness/control path; can be changed live.
  command1(0x51, 0xFF);  // display brightness max
  command1(0x53, 0x2C);  // brightness control enabled
  command1(0x55, 0x00);  // CABC off
  delay(40);
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

static void fillKnownGoodWhite() {
  // Exact address window and constant-FF write method from TFT_WHITE_INVON.
  setWindow(0, 0, 479, 319);

  beginTransaction();
  writeCommand8(0x2C);
  digitalWrite(PIN_DC, HIGH);

  writeBus8(0xFF);
  constexpr uint32_t PIXELS = 480UL * 320UL;
  for (uint32_t i = 0; i < PIXELS; ++i) {
    pulseWrite();
    pulseWrite();
  }

  endTransaction();
}

static void fillMarkerBar(uint16_t color) {
  constexpr uint16_t H = 18;
  setWindow(0, 0, 479, H - 1);

  const uint8_t hi = static_cast<uint8_t>(color >> 8);
  const uint8_t lo = static_cast<uint8_t>(color);

  beginTransaction();
  writeCommand8(0x2C);
  digitalWrite(PIN_DC, HIGH);

  constexpr uint32_t PIXELS = 480UL * H;
  for (uint32_t i = 0; i < PIXELS; ++i) {
    writeBus8(hi);
    pulseWrite();
    writeBus8(lo);
    pulseWrite();
  }

  endTransaction();
}

static void drawPhase(uint16_t markerColor) {
  fillKnownGoodWhite();
  fillMarkerBar(markerColor);

  // Electrically quiet idle state between phases.
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  writeBus8(0xFF);
}

void setup() {
  pinMode(15, INPUT);

  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_WR, OUTPUT);
  setDataOutput();

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_WR, HIGH);
  writeBus8(0x00);

  // LCD_RST is tied to EN.
  delay(1000);

  knownGoodBaselineInit();
}

void loop() {
  // 1) Proven baseline. If this is not full-screen and bright, stop here:
  //    the failure is below the power/VCOM experiment.
  drawPhase(0xF800);  // RED
  delay(10000);

  // 2) Cumulative POWER/VCOM.
  applyPowerVcomGroup();
  drawPhase(0x07E0);  // GREEN
  delay(10000);

  // 3) Cumulative panel-driving + gamma.
  applyPanelGammaGroup();
  drawPhase(0x07FF);  // CYAN
  delay(10000);

  // 4) Cumulative display-brightness control.
  applyBrightnessGroup();
  drawPhase(0x001F);  // BLUE
  delay(10000);

  // Do not reset/reinitialize. Hold the final cumulative state so the blue
  // phase is not destroyed by another SWRESET. Repaint every 10 s only.
  while (true) {
    drawPhase(0x001F);
    delay(10000);
  }
}
