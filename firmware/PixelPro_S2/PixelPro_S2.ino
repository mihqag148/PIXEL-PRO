/*
 * PIXEL PRO - ESP32-S2 Mini board-only bring-up
 * v0.1.1
 *
 * Goal:
 * - No TFT / matrix / encoder / RGB required
 * - Enumerates as USB keyboard + QMK-style Raw HID
 * - Lumi Macropad app can detect PIXEL PRO by VID/PID and HELLO
 * - Onboard BOOT button (D0) temporarily types 'A'
 */

#include <Arduino.h>
#include <Preferences.h>
#include "esp32-hal-tinyusb.h"
#include "tusb.h"

#define PIXEL_FW_VERSION "0.1.2"
#define PIXEL_USB_VID 0x303A
#define PIXEL_USB_PID 0x4009

static constexpr uint8_t REPORT_ID_KEYBOARD = 1;
static constexpr size_t RAW_REPORT_SIZE = 32;
static constexpr uint8_t BOOT_BUTTON = 0; // D0 / BOOT on ESP32-S2 Mini
static constexpr uint8_t STATUS_LED = 15;  // onboard LED on LOLIN S2 Mini

struct RawPacket {
  uint8_t data[RAW_REPORT_SIZE];
};

static QueueHandle_t rawQueue = nullptr;

// Raw HID must remain Usage Page 0xFF60 / Usage 0x61 for Lumi QmkRawHidLink.
// It intentionally has no report ID (report ID 0).
static const uint8_t hidReportDescriptor[] = {
  0x06, 0x60, 0xFF,       // Usage Page (Vendor 0xFF60)
  0x09, 0x61,             // Usage (0x61)
  0xA1, 0x01,             // Collection (Application)
  0x09, 0x62,
  0x15, 0x00,
  0x26, 0xFF, 0x00,
  0x75, 0x08,
  0x95, 0x20,             // 32 bytes
  0x81, 0x02,             // Input
  0x09, 0x63,
  0x15, 0x00,
  0x26, 0xFF, 0x00,
  0x75, 0x08,
  0x95, 0x20,             // 32 bytes
  0x91, 0x02,             // Output
  0xC0,

  // Normal USB keyboard on the same HID interface.
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))
};

