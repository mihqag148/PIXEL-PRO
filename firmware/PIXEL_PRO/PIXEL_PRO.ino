#include <Arduino.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include <mbedtls/base64.h>
#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error PIXEL PRO composite firmware requires USB CDC On Boot disabled
#else
USBCDC USBSerial;
#endif

static constexpr char FW_VERSION[] = "1.3.3";
static constexpr uint16_t USB_VID_PIXEL = 0x303A;
static constexpr uint16_t USB_PID_PIXEL = 0x80C2;
static constexpr uint8_t KEY_COUNT = 8;
static constexpr uint8_t PROFILE_COUNT = 20;
static constexpr uint8_t LAYER_COUNT = 4;
static constexpr uint8_t MACRO_COUNT = 20;
static constexpr uint8_t ACTION_COUNT = 32;
static constexpr uint8_t MACRO_MAX_LEN = 80;
static constexpr uint32_t DEBOUNCE_MS = 8;

// PIXEL PRO display: 3.5" ILI9486, 480x320 landscape, i8080 8-bit.
// The physical shield follows the common UNO/Mega2560 8-bit shield signal
// layout; HARDWARE.md maps those shield pins to these ESP32-S2 pins.
static constexpr uint16_t TFT_WIDTH = 480;
static constexpr uint16_t TFT_HEIGHT = 320;
static constexpr uint16_t GIF_WIDTH = 240;
static constexpr uint16_t GIF_HEIGHT = 160;
static constexpr uint8_t GIF_MAX_FRAMES = 32;
static constexpr uint8_t DISPLAY_REFRESH_CAP_HZ = 60;
static constexpr uint8_t GIF_MAX_FPS = 60;
static constexpr uint16_t GIF_MIN_FRAME_MS = 17;

static constexpr int8_t TFT_RD = 12;
static constexpr int8_t TFT_WR = 13;
static constexpr int8_t TFT_DC = 14;
static constexpr int8_t TFT_CS = 16;
static constexpr int8_t TFT_RST = 17;
static constexpr int8_t TFT_D0 = 33;
static constexpr int8_t TFT_D1 = 34;
static constexpr int8_t TFT_D2 = 35;
static constexpr int8_t TFT_D3 = 36;
static constexpr int8_t TFT_D4 = 37;
static constexpr int8_t TFT_D5 = 38;
static constexpr int8_t TFT_D6 = 39;
static constexpr int8_t TFT_D7 = 40;

static constexpr uint8_t BIND_DISABLED = 0;
static constexpr uint8_t BIND_KEYBOARD = 1;
static constexpr uint8_t BIND_CONSUMER = 2;
static constexpr uint8_t BIND_LAYER = 3;
static constexpr uint8_t BIND_MACRO = 4;
static constexpr uint8_t BIND_TRANSPARENT = 5;
static constexpr uint8_t BIND_ACTION = 6;

static constexpr uint8_t LAYER_MO = 1;
static constexpr uint8_t LAYER_TG = 2;
static constexpr uint8_t LAYER_TO = 3;
static constexpr uint8_t KEYMAP_STORAGE_VERSION = 3;

static const uint8_t KEY_PINS[KEY_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8};

struct __attribute__((packed)) KeyBinding {
  uint8_t type;
  uint8_t keyCode;
  uint8_t modifiers;
  uint16_t consumerCode;
};

struct KeyState {
  bool rawPressed;
  bool stablePressed;
  uint32_t changedAt;
};

USBHID HID;
USBHIDKeyboard Keyboard;
USBHIDConsumerControl ConsumerControl;
Preferences preferences;

Arduino_DataBus *tftBus =
    new Arduino_ESP32PAR8(
        TFT_DC,
        TFT_CS,
        TFT_WR,
        TFT_RD,
        TFT_D0,
        TFT_D1,
        TFT_D2,
        TFT_D3,
        TFT_D4,
        TFT_D5,
        TFT_D6,
        TFT_D7);

Arduino_GFX *tft =
    new Arduino_ILI9486(
        tftBus,
        TFT_RST,
        1,
        false);

static KeyState keyState[KEY_COUNT] = {};
static KeyBinding keymap[PROFILE_COUNT][LAYER_COUNT][KEY_COUNT] = {};
static KeyBinding activeBindings[KEY_COUNT] = {};
static String macros[MACRO_COUNT];

static uint8_t pressedMask = 0;
static uint16_t activeConsumerCode = 0;
static uint8_t activeProfile = 0;
static uint8_t baseLayer = 0;
static int8_t momentaryLayer = -1;
static uint8_t toggledLayerMask = 0;
static String cdcLine;

enum SaverPixelFormat : uint8_t {
  SAVER_NONE = 0,
  SAVER_RGB332 = 1,
  SAVER_RGB565 = 2,
};

static bool displayReady = false;
static uint16_t *renderBuffer = nullptr;

static uint8_t *saverData = nullptr;
static size_t saverDataBytes = 0;
static size_t saverFrameBytes = 0;
static size_t saverBytesReceived = 0;
static uint8_t saverFrameCount = 0;
static uint16_t saverWidth = 0;
static uint16_t saverHeight = 0;
static SaverPixelFormat saverFormat = SAVER_NONE;
static uint16_t saverDurations[GIF_MAX_FRAMES] = {};
static bool saverUploading = false;
static bool saverReady = false;
static bool saverActive = false;
static uint8_t saverFrameIndex = 0;
static uint32_t saverFrameStartedAt = 0;
static uint32_t lastUserActivityAt = 0;
static uint32_t saverDelayMs = 60000;

