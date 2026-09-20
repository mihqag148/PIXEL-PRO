/*
 * PIXEL PRO - Lumi Macropad
 * ESP32-S2 Mini + ILI9486 3.5" 480x320 8-bit parallel
 *
 * v0.1.0 hardware bring-up:
 * - 8-key 2x4 matrix, diode COL -> switch -> diode -> ROW (stripe/cathode to ROW)
 * - EC11 encoder + push profile switch
 * - USB HID keyboard + consumer control
 * - VIA-compatible dynamic keymap over QMK Raw HID usage 0xFF60/0x61
 * - Lumi Raw HID framing compatible with Lumi Macropad QmkRawHidLink
 * - ILI9486 480x320 key dashboard + basic PC Monitor / Now Playing screens
 */

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include "esp32-hal-tinyusb.h"
#include "tusb.h"

#define PIXEL_FW_VERSION "0.1.0"
#define PIXEL_USB_VID 0x303A
#define PIXEL_USB_PID 0x4009

static constexpr uint8_t REPORT_ID_KEYBOARD = 1;
static constexpr uint8_t REPORT_ID_CONSUMER = 2;
static constexpr size_t RAW_REPORT_SIZE = 32;

// Dxx = IOxx on ESP32-S2 Mini.
// TFT data/control D1..D12 are configured in the build flags.
static constexpr uint8_t ROW_PINS[2] = {13, 14};
static constexpr uint8_t COL_PINS[4] = {33, 34, 35, 36};
static constexpr uint8_t ENC_A = 37;
static constexpr uint8_t ENC_B = 38;
static constexpr uint8_t ENC_SW = 39;
static constexpr uint8_t RGB_DATA = 40;

static constexpr uint8_t MATRIX_ROWS = 2;
static constexpr uint8_t MATRIX_COLS = 4;
static constexpr uint8_t KEY_COUNT = 8;
static constexpr uint8_t PROFILE_COUNT = 5;
static constexpr uint16_t KC_NO = 0x0000;
static constexpr uint16_t KC_TRNS = 0x0001;
static constexpr uint16_t KC_AUDIO_MUTE = 0x00A8;
static constexpr uint16_t KC_AUDIO_VOL_UP = 0x00A9;
static constexpr uint16_t KC_AUDIO_VOL_DOWN = 0x00AA;
static constexpr uint16_t KC_MEDIA_NEXT_TRACK = 0x00AB;
static constexpr uint16_t KC_MEDIA_PREV_TRACK = 0x00AC;
static constexpr uint16_t KC_MEDIA_STOP = 0x00AD;
static constexpr uint16_t KC_MEDIA_PLAY_PAUSE = 0x00AE;

static uint16_t keymap[PROFILE_COUNT][MATRIX_ROWS][MATRIX_COLS];
static uint16_t encoderMap[PROFILE_COUNT][2];
static uint8_t activeProfile = 0;

static Preferences prefs;
static TFT_eSPI tft;

struct RawPacket {
  uint8_t data[RAW_REPORT_SIZE];
};
static QueueHandle_t rawQueue = nullptr;

// QMK Raw HID first, without a report ID; keyboard and consumer use IDs.
static const uint8_t hidReportDescriptor[] = {
  0x06, 0x60, 0xFF,
  0x09, 0x61,
  0xA1, 0x01,
  0x09, 0x62,
  0x15, 0x00,
  0x26, 0xFF, 0x00,
  0x75, 0x08,
  0x95, 0x20,
  0x81, 0x02,
  0x09, 0x63,
  0x15, 0x00,
  0x26, 0xFF, 0x00,
  0x75, 0x08,
  0x95, 0x20,
  0x91, 0x02,
  0xC0,

  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER))
};

extern "C" uint16_t pixel_hid_load_descriptor(uint8_t *dst, uint8_t *itf) {
  const uint8_t strIndex = tinyusb_add_string_descriptor("PIXEL PRO HID");
  const uint8_t epIn = tinyusb_get_free_in_endpoint();
  const uint8_t epOut = tinyusb_get_free_out_endpoint();
  TU_VERIFY(epIn != 0);
  TU_VERIFY(epOut != 0);

  const uint8_t descriptor[TUD_HID_INOUT_DESC_LEN] = {
    TUD_HID_INOUT_DESCRIPTOR(
      *itf, strIndex, HID_ITF_PROTOCOL_NONE,
      sizeof(hidReportDescriptor), epOut, (uint8_t)(0x80 | epIn), 64, 1)
  };
  *itf += 1;
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

extern "C" const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return hidReportDescriptor;
}