extern "C" uint16_t pixel_hid_load_descriptor(uint8_t *dst, uint8_t *itf) {
  const uint8_t strIndex = tinyusb_add_string_descriptor("PIXEL PRO HID");
  const uint8_t epIn = tinyusb_get_free_in_endpoint();
  const uint8_t epOut = tinyusb_get_free_out_endpoint();
  TU_VERIFY(epIn != 0);
  TU_VERIFY(epOut != 0);

  const uint8_t descriptor[TUD_HID_INOUT_DESC_LEN] = {
    TUD_HID_INOUT_DESCRIPTOR(
      *itf,
      strIndex,
      HID_ITF_PROTOCOL_NONE,
      sizeof(hidReportDescriptor),
      epOut,
      (uint8_t)(0x80 | epIn),
      64,
      1
    )
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
  uint8_t instance,
  uint8_t report_id,
  hid_report_type_t report_type,
  uint8_t *buffer,
  uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

extern "C" void tud_hid_set_report_cb(
  uint8_t instance,
  uint8_t report_id,
  hid_report_type_t report_type,
  const uint8_t *buffer,
  uint16_t bufsize) {
  (void)instance;

  if (report_id == 0 &&
      (report_type == HID_REPORT_TYPE_OUTPUT ||
       report_type == HID_REPORT_TYPE_INVALID) &&
      buffer != nullptr &&
      bufsize >= RAW_REPORT_SIZE &&
      rawQueue != nullptr) {
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

static void sendLumiResponse(uint8_t sequence, const String &text) {
  const size_t len = text.length();
  const size_t fragmentCount =
    len == 0 ? 1 : (len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;

  for (size_t fragment = 0; fragment < fragmentCount; ++fragment) {
    uint8_t report[RAW_REPORT_SIZE] = {};
    const size_t offset = fragment * PAYLOAD_SIZE;
    const size_t count =
      offset < len ? min((size_t)PAYLOAD_SIZE, len - offset) : 0;

    report[0] = MAGIC0;
    report[1] = MAGIC1;
    report[2] = FRAME_VERSION;
    report[3] = FLAG_RESPONSE;
    if (fragment == 0) report[3] |= FLAG_START;
    if (fragment == fragmentCount - 1) report[3] |= FLAG_END;
    report[4] = sequence;
    report[5] = (uint8_t)fragment;
    report[6] = (uint8_t)count;

    if (count) {
      memcpy(report + HEADER_SIZE, text.c_str() + offset, count);
    }

    for (int retry = 0; retry < 100; ++retry) {
      if (sendRaw(report)) break;
      delay(1);
    }
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

  if (cmd == "SYS|DFU") {
    delay(30);
    usb_persist_restart(RESTART_BOOTLOADER);
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

// Basic VIA protocol response so usevia.app can at least identify the device
// later; full keymap support is deliberately deferred.
static void processVia(const uint8_t data[RAW_REPORT_SIZE]) {
  uint8_t response[RAW_REPORT_SIZE];
  memcpy(response, data, RAW_REPORT_SIZE);

  switch (data[0]) {
    case 0x01: // GET_PROTOCOL_VERSION
      response[1] = 0x00;
      response[2] = 0x09;
      break;

    case 0x11: // GET_LAYER_COUNT
      response[1] = 1;
      break;

    default:
      response[0] = 0xFF; // unhandled
      break;
  }

  sendRaw(response);
}

static void processRawPacket(const uint8_t data[RAW_REPORT_SIZE]) {
  if (data[0] == MAGIC0 &&
      data[1] == MAGIC1 &&
      data[2] == FRAME_VERSION) {
    processLumiFrame(data);
  } else {
    processVia(data);
  }
}

// ---------- Keyboard test ----------
static bool lastBootPressed = false;
static uint32_t bootChangedAt = 0;
static bool bootRaw = false;
static constexpr uint32_t DEBOUNCE_MS = 15;

static void sendKeyboardA(bool pressed) {
  if (!tud_mounted() || !tud_hid_n_ready(0)) return;

  uint8_t keys[6] = {};
  if (pressed) {
    keys[0] = 0x04; // HID 'A'
  }

  tud_hid_n_keyboard_report(
    0,
    REPORT_ID_KEYBOARD,
    0,
    keys
  );
}

static void scanBootButton() {
  const bool rawPressed = digitalRead(BOOT_BUTTON) == LOW;
  const uint32_t now = millis();

  if (rawPressed != bootRaw) {
    bootRaw = rawPressed;
    bootChangedAt = now;
  }

  if (bootRaw != lastBootPressed &&
      now - bootChangedAt >= DEBOUNCE_MS) {
    lastBootPressed = bootRaw;
    sendKeyboardA(lastBootPressed);
  }
}

static bool initUsb() {
  rawQueue = xQueueCreate(8, sizeof(RawPacket));
  if (!rawQueue) return false;

  esp_err_t err = tinyusb_enable_interface(
    USB_INTERFACE_HID,
    TUD_HID_INOUT_DESC_LEN,
    pixel_hid_load_descriptor
  );
  if (err != ESP_OK) return false;

  tinyusb_device_config_t cfg = {};
  cfg.vid = PIXEL_USB_VID;
  cfg.pid = PIXEL_USB_PID;
  cfg.product_name = "PIXEL PRO";
  cfg.manufacturer_name = "Lumi3D";
  cfg.serial_number = "PIXELPRO";
  cfg.fw_version = 0x0102;
  cfg.usb_version = 0x0200;
  cfg.usb_class = 0;
  cfg.usb_subclass = 0;
  cfg.usb_protocol = 0;
  cfg.usb_attributes = TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP;
  cfg.usb_power_ma = 100;
  cfg.webusb_enabled = false;
  cfg.webusb_url = nullptr;

  return tinyusb_init(&cfg) == ESP_OK;
}

void setup() {
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);
  initUsb();
}

void loop() {
  RawPacket packet;

  while (rawQueue &&
         xQueueReceive(rawQueue, &packet, 0) == pdTRUE) {
    processRawPacket(packet.data);
  }

  scanBootButton();

  // Slow heartbeat when firmware is alive; faster after USB enumeration.
  static uint32_t lastBlink = 0;
  static bool ledState = false;
  const uint32_t interval = tud_mounted() ? 250 : 1000;
  const uint32_t now = millis();
  if (now - lastBlink >= interval) {
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(STATUS_LED, ledState ? HIGH : LOW);
  }

  delay(1);
}