static void cdcPrintln(const String &line) {
  USBSerial.println(line);
}

static KeyBinding disabledBinding() {
  KeyBinding binding = {};
  binding.type = BIND_DISABLED;
  return binding;
}

static KeyBinding transparentBinding() {
  KeyBinding binding = {};
  binding.type = BIND_TRANSPARENT;
  return binding;
}

static void setDefaultKeymap() {
  for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
    for (uint8_t layer = 0; layer < LAYER_COUNT; ++layer) {
      for (uint8_t i = 0; i < KEY_COUNT; ++i) {
        keymap[profile][layer][i] =
            layer == 0 ? disabledBinding() : transparentBinding();
      }
    }

    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
      keymap[profile][0][i].type = BIND_KEYBOARD;
      keymap[profile][0][i].keyCode = static_cast<uint8_t>(HID_KEY_A + i);
      keymap[profile][0][i].modifiers = 0;
      keymap[profile][0][i].consumerCode = 0;
    }
  }
}

static bool bindingIsValid(const KeyBinding &binding) {
  switch (binding.type) {
    case BIND_DISABLED:
    case BIND_TRANSPARENT:
      return binding.keyCode == 0 &&
             binding.modifiers == 0 &&
             binding.consumerCode == 0;

    case BIND_KEYBOARD:
      return (binding.keyCode != 0 || binding.modifiers != 0) &&
             (binding.modifiers & 0xF0) == 0 &&
             binding.consumerCode == 0;

    case BIND_CONSUMER:
      return binding.keyCode == 0 &&
             binding.modifiers == 0 &&
             binding.consumerCode != 0;

    case BIND_LAYER:
      return binding.keyCode < LAYER_COUNT &&
             (binding.modifiers == LAYER_MO ||
              binding.modifiers == LAYER_TG ||
              binding.modifiers == LAYER_TO) &&
             binding.consumerCode == 0;

    case BIND_MACRO:
      return binding.keyCode < MACRO_COUNT &&
             binding.modifiers == 0 &&
             binding.consumerCode == 0;

    case BIND_ACTION:
      return binding.keyCode >= 1 &&
             binding.keyCode <= ACTION_COUNT &&
             binding.modifiers == 0 &&
             binding.consumerCode == 0;

    default:
      return false;
  }
}

static void saveKeymap() {
  preferences.putUChar("mapver", KEYMAP_STORAGE_VERSION);
  preferences.putBytes("keymap", keymap, sizeof(keymap));
}

static void loadKeymap() {
  setDefaultKeymap();

  if (preferences.getUChar("mapver", 0) != KEYMAP_STORAGE_VERSION ||
      preferences.getBytesLength("keymap") != sizeof(keymap)) {
    saveKeymap();
    return;
  }

  static KeyBinding stored[PROFILE_COUNT][LAYER_COUNT][KEY_COUNT] = {};
  size_t read = preferences.getBytes("keymap", stored, sizeof(stored));

  if (read != sizeof(stored)) {
    saveKeymap();
    return;
  }

  for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
    for (uint8_t layer = 0; layer < LAYER_COUNT; ++layer) {
      for (uint8_t i = 0; i < KEY_COUNT; ++i) {
        if (!bindingIsValid(stored[profile][layer][i])) {
          saveKeymap();
          return;
        }
      }
    }
  }

  memcpy(keymap, stored, sizeof(keymap));
}

static void loadMacros() {
  for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
    char key[5];
    snprintf(key, sizeof(key), "m%u", i);
    macros[i] = preferences.getString(key, "");
    if (macros[i].length() > MACRO_MAX_LEN) {
      macros[i].remove(MACRO_MAX_LEN);
    }
  }
}

static void saveMacro(uint8_t index) {
  if (index >= MACRO_COUNT) {
    return;
  }

  char key[5];
  snprintf(key, sizeof(key), "m%u", index);
  preferences.putString(key, macros[index]);
}

static uint8_t currentLayer() {
  if (momentaryLayer >= 0 && momentaryLayer < LAYER_COUNT) {
    return static_cast<uint8_t>(momentaryLayer);
  }

  for (int8_t layer = LAYER_COUNT - 1; layer >= 0; --layer) {
    if ((toggledLayerMask & static_cast<uint8_t>(1U << layer)) != 0) {
      return static_cast<uint8_t>(layer);
    }
  }

  return baseLayer;
}

static KeyBinding resolveBinding(uint8_t layer, uint8_t keyIndex) {
  int8_t scan = static_cast<int8_t>(layer);

  while (scan >= 0) {
    const KeyBinding &binding = keymap[activeProfile][scan][keyIndex];

    if (binding.type != BIND_TRANSPARENT) {
      return binding;
    }

    --scan;
  }

  return disabledBinding();
}

static String serializeBinding(const KeyBinding &binding) {
  char out[28];

  switch (binding.type) {
    case BIND_DISABLED:
      return String("D:0:0");

    case BIND_TRANSPARENT:
      return String("T:0:0");

    case BIND_CONSUMER:
      snprintf(
          out,
          sizeof(out),
          "C:%u:0",
          static_cast<unsigned>(binding.consumerCode));
      return String(out);

    case BIND_LAYER:
      snprintf(
          out,
          sizeof(out),
          "L:%u:%u",
          static_cast<unsigned>(binding.keyCode),
          static_cast<unsigned>(binding.modifiers));
      return String(out);

    case BIND_MACRO:
      snprintf(
          out,
          sizeof(out),
          "M:%u:0",
          static_cast<unsigned>(binding.keyCode));
      return String(out);

    case BIND_ACTION:
      snprintf(
          out,
          sizeof(out),
          "A:%u:0",
          static_cast<unsigned>(binding.keyCode));
      return String(out);

    case BIND_KEYBOARD:
    default:
      snprintf(
          out,
          sizeof(out),
          "K:%u:%u",
          static_cast<unsigned>(binding.keyCode),
          static_cast<unsigned>(binding.modifiers));
      return String(out);
  }
}