extern "C" uint16_t tud_hid_get_report_cb(
  uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
  uint8_t *buffer, uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

extern "C" void tud_hid_set_report_cb(
  uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
  const uint8_t *buffer, uint16_t bufsize) {
  (void)instance;
  if (report_id == 0 &&
      (report_type == HID_REPORT_TYPE_OUTPUT || report_type == HID_REPORT_TYPE_INVALID) &&
      buffer != nullptr && bufsize >= RAW_REPORT_SIZE && rawQueue != nullptr) {
    RawPacket packet{};
    memcpy(packet.data, buffer, RAW_REPORT_SIZE);
    xQueueSend(rawQueue, &packet, 0);
  }
}

extern "C" void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
  (void)instance;
  (void)protocol;
}

extern "C" bool tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate) {
  (void)instance;
  (void)idle_rate;
  return true;
}

static bool sendRaw(const uint8_t data[RAW_REPORT_SIZE]) {
  if (!tud_mounted() || !tud_hid_n_ready(0)) return false;
  return tud_hid_n_report(0, 0, data, RAW_REPORT_SIZE);
}

static void loadDefaults() {
  memset(keymap, 0, sizeof(keymap));
  memset(encoderMap, 0, sizeof(encoderMap));

  keymap[0][0][0] = 0x0106; // LCtrl+C
  keymap[0][0][1] = 0x0119; // LCtrl+V
  keymap[0][0][2] = 0x011D; // LCtrl+Z
  keymap[0][0][3] = 0x011C; // LCtrl+Y
  keymap[0][1][0] = KC_MEDIA_PREV_TRACK;
  keymap[0][1][1] = KC_MEDIA_PLAY_PAUSE;
  keymap[0][1][2] = KC_MEDIA_NEXT_TRACK;
  keymap[0][1][3] = KC_AUDIO_MUTE;

  for (uint8_t layer = 1; layer < PROFILE_COUNT; ++layer) {
    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
      keymap[layer][i / MATRIX_COLS][i % MATRIX_COLS] = 0x0068 + i; // F13..F20
    }
  }

  for (uint8_t layer = 0; layer < PROFILE_COUNT; ++layer) {
    encoderMap[layer][0] = KC_AUDIO_VOL_DOWN;
    encoderMap[layer][1] = KC_AUDIO_VOL_UP;
  }
}

static void saveKeymap() {
  prefs.putBytes("keymap", keymap, sizeof(keymap));
  prefs.putBytes("encmap", encoderMap, sizeof(encoderMap));
}

static void loadSettings() {
  prefs.begin("pixelpro", false);
  loadDefaults();
  if (prefs.getBytesLength("keymap") == sizeof(keymap)) {
    prefs.getBytes("keymap", keymap, sizeof(keymap));
  }
  if (prefs.getBytesLength("encmap") == sizeof(encoderMap)) {
    prefs.getBytes("encmap", encoderMap, sizeof(encoderMap));
  }
  activeProfile = (uint8_t)constrain((int)prefs.getUChar("profile", 0), 0, PROFILE_COUNT - 1);
}

static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_CARD = 0x18E3;
static const uint16_t C_CARD_PRESSED = 0xFD20;
static const uint16_t C_ACCENT = 0xFD20;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_MUTED = 0x9CF3;

static String keyName(uint16_t kc) {
  if (kc == KC_NO) return "NONE";
  if (kc == KC_TRNS) return "TRNS";

  uint16_t base = kc;
  if (kc >= 0x0100 && kc <= 0x1FFF) base = kc & 0xFF;

  if (base >= 0x04 && base <= 0x1D) {
    char c = 'A' + (char)(base - 0x04);
    String s(c);
    if (kc != base) {
      uint8_t m = (kc >> 8) & 0x1F;
      String p;
      if (m & 0x01) p += "C+";
      if (m & 0x02) p += "S+";
      if (m & 0x04) p += "A+";
      if (m & 0x08) p += "G+";
      return p + s;
    }
    return s;
  }
  if (base >= 0x1E && base <= 0x26) return String((int)(base - 0x1D));
  if (base == 0x27) return "0";
  if (base == 0x28) return "ENTER";
  if (base == 0x29) return "ESC";
  if (base == 0x2A) return "BKSP";
  if (base == 0x2B) return "TAB";
  if (base == 0x2C) return "SPACE";
  if (base >= 0x3A && base <= 0x45) return "F" + String((int)(base - 0x39));
  if (base >= 0x68 && base <= 0x73) return "F" + String((int)(base - 0x5B));
  if (kc == KC_AUDIO_MUTE) return "MUTE";
  if (kc == KC_AUDIO_VOL_UP) return "VOL+";
  if (kc == KC_AUDIO_VOL_DOWN) return "VOL-";
  if (kc == KC_MEDIA_NEXT_TRACK) return "NEXT";
  if (kc == KC_MEDIA_PREV_TRACK) return "PREV";
  if (kc == KC_MEDIA_STOP) return "STOP";
  if (kc == KC_MEDIA_PLAY_PAUSE) return "PLAY";
  char tmp[10];
  snprintf(tmp, sizeof(tmp), "%04X", kc);
  return String(tmp);
}

