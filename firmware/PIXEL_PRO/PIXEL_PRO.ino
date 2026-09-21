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

static constexpr char FW_VERSION[] = "1.1.0";
static constexpr uint16_t USB_VID_PIXEL = 0x303A;
static constexpr uint16_t USB_PID_PIXEL = 0x80C2;
static constexpr uint8_t KEY_COUNT = 8;
static constexpr uint32_t DEBOUNCE_MS = 8;

static constexpr uint8_t BIND_DISABLED = 0;
static constexpr uint8_t BIND_KEYBOARD = 1;
static constexpr uint8_t BIND_CONSUMER = 2;
static constexpr uint8_t KEYMAP_STORAGE_VERSION = 1;

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
static KeyBinding keymap[KEY_COUNT] = {};
static uint8_t pressedMask = 0;
static uint16_t activeConsumerCode = 0;
static String cdcLine;

static void cdcPrintln(const String &line) {
  USBSerial.println(line);
}

static void setDefaultKeymap() {
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    keymap[i].type = BIND_KEYBOARD;
    keymap[i].keyCode = static_cast<uint8_t>(HID_KEY_A + i);
    keymap[i].modifiers = 0;
    keymap[i].consumerCode = 0;
  }
}

static bool bindingIsValid(const KeyBinding &binding) {
  if (binding.type == BIND_DISABLED) {
    return binding.keyCode == 0 &&
           binding.modifiers == 0 &&
           binding.consumerCode == 0;
  }

  if (binding.type == BIND_KEYBOARD) {
    return binding.keyCode != 0 &&
           (binding.modifiers & 0xF0) == 0 &&
           binding.consumerCode == 0;
  }

  if (binding.type == BIND_CONSUMER) {
    return binding.keyCode == 0 &&
           binding.modifiers == 0 &&
           binding.consumerCode != 0;
  }

  return false;
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

  KeyBinding stored[KEY_COUNT] = {};
  size_t read = preferences.getBytes("keymap", stored, sizeof(stored));
  if (read != sizeof(stored)) {
    saveKeymap();
    return;
  }

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (!bindingIsValid(stored[i])) {
      saveKeymap();
      return;
    }
  }

  memcpy(keymap, stored, sizeof(keymap));
}

static String serializeBinding(const KeyBinding &binding) {
  char out[24];

  if (binding.type == BIND_DISABLED) {
    return String("D:0:0");
  }

  if (binding.type == BIND_CONSUMER) {
    snprintf(
        out,
        sizeof(out),
        "C:%u:0",
        static_cast<unsigned>(binding.consumerCode));
    return String(out);
  }

  snprintf(
      out,
      sizeof(out),
      "K:%u:%u",
      static_cast<unsigned>(binding.keyCode),
      static_cast<unsigned>(binding.modifiers));
  return String(out);
}

static String serializeKeymap() {
  String out = "KEYMAP|";
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (i) {
      out += ',';
    }
    out += serializeBinding(keymap[i]);
  }
  return out;
}

static bool parseUnsigned(const String &text, uint16_t maxValue, uint16_t &value) {
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

  if (type == 'K') {
    if (code == 0 || code > 0xFF || mods > 0x0F) {
      return false;
    }

    binding.type = BIND_KEYBOARD;
    binding.keyCode = static_cast<uint8_t>(code);
    binding.modifiers = static_cast<uint8_t>(mods);
    binding.consumerCode = 0;
    return true;
  }

  if (type == 'C') {
    if (code == 0 || mods != 0) {
      return false;
    }

    binding.type = BIND_CONSUMER;
    binding.keyCode = 0;
    binding.modifiers = 0;
    binding.consumerCode = code;
    return true;
  }

  return false;
}

static bool parseKeymapPayload(const String &payload, KeyBinding (&parsed)[KEY_COUNT]) {
  int start = 0;

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    int comma = payload.indexOf(',', start);
    bool last = i == KEY_COUNT - 1;

    if ((!last && comma < 0) || (last && comma >= 0)) {
      return false;
    }

    String token = last
        ? payload.substring(start)
        : payload.substring(start, comma);

    if (!parseBindingToken(token, parsed[i])) {
      return false;
    }

    start = comma + 1;
  }

  return true;
}

static String deviceHello() {
  char out[176];
  snprintf(
      out,
      sizeof(out),
      "PIXELPRO|1|FW=%s|MCU=ESP32S2|KEYS=8|CAPS=HID,CDC,KEYMAP|VID=%04X|PID=%04X",
      FW_VERSION,
      USB_VID_PIXEL,
      USB_PID_PIXEL);
  return String(out);
}

static void sendKeyState() {
  char out[32];
  snprintf(out, sizeof(out), "KEYS|%02X", pressedMask);
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

    const KeyBinding &binding = keymap[i];

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

static void handleCommand(String command) {
  command.trim();

  String upper = command;
  upper.toUpperCase();

  if (upper == "HELLO" || upper == "GET_INFO") {
    cdcPrintln(deviceHello());
  } else if (upper == "GET_KEYS") {
    sendKeyState();
  } else if (upper == "GET_KEYMAP") {
    cdcPrintln(serializeKeymap());
  } else if (upper.startsWith("SET_KEYMAP|")) {
    KeyBinding parsed[KEY_COUNT] = {};
    String payload = command.substring(command.indexOf('|') + 1);

    if (!parseKeymapPayload(payload, parsed)) {
      cdcPrintln("ERR|BAD_KEYMAP");
      return;
    }

    memcpy(keymap, parsed, sizeof(keymap));
    saveKeymap();
    sendMappedReports();
    cdcPrintln("OK|KEYMAP");
  } else if (upper == "RESET_KEYMAP") {
    setDefaultKeymap();
    saveKeymap();
    sendMappedReports();
    cdcPrintln("OK|KEYMAP_RESET");
  } else if (upper == "PING") {
    cdcPrintln("PONG|PIXELPRO");
  } else if (upper == "REBOOT") {
    cdcPrintln("OK|REBOOT");
    USBSerial.flush();
    delay(50);
    ESP.restart();
  } else if (upper.length()) {
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

    if (cdcLine.length() < 220) {
      cdcLine += ch;
    } else {
      cdcLine = "";
      cdcPrintln("ERR|LINE_TOO_LONG");
    }
  }
}

static void emitKeyEvent(uint8_t index, bool pressed) {
  if (pressed) {
    pressedMask |= static_cast<uint8_t>(1U << index);
  } else {
    pressedMask &= static_cast<uint8_t>(~(1U << index));
  }

  sendMappedReports();

  char out[32];
  snprintf(
      out,
      sizeof(out),
      "KEY|%u|%s",
      index + 1,
      pressed ? "DOWN" : "UP");
  cdcPrintln(out);
}

static void initKeys() {
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    pinMode(KEY_PINS[i], INPUT_PULLUP);

    bool pressed = digitalRead(KEY_PINS[i]) == LOW;
    keyState[i].rawPressed = pressed;
    keyState[i].stablePressed = pressed;
    keyState[i].changedAt = millis();

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
  USB.firmwareVersion(0x0110);

  USBSerial.begin();
  Keyboard.begin();
  ConsumerControl.begin();

  USB.begin();

  delay(500);
  sendMappedReports();
  cdcPrintln("BOOT|PIXELPRO|1.1.0");
}

void loop() {
  pollKeys();
  pollCdc();
  delay(1);
}
