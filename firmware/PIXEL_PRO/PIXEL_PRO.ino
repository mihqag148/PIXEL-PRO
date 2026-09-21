#include <Arduino.h>
#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error PIXEL PRO composite firmware requires USB CDC On Boot disabled
#else
USBCDC USBSerial;
#endif

static constexpr char FW_VERSION[] = "1.0.1";
static constexpr uint16_t USB_VID_PIXEL = 0x303A;
static constexpr uint16_t USB_PID_PIXEL = 0x80C2;
static constexpr uint8_t KEY_COUNT = 8;
static constexpr uint32_t DEBOUNCE_MS = 8;

static const uint8_t KEY_PINS[KEY_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t KEY_CODES[KEY_COUNT] = {
    HID_KEY_A, HID_KEY_B, HID_KEY_C, HID_KEY_D,
    HID_KEY_E, HID_KEY_F, HID_KEY_G, HID_KEY_H
};

USBHID HID;
USBHIDKeyboard Keyboard;

struct KeyState {
  bool rawPressed;
  bool stablePressed;
  uint32_t changedAt;
};

static KeyState keyState[KEY_COUNT] = {};
static uint8_t pressedMask = 0;
static String cdcLine;

static void cdcPrintln(const String &line) {
  USBSerial.println(line);
}

static String deviceHello() {
  char out[160];
  snprintf(
      out, sizeof(out),
      "PIXELPRO|1|FW=%s|MCU=ESP32S2|KEYS=8|CAPS=HID,CDC|VID=%04X|PID=%04X",
      FW_VERSION, USB_VID_PIXEL, USB_PID_PIXEL);
  return String(out);
}

static void sendKeyState() {
  char out[32];
  snprintf(out, sizeof(out), "KEYS|%02X", pressedMask);
  cdcPrintln(out);
}

static void handleCommand(String command) {
  command.trim();
  command.toUpperCase();

  if (command == "HELLO" || command == "GET_INFO") {
    cdcPrintln(deviceHello());
  } else if (command == "GET_KEYS") {
    sendKeyState();
  } else if (command == "PING") {
    cdcPrintln("PONG|PIXELPRO");
  } else if (command == "REBOOT") {
    cdcPrintln("OK|REBOOT");
    USBSerial.flush();
    delay(50);
    ESP.restart();
  } else if (command.length()) {
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

    if (cdcLine.length() < 160) {
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

  if (HID.ready()) {
    if (pressed) {
      Keyboard.pressRaw(KEY_CODES[index]);
    } else {
      Keyboard.releaseRaw(KEY_CODES[index]);
    }
  }

  char out[32];
  snprintf(out, sizeof(out), "KEY|%u|%s", index + 1, pressed ? "DOWN" : "UP");
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
  initKeys();

  uint64_t mac = ESP.getEfuseMac();
  char serial[24];
  snprintf(
      serial,
      sizeof(serial),
      "PIXELPRO-%012llX",
      static_cast<unsigned long long>(mac));

  // CDC-on-boot is intentionally disabled in the build. All composite
  // descriptors are therefore configured before the single USB.begin().
  USB.VID(USB_VID_PIXEL);
  USB.PID(USB_PID_PIXEL);
  USB.productName("PIXEL PRO");
  USB.manufacturerName("Lumi3D");
  USB.serialNumber(serial);
  USB.firmwareVersion(0x0101);

  USBSerial.begin();
  Keyboard.begin();

  if (!USB.begin()) {
    // There is no second transport available here; keep running so a reset can
    // recover without entering a reboot loop.
  }

  delay(500);
  cdcPrintln("BOOT|PIXELPRO|1.0.1");
}

void loop() {
  pollKeys();
  pollCdc();
  delay(1);
}