static void drawKey(uint8_t index, bool pressed) {
  const int gap = 6;
  const int top = 44;
  const int footer = 28;
  const int cardW = (480 - gap * 5) / 4;
  const int cardH = (320 - top - footer - gap * 3) / 2;
  int col = index % 4;
  int row = index / 4;
  int x = gap + col * (cardW + gap);
  int y = top + gap + row * (cardH + gap);

  tft.fillRoundRect(x, y, cardW, cardH, 10, pressed ? C_CARD_PRESSED : C_CARD);
  tft.drawRoundRect(x, y, cardW, cardH, 10, pressed ? TFT_WHITE : 0x4208);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(pressed ? TFT_BLACK : C_TEXT, pressed ? C_CARD_PRESSED : C_CARD);
  tft.setTextFont(4);
  String label = keyName(keymap[activeProfile][row][col]);
  tft.drawString(label, x + cardW / 2, y + cardH / 2 - 4);

  tft.setTextFont(2);
  tft.setTextColor(pressed ? TFT_BLACK : C_MUTED, pressed ? C_CARD_PRESSED : C_CARD);
  tft.drawString("K" + String(index + 1), x + cardW / 2, y + cardH - 15);
}

static void drawHeader() {
  tft.fillRect(0, 0, 480, 44, C_BG);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextFont(4);
  tft.drawString("PIXEL PRO", 10, 22);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("PROFILE " + String(activeProfile + 1), 470, 22);
}

static void drawFooter() {
  tft.fillRect(0, 292, 480, 28, C_BG);
  tft.setTextFont(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("VIA + LUMI   ESP32-S2", 10, 306);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("FW " PIXEL_FW_VERSION, 470, 306);
}

static void drawDashboard() {
  tft.fillScreen(C_BG);
  drawHeader();
  for (uint8_t i = 0; i < KEY_COUNT; ++i) drawKey(i, false);
  drawFooter();
}

static void setProfile(uint8_t profile) {
  activeProfile = profile % PROFILE_COUNT;
  prefs.putUChar("profile", activeProfile);
  drawDashboard();
}

static void showPcMonitor(int cpu, int cpuTemp, int gpu, int gpuTemp, int ram, int fps) {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextFont(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("PC MONITOR", 12, 10);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setTextFont(7);
  tft.drawString(String(cpu) + "%", 20, 62);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextFont(4);
  tft.drawString("CPU  " + String(cpuTemp) + "C", 24, 142);
  tft.drawString("GPU  " + String(gpu) + "%  " + String(gpuTemp) + "C", 244, 72);
  tft.drawString("RAM  " + String(ram) + "%", 244, 126);
  tft.drawString("FPS  " + String(fps), 244, 180);
  tft.setTextColor(C_MUTED, C_BG);
  tft.setTextFont(2);
  tft.drawString("Press any key to return", 12, 296);
}

static String urlDecode(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '%' && i + 2 < s.length()) {
      char h1 = s[i + 1], h2 = s[i + 2];
      auto hv = [](char h)->int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        return -1;
      };
      int a = hv(h1), b = hv(h2);
      if (a >= 0 && b >= 0) {
        out += (char)((a << 4) | b);
        i += 2;
        continue;
      }
    }
    if (c == '+') c = ' ';
    out += c;
  }
  return out;
}

static void showNowPlaying(const String &source, const String &title, const String &artist, bool playing) {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setTextFont(4);
  tft.drawString(playing ? "NOW PLAYING" : "MEDIA", 12, 12);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextFont(4);
  tft.drawString(title.substring(0, 24), 18, 82);
  tft.setTextColor(C_MUTED, C_BG);
  tft.setTextFont(2);
  tft.drawString(artist.substring(0, 38), 20, 140);
  tft.drawString(source.substring(0, 30), 20, 180);
  tft.drawString("Press any key to return", 20, 294);
}

