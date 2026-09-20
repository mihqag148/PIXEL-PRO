/*
 * PIXEL PRO - ESP32-S2 Mini board-only bring-up
 * v0.1.3
 *
 * Uses Arduino-ESP32's proven USB classes instead of a hand-built TinyUSB
 * descriptor. This avoids the invalid mixed report-ID descriptor that kept
 * Windows from enumerating v0.1.1/v0.1.2.
 *
 * Features:
 * - USB keyboard
 * - USB HID vendor channel (report ID 6, 32-byte payload)
 * - Lumi app HELLO/MEM handshake over vendor HID
 * - BOOT button types 'A'
 * - D15 LED: slow blink = USB not mounted, fast blink = USB mounted
 */

#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDVendor.h"

#define PIXEL_FW_VERSION "0.1.3"
#define PIXEL_USB_VID 0x303A
#define PIXEL_USB_PID 0x4009

static constexpr uint8_t BOOT_BUTTON = 0;
static constexpr uint8_t STATUS_LED = 15;
static constexpr size_t RAW_REPORT_SIZE = 32;

USBHIDKeyboard Keyboard;
USBHIDVendor Vendor(RAW_REPORT_SIZE, false);

// ---------- Lumi framing ----------
static constexpr uint8_t MAGIC0 = 'L';
static constexpr uint8_t MAGIC1 = 'Q';
static constexpr uint8_t FRAME_VERSION = 1;
static constexpr uint8_t FLAG_START = 0x01;
static constexpr uint8_t FLAG_END = 0x02;
static constexpr uint8_t FLAG_RESPONSE = 0x04;
static constexpr uint8_t HEADER_SIZE = 7;
static constexpr uint8_t PAYLOAD_SIZE = RAW_REPORT_SIZE - HEADER_SIZE;
static constexpr size_t MAX_MESSAGE = 512;

static char rxMessage[MAX_MESSAGE + 1];
static size_t rxLength = 0;
static uint8_t rxSequence = 0;
static uint8_t rxFragment = 0;
static bool rxActive = false;

static bool sendRaw(const uint8_t data[RAW_REPORT_SIZE]) {
  return Vendor.write(data, RAW_REPORT_SIZE) == RAW_REPORT_SIZE;
}

static void sendLumiResponse(uint8_t sequence, const String &text) {
  const size_t len = text.length();
  const size_t fragments = len == 0 ? 1 : (len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;

  for (size_t f = 0; f < fragments; ++f) {
    uint8_t report[RAW_REPORT_SIZE] = {};
    const size_t offset = f * PAYLOAD_SIZE;
    const size_t count = offset < len ? min((size_t)PAYLOAD_SIZE, len - offset) : 0;

    report[0] = MAGIC0;
    report[1] = MAGIC1;
    report[2] = FRAME_VERSION;
    report[3] = FLAG_RESPONSE;
    if (f == 0) report[3] |= FLAG_START;
    if (f == fragments - 1) report[3] |= FLAG_END;
    report[4] = sequence;
    report[5] = (uint8_t)f;
    report[6] = (uint8_t)count;

    if (count) {
      memcpy(report + HEADER_SIZE, text.c_str() + offset, count);
    }

    sendRaw(report);
  }
}

static String commandResponse(const String &cmd) {
  if (cmd == "HELLO") {
    return "LUMIPAD|3|FW=" PIXEL_FW_VERSION "|CAPS=MEM";
  }

  if (cmd == "MEM") {
    const uint32_t flashTotal = ESP.getFlashChipSize();
    const uint32_t flashUsed = ESP.getSketchSize();
    const uint32_t ramTotal = ESP.getHeapSize();
    const uint32_t ramUsed = ramTotal - ESP.getFreeHeap();

    return "MEM|" + String(flashUsed) + "|" + String(flashTotal) + "|" +
           String(ramUsed) + "|" + String(ramTotal);
  }

  if (cmd == "SYS|RESTART") {
    delay(30);
    ESP.restart();
  }

  return "";
}

static void processLumiFrame(const uint8_t data[RAW_REPORT_SIZE]) {
  const uint8_t flags = data[3];
  const uint8_t sequence = data[4];
  const uint8_t fragment = data[5];
  const uint8_t count = data[6];

  if ((flags & FLAG_RESPONSE) || count > PAYLOAD_SIZE) return;

  if (flags & FLAG_START) {
    rxActive = true;
    rxSequence = sequence;
    rxFragment = 0;
    rxLength = 0;
  }

  if (!rxActive || sequence != rxSequence || fragment != rxFragment) {
    rxActive = false;
    return;
  }

  if (rxLength + count > MAX_MESSAGE) {
    rxActive = false;
    return;
  }

  if (count) {
    memcpy(rxMessage + rxLength, data + HEADER_SIZE, count);
    rxLength += count;
  }

  rxFragment++;

  if (!(flags & FLAG_END)) return;

  rxActive = false;
  rxMessage[rxLength] = '\0';

  const String command(rxMessage);
  const String response = commandResponse(command);

  if (command == "HELLO" || command == "MEM") {
    sendLumiResponse(sequence, response);
  }
}

static void processRawPacket(const uint8_t data[RAW_REPORT_SIZE]) {
  if (data[0] == MAGIC0 && data[1] == MAGIC1 && data[2] == FRAME_VERSION) {
    processLumiFrame(data);
    return;
  }

  // Minimal VIA protocol response for later bring-up.
  uint8_t response[RAW_REPORT_SIZE] = {};
  memcpy(response, data, RAW_REPORT_SIZE);

  if (data[0] == 0x01) {
    response[1] = 0x00;
    response[2] = 0x09;
  } else if (data[0] == 0x11) {
    response[1] = 1;
  } else {
    response[0] = 0xFF;
  }

  sendRaw(response);
}

// ---------- BOOT -> A test ----------
static int lastBootState = HIGH;
static uint32_t lastBootChange = 0;

static void scanBootButton() {
  const int nowState = digitalRead(BOOT_BUTTON);

  if (nowState != lastBootState && millis() - lastBootChange >= 20) {
    lastBootChange = millis();
    lastBootState = nowState;

    if (nowState == LOW) {
      Keyboard.press('a');
    } else {
      Keyboard.release('a');
    }
  }
}

void setup() {
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // Set descriptors before USB.begin().
  USB.VID(PIXEL_USB_VID);
  USB.PID(PIXEL_USB_PID);
  USB.productName("PIXEL PRO");
  USB.manufacturerName("Lumi3D");
  USB.serialNumber("PIXELPRO");
  USB.firmwareVersion(0x0103);
  USB.usbPower(100);

  Keyboard.begin();
  Vendor.setRxBufferSize(1024);
  Vendor.begin();
  USB.begin();
}

void loop() {
  while (Vendor.available() >= (int)RAW_REPORT_SIZE) {
    uint8_t packet[RAW_REPORT_SIZE] = {};
    const size_t got = Vendor.read(packet, RAW_REPORT_SIZE);
    if (got == RAW_REPORT_SIZE) {
      processRawPacket(packet);
    }
  }

  scanBootButton();

  static uint32_t lastBlink = 0;
  static bool led = false;
  const uint32_t interval = USB ? 250 : 1000;
  const uint32_t now = millis();

  if (now - lastBlink >= interval) {
    lastBlink = now;
    led = !led;
    digitalWrite(STATUS_LED, led ? HIGH : LOW);
  }

  delay(1);
}