static String serializeKeymap(uint8_t profile, uint8_t layer) {
  String out = "KEYMAP|";
  out += String(profile);
  out += '|';
  out += String(layer);
  out += '|';

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (i) {
      out += ',';
    }

    out += serializeBinding(keymap[profile][layer][i]);
  }

  return out;
}

static bool parseUnsigned(
    const String &text,
    uint16_t maxValue,
    uint16_t &value) {
  if (text.length() == 0) {
    return false;
  }

  for (size_t i = 0; i < text.length(); ++i) {
    if (!isDigit(text[i])) {
      return false;
    }
  }

  unsigned long parsed = text.toInt();
  if (parsed > maxValue) {
    return false;
  }

  value = static_cast<uint16_t>(parsed);
  return true;
}

static bool parseUnsignedLong(
    const String &text,
    uint32_t maxValue,
    uint32_t &value) {
  if (text.length() == 0) {
    return false;
  }

  for (size_t i = 0; i < text.length(); ++i) {
    if (!isDigit(text[i])) {
      return false;
    }
  }

  unsigned long parsed = strtoul(text.c_str(), nullptr, 10);

  if (parsed > maxValue) {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
  return true;
}

static bool parseBindingToken(String token, KeyBinding &binding) {
  token.trim();
  token.toUpperCase();

  int first = token.indexOf(':');
  int second = first >= 0 ? token.indexOf(':', first + 1) : -1;

  if (first != 1 || second < 0) {
    return false;
  }

  char type = token[0];
  String codePart = token.substring(first + 1, second);
  String modsPart = token.substring(second + 1);

  uint16_t code = 0;
  uint16_t mods = 0;

  if (!parseUnsigned(codePart, 0xFFFF, code) ||
      !parseUnsigned(modsPart, 0xFF, mods)) {
    return false;
  }

  binding = {};

  if (type == 'D') {
    binding.type = BIND_DISABLED;
    return code == 0 && mods == 0;
  }

  if (type == 'T') {
    binding.type = BIND_TRANSPARENT;
    return code == 0 && mods == 0;
  }

  if (type == 'K') {
    if ((code == 0 && mods == 0) || code > 0xFF || mods > 0x0F) {
      return false;
    }

    binding.type = BIND_KEYBOARD;
    binding.keyCode = static_cast<uint8_t>(code);
    binding.modifiers = static_cast<uint8_t>(mods);
    return true;
  }

  if (type == 'C') {
    if (code == 0 || mods != 0) {
      return false;
    }

    binding.type = BIND_CONSUMER;
    binding.consumerCode = code;
    return true;
  }

  if (type == 'L') {
    if (code >= LAYER_COUNT ||
        (mods != LAYER_MO && mods != LAYER_TG && mods != LAYER_TO)) {
      return false;
    }

    binding.type = BIND_LAYER;
    binding.keyCode = static_cast<uint8_t>(code);
    binding.modifiers = static_cast<uint8_t>(mods);
    return true;
  }

  if (type == 'M') {
    if (code >= MACRO_COUNT || mods != 0) {
      return false;
    }

    binding.type = BIND_MACRO;
    binding.keyCode = static_cast<uint8_t>(code);
    return true;
  }

  if (type == 'A') {
    if (code < 1 || code > ACTION_COUNT || mods != 0) {
      return false;
    }

    binding.type = BIND_ACTION;
    binding.keyCode = static_cast<uint8_t>(code);
    return true;
  }

  return false;
}

static bool parseKeymapPayload(
    const String &payload,
    KeyBinding (&parsed)[KEY_COUNT]) {
  int start = 0;

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    int comma = payload.indexOf(',', start);
    bool last = i == KEY_COUNT - 1;

    if ((!last && comma < 0) || (last && comma >= 0)) {
      return false;
    }

    String token =
        last ? payload.substring(start) : payload.substring(start, comma);

    if (!parseBindingToken(token, parsed[i])) {
      return false;
    }

    start = comma + 1;
  }

  return true;
}

static String hexEncode(const String &input) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String out;
  out.reserve(input.length() * 2);

  for (size_t i = 0; i < input.length(); ++i) {
    uint8_t value = static_cast<uint8_t>(input[i]);
    out += HEX_DIGITS[value >> 4];
    out += HEX_DIGITS[value & 0x0F];
  }

  return out;
}