static bool stableKeys[KEY_COUNT] = {};
static bool rawKeys[KEY_COUNT] = {};
static uint32_t rawChangedAt[KEY_COUNT] = {};
static constexpr uint32_t DEBOUNCE_MS = 8;

static uint16_t consumerUsage(uint16_t kc) {
  switch (kc) {
    case KC_AUDIO_MUTE: return 0x00E2;
    case KC_AUDIO_VOL_UP: return 0x00E9;
    case KC_AUDIO_VOL_DOWN: return 0x00EA;
    case KC_MEDIA_NEXT_TRACK: return 0x00B5;
    case KC_MEDIA_PREV_TRACK: return 0x00B6;
    case KC_MEDIA_STOP: return 0x00B7;
    case KC_MEDIA_PLAY_PAUSE: return 0x00CD;
    default: return 0;
  }
}

static void buildAndSendHidState() {
  uint8_t modifiers = 0;
  uint8_t keys[6] = {0};
  uint8_t keyCount = 0;
  uint16_t consumer = 0;

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (!stableKeys[i]) continue;
    uint16_t kc = keymap[activeProfile][i / MATRIX_COLS][i % MATRIX_COLS];
    if (kc == KC_NO || kc == KC_TRNS) continue;

    uint16_t media = consumerUsage(kc);
    if (media) {
      if (!consumer) consumer = media;
      continue;
    }

    uint16_t base = kc;
    if (kc >= 0x0100 && kc <= 0x1FFF) {
      uint8_t qmods = (kc >> 8) & 0x1F;
      bool right = (qmods & 0x10) != 0;
      uint8_t low = qmods & 0x0F;
      modifiers |= right ? (uint8_t)(low << 4) : low;
      base = kc & 0xFF;
    }

    if (base >= 0xE0 && base <= 0xE7) {
      modifiers |= (uint8_t)(1U << (base - 0xE0));
    } else if (base >= 0x04 && base <= 0xA4 && keyCount < 6) {
      keys[keyCount++] = (uint8_t)base;
    }
  }

  if (tud_mounted() && tud_hid_n_ready(0)) {
    tud_hid_n_keyboard_report(0, REPORT_ID_KEYBOARD, modifiers, keys);
    delayMicroseconds(250);
    if (tud_hid_n_ready(0)) {
      tud_hid_n_report(0, REPORT_ID_CONSUMER, &consumer, sizeof(consumer));
    }
  }
}

static void tapKeycode(uint16_t kc) {
  uint16_t media = consumerUsage(kc);
  if (media) {
    if (tud_mounted() && tud_hid_n_ready(0)) {
      tud_hid_n_report(0, REPORT_ID_CONSUMER, &media, sizeof(media));
      delay(15);
      uint16_t zero = 0;
      while (!tud_hid_n_ready(0)) delay(1);
      tud_hid_n_report(0, REPORT_ID_CONSUMER, &zero, sizeof(zero));
    }
    return;
  }

  uint8_t mods = 0;
  uint16_t base = kc;
  if (kc >= 0x0100 && kc <= 0x1FFF) {
    uint8_t qmods = (kc >> 8) & 0x1F;
    bool right = (qmods & 0x10) != 0;
    uint8_t low = qmods & 0x0F;
    mods = right ? (uint8_t)(low << 4) : low;
    base = kc & 0xFF;
  }
  if (base >= 0x04 && base <= 0xA4) {
    uint8_t list[6] = {(uint8_t)base, 0, 0, 0, 0, 0};
    if (tud_mounted() && tud_hid_n_ready(0)) {
      tud_hid_n_keyboard_report(0, REPORT_ID_KEYBOARD, mods, list);
      delay(15);
      memset(list, 0, sizeof(list));
      while (!tud_hid_n_ready(0)) delay(1);
      tud_hid_n_keyboard_report(0, REPORT_ID_KEYBOARD, 0, list);
    }
  }
}

static void initMatrix() {
  for (uint8_t c = 0; c < MATRIX_COLS; ++c) pinMode(COL_PINS[c], INPUT_PULLUP);
  for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], HIGH);
  }
}

