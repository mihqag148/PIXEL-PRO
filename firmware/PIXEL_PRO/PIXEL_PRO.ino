#include <Arduino.h>
#include <Preferences.h>
#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error PIXEL PRO composite firmware requires USB CDC On Boot disabled
#else
USBCDC USBSerial;
#endif

static constexpr char FW_VERSION[] = "1.2.0";
static constexpr uint16_t USB_VID_PIXEL = 0x303A;
static constexpr uint16_t USB_PID_PIXEL = 0x80C2;
static constexpr uint8_t KEY_COUNT = 8;
static constexpr uint8_t LAYER_COUNT = 4;
static constexpr uint8_t MACRO_COUNT = 8;
static constexpr uint8_t MACRO_MAX_LEN = 80;
static constexpr uint32_t DEBOUNCE_MS = 8;

static constexpr uint8_t BIND_DISABLED = 0;
static constexpr uint8_t BIND_KEYBOARD = 1;
static constexpr uint8_t BIND_CONSUMER = 2;
static constexpr uint8_t BIND_LAYER = 3;
static constexpr uint8_t BIND_MACRO = 4;
static constexpr uint8_t BIND_TRANSPARENT = 5;

static constexpr uint8_t LAYER_MO = 1;
static constexpr uint8_t LAYER_TG = 2;
static constexpr uint8_t LAYER_TO = 3;
static constexpr uint8_t KEYMAP_STORAGE_VERSION = 2;

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

static KeyState keyState[KEY_COUNT] = {};
static KeyBinding keymap[LAYER_COUNT][KEY_COUNT] = {};
static KeyBinding activeBindings[KEY_COUNT] = {};
static String macros[MACRO_COUNT];

static uint8_t pressedMask = 0;
static uint16_t activeConsumerCode = 0;
static uint8_t baseLayer = 0;
static int8_t momentaryLayer = -1;
static uint8_t toggledLayerMask = 0;
static String cdcLine;

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
  for (uint8_t layer = 0; layer < LAYER_COUNT; ++layer) {
    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
      keymap[layer][i] =
          layer == 0 ? disabledBinding() : transparentBinding();
    }
  }

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    keymap[0][i].type = BIND_KEYBOARD;
    keymap[0][i].keyCode = static_cast<uint8_t>(HID_KEY_A + i);
    keymap[0][i].modifiers = 0;
    keymap[0][i].consumerCode = 0;
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

  KeyBinding stored[LAYER_COUNT][KEY_COUNT] = {};
  size_t read = preferences.getBytes("keymap", stored, sizeof(stored));

  if (read != sizeof(stored)) {
    saveKeymap();
    return;
  }

  for (uint8_t layer = 0; layer < LAYER_COUNT; ++layer) {
    for (uint8_t i = 0; i < KEY_COUNT; ++i) {
      if (!bindingIsValid(stored[layer][i])) {
        saveKeymap();
        return;
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
    const KeyBinding &binding = keymap[scan][keyIndex];

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

static String serializeKeymap(uint8_t layer) {
  String out = "KEYMAP|";
  out += String(layer);
  out += '|';

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (i) {
      out += ',';
    }

    out += serializeBinding(keymap[layer][i]);
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

static String deviceHello() {
  char out[210];
  snprintf(
      out,
      sizeof(out),
      "PIXELPRO|1|FW=%s|MCU=ESP32S2|KEYS=8|LAYERS=4|MACROS=8|CAPS=HID,CDC,KEYMAP,LAYERS,MACRO|VID=%04X|PID=%04X",
      FW_VERSION,
      USB_VID_PIXEL,
      USB_PID_PIXEL);
  return String(out);
}

static void sendKeyState() {
  char out[48];
  snprintf(
      out,
      sizeof(out),
      "KEYS|%02X|L=%u",
      pressedMask,
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
        "LAYER|ACTIVE=%u|BASE=%u|TOGGLE=%u",
        static_cast<unsigned>(currentLayer()),
        static_cast<unsigned>(baseLayer),
        static_cast<unsigned>(toggledLayerMask));
    cdcPrintln(out);
    return;
  }

  if (upper.startsWith("GET_KEYMAP")) {
    uint8_t layer = 0;

    int sep = command.indexOf('|');
    if (sep >= 0) {
      uint16_t parsed = 0;
      if (!parseUnsigned(command.substring(sep + 1), LAYER_COUNT - 1, parsed)) {
        cdcPrintln("ERR|BAD_LAYER");
        return;
      }
      layer = static_cast<uint8_t>(parsed);
    }

    cdcPrintln(serializeKeymap(layer));
    return;
  }

  if (upper.startsWith("SET_KEYMAP|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    uint8_t layer = 0;
    String payload;

    if (second >= 0) {
      uint16_t parsedLayer = 0;
      if (!parseUnsigned(
              command.substring(first + 1, second),
              LAYER_COUNT - 1,
              parsedLayer)) {
        cdcPrintln("ERR|BAD_LAYER");
        return;
      }

      layer = static_cast<uint8_t>(parsedLayer);
      payload = command.substring(second + 1);
    } else {
      payload = command.substring(first + 1);
    }

    KeyBinding parsed[KEY_COUNT] = {};

    if (!parseKeymapPayload(payload, parsed)) {
      cdcPrintln("ERR|BAD_KEYMAP");
      return;
    }

    memcpy(keymap[layer], parsed, sizeof(parsed));
    saveKeymap();
    sendMappedReports();

    char out[28];
    snprintf(out, sizeof(out), "OK|KEYMAP|%u", layer);
    cdcPrintln(out);
    return;
  }

  if (upper == "RESET_KEYMAP") {
    setDefaultKeymap();
    saveKeymap();
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
    uint8_t layer = currentLayer();
    KeyBinding resolved = resolveBinding(layer, index);
    activeBindings[index] = resolved;
    pressedMask |= static_cast<uint8_t>(1U << index);

    if (resolved.type == BIND_LAYER) {
      applyLayerPress(resolved);
    } else if (resolved.type == BIND_MACRO) {
      executeMacro(resolved.keyCode);
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
      "KEY|%u|%s|L=%u",
      index + 1,
      pressed ? "DOWN" : "UP",
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
  USB.firmwareVersion(0x0120);

  USBSerial.begin();
  Keyboard.begin();
  ConsumerControl.begin();

  USB.begin();

  delay(500);
  sendMappedReports();
  cdcPrintln("BOOT|PIXELPRO|1.2.0");
}

void loop() {
  pollKeys();
  pollCdc();
  delay(1);
}