static int8_t hexNibble(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

static bool hexDecode(const String &hex, String &out) {
  if ((hex.length() % 2) != 0 ||
      hex.length() > static_cast<size_t>(MACRO_MAX_LEN * 2)) {
    return false;
  }

  out = "";
  out.reserve(hex.length() / 2);

  for (size_t i = 0; i < hex.length(); i += 2) {
    int8_t hi = hexNibble(hex[i]);
    int8_t lo = hexNibble(hex[i + 1]);

    if (hi < 0 || lo < 0) {
      return false;
    }

    char value = static_cast<char>((hi << 4) | lo);

    if (value < 0x20 || value > 0x7E) {
      return false;
    }

    out += value;
  }

  return true;
}


static void clearSaverBuffer() {
  if (saverData != nullptr) {
    free(saverData);
    saverData = nullptr;
  }

  saverDataBytes = 0;
  saverFrameBytes = 0;
  saverBytesReceived = 0;
  saverFrameCount = 0;
  saverWidth = 0;
  saverHeight = 0;
  saverFormat = SAVER_NONE;
  saverUploading = false;
  saverReady = false;
  saverActive = false;
  saverFrameIndex = 0;
  saverFrameStartedAt = 0;

  memset(saverDurations, 0, sizeof(saverDurations));
}

static void initDisplay() {
  displayReady = tft->begin();

  if (!displayReady) {
    return;
  }

  tft->setRotation(1);

  // ILI9486 FRMCTR1 has discrete frame-rate steps. FRS=0xA is the
  // controller step nearest 60 Hz (~62 Hz). PIXEL PRO's renderer and
  // host-media protocol are capped at 60 FPS.
  tftBus->beginWrite();
  tftBus->writeCommand(0xB1);
  tftBus->write(0xA0);
  tftBus->write(0x10);
  tftBus->endWrite();

  tft->fillScreen(RGB565_BLACK);

  if (ESP.getPsramSize() > 0) {
    renderBuffer = static_cast<uint16_t *>(
        ps_malloc(static_cast<size_t>(TFT_WIDTH) * TFT_HEIGHT * 2));
  }
}

static uint16_t rgb332To565(uint8_t value) {
  uint8_t r3 = (value >> 5) & 0x07;
  uint8_t g3 = (value >> 2) & 0x07;
  uint8_t b2 = value & 0x03;

  uint16_t r5 = static_cast<uint16_t>((r3 * 31 + 3) / 7);
  uint16_t g6 = static_cast<uint16_t>((g3 * 63 + 3) / 7);
  uint16_t b5 = static_cast<uint16_t>((b2 * 31 + 1) / 3);

  return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

static void renderSaverFrame(uint8_t index) {
  if (!displayReady ||
      !saverReady ||
      saverData == nullptr ||
      index >= saverFrameCount) {
    return;
  }

  uint8_t *frame =
      saverData + static_cast<size_t>(index) * saverFrameBytes;

  if (saverFormat == SAVER_RGB565) {
    tft->draw16bitRGBBitmap(
        0,
        0,
        reinterpret_cast<uint16_t *>(frame),
        TFT_WIDTH,
        TFT_HEIGHT);
    return;
  }

  if (saverFormat != SAVER_RGB332 ||
      renderBuffer == nullptr) {
    return;
  }

  for (uint16_t y = 0; y < GIF_HEIGHT; ++y) {
    const uint8_t *src =
        frame + static_cast<size_t>(y) * GIF_WIDTH;

    uint16_t *row0 =
        renderBuffer +
        static_cast<size_t>(y * 2) * TFT_WIDTH;

    uint16_t *row1 = row0 + TFT_WIDTH;

    for (uint16_t x = 0; x < GIF_WIDTH; ++x) {
      uint16_t color = rgb332To565(src[x]);
      uint16_t dx = x * 2;

      row0[dx] = color;
      row0[dx + 1] = color;
      row1[dx] = color;
      row1[dx + 1] = color;
    }
  }

  tft->draw16bitRGBBitmap(
      0,
      0,
      renderBuffer,
      TFT_WIDTH,
      TFT_HEIGHT);
}

static void startSaverNow() {
  if (!saverReady || saverData == nullptr || !displayReady) {
    return;
  }

  saverActive = true;
  saverFrameIndex = 0;
  saverFrameStartedAt = millis();
  renderSaverFrame(0);
}

static void stopSaver() {
  if (!saverActive) {
    return;
  }

  saverActive = false;
  saverFrameIndex = 0;
  saverFrameStartedAt = 0;

  if (displayReady) {
    tft->fillScreen(RGB565_BLACK);
  }
}

static void pollSaver() {
  uint32_t now = millis();

  if (!saverActive) {
    if (saverReady &&
        saverDelayMs > 0 &&
        pressedMask == 0 &&
        static_cast<uint32_t>(now - lastUserActivityAt) >= saverDelayMs) {
      startSaverNow();
    }

    return;
  }

  if (!saverReady ||
      saverFrameCount <= 1 ||
      saverFormat == SAVER_RGB565) {
    return;
  }

  uint16_t duration =
      saverDurations[saverFrameIndex] < GIF_MIN_FRAME_MS
          ? GIF_MIN_FRAME_MS
          : saverDurations[saverFrameIndex];

  if (static_cast<uint32_t>(now - saverFrameStartedAt) < duration) {
    return;
  }

  saverFrameIndex =
      static_cast<uint8_t>((saverFrameIndex + 1) % saverFrameCount);

  saverFrameStartedAt = now;
  renderSaverFrame(saverFrameIndex);
}

static bool parseSaverDurations(
    const String &csv,
    uint8_t frameCount) {
  int start = 0;

  for (uint8_t i = 0; i < frameCount; ++i) {
    int comma = csv.indexOf(',', start);
    bool last = i == frameCount - 1;

    if ((!last && comma < 0) ||
        (last && comma >= 0)) {
      return false;
    }

    String token =
        last ? csv.substring(start) : csv.substring(start, comma);

    uint16_t duration = 0;

    if (!parseUnsigned(token, 5000, duration)) {
      return false;
    }

    saverDurations[i] =
        duration < GIF_MIN_FRAME_MS
            ? GIF_MIN_FRAME_MS
            : duration;

    start = comma + 1;
  }

  return true;
}

static bool beginSaverUpload(
    uint8_t frameCount,
    uint16_t width,
    uint16_t height,
    SaverPixelFormat format,
    const String &durationCsv) {
  clearSaverBuffer();

  bool staticImage =
      format == SAVER_RGB565 &&
      frameCount == 1 &&
      width == TFT_WIDTH &&
      height == TFT_HEIGHT;

  bool animated =
      format == SAVER_RGB332 &&
      frameCount >= 1 &&
      frameCount <= GIF_MAX_FRAMES &&
      width == GIF_WIDTH &&
      height == GIF_HEIGHT;

  if (!staticImage && !animated) {
    return false;
  }

  if (!parseSaverDurations(durationCsv, frameCount)) {
    clearSaverBuffer();
    return false;
  }

  saverFrameCount = frameCount;
  saverWidth = width;
  saverHeight = height;
  saverFormat = format;

  saverFrameBytes =
      static_cast<size_t>(width) *
      height *
      (format == SAVER_RGB565 ? 2 : 1);

  saverDataBytes =
      saverFrameBytes * frameCount;

  if (saverDataBytes == 0 ||
      saverDataBytes > ESP.getFreePsram()) {
    clearSaverBuffer();
    return false;
  }

  saverData =
      static_cast<uint8_t *>(ps_malloc(saverDataBytes));

  if (saverData == nullptr) {
    clearSaverBuffer();
    return false;
  }

  memset(saverData, 0, saverDataBytes);
  saverBytesReceived = 0;
  saverUploading = true;
  saverReady = false;
  saverActive = false;

  return true;
}

static bool writeSaverChunk(
    uint8_t frameIndex,
    size_t offset,
    const String &encoded) {
  if (!saverUploading ||
      saverData == nullptr ||
      frameIndex >= saverFrameCount ||
      offset >= saverFrameBytes) {
    return false;
  }

  size_t expectedOffset =
      static_cast<size_t>(frameIndex) * saverFrameBytes +
      offset;

  if (expectedOffset != saverBytesReceived) {
    return false;
  }

  uint8_t decoded[320] = {};
  size_t decodedLength = 0;

  int result =
      mbedtls_base64_decode(
          decoded,
          sizeof(decoded),
          &decodedLength,
          reinterpret_cast<const unsigned char *>(encoded.c_str()),
          encoded.length());

  if (result != 0 ||
      decodedLength == 0 ||
      offset + decodedLength > saverFrameBytes ||
      saverBytesReceived + decodedLength > saverDataBytes) {
    return false;
  }

  memcpy(
      saverData + saverBytesReceived,
      decoded,
      decodedLength);

  saverBytesReceived += decodedLength;
  return true;
}

static bool finishSaverUpload() {
  if (!saverUploading ||
      saverData == nullptr ||
      saverBytesReceived != saverDataBytes) {
    return false;
  }

  saverUploading = false;
  saverReady = true;
  saverActive = false;
  saverFrameIndex = 0;
  saverFrameStartedAt = 0;

  return true;
}

static String deviceHello() {
  char out[320];
  snprintf(
      out,
      sizeof(out),
      "PIXELPRO|1|FW=%s|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|ACTIONS=32|DISPLAY=ILI9486,480x320,i8080-8|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO,HOST_ACTION,MEM,PANEL,SAVER,MEDIA|VID=%04X|PID=%04X",
      FW_VERSION,
      USB_VID_PIXEL,
      USB_PID_PIXEL);
  return String(out);
}

static void sendMemoryInfo() {
  const uint32_t flashTotal = ESP.getFlashChipSize();
  const uint32_t flashUsed = ESP.getSketchSize();

  const uint32_t sramTotal = ESP.getHeapSize();
  const uint32_t sramFree = ESP.getFreeHeap();
  const uint32_t sramUsed =
      sramTotal > sramFree ? sramTotal - sramFree : 0;

  const uint32_t psramTotal = ESP.getPsramSize();
  const uint32_t psramFree = ESP.getFreePsram();
  const uint32_t psramUsed =
      psramTotal > psramFree ? psramTotal - psramFree : 0;

  char out[160];
  snprintf(
      out,
      sizeof(out),
      "MEM|%lu|%lu|%lu|%lu|%lu|%lu",
      static_cast<unsigned long>(flashUsed),
      static_cast<unsigned long>(flashTotal),
      static_cast<unsigned long>(sramUsed),
      static_cast<unsigned long>(sramTotal),
      static_cast<unsigned long>(psramUsed),
      static_cast<unsigned long>(psramTotal));
  cdcPrintln(out);
}

static void sendKeyState() {
  char out[48];
  snprintf(
      out,
      sizeof(out),
      "KEYS|%02X|P=%u|L=%u",
      pressedMask,
      static_cast<unsigned>(activeProfile),
      static_cast<unsigned>(currentLayer()));
  cdcPrintln(out);
}

static void sendMappedReports() {
  if (!HID.ready()) {
    return;
  }

  KeyReport report = {};
  uint8_t slot = 0;
  uint16_t consumer = 0;

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if ((pressedMask & static_cast<uint8_t>(1U << i)) == 0) {
      continue;
    }

    const KeyBinding &binding = activeBindings[i];

    if (binding.type == BIND_KEYBOARD) {
      report.modifiers |= binding.modifiers;

      if (binding.keyCode != 0 && slot < 6) {
        report.keys[slot++] = binding.keyCode;
      }
    } else if (binding.type == BIND_CONSUMER && consumer == 0) {
      consumer = binding.consumerCode;
    }
  }

  Keyboard.sendReport(&report);

  if (consumer != activeConsumerCode) {
    if (activeConsumerCode != 0) {
      ConsumerControl.release();
    }

    if (consumer != 0) {
      ConsumerControl.press(consumer);
    }

    activeConsumerCode = consumer;
  }
}

static void executeMacro(uint8_t index) {
  if (index >= MACRO_COUNT ||
      macros[index].length() == 0 ||
      !HID.ready()) {
    return;
  }

  Keyboard.print(macros[index]);
  delay(2);
  sendMappedReports();
}

static void applyLayerPress(const KeyBinding &binding) {
  uint8_t target = binding.keyCode;

  if (target >= LAYER_COUNT) {
    return;
  }

  if (binding.modifiers == LAYER_MO) {
    momentaryLayer = target;
  } else if (binding.modifiers == LAYER_TG) {
    toggledLayerMask ^= static_cast<uint8_t>(1U << target);
  } else if (binding.modifiers == LAYER_TO) {
    baseLayer = target;
    momentaryLayer = -1;
    toggledLayerMask = 0;
  }
}

static void applyLayerRelease(const KeyBinding &binding) {
  if (binding.type == BIND_LAYER &&
      binding.modifiers == LAYER_MO &&
      momentaryLayer == binding.keyCode) {
    momentaryLayer = -1;
  }
}

static void handleCommand(String command) {
  command.trim();

  String upper = command;
  upper.toUpperCase();

  if (upper == "HELLO" || upper == "GET_INFO") {
    cdcPrintln(deviceHello());
    return;
  }

  if (upper == "GET_KEYS") {
    sendKeyState();
    return;
  }

  if (upper == "GET_LAYER") {
    char out[48];
    snprintf(
        out,
        sizeof(out),
        "LAYER|PROFILE=%u|ACTIVE=%u|BASE=%u|TOGGLE=%u",
        static_cast<unsigned>(activeProfile),
        static_cast<unsigned>(currentLayer()),
        static_cast<unsigned>(baseLayer),
        static_cast<unsigned>(toggledLayerMask));
    cdcPrintln(out);
    return;
  }

  if (upper == "MEM" || upper == "GET_MEMORY") {
    sendMemoryInfo();
    return;
  }

  if (upper == "PANEL") {
    cdcPrintln("PANEL|ILI9486|60|0|60");
    return;
  }

  if (upper == "SAVERSTATE") {
    if (saverUploading) {
      cdcPrintln("SAVERSTATE|UPLOADING");
    } else if (saverReady) {
      cdcPrintln("SAVERSTATE|READY");
    } else {
      cdcPrintln("SAVERSTATE|EMPTY");
    }
    return;
  }

  if (upper == "SAVCLEAR") {
    clearSaverBuffer();
    lastUserActivityAt = millis();

    if (displayReady) {
      tft->fillScreen(RGB565_BLACK);
    }

    cdcPrintln("OK|SAVCLEAR");
    return;
  }

  if (upper == "SAVSHOW") {
    if (!saverReady) {
      cdcPrintln("ERR|SAVER_EMPTY");
      return;
    }

    startSaverNow();
    cdcPrintln("OK|SAVSHOW");
    return;
  }

  if (upper == "SAVSOURCE|MEDIA") {
    cdcPrintln("OK|SAVSOURCE|MEDIA");
    return;
  }

  if (upper.startsWith("SAVDELAY|")) {
    int sep = command.indexOf('|');
    uint32_t seconds = 0;

    if (sep < 0 ||
        !parseUnsignedLong(
            command.substring(sep + 1),
            86400,
            seconds)) {
      cdcPrintln("ERR|BAD_SAVDELAY");
      return;
    }

    saverDelayMs =
        seconds == 0
            ? 0
            : seconds * 1000UL;

    lastUserActivityAt = millis();
    cdcPrintln("OK|SAVDELAY");
    return;
  }

  if (upper.startsWith("SAVBEGIN|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);
    int third = command.indexOf('|', second + 1);
    int fourth = command.indexOf('|', third + 1);
    int fifth = command.indexOf('|', fourth + 1);

    if (first < 0 || second < 0 || third < 0 ||
        fourth < 0 || fifth < 0) {
      cdcPrintln("ERR|BAD_SAVBEGIN");
      return;
    }

    uint16_t frames = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    if (!parseUnsigned(
            command.substring(first + 1, second),
            GIF_MAX_FRAMES,
            frames) ||
        frames == 0 ||
        !parseUnsigned(
            command.substring(second + 1, third),
            TFT_WIDTH,
            width) ||
        !parseUnsigned(
            command.substring(third + 1, fourth),
            TFT_HEIGHT,
            height)) {
      cdcPrintln("ERR|BAD_SAVBEGIN");
      return;
    }

    String formatText =
        command.substring(fourth + 1, fifth);
    formatText.toUpperCase();

    SaverPixelFormat format =
        formatText == "RGB565"
            ? SAVER_RGB565
            : formatText == "RGB332"
                ? SAVER_RGB332
                : SAVER_NONE;

    if (format == SAVER_NONE ||
        !beginSaverUpload(
            static_cast<uint8_t>(frames),
            width,
            height,
            format,
            command.substring(fifth + 1))) {
      cdcPrintln("ERR|SAVBEGIN_ALLOC");
      return;
    }

    cdcPrintln("OK|SAVBEGIN");
    return;
  }

  if (upper.startsWith("SAVDATA|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);
    int third = command.indexOf('|', second + 1);

    if (first < 0 || second < 0 || third < 0) {
      cdcPrintln("ERR|BAD_SAVDATA");
      return;
    }

    uint16_t frame = 0;
    uint32_t offset = 0;

    if (!parseUnsigned(
            command.substring(first + 1, second),
            GIF_MAX_FRAMES - 1,
            frame) ||
        !parseUnsignedLong(
            command.substring(second + 1, third),
            static_cast<uint32_t>(
                TFT_WIDTH * TFT_HEIGHT * 2),
            offset)) {
      cdcPrintln("ERR|BAD_SAVDATA");
      return;
    }

    if (!writeSaverChunk(
            static_cast<uint8_t>(frame),
            offset,
            command.substring(third + 1))) {
      cdcPrintln("ERR|SAVDATA_WRITE");
      return;
    }

    if (saverFrameBytes > 0 &&
        saverBytesReceived > 0 &&
        (saverBytesReceived % saverFrameBytes) == 0) {
      char out[32];
      snprintf(
          out,
          sizeof(out),
          "OK|SAVFRAME|%u",
          static_cast<unsigned>(frame));
      cdcPrintln(out);
    }

    return;
  }

  if (upper == "SAVEND") {
    if (!finishSaverUpload()) {
      cdcPrintln("ERR|SAVEND_INCOMPLETE");
      return;
    }

    lastUserActivityAt = millis();
    cdcPrintln("OK|SAVER|READY");
    return;
  }

  if (upper.startsWith("GET_KEYMAP")) {
    uint8_t profile = activeProfile;
    uint8_t layer = 0;

    int first = command.indexOf('|');
    if (first >= 0) {
      int second = command.indexOf('|', first + 1);

      uint16_t parsedProfile = 0;
      if (!parseUnsigned(
              second >= 0
                  ? command.substring(first + 1, second)
                  : command.substring(first + 1),
              PROFILE_COUNT - 1,
              parsedProfile)) {
        cdcPrintln("ERR|BAD_PROFILE");
        return;
      }

      profile = static_cast<uint8_t>(parsedProfile);

      if (second >= 0) {
        uint16_t parsedLayer = 0;
        if (!parseUnsigned(
                command.substring(second + 1),
                LAYER_COUNT - 1,
                parsedLayer)) {
          cdcPrintln("ERR|BAD_LAYER");
          return;
        }

        layer = static_cast<uint8_t>(parsedLayer);
      }
    }

    cdcPrintln(serializeKeymap(profile, layer));
    return;
  }

  if (upper.startsWith("SET_KEYMAP|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);
    int third = second >= 0 ? command.indexOf('|', second + 1) : -1;

    if (first < 0 || second < 0 || third < 0) {
      cdcPrintln("ERR|BAD_KEYMAP");
      return;
    }

    uint16_t parsedProfile = 0;
    uint16_t parsedLayer = 0;

    if (!parseUnsigned(
            command.substring(first + 1, second),
            PROFILE_COUNT - 1,
            parsedProfile)) {
      cdcPrintln("ERR|BAD_PROFILE");
      return;
    }

    if (!parseUnsigned(
            command.substring(second + 1, third),
            LAYER_COUNT - 1,
            parsedLayer)) {
      cdcPrintln("ERR|BAD_LAYER");
      return;
    }

    uint8_t profile = static_cast<uint8_t>(parsedProfile);
    uint8_t layer = static_cast<uint8_t>(parsedLayer);
    String payload = command.substring(third + 1);

    KeyBinding parsed[KEY_COUNT] = {};

    if (!parseKeymapPayload(payload, parsed)) {
      cdcPrintln("ERR|BAD_KEYMAP");
      return;
    }

    memcpy(keymap[profile][layer], parsed, sizeof(parsed));
    saveKeymap();
    sendMappedReports();

    char out[40];
    snprintf(
        out,
        sizeof(out),
        "OK|KEYMAP|%u|%u",
        static_cast<unsigned>(profile),
        static_cast<unsigned>(layer));
    cdcPrintln(out);
    return;
  }

  if (upper == "RESET_KEYMAP") {
    setDefaultKeymap();
    saveKeymap();
    activeProfile = 0;
    baseLayer = 0;
    momentaryLayer = -1;
    toggledLayerMask = 0;
    sendMappedReports();
    cdcPrintln("OK|KEYMAP_RESET");
    return;
  }

  if (upper.startsWith("GET_MACRO|")) {
    uint16_t index = 0;
    int sep = command.indexOf('|');

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(sep + 1),
            MACRO_COUNT - 1,
            index)) {
      cdcPrintln("ERR|BAD_MACRO");
      return;
    }

    String out = "MACRO|";
    out += String(index);
    out += '|';
    out += hexEncode(macros[index]);
    cdcPrintln(out);
    return;
  }

  if (upper.startsWith("SET_MACRO|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (first < 0 || second < 0) {
      cdcPrintln("ERR|BAD_MACRO");
      return;
    }

    uint16_t index = 0;
    if (!parseUnsigned(
            command.substring(first + 1, second),
            MACRO_COUNT - 1,
            index)) {
      cdcPrintln("ERR|BAD_MACRO");
      return;
    }

    String decoded;
    if (!hexDecode(command.substring(second + 1), decoded)) {
      cdcPrintln("ERR|BAD_MACRO_DATA");
      return;
    }

    macros[index] = decoded;
    saveMacro(static_cast<uint8_t>(index));

    String out = "OK|MACRO|";
    out += String(index);
    cdcPrintln(out);
    return;
  }

  if (upper == "GET_PROFILE") {
    char out[48];
    snprintf(
        out,
        sizeof(out),
        "PROFILE|ACTIVE=%u|LAYER=%u",
        static_cast<unsigned>(activeProfile),
        static_cast<unsigned>(baseLayer));
    cdcPrintln(out);
    return;
  }

  if (upper.startsWith("SET_PROFILE|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (first < 0 || second < 0) {
      cdcPrintln("ERR|BAD_PROFILE");
      return;
    }

    uint16_t profile = 0;
    uint16_t layer = 0;

    if (!parseUnsigned(
            command.substring(first + 1, second),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsigned(
            command.substring(second + 1),
            LAYER_COUNT - 1,
            layer)) {
      cdcPrintln("ERR|BAD_PROFILE");
      return;
    }

    activeProfile = static_cast<uint8_t>(profile);
    baseLayer = static_cast<uint8_t>(layer);
    momentaryLayer = -1;
    toggledLayerMask = 0;
    sendMappedReports();

    char out[40];
    snprintf(
        out,
        sizeof(out),
        "OK|PROFILE|%u|%u",
        static_cast<unsigned>(activeProfile),
        static_cast<unsigned>(baseLayer));
    cdcPrintln(out);
    return;
  }

  if (upper == "PING") {
    cdcPrintln("PONG|PIXELPRO");
    return;
  }

  if (upper == "REBOOT") {
    cdcPrintln("OK|REBOOT");
    USBSerial.flush();
    delay(50);
    ESP.restart();
    return;
  }

  if (upper.length()) {
    cdcPrintln("ERR|UNKNOWN_COMMAND");
  }
}

static void pollCdc() {
  while (USBSerial.available()) {
    char ch = static_cast<char>(USBSerial.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (cdcLine.length()) {
        handleCommand(cdcLine);
        cdcLine = "";
      }
      continue;
    }

    if (cdcLine.length() < 512) {
      cdcLine += ch;
    } else {
      cdcLine = "";
      cdcPrintln("ERR|LINE_TOO_LONG");
    }
  }
}

static void emitKeyEvent(uint8_t index, bool pressed) {
  if (pressed) {
    lastUserActivityAt = millis();
    stopSaver();

    uint8_t layer = currentLayer();
    KeyBinding resolved = resolveBinding(layer, index);
    activeBindings[index] = resolved;
    pressedMask |= static_cast<uint8_t>(1U << index);

    if (resolved.type == BIND_LAYER) {
      applyLayerPress(resolved);
    } else if (resolved.type == BIND_MACRO) {
      char macroOut[64];
      snprintf(
          macroOut,
          sizeof(macroOut),
          "MACRO|%u|KEY=%u|P=%u|L=%u",
          static_cast<unsigned>(resolved.keyCode + 1),
          static_cast<unsigned>(index + 1),
          static_cast<unsigned>(activeProfile),
          static_cast<unsigned>(layer));
      cdcPrintln(macroOut);
    } else if (resolved.type == BIND_ACTION) {
      char actionOut[64];
      snprintf(
          actionOut,
          sizeof(actionOut),
          "ACTION|%u|KEY=%u|P=%u|L=%u",
          static_cast<unsigned>(resolved.keyCode),
          static_cast<unsigned>(index + 1),
          static_cast<unsigned>(activeProfile),
          static_cast<unsigned>(layer));
      cdcPrintln(actionOut);
    }
  } else {
    applyLayerRelease(activeBindings[index]);
    pressedMask &= static_cast<uint8_t>(~(1U << index));
    activeBindings[index] = disabledBinding();
  }

  sendMappedReports();

  char out[48];
  snprintf(
      out,
      sizeof(out),
      "KEY|%u|%s|P=%u|L=%u",
      index + 1,
      pressed ? "DOWN" : "UP",
      static_cast<unsigned>(activeProfile),
      static_cast<unsigned>(currentLayer()));
  cdcPrintln(out);
}

static void initKeys() {
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    pinMode(KEY_PINS[i], INPUT_PULLUP);

    bool pressed = digitalRead(KEY_PINS[i]) == LOW;
    keyState[i].rawPressed = pressed;
    keyState[i].stablePressed = pressed;
    keyState[i].changedAt = millis();
    activeBindings[i] = disabledBinding();

    if (pressed) {
      pressedMask |= static_cast<uint8_t>(1U << i);
    }
  }
}

static void pollKeys() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    bool pressed = digitalRead(KEY_PINS[i]) == LOW;

    if (pressed != keyState[i].rawPressed) {
      keyState[i].rawPressed = pressed;
      keyState[i].changedAt = now;
    }

    if (pressed != keyState[i].stablePressed &&
        (now - keyState[i].changedAt) >= DEBOUNCE_MS) {
      keyState[i].stablePressed = pressed;
      emitKeyEvent(i, pressed);
    }
  }
}

void setup() {
  preferences.begin("pixelpro", false);
  loadKeymap();
  loadMacros();
  initKeys();
  initDisplay();
  lastUserActivityAt = millis();

  uint64_t mac = ESP.getEfuseMac();
  char serial[24];
  snprintf(
      serial,
      sizeof(serial),
      "PIXELPRO-%012llX",
      static_cast<unsigned long long>(mac));

  USB.VID(USB_VID_PIXEL);
  USB.PID(USB_PID_PIXEL);
  USB.productName("PIXEL PRO");
  USB.manufacturerName("Lumi3D");
  USB.serialNumber(serial);
  USB.firmwareVersion(0x0133);

  USBSerial.begin();
  Keyboard.begin();
  ConsumerControl.begin();

  USB.begin();

  delay(500);
  sendMappedReports();
  cdcPrintln("BOOT|PIXELPRO|1.3.3");
}

void loop() {
  pollKeys();
  pollCdc();
  pollSaver();
  delay(1);
}