static void scanMatrix() {
  bool now[KEY_COUNT] = {};

  for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(4);
    for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
      now[r * MATRIX_COLS + c] = digitalRead(COL_PINS[c]) == LOW;
    }
    digitalWrite(ROW_PINS[r], HIGH);
  }

  uint32_t ms = millis();
  bool hidDirty = false;
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (now[i] != rawKeys[i]) {
      rawKeys[i] = now[i];
      rawChangedAt[i] = ms;
    }
    if (rawKeys[i] != stableKeys[i] && (ms - rawChangedAt[i]) >= DEBOUNCE_MS) {
      stableKeys[i] = rawKeys[i];
      drawDashboard();
      drawKey(i, stableKeys[i]);
      hidDirty = true;
    }
  }
  if (hidDirty) buildAndSendHidState();
}

static int8_t encAccum = 0;
static uint8_t encPrev = 0;
static const int8_t ENC_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
static bool encBtnStable = true;
static bool encBtnRaw = true;
static uint32_t encBtnChanged = 0;

static void initEncoder() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  encPrev = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
}

static void scanEncoder() {
  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  uint8_t idx = (encPrev << 2) | state;
  encPrev = state;
  encAccum += ENC_TABLE[idx & 0x0F];

  if (encAccum >= 4) {
    encAccum = 0;
    tapKeycode(encoderMap[activeProfile][1]);
  } else if (encAccum <= -4) {
    encAccum = 0;
    tapKeycode(encoderMap[activeProfile][0]);
  }

  bool raw = digitalRead(ENC_SW) != LOW;
  uint32_t ms = millis();
  if (raw != encBtnRaw) {
    encBtnRaw = raw;
    encBtnChanged = ms;
  }
  if (encBtnStable != encBtnRaw && ms - encBtnChanged >= 12) {
    encBtnStable = encBtnRaw;
    if (!encBtnStable) setProfile((activeProfile + 1) % PROFILE_COUNT);
  }
}

enum ViaCommand : uint8_t {
  VIA_GET_PROTOCOL_VERSION = 0x01,
  VIA_DYNAMIC_KEYMAP_GET_KEYCODE = 0x04,
  VIA_DYNAMIC_KEYMAP_SET_KEYCODE = 0x05,
  VIA_EEPROM_RESET = 0x0A,
  VIA_BOOTLOADER_JUMP = 0x0B,
  VIA_MACRO_GET_COUNT = 0x0C,
  VIA_MACRO_GET_BUFFER_SIZE = 0x0D,
  VIA_MACRO_GET_BUFFER = 0x0E,
  VIA_MACRO_SET_BUFFER = 0x0F,
  VIA_MACRO_RESET = 0x10,
  VIA_DYNAMIC_KEYMAP_GET_LAYER_COUNT = 0x11,
  VIA_DYNAMIC_KEYMAP_GET_BUFFER = 0x12,
  VIA_DYNAMIC_KEYMAP_SET_BUFFER = 0x13,
  VIA_DYNAMIC_KEYMAP_GET_ENCODER = 0x14,
  VIA_DYNAMIC_KEYMAP_SET_ENCODER = 0x15
};

static uint8_t keymapByteAt(uint16_t offset) {
  const uint16_t total = PROFILE_COUNT * MATRIX_ROWS * MATRIX_COLS;
  uint16_t word = offset / 2;
  if (word >= total) return 0;
  uint8_t layer = word / (MATRIX_ROWS * MATRIX_COLS);
  uint8_t rem = word % (MATRIX_ROWS * MATRIX_COLS);
  uint8_t row = rem / MATRIX_COLS;
  uint8_t col = rem % MATRIX_COLS;
  uint16_t kc = keymap[layer][row][col];
  return (offset & 1) ? (uint8_t)(kc & 0xFF) : (uint8_t)(kc >> 8);
}

static void setKeymapByteAt(uint16_t offset, uint8_t value) {
  const uint16_t total = PROFILE_COUNT * MATRIX_ROWS * MATRIX_COLS;
  uint16_t word = offset / 2;
  if (word >= total) return;
  uint8_t layer = word / (MATRIX_ROWS * MATRIX_COLS);
  uint8_t rem = word % (MATRIX_ROWS * MATRIX_COLS);
  uint8_t row = rem / MATRIX_COLS;
  uint8_t col = rem % MATRIX_COLS;
  uint16_t &kc = keymap[layer][row][col];
  if (offset & 1) kc = (kc & 0xFF00) | value;
  else kc = (kc & 0x00FF) | ((uint16_t)value << 8);
}

static void processVia(const uint8_t req[RAW_REPORT_SIZE]) {
  uint8_t resp[RAW_REPORT_SIZE];
  memcpy(resp, req, RAW_REPORT_SIZE);

  switch (req[0]) {
    case VIA_GET_PROTOCOL_VERSION:
      resp[1] = 0x00;
      resp[2] = 0x09;
      break;

    case VIA_DYNAMIC_KEYMAP_GET_KEYCODE: {
      uint8_t l = req[1], r = req[2], c = req[3];
      uint16_t kc = (l < PROFILE_COUNT && r < MATRIX_ROWS && c < MATRIX_COLS)
        ? keymap[l][r][c] : KC_NO;
      resp[4] = (uint8_t)(kc >> 8);
      resp[5] = (uint8_t)kc;
      break;
    }

    case VIA_DYNAMIC_KEYMAP_SET_KEYCODE: {
      uint8_t l = req[1], r = req[2], c = req[3];
      if (l < PROFILE_COUNT && r < MATRIX_ROWS && c < MATRIX_COLS) {
        keymap[l][r][c] = ((uint16_t)req[4] << 8) | req[5];
        saveKeymap();
        if (l == activeProfile) drawDashboard();
      }
      break;
    }

    case VIA_DYNAMIC_KEYMAP_GET_LAYER_COUNT:
      resp[1] = PROFILE_COUNT;
      break;

    case VIA_DYNAMIC_KEYMAP_GET_BUFFER: {
      uint16_t offset = ((uint16_t)req[1] << 8) | req[2];
      uint8_t size = min<uint8_t>(req[3], RAW_REPORT_SIZE - 4);
      for (uint8_t i = 0; i < size; ++i) resp[4 + i] = keymapByteAt(offset + i);
      break;
    }

    case VIA_DYNAMIC_KEYMAP_SET_BUFFER: {
      uint16_t offset = ((uint16_t)req[1] << 8) | req[2];
      uint8_t size = min<uint8_t>(req[3], RAW_REPORT_SIZE - 4);
      for (uint8_t i = 0; i < size; ++i) setKeymapByteAt(offset + i, req[4 + i]);
      saveKeymap();
      drawDashboard();
      break;
    }

    case VIA_DYNAMIC_KEYMAP_GET_ENCODER: {
      uint8_t l = req[1], id = req[2], clockwise = req[3];
      uint16_t kc = (l < PROFILE_COUNT && id == 0)
        ? encoderMap[l][clockwise ? 1 : 0] : KC_NO;
      resp[4] = (uint8_t)(kc >> 8);
      resp[5] = (uint8_t)kc;
      break;
    }

    case VIA_DYNAMIC_KEYMAP_SET_ENCODER: {
      uint8_t l = req[1], id = req[2], clockwise = req[3];
      if (l < PROFILE_COUNT && id == 0) {
        encoderMap[l][clockwise ? 1 : 0] = ((uint16_t)req[4] << 8) | req[5];
        saveKeymap();
      }
      break;
    }

    case VIA_EEPROM_RESET:
      loadDefaults();
      saveKeymap();
      setProfile(0);
      break;

    case VIA_BOOTLOADER_JUMP:
      sendRaw(resp);
      delay(80);
      usb_persist_restart(RESTART_BOOTLOADER);
      return;

    case VIA_MACRO_GET_COUNT:
      resp[1] = 0;
      break;

    case VIA_MACRO_GET_BUFFER_SIZE:
      resp[1] = 0;
      resp[2] = 0;
      break;

    case VIA_MACRO_GET_BUFFER:
    case VIA_MACRO_SET_BUFFER:
    case VIA_MACRO_RESET:
      break;

    default:
      resp[0] = 0xFF;
      break;
  }

  sendRaw(resp);
}

static constexpr uint8_t LUMI_MAGIC0 = 'L';
static constexpr uint8_t LUMI_MAGIC1 = 'Q';
static constexpr uint8_t LUMI_FRAME_VERSION = 1;
static constexpr uint8_t LUMI_FLAG_START = 0x01;
static constexpr uint8_t LUMI_FLAG_END = 0x02;
static constexpr uint8_t LUMI_FLAG_RESPONSE = 0x04;
static constexpr uint8_t LUMI_HEADER_SIZE = 7;
static constexpr uint8_t LUMI_PAYLOAD_SIZE = RAW_REPORT_SIZE - LUMI_HEADER_SIZE;
static constexpr size_t LUMI_MAX_MESSAGE = 1024;

static char lumiRx[LUMI_MAX_MESSAGE + 1];
static size_t lumiRxLen = 0;
static uint8_t lumiSeq = 0;
static uint8_t lumiFrag = 0;
static bool lumiActive = false;

static uint32_t actionSeq = 0;
static int lastActionId = 0;
static int lastActionPosition = 0;

static void lumiSendResponse(uint8_t seq, const String &text) {
  size_t len = text.length();
  size_t fragments = len == 0 ? 1 : (len + LUMI_PAYLOAD_SIZE - 1) / LUMI_PAYLOAD_SIZE;

  for (size_t f = 0; f < fragments; ++f) {
    uint8_t report[RAW_REPORT_SIZE] = {};
    size_t offset = f * LUMI_PAYLOAD_SIZE;
    size_t count = offset < len ? min((size_t)LUMI_PAYLOAD_SIZE, len - offset) : 0;

    report[0] = LUMI_MAGIC0;
    report[1] = LUMI_MAGIC1;
    report[2] = LUMI_FRAME_VERSION;
    report[3] = LUMI_FLAG_RESPONSE;
    if (f == 0) report[3] |= LUMI_FLAG_START;
    if (f == fragments - 1) report[3] |= LUMI_FLAG_END;
    report[4] = seq;
    report[5] = (uint8_t)f;
    report[6] = (uint8_t)count;
    if (count) memcpy(&report[LUMI_HEADER_SIZE], text.c_str() + offset, count);

    for (int retry = 0; retry < 50; ++retry) {
      if (sendRaw(report)) break;
      delay(1);
    }
  }
}

static bool splitField(const String &s, int field, String &out) {
  int start = 0;
  int current = 0;
  for (int i = 0; i <= (int)s.length(); ++i) {
    if (i == (int)s.length() || s[i] == '|') {
      if (current == field) {
        out = s.substring(start, i);
        return true;
      }
      current++;
      start = i + 1;
    }
  }
  return false;
}

static String processLumiCommand(const String &cmd) {
  if (cmd == "HELLO") {
    return "LUMIPAD|3|FW=" PIXEL_FW_VERSION "|CAPS=PROFILE,ACTION,PCMON,PANEL,MEM,SAVERSTATE";
  }
  if (cmd == "PANEL") {
    return "PANEL|ILI9486-8BIT|60|0|25";
  }
  if (cmd == "MEM") {
    uint32_t flashTotal = ESP.getFlashChipSize();
    uint32_t flashUsed = ESP.getSketchSize();
    uint32_t ramTotal = ESP.getHeapSize();
    uint32_t ramUsed = ramTotal - ESP.getFreeHeap();
    return "MEM|" + String(flashUsed) + "|" + String(flashTotal) + "|" +
           String(ramUsed) + "|" + String(ramTotal);
  }
  if (cmd.startsWith("ACTION|")) {
    uint32_t after = cmd.substring(7).toInt();
    if (actionSeq > after && lastActionId > 0) {
      return "ACTION|" + String(actionSeq) + "|" + String(lastActionId) + "|" + String(lastActionPosition);
    }
    return "ACTION|NONE|" + String(actionSeq);
  }
  if (cmd == "BAT") return "BAT|100";
  if (cmd == "SAVERSTATE") return "SAVERSTATE|SRC=0;DELAY=30;SLEEP=0";

  if (cmd.startsWith("CFG|PROFILE|")) {
    int p = cmd.substring(12).toInt();
    if (p >= 0 && p < PROFILE_COUNT) setProfile((uint8_t)p);
    return "";
  }

  if (cmd.startsWith("PCMON|")) {
    String f;
    int cpu = 0, cpuTemp = -1, gpu = 0, gpuTemp = -1, ram = 0, fps = -1;
    if (splitField(cmd, 1, f)) cpu = f.toInt();
    if (splitField(cmd, 2, f)) cpuTemp = f.toInt();
    if (splitField(cmd, 4, f)) gpu = f.toInt();
    if (splitField(cmd, 5, f)) gpuTemp = f.toInt();
    if (splitField(cmd, 7, f)) ram = f.toInt();
    if (splitField(cmd, 12, f)) fps = f.toInt();
    showPcMonitor(cpu, cpuTemp, gpu, gpuTemp, ram, fps);
    return "";
  }

  if (cmd == "PCMONCLR") {
    drawDashboard();
    return "";
  }

  if (cmd.startsWith("NP|")) {
    String playingS, source, title, artist;
    splitField(cmd, 3, playingS);
    splitField(cmd, 4, source);
    splitField(cmd, 5, title);
    splitField(cmd, 6, artist);
    showNowPlaying(urlDecode(source), urlDecode(title), urlDecode(artist), playingS.toInt() != 0);
    return "";
  }

  if (cmd == "NPCLR") {
    drawDashboard();
    return "";
  }

  if (cmd == "SYS|RESTART") {
    delay(30);
    ESP.restart();
    return "";
  }
  if (cmd == "SYS|DFU") {
    delay(30);
    usb_persist_restart(RESTART_BOOTLOADER);
    return "";
  }
  if (cmd == "SYS|WAKE") {
    drawDashboard();
    return "";
  }
  if (cmd == "SYS|SLEEP") {
    tft.fillScreen(TFT_BLACK);
    return "";
  }

  if (cmd.startsWith("RGB|") || cmd.startsWith("CFG|") || cmd.startsWith("PCCFG|") ||
      cmd.startsWith("IMGBEGIN|") || cmd.startsWith("IMGCHUNK|") || cmd == "IMGEND" ||
      cmd.startsWith("SAVBEGIN|") || cmd.startsWith("SAVCHUNK|") ||
      cmd == "SAVEND" || cmd == "SAVCLEAR") {
    return "";
  }

  return "";
}

static void processLumiFrame(const uint8_t data[RAW_REPORT_SIZE]) {
  uint8_t flags = data[3];
  uint8_t seq = data[4];
  uint8_t fragment = data[5];
  uint8_t count = data[6];

  if ((flags & LUMI_FLAG_RESPONSE) || count > LUMI_PAYLOAD_SIZE) return;

  if (flags & LUMI_FLAG_START) {
    lumiActive = true;
    lumiSeq = seq;
    lumiFrag = 0;
    lumiRxLen = 0;
  }

  if (!lumiActive || seq != lumiSeq || fragment != lumiFrag) {
    lumiActive = false;
    return;
  }
  if (lumiRxLen + count > LUMI_MAX_MESSAGE) {
    lumiActive = false;
    return;
  }

  if (count) {
    memcpy(lumiRx + lumiRxLen, data + LUMI_HEADER_SIZE, count);
    lumiRxLen += count;
  }
  lumiFrag++;

  if (!(flags & LUMI_FLAG_END)) return;

  lumiActive = false;
  lumiRx[lumiRxLen] = '\0';
  String command(lumiRx);
  String response = processLumiCommand(command);
  if (command == "HELLO" || command == "PANEL" || command == "MEM" ||
      command == "BAT" || command == "SAVERSTATE" || command.startsWith("ACTION|")) {
    lumiSendResponse(seq, response);
  }
}

static void processRawPacket(const uint8_t data[RAW_REPORT_SIZE]) {
  if (data[0] == LUMI_MAGIC0 && data[1] == LUMI_MAGIC1 && data[2] == LUMI_FRAME_VERSION) {
    processLumiFrame(data);
  } else {
    processVia(data);
  }
}

static bool initUsb() {
  rawQueue = xQueueCreate(16, sizeof(RawPacket));
  if (!rawQueue) return false;

  esp_err_t err = tinyusb_enable_interface(
    USB_INTERFACE_HID, TUD_HID_INOUT_DESC_LEN, pixel_hid_load_descriptor);
  if (err != ESP_OK) return false;

  tinyusb_device_config_t cfg = TINYUSB_CONFIG_DEFAULT();
  cfg.vid = PIXEL_USB_VID;
  cfg.pid = PIXEL_USB_PID;
  cfg.product_name = "PIXEL PRO";
  cfg.manufacturer_name = "Lumi3D";
  cfg.serial_number = "PIXELPRO-S2";
  cfg.usb_class = 0;
  cfg.usb_subclass = 0;
  cfg.usb_protocol = 0;
  cfg.usb_attributes = TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP;
  cfg.usb_power_ma = 500;
  return tinyusb_init(&cfg) == ESP_OK;
}

void setup() {
  loadSettings();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("LUMI3D", 240, 126);
  tft.setTextColor(C_ACCENT, TFT_BLACK);
  tft.drawString("PIXEL PRO", 240, 166);
  delay(500);

  initMatrix();
  initEncoder();
  initUsb();
  drawDashboard();
}

void loop() {
  RawPacket packet;
  while (rawQueue && xQueueReceive(rawQueue, &packet, 0) == pdTRUE) {
    processRawPacket(packet.data);
  }

  scanMatrix();
  scanEncoder();
  delay(1);
}
