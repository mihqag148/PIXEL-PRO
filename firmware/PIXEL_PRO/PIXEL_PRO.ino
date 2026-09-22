#include <Arduino.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <JPEGDEC.h>
#include <Adafruit_NeoPixel.h>
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

static constexpr char FW_VERSION[] = "1.8.0";
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
static constexpr uint16_t GIF_LANDSCAPE_WIDTH = 480;
static constexpr uint16_t GIF_LANDSCAPE_HEIGHT = 320;
static constexpr uint16_t GIF_NATIVE_WIDTH = 320;
static constexpr uint16_t GIF_NATIVE_HEIGHT = 480;
static constexpr uint8_t DISPLAY_REFRESH_CAP_HZ = 60;
static constexpr uint8_t GIF_MAX_FPS = 60;
static constexpr uint16_t GIF_MIN_FRAME_MS = 17;
static constexpr uint32_t GIF_UPLOAD_LIMIT_BYTES = 8UL * 1024UL * 1024UL;
static constexpr uint32_t JPEG_UPLOAD_LIMIT_BYTES = 2UL * 1024UL * 1024UL;
static constexpr uint32_t PACKED_UPLOAD_LIMIT_BYTES = 2UL * 1024UL * 1024UL;

// PIXEL PRO main-menu artwork. Each keymap profile owns one 2x4 menu,
// matching the eight physical keys. Backgrounds and icons are JPEG assets.
// Native 96x96 icon decode avoids the old 40x40 upscaling blur.
static constexpr uint8_t MENU_SLOT_COUNT = 8;
static constexpr uint8_t MENU_ICON_WIDTH = 96;
static constexpr uint8_t MENU_ICON_HEIGHT = 96;
static constexpr uint32_t MENU_ICON_MAX_BYTES = 24UL * 1024UL;
static constexpr uint32_t MENU_BACKGROUND_LIMIT_BYTES = 96UL * 1024UL;
static constexpr uint8_t MENU_LABEL_MAX_LEN = 16;
static constexpr uint8_t MENU_STORAGE_VERSION = 4;
static constexpr uint8_t MENU_STATUS_HEIGHT = 54;

// Legacy raw-frame constants are kept only so older app builds can still
// upload their previous 240x160 RGB332 format. New app builds upload the
// original full-resolution GIF file instead.
static constexpr uint16_t GIF_WIDTH = 240;
static constexpr uint16_t GIF_HEIGHT = 160;
static constexpr uint8_t GIF_MAX_FRAMES = 32;

static constexpr char GIF_PATH[] = "/screensaver.gif";
static constexpr char GIF_TMP_PATH[] = "/screensaver.tmp";
static constexpr char JPEG_PATH[] = "/screensaver.jpg";
static constexpr char JPEG_TMP_PATH[] = "/screensaver_jpg.tmp";
static constexpr char PACKED_PATH[] = "/screensaver.pxq";
static constexpr char PACKED_TMP_PATH[] = "/screensaver_pxq.tmp";

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

static constexpr uint8_t RGB_PIN = 18;
static constexpr uint8_t RGB_LED_COUNT = 8;
static constexpr uint8_t RGB_STORAGE_VERSION = 1;
// Physical LED order requested by PIXEL PRO layout:
// LED1=K1, LED2=K2, LED3=K3, LED4=K4,
// LED5=K8, LED6=K7, LED7=K6, LED8=K5.
static constexpr uint8_t KEY_TO_LED[KEY_COUNT] = {0, 1, 2, 3, 7, 6, 5, 4};

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

struct __attribute__((packed)) MainMenuConfig {
  uint8_t version;
  uint8_t actions[PROFILE_COUNT][MENU_SLOT_COUNT];
  char labels[PROFILE_COUNT][MENU_SLOT_COUNT][MENU_LABEL_MAX_LEN + 1];
};

struct __attribute__((packed)) LegacyMainMenuConfigV3 {
  uint8_t version;
  uint8_t actions[PROFILE_COUNT][12];
  char labels[PROFILE_COUNT][12][MENU_LABEL_MAX_LEN + 1];
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
static MainMenuConfig mainMenuConfig = {};
static File menuUploadFile;
static uint8_t menuUploadKind = 0;  // 1=background, 2=icon
static uint8_t menuUploadProfile = 0;
static uint8_t menuUploadSlot = 0;
static uint32_t menuUploadExpectedBytes = 0;
static uint32_t menuUploadReceivedBytes = 0;

static int16_t menuCpuLoad = -1;
static int16_t menuCpuTemp = -1;
static int16_t menuGpuLoad = -1;
static int16_t menuGpuTemp = -1;
static uint8_t menuMonth = 0;
static uint8_t menuDay = 0;
static uint8_t menuHour = 0;
static uint8_t menuMinute = 0;
static bool menuPcStatusValid = false;

static uint8_t rgbProfiles[PROFILE_COUNT][KEY_COUNT][3] = {};
// PIXEL effects: 0 rainbow, 1 purple ping-pong, 2 orange blink,
 // 3 static, 4 fade, 5 chase, 6 breathe, 7 color shift, 8 rain, 9 wave.
static uint8_t rgbEffects[PROFILE_COUNT] = {};
static bool rgbEnabled = true;
static uint8_t rgbBrightnessPercent = 25;
static uint8_t rgbSpeedPercent = 50;
static uint32_t rgbLastFrameAt = 0;
static uint16_t rgbAnimationStep = 0;
static Adafruit_NeoPixel rgbStrip(
    RGB_LED_COUNT,
    RGB_PIN,
    NEO_GRB + NEO_KHZ800);

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
  SAVER_GIF = 3,
  SAVER_JPEG = 4,
  SAVER_PACKED = 5,
};

enum GifScaleMode : uint8_t {
  GIF_SCALE_FILL = 0,
  GIF_SCALE_FIT = 1,
  GIF_SCALE_STRETCH = 2,
  GIF_SCALE_TILE = 3,
  GIF_SCALE_CENTER = 4,
  GIF_SCALE_SPAN = 5,
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

static bool littleFsReady = false;
static File gifUploadFile;
static uint32_t gifUploadExpectedBytes = 0;
static uint16_t gifUploadWidth = 0;
static uint16_t gifUploadHeight = 0;

static File jpegUploadFile;
static uint32_t jpegUploadExpectedBytes = 0;
static uint16_t jpegUploadWidth = 0;
static uint16_t jpegUploadHeight = 0;
static JPEGDEC jpegDecoder;
static File jpegPlaybackFile;

static File packedUploadFile;
static File packedPlaybackFile;
static uint32_t packedUploadExpectedBytes = 0;
static uint16_t packedStorageWidth = 0;
static uint16_t packedStorageHeight = 0;
static uint16_t packedFrameCount = 0;
static uint16_t packedFps = 0;
static uint16_t packedFrameIndex = 0;
static uint16_t packedPaletteCount = 0;
static uint8_t packedColorMode = 0;
static uint32_t packedDurationMs = 0;
static uint32_t packedFramesOffset = 0;
static uint32_t packedNextFrameAt = 0;
static uint16_t packedPalette565[256] = {};
static uint16_t *packedLineBuffer = nullptr;
static uint16_t packedLineFallback[TFT_WIDTH] = {};

static AnimatedGIF gifDecoder;
static File gifPlaybackFile;
static bool gifDecoderOpen = false;
static bool gifAtEnd = false;
static uint16_t gifCanvasWidth = 0;
static uint16_t gifCanvasHeight = 0;
static uint32_t gifNextFrameAt = 0;
static GifScaleMode gifScaleMode = GIF_SCALE_CENTER;
static GifScaleMode gifUploadScaleMode = GIF_SCALE_CENTER;
static bool bootloaderArmed = false;
static uint32_t bootloaderArmUntil = 0;
static float gifScaleX = 1.0f;
static float gifScaleY = 1.0f;
static float gifOffsetX = 0.0f;
static float gifOffsetY = 0.0f;

static void stopSaver();
static void clearSaverBuffer();
static void closeJpegUploadFile();
static void closePackedFiles();

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

static void setDefaultRgbProfiles() {
  for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
    rgbEffects[profile] = 3;

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      rgbProfiles[profile][key][0] =
          static_cast<uint8_t>(255 - key * 16);
      rgbProfiles[profile][key][1] =
          static_cast<uint8_t>(96 + key * 18);
      uint16_t blue =
          static_cast<uint16_t>(profile) * 5U;
      rgbProfiles[profile][key][2] =
          static_cast<uint8_t>(
              blue > 120U
                  ? 120U
                  : blue);
    }
  }
}

static void saveRgbProfiles() {
  preferences.putUChar("rgbver", RGB_STORAGE_VERSION);
  preferences.putBytes(
      "rgbkeys",
      rgbProfiles,
      sizeof(rgbProfiles));
  preferences.putBytes(
      "rgbfx",
      rgbEffects,
      sizeof(rgbEffects));
  preferences.putBool("rgben", rgbEnabled);
  preferences.putUChar(
      "rgbbr",
      rgbBrightnessPercent);
  preferences.putUChar(
      "rgbspd",
      rgbSpeedPercent);
}

static void loadRgbProfiles() {
  setDefaultRgbProfiles();

  if (preferences.getUChar("rgbver", 0) == RGB_STORAGE_VERSION &&
      preferences.getBytesLength("rgbkeys") == sizeof(rgbProfiles)) {
    size_t read =
        preferences.getBytes(
            "rgbkeys",
            rgbProfiles,
            sizeof(rgbProfiles));

    if (read != sizeof(rgbProfiles)) {
      setDefaultRgbProfiles();
    }
  }

  rgbEnabled =
      preferences.getBool(
          "rgben",
          true);

  if (preferences.getBytesLength("rgbfx") == sizeof(rgbEffects)) {
    preferences.getBytes(
        "rgbfx",
        rgbEffects,
        sizeof(rgbEffects));

    for (uint8_t profile = 0; profile < PROFILE_COUNT; ++profile) {
      if (rgbEffects[profile] > 9) {
        rgbEffects[profile] = 3;
      }
    }
  }

  rgbBrightnessPercent =
      static_cast<uint8_t>(
          constrain(
              preferences.getUChar(
                  "rgbbr",
                  25),
              0,
              100));

  rgbSpeedPercent =
      static_cast<uint8_t>(
          constrain(
              preferences.getUChar(
                  "rgbspd",
                  50),
              10,
              100));
}

static uint16_t rgbFrameIntervalMs() {
  return static_cast<uint16_t>(
      map(
          rgbSpeedPercent,
          10,
          100,
          180,
          24));
}

static void applyRgbBrightness() {
  uint8_t brightness =
      rgbEnabled
          ? static_cast<uint8_t>(
                map(
                    rgbBrightnessPercent,
                    0,
                    100,
                    0,
                    255))
          : 0;

  rgbStrip.setBrightness(brightness);
}

static void renderRgbStatic() {
  applyRgbBrightness();

  for (uint8_t key = 0; key < KEY_COUNT; ++key) {
    uint8_t led =
        KEY_TO_LED[key];

    rgbStrip.setPixelColor(
        led,
        rgbProfiles[activeProfile][key][0],
        rgbProfiles[activeProfile][key][1],
        rgbProfiles[activeProfile][key][2]);
  }

  rgbStrip.show();
}

static uint8_t triangle8(
    uint16_t value) {
  uint8_t phase =
      static_cast<uint8_t>(
          value & 0xFFU);

  return phase < 128
      ? static_cast<uint8_t>(
            phase * 2U)
      : static_cast<uint8_t>(
            (255U - phase) *
            2U);
}

static uint32_t scaledProfileColor(
    uint8_t key,
    uint8_t scale) {
  uint16_t r =
      static_cast<uint16_t>(
          rgbProfiles[activeProfile][key][0]) *
      scale /
      255U;

  uint16_t g =
      static_cast<uint16_t>(
          rgbProfiles[activeProfile][key][1]) *
      scale /
      255U;

  uint16_t b =
      static_cast<uint16_t>(
          rgbProfiles[activeProfile][key][2]) *
      scale /
      255U;

  return rgbStrip.Color(
      static_cast<uint8_t>(r),
      static_cast<uint8_t>(g),
      static_cast<uint8_t>(b));
}

static void pollRgbEffect(bool force = false) {
  uint8_t effect =
      rgbEffects[activeProfile];

  if (effect == 3) {
    if (force) {
      renderRgbStatic();
    }
    return;
  }

  uint32_t now =
      millis();

  uint16_t interval =
      rgbFrameIntervalMs();

  if (!force &&
      static_cast<uint32_t>(
          now - rgbLastFrameAt) <
          interval) {
    return;
  }

  rgbLastFrameAt =
      now;

  applyRgbBrightness();

  if (effect == 0) {
    // Rainbow: spatial rainbow flowing through logical K1..K8.
    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      uint16_t hue =
          static_cast<uint16_t>(
              rgbAnimationStep * 512U +
              key *
                  (65535U /
                   KEY_COUNT));

      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          rgbStrip.ColorHSV(
              hue,
              255,
              255));
    }
  } else if (effect == 1) {
    // Purple Ping-Pong.
    uint8_t phase =
        static_cast<uint8_t>(
            rgbAnimationStep %
            14U);

    uint8_t position =
        phase < 8
            ? phase
            : static_cast<uint8_t>(
                  14U - phase);

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      uint8_t distance =
          key > position
              ? key - position
              : position - key;

      uint8_t level =
          distance == 0
              ? 255
              : distance == 1
                  ? 72
                  : 12;

      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          rgbStrip.Color(
              static_cast<uint8_t>(
                  190U *
                  level /
                  255U),
              static_cast<uint8_t>(
                  40U *
                  level /
                  255U),
              static_cast<uint8_t>(
                  255U *
                  level /
                  255U)));
    }
  } else if (effect == 2) {
    // Orange Blink.
    bool on =
        (rgbAnimationStep &
         1U) == 0;

    uint32_t color =
        on
            ? rgbStrip.Color(
                  255,
                  90,
                  0)
            : rgbStrip.Color(
                  0,
                  0,
                  0);

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          color);
    }
  } else if (effect == 4) {
    // Fade: crossfade each saved key color into the next key color.
    uint8_t mix =
        static_cast<uint8_t>(
            rgbAnimationStep &
            0xFFU);

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      uint8_t next =
          static_cast<uint8_t>(
              (key + 1U) %
              KEY_COUNT);

      uint16_t inv =
          255U - mix;

      uint8_t r =
          static_cast<uint8_t>(
              (rgbProfiles[activeProfile][key][0] *
                   inv +
               rgbProfiles[activeProfile][next][0] *
                   mix) /
              255U);

      uint8_t g =
          static_cast<uint8_t>(
              (rgbProfiles[activeProfile][key][1] *
                   inv +
               rgbProfiles[activeProfile][next][1] *
                   mix) /
              255U);

      uint8_t b =
          static_cast<uint8_t>(
              (rgbProfiles[activeProfile][key][2] *
                   inv +
               rgbProfiles[activeProfile][next][2] *
                   mix) /
              255U);

      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          rgbStrip.Color(
              r,
              g,
              b));
    }
  } else if (effect == 5) {
    // Chase: selected per-key colors chase around K1..K8 with a short tail.
    uint8_t head =
        static_cast<uint8_t>(
            rgbAnimationStep %
            KEY_COUNT);

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      uint8_t distance =
          static_cast<uint8_t>(
              (head +
               KEY_COUNT -
               key) %
              KEY_COUNT);

      uint8_t level =
          distance == 0
              ? 255
              : distance == 1
                  ? 110
                  : distance == 2
                      ? 42
                      : 6;

      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          scaledProfileColor(
              key,
              level));
    }
  } else if (effect == 6) {
    // Breathe: all saved per-key colors breathe together.
    uint8_t level =
        static_cast<uint8_t>(
            24U +
            (static_cast<uint16_t>(
                 triangle8(
                     rgbAnimationStep *
                     3U)) *
             231U /
             255U));

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          scaledProfileColor(
              key,
              level));
    }
  } else if (effect == 7) {
    // Color Shift: one hue slowly shifts across all keys.
    uint16_t hue =
        static_cast<uint16_t>(
            rgbAnimationStep *
            420U);

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          rgbStrip.ColorHSV(
              hue,
              255,
              255));
    }
  } else if (effect == 8) {
    // Rain: deterministic blue/cyan drops with fading trails.
    uint8_t drop =
        static_cast<uint8_t>(
            (rgbAnimationStep *
                 5U +
             (rgbAnimationStep >>
              2U) *
                 3U) %
            KEY_COUNT);

    uint8_t second =
        static_cast<uint8_t>(
            (drop + 3U) %
            KEY_COUNT);

    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      uint8_t level =
          key == drop
              ? 255
              : key == second
                  ? 150
                  : static_cast<uint8_t>(
                        12U +
                        ((key * 17U +
                          rgbAnimationStep * 11U) %
                         24U));

      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          rgbStrip.Color(
              0,
              static_cast<uint8_t>(
                  level *
                  3U /
                  5U),
              level));
    }
  } else {
    // Wave: brightness wave travels through the saved per-key colors.
    for (uint8_t key = 0; key < KEY_COUNT; ++key) {
      uint8_t level =
          static_cast<uint8_t>(
              18U +
              (static_cast<uint16_t>(
                   triangle8(
                       rgbAnimationStep *
                           4U +
                       key *
                           28U)) *
               237U /
               255U));

      rgbStrip.setPixelColor(
          KEY_TO_LED[key],
          scaledProfileColor(
              key,
              level));
    }
  }

  rgbStrip.show();
  rgbAnimationStep++;
}

static void applyRgbProfile() {
  rgbAnimationStep = 0;
  rgbLastFrameAt = 0;
  pollRgbEffect(true);
}

static bool parseRgbHex(
    const String &token,
    uint8_t &r,
    uint8_t &g,
    uint8_t &b) {
  if (token.length() != 6) {
    return false;
  }

  char buffer[7] = {};
  token.toCharArray(
      buffer,
      sizeof(buffer));

  char *end = nullptr;
  unsigned long value =
      strtoul(
          buffer,
          &end,
          16);

  if (end == buffer ||
      *end != '\0' ||
      value > 0xFFFFFFUL) {
    return false;
  }

  r = static_cast<uint8_t>(
      (value >> 16) & 0xFF);
  g = static_cast<uint8_t>(
      (value >> 8) & 0xFF);
  b = static_cast<uint8_t>(
      value & 0xFF);

  return true;
}

static String serializeRgbProfile(
    uint8_t profile) {
  String out = "RGB_PROFILE|";
  out += String(profile);
  out += '|';

  char color[7];

  for (uint8_t key = 0; key < KEY_COUNT; ++key) {
    if (key) {
      out += ',';
    }

    snprintf(
        color,
        sizeof(color),
        "%02X%02X%02X",
        rgbProfiles[profile][key][0],
        rgbProfiles[profile][key][1],
        rgbProfiles[profile][key][2]);

    out += color;
  }

  return out;
}

static bool parseRgbProfileCsv(
    const String &csv,
    uint8_t colors[KEY_COUNT][3]) {
  int start = 0;

  for (uint8_t key = 0; key < KEY_COUNT; ++key) {
    int comma =
        csv.indexOf(
            ',',
            start);

    bool last =
        key ==
        KEY_COUNT - 1;

    if ((!last && comma < 0) ||
        (last && comma >= 0)) {
      return false;
    }

    String token =
        last
            ? csv.substring(start)
            : csv.substring(start, comma);

    if (!parseRgbHex(
            token,
            colors[key][0],
            colors[key][1],
            colors[key][2])) {
      return false;
    }

    start =
        comma + 1;
  }

  return true;
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


static void setDefaultMainMenuConfig() {
  memset(&mainMenuConfig, 0, sizeof(mainMenuConfig));
  mainMenuConfig.version = MENU_STORAGE_VERSION;
}

static void saveMainMenuConfig() {
  mainMenuConfig.version = MENU_STORAGE_VERSION;
  preferences.putUChar("menuver", MENU_STORAGE_VERSION);
  preferences.putBytes("menucfg", &mainMenuConfig, sizeof(mainMenuConfig));
}

static void loadMainMenuConfig() {
  setDefaultMainMenuConfig();

  uint8_t storedVersion =
      preferences.getUChar("menuver", 0);

  size_t storedBytes =
      preferences.getBytesLength("menucfg");

  if (storedVersion == 3 &&
      storedBytes == sizeof(LegacyMainMenuConfigV3)) {
    LegacyMainMenuConfigV3 legacy = {};

    if (preferences.getBytes(
            "menucfg",
            &legacy,
            sizeof(legacy)) ==
            sizeof(legacy) &&
        legacy.version == 3) {
      for (uint8_t profile = 0;
           profile < PROFILE_COUNT;
           ++profile) {
        for (uint8_t slot = 0;
             slot < MENU_SLOT_COUNT;
             ++slot) {
          mainMenuConfig.actions[profile][slot] =
              legacy.actions[profile][slot];

          memcpy(
              mainMenuConfig.labels[profile][slot],
              legacy.labels[profile][slot],
              MENU_LABEL_MAX_LEN + 1);

          mainMenuConfig.labels[profile][slot]
              [MENU_LABEL_MAX_LEN] = '\0';
        }
      }

      saveMainMenuConfig();
      return;
    }
  }

  if (storedVersion != MENU_STORAGE_VERSION ||
      storedBytes != sizeof(mainMenuConfig)) {
    saveMainMenuConfig();
    return;
  }

  MainMenuConfig stored = {};
  if (preferences.getBytes(
          "menucfg",
          &stored,
          sizeof(stored)) != sizeof(stored) ||
      stored.version != MENU_STORAGE_VERSION) {
    saveMainMenuConfig();
    return;
  }

  for (uint8_t profile = 0;
       profile < PROFILE_COUNT;
       ++profile) {
    for (uint8_t slot = 0;
         slot < MENU_SLOT_COUNT;
         ++slot) {
      if (stored.actions[profile][slot] > ACTION_COUNT) {
        saveMainMenuConfig();
        return;
      }

      stored.labels[profile][slot]
          [MENU_LABEL_MAX_LEN] = '\0';
    }
  }

  memcpy(
      &mainMenuConfig,
      &stored,
      sizeof(mainMenuConfig));
}

static void menuBackgroundPath(
    uint8_t profile,
    bool temporary,
    char *out,
    size_t outSize) {
  snprintf(
      out,
      outSize,
      temporary ? "/mb%u.tmp" : "/mb%u.jpg",
      static_cast<unsigned>(profile));
}

static void menuIconPath(
    uint8_t profile,
    uint8_t slot,
    bool temporary,
    char *out,
    size_t outSize) {
  snprintf(
      out,
      outSize,
      temporary ? "/mi%u_%u.tmp" : "/mi%u_%u.jpg",
      static_cast<unsigned>(profile),
      static_cast<unsigned>(slot));
}

static int mainMenuJpegDraw(JPEGDRAW *draw) {
  if (draw == nullptr ||
      draw->pPixels == nullptr ||
      !displayReady) {
    return 0;
  }

  int width =
      draw->iWidthUsed > 0
          ? draw->iWidthUsed
          : draw->iWidth;

  if (draw->x < 0 ||
      draw->y < 0 ||
      draw->x + width > TFT_WIDTH ||
      draw->y + draw->iHeight > TFT_HEIGHT) {
    return 0;
  }

  for (int row = 0;
       row < draw->iHeight;
       ++row) {
    tft->draw16bitRGBBitmap(
        draw->x,
        draw->y + row,
        draw->pPixels +
            static_cast<size_t>(row) *
                draw->iWidth,
        width,
        1);
  }

  return 1;
}

static bool renderMainMenuBackground(
    uint8_t profile) {
  if (profile >= PROFILE_COUNT) {
    profile = 0;
  }

  char path[24] = {};
  menuBackgroundPath(
      profile,
      false,
      path,
      sizeof(path));

  if (!littleFsReady ||
      !LittleFS.exists(path)) {
    tft->fillScreen(RGB565_BLACK);
    return false;
  }

  File file =
      LittleFS.open(
          path,
          "r");

  if (!file) {
    tft->fillScreen(RGB565_BLACK);
    return false;
  }

  JPEGDEC decoder;
  if (!decoder.open(
          file,
          mainMenuJpegDraw) ||
      decoder.getWidth() != TFT_WIDTH ||
      decoder.getHeight() != TFT_HEIGHT) {
    decoder.close();
    file.close();
    tft->fillScreen(RGB565_BLACK);
    return false;
  }

  tft->fillScreen(RGB565_BLACK);

  int result =
      decoder.decode(
          0,
          0,
          0);

  decoder.close();
  file.close();

  return result != 0;
}

static bool renderMainMenuIcon(
    uint8_t profile,
    uint8_t slot,
    int x,
    int y) {
  if (!littleFsReady ||
      profile >= PROFILE_COUNT ||
      slot >= MENU_SLOT_COUNT) {
    return false;
  }

  char path[24] = {};
  menuIconPath(
      profile,
      slot,
      false,
      path,
      sizeof(path));

  if (!LittleFS.exists(path)) {
    return false;
  }

  File file =
      LittleFS.open(
          path,
          "r");

  if (!file) {
    return false;
  }

  JPEGDEC decoder;
  bool valid =
      decoder.open(
          file,
          mainMenuJpegDraw) &&
      decoder.getWidth() == MENU_ICON_WIDTH &&
      decoder.getHeight() == MENU_ICON_HEIGHT;

  if (!valid) {
    decoder.close();
    file.close();
    return false;
  }

  int result =
      decoder.decode(
          x,
          y,
          0);

  decoder.close();
  file.close();

  return result != 0;
}

static void renderMainMenuStatusBar() {
  if (!displayReady ||
      saverActive) {
    return;
  }

  const int y =
      TFT_HEIGHT -
      MENU_STATUS_HEIGHT;

  tft->fillRect(
      0,
      y,
      TFT_WIDTH,
      MENU_STATUS_HEIGHT,
      0x0000);

  tft->fillRect(
      0,
      y,
      TFT_WIDTH,
      1,
      0x7BEF);

  tft->fillRect(
      118,
      y + 5,
      1,
      MENU_STATUS_HEIGHT - 10,
      0x4208);

  tft->fillRect(
      235,
      y + 5,
      1,
      MENU_STATUS_HEIGHT - 10,
      0x4208);

  tft->fillRect(
      356,
      y + 5,
      1,
      MENU_STATUS_HEIGHT - 10,
      0x4208);

  tft->setTextSize(1);
  tft->setTextColor(0xFFFF);

  char line[32] = {};

  snprintf(
      line,
      sizeof(line),
      "Profile:%02u/%02u",
      static_cast<unsigned>(activeProfile + 1),
      static_cast<unsigned>(PROFILE_COUNT));

  tft->setCursor(
      8,
      y + 9);
  tft->print(line);

  tft->setTextColor(0xBDF7);
  tft->setCursor(
      8,
      y + 31);
  tft->print("PIXEL PRO");

  tft->setTextColor(0xFFFF);

  if (menuPcStatusValid) {
    snprintf(
        line,
        sizeof(line),
        "%02u-%02u",
        static_cast<unsigned>(menuMonth),
        static_cast<unsigned>(menuDay));

    tft->setCursor(
        137,
        y + 9);
    tft->print(line);

    snprintf(
        line,
        sizeof(line),
        "%02u:%02u",
        static_cast<unsigned>(menuHour),
        static_cast<unsigned>(menuMinute));

    tft->setCursor(
        137,
        y + 31);
    tft->print(line);
  } else {
    tft->setCursor(
        137,
        y + 9);
    tft->print("-- --");

    tft->setCursor(
        137,
        y + 31);
    tft->print("--:--");
  }

  if (menuCpuLoad >= 0) {
    snprintf(
        line,
        sizeof(line),
        "CPU %d%%",
        static_cast<int>(menuCpuLoad));
  } else {
    snprintf(
        line,
        sizeof(line),
        "CPU --%%");
  }

  tft->setCursor(
      253,
      y + 9);
  tft->print(line);

  if (menuCpuTemp >= 0) {
    snprintf(
        line,
        sizeof(line),
        "%dC",
        static_cast<int>(menuCpuTemp));
  } else {
    snprintf(
        line,
        sizeof(line),
        "--C");
  }

  tft->setCursor(
      253,
      y + 31);
  tft->print(line);

  if (menuGpuLoad >= 0) {
    snprintf(
        line,
        sizeof(line),
        "GPU %d%%",
        static_cast<int>(menuGpuLoad));
  } else {
    snprintf(
        line,
        sizeof(line),
        "GPU --%%");
  }

  tft->setCursor(
      374,
      y + 9);
  tft->print(line);

  if (menuGpuTemp >= 0) {
    snprintf(
        line,
        sizeof(line),
        "%dC",
        static_cast<int>(menuGpuTemp));
  } else {
    snprintf(
        line,
        sizeof(line),
        "--C");
  }

  tft->setCursor(
      374,
      y + 31);
  tft->print(line);
}

static void renderMainMenu() {
  if (!displayReady ||
      saverActive) {
    return;
  }

  const uint8_t profile =
      activeProfile < PROFILE_COUNT
          ? activeProfile
          : 0;

  renderMainMenuBackground(
      profile);

  const int statusY =
      TFT_HEIGHT -
      MENU_STATUS_HEIGHT;

  const int marginX = 12;
  const int marginY = 8;
  const int gapX = 8;
  const int gapY = 8;

  const int cellW =
      (TFT_WIDTH -
       marginX * 2 -
       gapX * 3) /
      4;

  const int cellH =
      (statusY -
       marginY * 2 -
       gapY) /
      2;

  for (uint8_t slot = 0;
       slot < MENU_SLOT_COUNT;
       ++slot) {
    int col =
        slot % 4;

    int row =
        slot / 4;

    int x =
        marginX +
        col *
            (cellW + gapX);

    int y =
        marginY +
        row *
            (cellH + gapY);

    tft->drawRoundRect(
        x,
        y,
        cellW,
        cellH,
        9,
        0x7BEF);

    int iconX =
        x +
        (cellW -
         MENU_ICON_WIDTH) /
            2;

    int iconY =
        y +
        (cellH -
         MENU_ICON_HEIGHT) /
            2;

    bool drewIcon =
        renderMainMenuIcon(
            profile,
            slot,
            iconX,
            iconY);

    uint8_t action =
        mainMenuConfig
            .actions[profile][slot];

    if (!drewIcon &&
        action > 0) {
      const char *label =
          mainMenuConfig
              .labels[profile][slot];

      char fallback[8] = {};

      if (label[0] == '\0') {
        snprintf(
            fallback,
            sizeof(fallback),
            "A%02u",
            static_cast<unsigned>(
                action));

        label =
            fallback;
      }

      size_t len =
          strnlen(
              label,
              MENU_LABEL_MAX_LEN);

      tft->setTextSize(1);
      tft->setTextColor(0xFFFF);

      int textWidth =
          static_cast<int>(len) *
          6;

      int textX =
          x +
          max(
              4,
              (cellW -
               textWidth) /
                  2);

      int textY =
          y +
          (cellH - 8) /
              2;

      tft->setCursor(
          textX,
          textY);

      tft->print(
          label);
    }
  }

  renderMainMenuStatusBar();
}

static void closeMenuUpload() {
  if (menuUploadFile) {
    menuUploadFile.close();
  }

  menuUploadKind = 0;
  menuUploadExpectedBytes = 0;
  menuUploadReceivedBytes = 0;
}

static bool beginMenuBackgroundUpload(
    uint8_t profile,
    uint32_t expectedBytes) {
  if (!littleFsReady ||
      profile >= PROFILE_COUNT ||
      expectedBytes < 4 ||
      expectedBytes > MENU_BACKGROUND_LIMIT_BYTES) {
    return false;
  }

  closeMenuUpload();

  char finalPath[24] = {};
  char tempPath[24] = {};
  menuBackgroundPath(
      profile,
      false,
      finalPath,
      sizeof(finalPath));
  menuBackgroundPath(
      profile,
      true,
      tempPath,
      sizeof(tempPath));

  // Reclaim this profile's old background before checking free space.
  LittleFS.remove(tempPath);
  LittleFS.remove(finalPath);

  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t freeBytes = total > used ? total - used : 0;

  if (expectedBytes + 4096 > freeBytes) {
    return false;
  }

  menuUploadFile = LittleFS.open(tempPath, "w");
  if (!menuUploadFile) {
    return false;
  }

  menuUploadKind = 1;
  menuUploadProfile = profile;
  menuUploadExpectedBytes = expectedBytes;
  menuUploadReceivedBytes = 0;
  return true;
}

static bool beginMenuIconUpload(
    uint8_t profile,
    uint8_t slot,
    uint32_t expectedBytes) {
  if (!littleFsReady ||
      profile >= PROFILE_COUNT ||
      slot >= MENU_SLOT_COUNT ||
      expectedBytes < 4 ||
      expectedBytes > MENU_ICON_MAX_BYTES) {
    return false;
  }

  closeMenuUpload();

  char finalPath[24] = {};
  char tempPath[24] = {};
  menuIconPath(
      profile,
      slot,
      false,
      finalPath,
      sizeof(finalPath));
  menuIconPath(
      profile,
      slot,
      true,
      tempPath,
      sizeof(tempPath));

  LittleFS.remove(tempPath);
  LittleFS.remove(finalPath);

  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t freeBytes = total > used ? total - used : 0;

  if (expectedBytes + 1024 > freeBytes) {
    return false;
  }

  menuUploadFile = LittleFS.open(tempPath, "w");
  if (!menuUploadFile) {
    return false;
  }

  menuUploadKind = 2;
  menuUploadProfile = profile;
  menuUploadSlot = slot;
  menuUploadExpectedBytes = expectedBytes;
  menuUploadReceivedBytes = 0;
  return true;
}

static bool writeMenuAssetChunk(
    uint32_t offset,
    const String &encoded) {
  if (menuUploadKind == 0 ||
      !menuUploadFile ||
      offset != menuUploadReceivedBytes) {
    return false;
  }

  uint8_t decoded[1100] = {};
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
      menuUploadReceivedBytes + decodedLength >
          menuUploadExpectedBytes) {
    return false;
  }

  if (menuUploadFile.write(decoded, decodedLength) != decodedLength) {
    return false;
  }

  menuUploadReceivedBytes += decodedLength;
  return true;
}

static bool finishMenuBackgroundUpload() {
  if (menuUploadKind != 1 ||
      !menuUploadFile ||
      menuUploadProfile >= PROFILE_COUNT ||
      menuUploadReceivedBytes != menuUploadExpectedBytes) {
    closeMenuUpload();
    return false;
  }

  uint8_t profile =
      menuUploadProfile;

  char finalPath[24] = {};
  char tempPath[24] = {};
  menuBackgroundPath(
      profile,
      false,
      finalPath,
      sizeof(finalPath));
  menuBackgroundPath(
      profile,
      true,
      tempPath,
      sizeof(tempPath));

  menuUploadFile.flush();
  menuUploadFile.close();

  File file = LittleFS.open(tempPath, "r");
  if (!file) {
    closeMenuUpload();
    return false;
  }

  JPEGDEC decoder;
  bool valid =
      decoder.open(file, mainMenuJpegDraw) &&
      decoder.getWidth() == TFT_WIDTH &&
      decoder.getHeight() == TFT_HEIGHT &&
      file.size() == menuUploadExpectedBytes;

  decoder.close();
  file.close();

  if (!valid) {
    LittleFS.remove(tempPath);
    closeMenuUpload();
    return false;
  }

  if (!LittleFS.rename(tempPath, finalPath)) {
    LittleFS.remove(tempPath);
    closeMenuUpload();
    return false;
  }

  closeMenuUpload();

  if (profile == activeProfile) {
    renderMainMenu();
  }

  return true;
}

static bool finishMenuIconUpload() {
  if (menuUploadKind != 2 ||
      !menuUploadFile ||
      menuUploadProfile >= PROFILE_COUNT ||
      menuUploadReceivedBytes != menuUploadExpectedBytes) {
    closeMenuUpload();
    return false;
  }

  uint8_t profile =
      menuUploadProfile;

  uint8_t slot =
      menuUploadSlot;

  menuUploadFile.flush();
  menuUploadFile.close();

  char finalPath[24] = {};
  char tempPath[24] = {};
  menuIconPath(
      profile,
      slot,
      false,
      finalPath,
      sizeof(finalPath));
  menuIconPath(
      profile,
      slot,
      true,
      tempPath,
      sizeof(tempPath));

  File verify =
      LittleFS.open(
          tempPath,
          "r");

  bool valid =
      false;

  if (verify) {
    JPEGDEC decoder;

    valid =
        decoder.open(
            verify,
            mainMenuJpegDraw) &&
        decoder.getWidth() == MENU_ICON_WIDTH &&
        decoder.getHeight() == MENU_ICON_HEIGHT &&
        verify.size() == menuUploadExpectedBytes;

    decoder.close();
    verify.close();
  }

  if (!valid ||
      !LittleFS.rename(
          tempPath,
          finalPath)) {
    LittleFS.remove(tempPath);
    closeMenuUpload();
    return false;
  }

  closeMenuUpload();

  if (profile == activeProfile) {
    renderMainMenu();
  }

  return true;
}

static void clearMainMenuBackground(
    uint8_t profile) {
  if (!littleFsReady ||
      profile >= PROFILE_COUNT) {
    return;
  }

  char finalPath[24] = {};
  char tempPath[24] = {};
  menuBackgroundPath(
      profile,
      false,
      finalPath,
      sizeof(finalPath));
  menuBackgroundPath(
      profile,
      true,
      tempPath,
      sizeof(tempPath));

  LittleFS.remove(tempPath);
  LittleFS.remove(finalPath);
}

static void clearMainMenuIcon(
    uint8_t profile,
    uint8_t slot) {
  if (!littleFsReady ||
      profile >= PROFILE_COUNT ||
      slot >= MENU_SLOT_COUNT) {
    return;
  }

  char finalPath[24] = {};
  char tempPath[24] = {};

  menuIconPath(
      profile,
      slot,
      false,
      finalPath,
      sizeof(finalPath));

  menuIconPath(
      profile,
      slot,
      true,
      tempPath,
      sizeof(tempPath));

  LittleFS.remove(tempPath);
  LittleFS.remove(finalPath);
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



static void *gifAlloc(uint32_t size) {
  void *ptr = nullptr;

  if (ESP.getPsramSize() > 0) {
    ptr = ps_malloc(size);
  }

  if (ptr == nullptr) {
    ptr = malloc(size);
  }

  return ptr;
}

static void gifFree(void *ptr) {
  if (ptr != nullptr) {
    free(ptr);
  }
}

static void *gifOpenFile(
    const char *filename,
    int32_t *fileSize) {
  (void)filename;

  gifPlaybackFile = LittleFS.open(GIF_PATH, "r");

  if (!gifPlaybackFile) {
    return nullptr;
  }

  *fileSize =
      static_cast<int32_t>(gifPlaybackFile.size());

  return &gifPlaybackFile;
}

static void gifCloseFile(void *handle) {
  File *file = static_cast<File *>(handle);

  if (file != nullptr) {
    file->close();
  }
}

static int32_t gifReadFile(
    GIFFILE *file,
    uint8_t *buffer,
    int32_t length) {
  if (file == nullptr ||
      file->fHandle == nullptr ||
      buffer == nullptr ||
      length <= 0) {
    return 0;
  }

  File *source =
      static_cast<File *>(file->fHandle);

  int32_t remaining =
      file->iSize - file->iPos;

  if (remaining <= 0) {
    return 0;
  }

  int32_t toRead =
      length < remaining ? length : remaining;

  int32_t read =
      static_cast<int32_t>(
          source->read(
              buffer,
              static_cast<size_t>(toRead)));

  file->iPos =
      static_cast<int32_t>(
          source->position());

  return read;
}

static int32_t gifSeekFile(
    GIFFILE *file,
    int32_t position) {
  if (file == nullptr ||
      file->fHandle == nullptr ||
      position < 0) {
    return -1;
  }

  File *source =
      static_cast<File *>(file->fHandle);

  if (!source->seek(
          static_cast<uint32_t>(position))) {
    return -1;
  }

  file->iPos =
      static_cast<int32_t>(
          source->position());

  return file->iPos;
}

static GifScaleMode parseGifScaleMode(String value) {
  value.trim();
  value.toUpperCase();

  if (value == "FIT") return GIF_SCALE_FIT;
  if (value == "STRETCH") return GIF_SCALE_STRETCH;
  if (value == "TILE") return GIF_SCALE_TILE;
  if (value == "CENTER") return GIF_SCALE_CENTER;
  if (value == "SPAN") return GIF_SCALE_SPAN;
  return GIF_SCALE_FILL;
}

static void configureGifTransform() {
  float sourceW =
      static_cast<float>(gifCanvasWidth);

  float sourceH =
      static_cast<float>(gifCanvasHeight);

  float fit =
      min(
          TFT_WIDTH / sourceW,
          TFT_HEIGHT / sourceH);

  float fill =
      max(
          TFT_WIDTH / sourceW,
          TFT_HEIGHT / sourceH);

  gifScaleX = 1.0f;
  gifScaleY = 1.0f;

  switch (gifScaleMode) {
    case GIF_SCALE_FIT:
      gifScaleX = gifScaleY = fit;
      break;

    case GIF_SCALE_STRETCH:
      gifScaleX = TFT_WIDTH / sourceW;
      gifScaleY = TFT_HEIGHT / sourceH;
      break;

    case GIF_SCALE_CENTER:
      // Original-size mode: never upscale small GIFs. Only shrink if the
      // source is larger than the 480x320 panel, preserving aspect ratio.
      gifScaleX = gifScaleY = min(1.0f, fit);
      break;

    case GIF_SCALE_SPAN:
      gifScaleX = gifScaleY = fill * 1.08f;
      break;

    case GIF_SCALE_TILE:
      // Animated GIF tiling is intentionally lightweight: use Fit instead
      // of expanding the compressed file into several raw copies.
      gifScaleX = gifScaleY = fit;
      break;

    case GIF_SCALE_FILL:
    default:
      gifScaleX = gifScaleY = fill;
      break;
  }

  float drawW = sourceW * gifScaleX;
  float drawH = sourceH * gifScaleY;

  gifOffsetX =
      (TFT_WIDTH - drawW) * 0.5f;

  gifOffsetY =
      (TFT_HEIGHT - drawH) * 0.5f;
}

static void gifDraw(GIFDRAW *draw) {
  if (draw == nullptr ||
      renderBuffer == nullptr ||
      draw->pPixels == nullptr) {
    return;
  }

  const uint16_t *pixels =
      reinterpret_cast<const uint16_t *>(
          draw->pPixels);

  int sourceY =
      draw->iY + draw->y;

  int sourceX =
      draw->iX;

  int width = draw->iWidth;

  for (int x = 0; x < width; ++x) {
    int sx = sourceX + x;
    int sy = sourceY;

    if (sx < 0 ||
        sy < 0 ||
        sx >= gifCanvasWidth ||
        sy >= gifCanvasHeight) {
      continue;
    }

    float rx = static_cast<float>(sx);
    float ry = static_cast<float>(sy);

    int dx0 =
        static_cast<int>(
            floorf(
                gifOffsetX +
                rx * gifScaleX));

    int dx1 =
        static_cast<int>(
            ceilf(
                gifOffsetX +
                (rx + 1.0f) * gifScaleX)) - 1;

    int dy0 =
        static_cast<int>(
            floorf(
                gifOffsetY +
                ry * gifScaleY));

    int dy1 =
        static_cast<int>(
            ceilf(
                gifOffsetY +
                (ry + 1.0f) * gifScaleY)) - 1;

    if (dx1 < 0 ||
        dy1 < 0 ||
        dx0 >= TFT_WIDTH ||
        dy0 >= TFT_HEIGHT) {
      continue;
    }

    dx0 = dx0 < 0 ? 0 : dx0;
    dy0 = dy0 < 0 ? 0 : dy0;
    dx1 =
        dx1 >= TFT_WIDTH
            ? TFT_WIDTH - 1
            : dx1;
    dy1 =
        dy1 >= TFT_HEIGHT
            ? TFT_HEIGHT - 1
            : dy1;

    uint16_t color = pixels[x];

    for (int dy = dy0; dy <= dy1; ++dy) {
      uint16_t *row =
          renderBuffer +
          static_cast<size_t>(dy) * TFT_WIDTH;

      for (int dx = dx0; dx <= dx1; ++dx) {
        row[dx] = color;
      }
    }
  }
}

static void closeGifDecoder() {
  if (gifDecoder.getFrameBuf() != nullptr) {
    gifDecoder.freeFrameBuf(gifFree);
  }

  if (gifDecoderOpen) {
    gifDecoder.close();
  }

  if (gifPlaybackFile) {
    gifPlaybackFile.close();
  }

  gifDecoderOpen = false;
  gifAtEnd = false;
  gifCanvasWidth = 0;
  gifCanvasHeight = 0;
  gifNextFrameAt = 0;
}

static bool gifDimensionsSupported(
    uint16_t width,
    uint16_t height) {
  return
      width >= 1 &&
      height >= 1 &&
      width <= 1024 &&
      height <= 1024;
}

static bool readGifHeader(
    const char *path,
    uint16_t &width,
    uint16_t &height,
    size_t &fileSize) {
  if (!littleFsReady) {
    return false;
  }

  File file = LittleFS.open(path, "r");

  if (!file) {
    return false;
  }

  fileSize = file.size();

  uint8_t header[10] = {};

  size_t read =
      file.read(
          header,
          sizeof(header));

  file.close();

  if (read != sizeof(header) ||
      header[0] != 'G' ||
      header[1] != 'I' ||
      header[2] != 'F') {
    return false;
  }

  width =
      static_cast<uint16_t>(
          header[6] |
          (header[7] << 8));

  height =
      static_cast<uint16_t>(
          header[8] |
          (header[9] << 8));

  return gifDimensionsSupported(
      width,
      height);
}

static bool openGifDecoder() {
  closeGifDecoder();

  if (!littleFsReady ||
      !LittleFS.exists(GIF_PATH) ||
      renderBuffer == nullptr) {
    return false;
  }

  gifDecoder.begin(
      GIF_PALETTE_RGB565_LE);

  if (!gifDecoder.open(
          GIF_PATH,
          gifOpenFile,
          gifCloseFile,
          gifReadFile,
          gifSeekFile,
          gifDraw)) {
    closeGifDecoder();
    return false;
  }

  gifCanvasWidth =
      static_cast<uint16_t>(
          gifDecoder.getCanvasWidth());

  gifCanvasHeight =
      static_cast<uint16_t>(
          gifDecoder.getCanvasHeight());

  if (!gifDimensionsSupported(
          gifCanvasWidth,
          gifCanvasHeight)) {
    closeGifDecoder();
    return false;
  }

  if (gifDecoder.allocFrameBuf(gifAlloc) !=
      GIF_SUCCESS) {
    closeGifDecoder();
    return false;
  }

  if (gifDecoder.setDrawType(
          GIF_DRAW_COOKED) != GIF_SUCCESS) {
    closeGifDecoder();
    return false;
  }

  configureGifTransform();

  memset(
      renderBuffer,
      0,
      static_cast<size_t>(TFT_WIDTH) *
          TFT_HEIGHT *
          sizeof(uint16_t));

  gifDecoderOpen = true;
  gifAtEnd = false;
  gifNextFrameAt = 0;

  return true;
}

static bool decodeNextGifFrame() {
  if (!gifDecoderOpen ||
      renderBuffer == nullptr ||
      !displayReady) {
    return false;
  }

  if (gifAtEnd) {
    gifDecoder.reset();
    gifAtEnd = false;

    memset(
        renderBuffer,
        0,
        static_cast<size_t>(TFT_WIDTH) *
            TFT_HEIGHT *
            sizeof(uint16_t));
  }

  int delayMs = 0;

  int hasMore =
      gifDecoder.playFrame(
          false,
          &delayMs,
          nullptr);

  tft->draw16bitRGBBitmap(
      0,
      0,
      renderBuffer,
      TFT_WIDTH,
      TFT_HEIGHT);

  gifAtEnd = hasMore == 0;

  uint32_t holdMs =
      delayMs < GIF_MIN_FRAME_MS
          ? GIF_MIN_FRAME_MS
          : static_cast<uint32_t>(delayMs);

  gifNextFrameAt =
      millis() + holdMs;

  return true;
}

static void closeGifUploadFile() {
  if (gifUploadFile) {
    gifUploadFile.close();
  }
}

static bool beginGifUpload(
    uint32_t expectedBytes,
    uint16_t width,
    uint16_t height,
    GifScaleMode scaleMode) {
  if (!littleFsReady ||
      expectedBytes < 10 ||
      !gifDimensionsSupported(
          width,
          height)) {
    return false;
  }

  // Drop any previous raw/GIF/JPEG media before allocating the new
  // compressed upload. This also frees legacy PSRAM frame buffers.
  clearSaverBuffer();

  size_t total =
      LittleFS.totalBytes();

  size_t used =
      LittleFS.usedBytes();

  size_t freeBytes =
      total > used
          ? total - used
          : 0;

  if (expectedBytes + 4096 > freeBytes) {
    return false;
  }

  gifUploadFile =
      LittleFS.open(
          GIF_TMP_PATH,
          "w");

  if (!gifUploadFile) {
    return false;
  }

  gifUploadExpectedBytes =
      expectedBytes;

  gifUploadWidth = width;
  gifUploadHeight = height;

  // PIXEL PRO no longer accepts host-side Fill/Fit for uploaded GIFs.
  // Small GIFs stay pixel-sized; only oversized canvases are reduced to fit.
  (void)scaleMode;
  gifUploadScaleMode = GIF_SCALE_CENTER;

  saverBytesReceived = 0;
  saverDataBytes = expectedBytes;
  saverWidth = width;
  saverHeight = height;
  saverFormat = SAVER_GIF;
  saverUploading = true;
  saverReady = false;
  saverActive = false;

  return true;
}

static bool writeGifUploadChunk(
    uint32_t offset,
    const String &encoded) {
  if (!saverUploading ||
      saverFormat != SAVER_GIF ||
      !gifUploadFile ||
      offset != saverBytesReceived) {
    return false;
  }

  uint8_t decoded[1100] = {};
  size_t decodedLength = 0;

  int result =
      mbedtls_base64_decode(
          decoded,
          sizeof(decoded),
          &decodedLength,
          reinterpret_cast<
              const unsigned char *>(
              encoded.c_str()),
          encoded.length());

  if (result != 0 ||
      decodedLength == 0 ||
      saverBytesReceived +
              decodedLength >
          gifUploadExpectedBytes) {
    return false;
  }

  size_t written =
      gifUploadFile.write(
          decoded,
          decodedLength);

  if (written != decodedLength) {
    return false;
  }

  saverBytesReceived +=
      decodedLength;

  return true;
}

static bool finishGifUpload() {
  if (!saverUploading ||
      saverFormat != SAVER_GIF ||
      saverBytesReceived !=
          gifUploadExpectedBytes) {
    closeGifUploadFile();
    return false;
  }

  gifUploadFile.flush();
  closeGifUploadFile();

  uint16_t actualWidth = 0;
  uint16_t actualHeight = 0;
  size_t actualSize = 0;

  if (!readGifHeader(
          GIF_TMP_PATH,
          actualWidth,
          actualHeight,
          actualSize) ||
      actualSize !=
          gifUploadExpectedBytes ||
      actualWidth != gifUploadWidth ||
      actualHeight != gifUploadHeight) {
    LittleFS.remove(GIF_TMP_PATH);
    saverUploading = false;
    saverReady = false;
    return false;
  }

  LittleFS.remove(GIF_PATH);

  if (!LittleFS.rename(
          GIF_TMP_PATH,
          GIF_PATH)) {
    LittleFS.remove(GIF_TMP_PATH);
    saverUploading = false;
    saverReady = false;
    return false;
  }

  LittleFS.remove(
      PACKED_PATH);
  LittleFS.remove(
      PACKED_TMP_PATH);

  saverUploading = false;
  saverReady = true;
  saverActive = false;
  saverFormat = SAVER_GIF;
  saverWidth = actualWidth;
  saverHeight = actualHeight;
  saverDataBytes = actualSize;
  saverBytesReceived = actualSize;
  gifScaleMode = gifUploadScaleMode;
  preferences.putUChar(
      "gscale",
      static_cast<uint8_t>(gifScaleMode));
  preferences.putUChar(
      "gscalev",
      2);

  return true;
}

static void loadPersistedGif() {
  if (!littleFsReady ||
      !LittleFS.exists(GIF_PATH)) {
    return;
  }

  uint16_t width = 0;
  uint16_t height = 0;
  size_t fileSize = 0;

  if (!readGifHeader(
          GIF_PATH,
          width,
          height,
          fileSize)) {
    LittleFS.remove(GIF_PATH);
    return;
  }

  saverFormat = SAVER_GIF;

  // Always migrate persisted media to Center/no-upscale. Older builds could
  // leave Fill/Fit in NVS and make a small GIF look huge after reboot.
  gifScaleMode = GIF_SCALE_CENTER;
  preferences.putUChar(
      "gscale",
      static_cast<uint8_t>(
          GIF_SCALE_CENTER));
  preferences.putUChar(
      "gscalev",
      3);

  saverWidth = width;
  saverHeight = height;
  saverDataBytes = fileSize;
  saverBytesReceived = fileSize;
  saverUploading = false;
  saverReady = true;
  saverActive = false;
}

static void closePackedFiles() {
  if (packedUploadFile) {
    packedUploadFile.close();
  }

  if (packedPlaybackFile) {
    packedPlaybackFile.close();
  }
}

static bool readPackedU16(
    File &file,
    uint16_t &value) {
  uint8_t bytes[2] = {};

  if (file.read(
          bytes,
          sizeof(bytes)) !=
      sizeof(bytes)) {
    return false;
  }

  value =
      static_cast<uint16_t>(
          bytes[0]) |
      (static_cast<uint16_t>(
           bytes[1]) <<
       8);

  return true;
}

static bool readPackedU32(
    File &file,
    uint32_t &value) {
  uint8_t bytes[4] = {};

  if (file.read(
          bytes,
          sizeof(bytes)) !=
      sizeof(bytes)) {
    return false;
  }

  value =
      static_cast<uint32_t>(
          bytes[0]) |
      (static_cast<uint32_t>(
           bytes[1]) <<
       8) |
      (static_cast<uint32_t>(
           bytes[2]) <<
       16) |
      (static_cast<uint32_t>(
           bytes[3]) <<
       24);

  return true;
}

static uint16_t rgb888To565(
    uint8_t r,
    uint8_t g,
    uint8_t b) {
  return static_cast<uint16_t>(
      ((r & 0xF8) << 8) |
      ((g & 0xFC) << 3) |
      (b >> 3));
}

static uint16_t expectedPackedPaletteCount(
    uint8_t mode) {
  switch (mode) {
    case 2:
      return 256;
    case 3:
      return 16;
    case 4:
      return 4;
    case 5:
      return 2;
    default:
      return 0;
  }
}

static bool readPackedHeader(
    File &file,
    bool loadPalette) {
  if (!file ||
      !file.seek(0)) {
    return false;
  }

  uint8_t magic[4] = {};

  if (file.read(
          magic,
          sizeof(magic)) !=
      sizeof(magic) ||
      magic[0] != 'P' ||
      magic[1] != 'X' ||
      magic[2] != 'Q' ||
      magic[3] != '1') {
    return false;
  }

  int modeRead =
      file.read();

  int flagsRead =
      file.read();

  if (modeRead < 0 ||
      flagsRead < 0) {
    return false;
  }

  uint8_t mode =
      static_cast<uint8_t>(
          modeRead);

  uint8_t flags =
      static_cast<uint8_t>(
          flagsRead);

  uint16_t storageWidth = 0;
  uint16_t storageHeight = 0;
  uint16_t displayWidth = 0;
  uint16_t displayHeight = 0;
  uint16_t frameCount = 0;
  uint16_t fps = 0;
  uint32_t durationMs = 0;
  uint16_t paletteCount = 0;
  uint16_t reserved = 0;

  if (!readPackedU16(
          file,
          storageWidth) ||
      !readPackedU16(
          file,
          storageHeight) ||
      !readPackedU16(
          file,
          displayWidth) ||
      !readPackedU16(
          file,
          displayHeight) ||
      !readPackedU16(
          file,
          frameCount) ||
      !readPackedU16(
          file,
          fps) ||
      !readPackedU32(
          file,
          durationMs) ||
      !readPackedU16(
          file,
          paletteCount) ||
      !readPackedU16(
          file,
          reserved)) {
    return false;
  }

  (void)reserved;

  bool supportedStorage =
      (storageWidth == 480 &&
       storageHeight == 320) ||
      (storageWidth == 360 &&
       storageHeight == 240) ||
      (storageWidth == 240 &&
       storageHeight == 160);

  if (mode > 5 ||
      (flags & 0x03) != 0x03 ||
      !supportedStorage ||
      displayWidth != TFT_WIDTH ||
      displayHeight != TFT_HEIGHT ||
      frameCount == 0 ||
      fps < 15 ||
      fps > 60 ||
      durationMs == 0 ||
      paletteCount !=
          expectedPackedPaletteCount(
              mode)) {
    return false;
  }

  if (file.size() == 0 ||
      file.size() >
          PACKED_UPLOAD_LIMIT_BYTES) {
    return false;
  }

  memset(
      packedPalette565,
      0,
      sizeof(packedPalette565));

  for (uint16_t index = 0;
       index < paletteCount;
       ++index) {
    int r = file.read();
    int g = file.read();
    int b = file.read();

    if (r < 0 ||
        g < 0 ||
        b < 0) {
      return false;
    }

    if (loadPalette) {
      packedPalette565[index] =
          rgb888To565(
              static_cast<uint8_t>(r),
              static_cast<uint8_t>(g),
              static_cast<uint8_t>(b));
    }
  }

  packedColorMode =
      mode;

  packedStorageWidth =
      storageWidth;

  packedStorageHeight =
      storageHeight;

  packedFrameCount =
      frameCount;

  packedFps =
      fps;

  packedDurationMs =
      durationMs;

  packedPaletteCount =
      paletteCount;

  packedFramesOffset =
      static_cast<uint32_t>(
          file.position());

  return true;
}

static bool inspectPackedFile(
    const char *path,
    size_t &fileSize) {
  if (!littleFsReady ||
      !LittleFS.exists(
          path)) {
    return false;
  }

  File file =
      LittleFS.open(
          path,
          "r");

  if (!file) {
    return false;
  }

  fileSize =
      file.size();

  bool ok =
      readPackedHeader(
          file,
          false);

  file.close();

  return ok;
}

static bool beginPackedUpload(
    uint32_t expectedBytes) {
  if (!littleFsReady ||
      expectedBytes < 26 ||
      expectedBytes >
          PACKED_UPLOAD_LIMIT_BYTES) {
    return false;
  }

  clearSaverBuffer();

  size_t total =
      LittleFS.totalBytes();

  size_t used =
      LittleFS.usedBytes();

  size_t freeBytes =
      total > used
          ? total - used
          : 0;

  if (expectedBytes + 4096 >
      freeBytes) {
    return false;
  }

  packedUploadFile =
      LittleFS.open(
          PACKED_TMP_PATH,
          "w");

  if (!packedUploadFile) {
    return false;
  }

  packedUploadExpectedBytes =
      expectedBytes;

  saverBytesReceived =
      0;

  saverDataBytes =
      expectedBytes;

  saverFormat =
      SAVER_PACKED;

  saverUploading =
      true;

  saverReady =
      false;

  saverActive =
      false;

  return true;
}

static bool writePackedUploadChunk(
    uint32_t offset,
    const String &encoded) {
  if (!saverUploading ||
      saverFormat != SAVER_PACKED ||
      !packedUploadFile ||
      offset !=
          saverBytesReceived) {
    return false;
  }

  uint8_t decoded[1100] = {};
  size_t decodedLength = 0;

  int result =
      mbedtls_base64_decode(
          decoded,
          sizeof(decoded),
          &decodedLength,
          reinterpret_cast<
              const unsigned char *>(
              encoded.c_str()),
          encoded.length());

  if (result != 0 ||
      decodedLength == 0 ||
      saverBytesReceived +
              decodedLength >
          packedUploadExpectedBytes) {
    return false;
  }

  size_t written =
      packedUploadFile.write(
          decoded,
          decodedLength);

  if (written !=
      decodedLength) {
    return false;
  }

  saverBytesReceived +=
      decodedLength;

  return true;
}

static bool finishPackedUpload() {
  if (!saverUploading ||
      saverFormat != SAVER_PACKED ||
      saverBytesReceived !=
          packedUploadExpectedBytes) {
    closePackedFiles();
    return false;
  }

  packedUploadFile.flush();
  packedUploadFile.close();

  size_t actualSize = 0;

  if (!inspectPackedFile(
          PACKED_TMP_PATH,
          actualSize) ||
      actualSize !=
          packedUploadExpectedBytes) {
    LittleFS.remove(
        PACKED_TMP_PATH);

    saverUploading =
        false;

    saverReady =
        false;

    return false;
  }

  LittleFS.remove(
      PACKED_PATH);

  if (!LittleFS.rename(
          PACKED_TMP_PATH,
          PACKED_PATH)) {
    LittleFS.remove(
        PACKED_TMP_PATH);

    saverUploading =
        false;

    saverReady =
        false;

    return false;
  }

  LittleFS.remove(
      GIF_PATH);

  LittleFS.remove(
      GIF_TMP_PATH);

  LittleFS.remove(
      JPEG_PATH);

  LittleFS.remove(
      JPEG_TMP_PATH);

  saverUploading =
      false;

  saverReady =
      true;

  saverActive =
      false;

  saverFormat =
      SAVER_PACKED;

  saverWidth =
      packedStorageWidth;

  saverHeight =
      packedStorageHeight;

  saverDataBytes =
      actualSize;

  saverBytesReceived =
      actualSize;

  return true;
}

static bool loadPersistedPacked() {
  if (!littleFsReady ||
      !LittleFS.exists(
          PACKED_PATH)) {
    return false;
  }

  size_t fileSize = 0;

  if (!inspectPackedFile(
          PACKED_PATH,
          fileSize)) {
    LittleFS.remove(
        PACKED_PATH);

    return false;
  }

  saverFormat =
      SAVER_PACKED;

  saverWidth =
      packedStorageWidth;

  saverHeight =
      packedStorageHeight;

  saverDataBytes =
      fileSize;

  saverBytesReceived =
      fileSize;

  saverUploading =
      false;

  saverReady =
      true;

  saverActive =
      false;

  return true;
}

static bool readPackedSingleCode(
    File &file,
    uint8_t mode,
    uint16_t &color) {
  if (mode == 0) {
    int r = file.read();
    int g = file.read();
    int b = file.read();

    if (r < 0 ||
        g < 0 ||
        b < 0) {
      return false;
    }

    color =
        rgb888To565(
            static_cast<uint8_t>(r),
            static_cast<uint8_t>(g),
            static_cast<uint8_t>(b));

    return true;
  }

  if (mode == 1) {
    return readPackedU16(
        file,
        color);
  }

  int index =
      file.read();

  if (index < 0 ||
      static_cast<uint16_t>(
          index) >=
          packedPaletteCount) {
    return false;
  }

  color =
      packedPalette565[
          static_cast<uint8_t>(
              index)];

  return true;
}

static void setPackedScaledPixel(
    uint16_t sourceX,
    uint16_t color) {
  uint16_t dx0 =
      static_cast<uint16_t>(
          (static_cast<uint32_t>(
               sourceX) *
           TFT_WIDTH) /
          packedStorageWidth);

  uint16_t dx1 =
      static_cast<uint16_t>(
          (static_cast<uint32_t>(
               sourceX + 1U) *
           TFT_WIDTH) /
          packedStorageWidth);

  if (dx1 <= dx0) {
    dx1 =
        static_cast<uint16_t>(
            dx0 + 1U >
                    TFT_WIDTH
                ? TFT_WIDTH
                : dx0 + 1U);
  }

  for (uint16_t x = dx0;
       x < dx1 &&
       x < TFT_WIDTH;
       ++x) {
    packedLineBuffer[x] =
        color;
  }
}

static bool decodePackedSpan(
    File &file,
    uint16_t sourceY,
    uint16_t sourceX,
    uint16_t sourceCount) {
  if (sourceY >=
          packedStorageHeight ||
      sourceX >=
          packedStorageWidth ||
      sourceCount == 0 ||
      static_cast<uint32_t>(
          sourceX) +
              sourceCount >
          packedStorageWidth) {
    return false;
  }

  uint16_t produced = 0;

  while (produced <
         sourceCount) {
    int controlRead =
        file.read();

    if (controlRead < 0) {
      return false;
    }

    uint8_t control =
        static_cast<uint8_t>(
            controlRead);

    uint16_t packetCount =
        static_cast<uint16_t>(
            (control & 0x7F) +
            1U);

    if (produced +
            packetCount >
        sourceCount) {
      return false;
    }

    bool repeat =
        (control & 0x80) != 0;

    if (repeat) {
      uint16_t color = 0;

      if (!readPackedSingleCode(
              file,
              packedColorMode,
              color)) {
        return false;
      }

      for (uint16_t i = 0;
           i < packetCount;
           ++i) {
        setPackedScaledPixel(
            static_cast<uint16_t>(
                sourceX +
                produced +
                i),
            color);
      }

      produced +=
          packetCount;

      continue;
    }

    if (packedColorMode <= 2) {
      for (uint16_t i = 0;
           i < packetCount;
           ++i) {
        uint16_t color = 0;

        if (!readPackedSingleCode(
                file,
                packedColorMode,
                color)) {
          return false;
        }

        setPackedScaledPixel(
            static_cast<uint16_t>(
                sourceX +
                produced +
                i),
            color);
      }

      produced +=
          packetCount;

      continue;
    }

    uint8_t bits =
        packedColorMode == 3
            ? 4
            : packedColorMode == 4
                ? 2
                : 1;

    size_t packedBytes =
        (static_cast<size_t>(
             packetCount) *
             bits +
         7U) /
        8U;

    uint8_t packed[64] = {};

    if (packedBytes >
            sizeof(packed) ||
        file.read(
            packed,
            packedBytes) !=
            packedBytes) {
      return false;
    }

    uint8_t mask =
        static_cast<uint8_t>(
            (1U << bits) -
            1U);

    for (uint16_t i = 0;
         i < packetCount;
         ++i) {
      uint16_t bitPosition =
          static_cast<uint16_t>(
              i *
              bits);

      uint16_t byteIndex =
          bitPosition /
          8U;

      uint8_t shift =
          static_cast<uint8_t>(
              bitPosition %
              8U);

      uint8_t paletteIndex =
          static_cast<uint8_t>(
              (packed[byteIndex] >>
               shift) &
              mask);

      if (paletteIndex >=
          packedPaletteCount) {
        return false;
      }

      setPackedScaledPixel(
          static_cast<uint16_t>(
              sourceX +
              produced +
              i),
          packedPalette565[
              paletteIndex]);
    }

    produced +=
        packetCount;
  }

  uint16_t dx0 =
      static_cast<uint16_t>(
          (static_cast<uint32_t>(
               sourceX) *
           TFT_WIDTH) /
          packedStorageWidth);

  uint16_t dx1 =
      static_cast<uint16_t>(
          (static_cast<uint32_t>(
               sourceX +
               sourceCount) *
           TFT_WIDTH) /
          packedStorageWidth);

  uint16_t dy0 =
      static_cast<uint16_t>(
          (static_cast<uint32_t>(
               sourceY) *
           TFT_HEIGHT) /
          packedStorageHeight);

  uint16_t dy1 =
      static_cast<uint16_t>(
          (static_cast<uint32_t>(
               sourceY + 1U) *
           TFT_HEIGHT) /
          packedStorageHeight);

  if (dx1 <= dx0 ||
      dy1 <= dy0 ||
      dx1 > TFT_WIDTH ||
      dy1 > TFT_HEIGHT) {
    return false;
  }

  for (uint16_t y = dy0;
       y < dy1;
       ++y) {
    tft->draw16bitRGBBitmap(
        dx0,
        y,
        packedLineBuffer +
            dx0,
        dx1 - dx0,
        1);
  }

  return true;
}

static bool openPackedPlayback() {
  closePackedFiles();

  if (!littleFsReady ||
      !LittleFS.exists(
          PACKED_PATH)) {
    return false;
  }

  packedPlaybackFile =
      LittleFS.open(
          PACKED_PATH,
          "r");

  if (!packedPlaybackFile) {
    return false;
  }

  if (!readPackedHeader(
          packedPlaybackFile,
          true)) {
    packedPlaybackFile.close();
    return false;
  }

  packedFrameIndex =
      0;

  packedNextFrameAt =
      millis();

  return true;
}

static bool decodeNextPackedFrame() {
  if (!packedPlaybackFile ||
      packedFrameCount == 0) {
    return false;
  }

  if (packedFrameIndex >=
      packedFrameCount) {
    if (!packedPlaybackFile.seek(
            packedFramesOffset)) {
      return false;
    }

    packedFrameIndex =
        0;

    tft->fillScreen(
        RGB565_BLACK);
  }

  uint16_t durationMs = 0;
  uint16_t spanCount = 0;

  if (!readPackedU16(
          packedPlaybackFile,
          durationMs) ||
      !readPackedU16(
          packedPlaybackFile,
          spanCount)) {
    return false;
  }

  for (uint16_t span = 0;
       span < spanCount;
       ++span) {
    uint16_t y = 0;
    uint16_t x = 0;
    uint16_t count = 0;

    if (!readPackedU16(
            packedPlaybackFile,
            y) ||
        !readPackedU16(
            packedPlaybackFile,
            x) ||
        !readPackedU16(
            packedPlaybackFile,
            count) ||
        !decodePackedSpan(
            packedPlaybackFile,
            y,
            x,
            count)) {
      return false;
    }
  }

  packedFrameIndex++;

  packedNextFrameAt =
      millis() +
      (durationMs == 0
           ? 1U
           : static_cast<uint32_t>(
                 durationMs));

  return true;
}

static void closeJpegUploadFile() {
  if (jpegUploadFile) {
    jpegUploadFile.close();
  }
}

static int jpegDraw(JPEGDRAW *draw) {
  if (draw == nullptr ||
      draw->pPixels == nullptr ||
      !displayReady) {
    return 0;
  }

  int x = draw->x;
  int y = draw->y;
  int sourceStride = draw->iWidth;
  int width =
      draw->iWidthUsed > 0
          ? draw->iWidthUsed
          : draw->iWidth;
  int height = draw->iHeight;

  if (x < 0 ||
      y < 0 ||
      x + width > TFT_WIDTH ||
      y + height > TFT_HEIGHT ||
      sourceStride < width) {
    return 0;
  }

  // iWidthUsed can be smaller than the MCU row stride on odd image widths.
  // Draw row-by-row so edge padding never writes outside the centered image.
  for (int row = 0; row < height; ++row) {
    tft->draw16bitRGBBitmap(
        x,
        y + row,
        draw->pPixels +
            static_cast<size_t>(row) *
            sourceStride,
        width,
        1);
  }

  return 1;
}

static bool inspectJpeg(
    const char *path,
    uint16_t &width,
    uint16_t &height,
    size_t &fileSize) {
  if (!littleFsReady) {
    return false;
  }

  File file =
      LittleFS.open(
          path,
          "r");

  if (!file) {
    return false;
  }

  fileSize =
      file.size();

  JPEGDEC decoder;

  if (!decoder.open(
          file,
          jpegDraw)) {
    file.close();
    return false;
  }

  int decodedWidth =
      decoder.getWidth();

  int decodedHeight =
      decoder.getHeight();

  decoder.close();
  file.close();

  if (decodedWidth < 1 ||
      decodedHeight < 1 ||
      decodedWidth > TFT_WIDTH ||
      decodedHeight > TFT_HEIGHT) {
    return false;
  }

  width =
      static_cast<uint16_t>(
          decodedWidth);

  height =
      static_cast<uint16_t>(
          decodedHeight);

  return true;
}

static bool renderJpeg() {
  if (!littleFsReady ||
      !LittleFS.exists(JPEG_PATH) ||
      !displayReady) {
    return false;
  }

  jpegPlaybackFile =
      LittleFS.open(
          JPEG_PATH,
          "r");

  if (!jpegPlaybackFile) {
    return false;
  }

  if (!jpegDecoder.open(
          jpegPlaybackFile,
          jpegDraw)) {
    jpegPlaybackFile.close();
    return false;
  }

  int width =
      jpegDecoder.getWidth();

  int height =
      jpegDecoder.getHeight();

  if (width < 1 ||
      height < 1 ||
      width > TFT_WIDTH ||
      height > TFT_HEIGHT) {
    jpegDecoder.close();
    jpegPlaybackFile.close();
    return false;
  }

  int offsetX =
      (TFT_WIDTH - width) / 2;

  int offsetY =
      (TFT_HEIGHT - height) / 2;

  tft->fillScreen(
      RGB565_BLACK);

  int result =
      jpegDecoder.decode(
          offsetX,
          offsetY,
          0);

  jpegDecoder.close();
  jpegPlaybackFile.close();

  return result != 0;
}

static bool beginJpegUpload(
    uint32_t expectedBytes,
    uint16_t width,
    uint16_t height) {
  if (!littleFsReady ||
      expectedBytes < 4 ||
      expectedBytes > JPEG_UPLOAD_LIMIT_BYTES ||
      width < 1 ||
      height < 1 ||
      width > TFT_WIDTH ||
      height > TFT_HEIGHT) {
    return false;
  }

  clearSaverBuffer();

  size_t total =
      LittleFS.totalBytes();

  size_t used =
      LittleFS.usedBytes();

  size_t freeBytes =
      total > used
          ? total - used
          : 0;

  if (expectedBytes + 4096 > freeBytes) {
    return false;
  }

  jpegUploadFile =
      LittleFS.open(
          JPEG_TMP_PATH,
          "w");

  if (!jpegUploadFile) {
    return false;
  }

  jpegUploadExpectedBytes =
      expectedBytes;
  jpegUploadWidth =
      width;
  jpegUploadHeight =
      height;

  saverBytesReceived = 0;
  saverDataBytes =
      expectedBytes;
  saverWidth =
      width;
  saverHeight =
      height;
  saverFormat =
      SAVER_JPEG;
  saverUploading = true;
  saverReady = false;
  saverActive = false;

  return true;
}

static bool writeJpegUploadChunk(
    uint32_t offset,
    const String &encoded) {
  if (!saverUploading ||
      saverFormat != SAVER_JPEG ||
      !jpegUploadFile ||
      offset != saverBytesReceived) {
    return false;
  }

  uint8_t decoded[1100] = {};
  size_t decodedLength = 0;

  int result =
      mbedtls_base64_decode(
          decoded,
          sizeof(decoded),
          &decodedLength,
          reinterpret_cast<
              const unsigned char *>(
              encoded.c_str()),
          encoded.length());

  if (result != 0 ||
      decodedLength == 0 ||
      saverBytesReceived +
              decodedLength >
          jpegUploadExpectedBytes) {
    return false;
  }

  size_t written =
      jpegUploadFile.write(
          decoded,
          decodedLength);

  if (written != decodedLength) {
    return false;
  }

  saverBytesReceived +=
      decodedLength;

  return true;
}

static bool finishJpegUpload() {
  if (!saverUploading ||
      saverFormat != SAVER_JPEG ||
      saverBytesReceived !=
          jpegUploadExpectedBytes) {
    closeJpegUploadFile();
    return false;
  }

  jpegUploadFile.flush();
  closeJpegUploadFile();

  uint16_t actualWidth = 0;
  uint16_t actualHeight = 0;
  size_t actualSize = 0;

  if (!inspectJpeg(
          JPEG_TMP_PATH,
          actualWidth,
          actualHeight,
          actualSize) ||
      actualSize !=
          jpegUploadExpectedBytes ||
      actualWidth !=
          jpegUploadWidth ||
      actualHeight !=
          jpegUploadHeight) {
    LittleFS.remove(
        JPEG_TMP_PATH);
    saverUploading = false;
    saverReady = false;
    return false;
  }

  LittleFS.remove(
      JPEG_PATH);

  if (!LittleFS.rename(
          JPEG_TMP_PATH,
          JPEG_PATH)) {
    LittleFS.remove(
        JPEG_TMP_PATH);
    saverUploading = false;
    saverReady = false;
    return false;
  }

  LittleFS.remove(
      GIF_PATH);
  LittleFS.remove(
      GIF_TMP_PATH);
  LittleFS.remove(
      PACKED_PATH);
  LittleFS.remove(
      PACKED_TMP_PATH);

  saverUploading = false;
  saverReady = true;
  saverActive = false;
  saverFormat =
      SAVER_JPEG;
  saverWidth =
      actualWidth;
  saverHeight =
      actualHeight;
  saverDataBytes =
      actualSize;
  saverBytesReceived =
      actualSize;

  preferences.putUShort(
      "jpgw",
      actualWidth);
  preferences.putUShort(
      "jpgh",
      actualHeight);

  return true;
}

static bool loadPersistedJpeg() {
  if (!littleFsReady ||
      !LittleFS.exists(
          JPEG_PATH)) {
    return false;
  }

  uint16_t width = 0;
  uint16_t height = 0;
  size_t fileSize = 0;

  if (!inspectJpeg(
          JPEG_PATH,
          width,
          height,
          fileSize)) {
    LittleFS.remove(
        JPEG_PATH);
    return false;
  }

  saverFormat =
      SAVER_JPEG;
  saverWidth =
      width;
  saverHeight =
      height;
  saverDataBytes =
      fileSize;
  saverBytesReceived =
      fileSize;
  saverUploading = false;
  saverReady = true;
  saverActive = false;

  return true;
}

static void loadPersistedMedia() {
  saverReady = false;

  if (loadPersistedPacked()) {
    return;
  }

  if (loadPersistedJpeg()) {
    return;
  }

  loadPersistedGif();
}

static void clearSaverBuffer() {
  closeGifDecoder();
  closeGifUploadFile();
  closeJpegUploadFile();
  closePackedFiles();

  if (jpegPlaybackFile) {
    jpegPlaybackFile.close();
  }

  if (littleFsReady) {
    LittleFS.remove(GIF_TMP_PATH);
    LittleFS.remove(GIF_PATH);
    LittleFS.remove(JPEG_TMP_PATH);
    LittleFS.remove(JPEG_PATH);
    LittleFS.remove(PACKED_TMP_PATH);
    LittleFS.remove(PACKED_PATH);
  }

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

  gifUploadExpectedBytes = 0;
  gifUploadWidth = 0;
  gifUploadHeight = 0;

  jpegUploadExpectedBytes = 0;
  jpegUploadWidth = 0;
  jpegUploadHeight = 0;

  packedUploadExpectedBytes = 0;
  packedStorageWidth = 0;
  packedStorageHeight = 0;
  packedFrameCount = 0;
  packedFps = 0;
  packedFrameIndex = 0;
  packedPaletteCount = 0;
  packedColorMode = 0;
  packedDurationMs = 0;
  packedFramesOffset = 0;
  packedNextFrameAt = 0;
  memset(
      packedPalette565,
      0,
      sizeof(packedPalette565));

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
  tftBus->write(0x11);
  tftBus->endWrite();

  tft->fillScreen(RGB565_BLACK);

  if (ESP.getPsramSize() > 0) {
    renderBuffer = static_cast<uint16_t *>(
        ps_malloc(static_cast<size_t>(TFT_WIDTH) * TFT_HEIGHT * 2));

    packedLineBuffer = static_cast<uint16_t *>(
        ps_malloc(static_cast<size_t>(TFT_WIDTH) * sizeof(uint16_t)));
  }

  // Persistent media stays in flash/LittleFS. PSRAM is used only as a
  // transient decode/render buffer; fall back to internal SRAM if PSRAM
  // allocation is unavailable.
  if (packedLineBuffer == nullptr) {
    packedLineBuffer = packedLineFallback;
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
  if (!saverReady || !displayReady) {
    return;
  }

  if (saverFormat == SAVER_PACKED) {
    if (!openPackedPlayback()) {
      saverReady = false;
      return;
    }

    saverActive = true;
    saverFrameStartedAt = millis();

    tft->fillScreen(
        RGB565_BLACK);

    if (!decodeNextPackedFrame()) {
      stopSaver();
    }

    return;
  }

  if (saverFormat == SAVER_GIF) {
    if (!openGifDecoder()) {
      saverReady = false;
      return;
    }

    saverActive = true;
    saverFrameIndex = 0;
    saverFrameStartedAt = millis();

    if (!decodeNextGifFrame()) {
      stopSaver();
    }

    return;
  }

  if (saverFormat == SAVER_JPEG) {
    saverActive = true;
    saverFrameIndex = 0;
    saverFrameStartedAt = millis();

    if (!renderJpeg()) {
      saverReady = false;
      stopSaver();
    }

    return;
  }

  if (saverData == nullptr) {
    return;
  }

  saverActive = true;
  saverFrameIndex = 0;
  saverFrameStartedAt = millis();
  renderSaverFrame(0);
}

static void stopSaver() {
  bool wasActive = saverActive;

  saverActive = false;
  saverFrameIndex = 0;
  saverFrameStartedAt = 0;

  closeGifDecoder();

  if (packedPlaybackFile) {
    packedPlaybackFile.close();
  }

  if (wasActive && displayReady) {
    renderMainMenu();
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

  if (!saverReady) {
    return;
  }

  if (saverFormat == SAVER_PACKED) {
    if (static_cast<int32_t>(
            now - packedNextFrameAt) >= 0) {
      if (!decodeNextPackedFrame()) {
        stopSaver();
      }
    }

    return;
  }

  if (saverFormat == SAVER_GIF) {
    if (static_cast<int32_t>(
            now - gifNextFrameAt) >= 0) {
      if (!decodeNextGifFrame()) {
        stopSaver();
      }
    }

    return;
  }

  if (saverFormat == SAVER_JPEG ||
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
      "PIXELPRO|1|FW=%s|MCU=ESP32S2|KEYS=8|PROFILES=20|LAYERS=4|MACROS=20|ACTIONS=32|DISPLAY=ILI9486,480x320,i8080-8|CAPS=HID,CDC,KEYMAP,LAYERS,HOST_MACRO,HOST_ACTION,MEM,PANEL,SAVER,MEDIA,DIRECT_GIF,DIRECT_JPEG,PXQ,RLE,DELTA,RGB_PER_KEY,RGB_EFFECTS,MAIN_MENU,MAIN_MENU_ICONS,ROM_BOOT|VID=%04X|PID=%04X",
      FW_VERSION,
      USB_VID_PIXEL,
      USB_PID_PIXEL);
  return String(out);
}

static void sendMemoryInfo() {
  const uint32_t flashTotal = ESP.getFlashChipSize();
  const uint32_t fileSystemUsed =
      littleFsReady
          ? static_cast<uint32_t>(LittleFS.usedBytes())
          : 0;
  const uint32_t sketchUsed = ESP.getSketchSize();
  const uint32_t flashUsed =
      sketchUsed + fileSystemUsed > flashTotal
          ? flashTotal
          : sketchUsed + fileSystemUsed;

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

  if (upper.startsWith("GET_MENUCFG|")) {
    uint16_t profile = 0;
    int sep = command.indexOf('|');

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(sep + 1),
            PROFILE_COUNT - 1,
            profile)) {
      cdcPrintln("ERR|BAD_MENU_PROFILE");
      return;
    }

    String out = "MENUCFG|";
    out += String(profile);
    out += '|';

    for (uint8_t slot = 0; slot < MENU_SLOT_COUNT; ++slot) {
      if (slot) out += ',';
      out += String(mainMenuConfig.actions[profile][slot]);
    }

    out += '|';

    for (uint8_t slot = 0; slot < MENU_SLOT_COUNT; ++slot) {
      if (slot) out += ',';
      out += String(mainMenuConfig.labels[profile][slot]);
    }

    cdcPrintln(out);
    return;
  }

  if (upper.startsWith("MENUCFG|")) {
    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    int p3 = command.indexOf('|', p2 + 1);

    uint16_t profile = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        p3 < 0 ||
        !parseUnsigned(
            command.substring(p1 + 1, p2),
            PROFILE_COUNT - 1,
            profile)) {
      cdcPrintln("ERR|BAD_MENUCFG");
      return;
    }

    String actionCsv =
        command.substring(
            p2 + 1,
            p3);

    int actionStart = 0;

    for (uint8_t slot = 0; slot < MENU_SLOT_COUNT; ++slot) {
      int comma =
          actionCsv.indexOf(
              ',',
              actionStart);

      bool last =
          slot ==
          MENU_SLOT_COUNT - 1;

      if ((!last && comma < 0) ||
          (last && comma >= 0)) {
        cdcPrintln("ERR|BAD_MENU_ACTIONS");
        return;
      }

      String token =
          last
              ? actionCsv.substring(actionStart)
              : actionCsv.substring(actionStart, comma);

      uint16_t action = 0;
      if (!parseUnsigned(
              token,
              ACTION_COUNT,
              action)) {
        cdcPrintln("ERR|BAD_MENU_ACTION");
        return;
      }

      mainMenuConfig.actions[profile][slot] =
          static_cast<uint8_t>(action);

      actionStart =
          comma + 1;
    }

    String labelCsv =
        command.substring(
            p3 + 1);

    int labelStart = 0;

    for (uint8_t slot = 0; slot < MENU_SLOT_COUNT; ++slot) {
      int comma =
          labelCsv.indexOf(
              ',',
              labelStart);

      bool last =
          slot ==
          MENU_SLOT_COUNT - 1;

      if ((!last && comma < 0) ||
          (last && comma >= 0)) {
        cdcPrintln("ERR|BAD_MENU_LABELS");
        return;
      }

      String label =
          last
              ? labelCsv.substring(labelStart)
              : labelCsv.substring(labelStart, comma);

      label.trim();

      if (label.length() > MENU_LABEL_MAX_LEN) {
        label =
            label.substring(
                0,
                MENU_LABEL_MAX_LEN);
      }

      memset(
          mainMenuConfig.labels[profile][slot],
          0,
          MENU_LABEL_MAX_LEN + 1);

      label.toCharArray(
          mainMenuConfig.labels[profile][slot],
          MENU_LABEL_MAX_LEN + 1);

      labelStart =
          comma + 1;
    }

    saveMainMenuConfig();

    if (profile == activeProfile &&
        !saverActive) {
      renderMainMenu();
    }

    cdcPrintln("OK|MENUCFG");
    return;
  }

  if (upper.startsWith("MENUBGBEGIN|")) {
    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    uint16_t profile = 0;
    uint32_t bytes = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        !parseUnsigned(
            command.substring(p1 + 1, p2),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsignedLong(
            command.substring(p2 + 1),
            MENU_BACKGROUND_LIMIT_BYTES,
            bytes) ||
        !beginMenuBackgroundUpload(
            static_cast<uint8_t>(profile),
            bytes)) {
      cdcPrintln("ERR|MENUBGBEGIN");
      return;
    }

    cdcPrintln("OK|MENUBGBEGIN");
    return;
  }

  if (upper.startsWith("MENUBGDATA|")) {
    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    uint32_t offset = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        !parseUnsignedLong(
            command.substring(p1 + 1, p2),
            menuUploadExpectedBytes,
            offset) ||
        !writeMenuAssetChunk(
            offset,
            command.substring(p2 + 1))) {
      cdcPrintln("ERR|MENUBGDATA");
      return;
    }

    char out[40];
    snprintf(
        out,
        sizeof(out),
        "OK|MENUBGDATA|%lu",
        static_cast<unsigned long>(menuUploadReceivedBytes));
    cdcPrintln(out);
    return;
  }

  if (upper == "MENUBGEND") {
    if (!finishMenuBackgroundUpload()) {
      cdcPrintln("ERR|MENUBGEND");
      return;
    }

    cdcPrintln("OK|MENUBGEND");
    return;
  }

  if (upper.startsWith("MENUBGCLEAR|")) {
    int sep = command.indexOf('|');
    uint16_t profile = 0;

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(sep + 1),
            PROFILE_COUNT - 1,
            profile)) {
      cdcPrintln("ERR|MENUBGCLEAR");
      return;
    }

    closeMenuUpload();
    clearMainMenuBackground(
        static_cast<uint8_t>(profile));

    if (profile == activeProfile &&
        !saverActive) {
      renderMainMenu();
    }

    cdcPrintln("OK|MENUBGCLEAR");
    return;
  }

  if (upper.startsWith("MENUICONBEGIN|")) {
    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    int p3 = command.indexOf('|', p2 + 1);
    uint16_t profile = 0;
    uint16_t slot = 0;
    uint32_t bytes = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        p3 < 0 ||
        !parseUnsigned(
            command.substring(p1 + 1, p2),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsigned(
            command.substring(p2 + 1, p3),
            MENU_SLOT_COUNT - 1,
            slot) ||
        !parseUnsignedLong(
            command.substring(p3 + 1),
            MENU_ICON_BYTES,
            bytes) ||
        !beginMenuIconUpload(
            static_cast<uint8_t>(profile),
            static_cast<uint8_t>(slot),
            bytes)) {
      cdcPrintln("ERR|MENUICONBEGIN");
      return;
    }

    cdcPrintln("OK|MENUICONBEGIN");
    return;
  }

  if (upper.startsWith("MENUICONDATA|")) {
    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    uint32_t offset = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        !parseUnsignedLong(
            command.substring(p1 + 1, p2),
            menuUploadExpectedBytes,
            offset) ||
        !writeMenuAssetChunk(
            offset,
            command.substring(p2 + 1))) {
      cdcPrintln("ERR|MENUICONDATA");
      return;
    }

    char out[40];
    snprintf(
        out,
        sizeof(out),
        "OK|MENUICONDATA|%lu",
        static_cast<unsigned long>(menuUploadReceivedBytes));
    cdcPrintln(out);
    return;
  }

  if (upper == "MENUICONEND") {
    if (!finishMenuIconUpload()) {
      cdcPrintln("ERR|MENUICONEND");
      return;
    }

    cdcPrintln("OK|MENUICONEND");
    return;
  }

  if (upper.startsWith("MENUICONCLEAR|")) {
    int p1 = command.indexOf('|');
    int p2 = command.indexOf('|', p1 + 1);
    uint16_t profile = 0;
    uint16_t slot = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        !parseUnsigned(
            command.substring(p1 + 1, p2),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsigned(
            command.substring(p2 + 1),
            MENU_SLOT_COUNT - 1,
            slot)) {
      cdcPrintln("ERR|MENUICONCLEAR");
      return;
    }

    clearMainMenuIcon(
        static_cast<uint8_t>(profile),
        static_cast<uint8_t>(slot));

    if (profile == activeProfile &&
        !saverActive) {
      renderMainMenu();
    }

    cdcPrintln("OK|MENUICONCLEAR");
    return;
  }

  if (upper == "MENUSHOW") {
    stopSaver();
    renderMainMenu();
    cdcPrintln("OK|MENUSHOW");
    return;
  }

  if (upper == "SAVERINFO") {
    size_t total =
        littleFsReady
            ? LittleFS.totalBytes()
            : 0;

    size_t used =
        littleFsReady
            ? LittleFS.usedBytes()
            : 0;

    size_t freeBytes =
        total > used
            ? total - used
            : 0;

    char out[128];
    snprintf(
        out,
        sizeof(out),
        "SAVERINFO|TOTAL=%lu|USED=%lu|FREE=%lu|FLASH=%lu",
        static_cast<unsigned long>(total),
        static_cast<unsigned long>(used),
        static_cast<unsigned long>(freeBytes),
        static_cast<unsigned long>(ESP.getFlashChipSize()));

    cdcPrintln(out);
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

  if (upper.startsWith("SAVPXBEGIN|")) {
    int sep =
        command.indexOf('|');

    uint32_t byteCount = 0;

    if (sep < 0 ||
        !parseUnsignedLong(
            command.substring(
                sep + 1),
            PACKED_UPLOAD_LIMIT_BYTES,
            byteCount) ||
        byteCount < 26) {
      cdcPrintln(
          "ERR|BAD_SAVPXBEGIN");
      return;
    }

    if (!beginPackedUpload(
            byteCount)) {
      cdcPrintln(
          "ERR|SAVPXBEGIN_ALLOC");
      return;
    }

    cdcPrintln(
        "OK|SAVPXBEGIN");
    return;
  }

  if (upper.startsWith("SAVPXDATA|")) {
    int first =
        command.indexOf('|');

    int second =
        command.indexOf(
            '|',
            first + 1);

    if (first < 0 ||
        second < 0) {
      cdcPrintln(
          "ERR|BAD_SAVPXDATA");
      return;
    }

    uint32_t offset = 0;

    if (!parseUnsignedLong(
            command.substring(
                first + 1,
                second),
            packedUploadExpectedBytes,
            offset) ||
        !writePackedUploadChunk(
            offset,
            command.substring(
                second + 1))) {
      cdcPrintln(
          "ERR|SAVPXDATA");
      return;
    }

    char out[40];

    snprintf(
        out,
        sizeof(out),
        "OK|SAVPXDATA|%lu",
        static_cast<unsigned long>(
            saverBytesReceived));

    cdcPrintln(out);
    return;
  }

  if (upper == "SAVPXEND") {
    if (!finishPackedUpload()) {
      cdcPrintln(
          "ERR|SAVPXEND");
      return;
    }

    lastUserActivityAt =
        millis();

    cdcPrintln(
        "OK|SAVER|READY");
    return;
  }

  if (upper.startsWith("SAVJPGBEGIN|")) {
    int first =
        command.indexOf('|');
    int second =
        command.indexOf(
            '|',
            first + 1);
    int third =
        command.indexOf(
            '|',
            second + 1);

    if (first < 0 ||
        second < 0 ||
        third < 0) {
      cdcPrintln(
          "ERR|BAD_SAVJPGBEGIN");
      return;
    }

    uint32_t byteCount = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    if (!parseUnsignedLong(
            command.substring(
                first + 1,
                second),
            JPEG_UPLOAD_LIMIT_BYTES,
            byteCount) ||
        !parseUnsigned(
            command.substring(
                second + 1,
                third),
            TFT_WIDTH,
            width) ||
        !parseUnsigned(
            command.substring(
                third + 1),
            TFT_HEIGHT,
            height) ||
        width == 0 ||
        height == 0) {
      cdcPrintln(
          "ERR|BAD_SAVJPGBEGIN");
      return;
    }

    if (!beginJpegUpload(
            byteCount,
            width,
            height)) {
      cdcPrintln(
          "ERR|SAVJPGBEGIN_ALLOC");
      return;
    }

    cdcPrintln(
        "OK|SAVJPGBEGIN");
    return;
  }

  if (upper.startsWith("SAVJPGDATA|")) {
    int first =
        command.indexOf('|');
    int second =
        command.indexOf(
            '|',
            first + 1);

    if (first < 0 ||
        second < 0) {
      cdcPrintln(
          "ERR|BAD_SAVJPGDATA");
      return;
    }

    uint32_t offset = 0;

    if (!parseUnsignedLong(
            command.substring(
                first + 1,
                second),
            jpegUploadExpectedBytes,
            offset) ||
        !writeJpegUploadChunk(
            offset,
            command.substring(
                second + 1))) {
      cdcPrintln(
          "ERR|SAVJPGDATA");
      return;
    }

    char out[40];
    snprintf(
        out,
        sizeof(out),
        "OK|SAVJPGDATA|%lu",
        static_cast<unsigned long>(
            saverBytesReceived));

    cdcPrintln(out);
    return;
  }

  if (upper == "SAVJPGEND") {
    if (!finishJpegUpload()) {
      cdcPrintln(
          "ERR|SAVJPGEND");
      return;
    }

    lastUserActivityAt =
        millis();

    cdcPrintln(
        "OK|SAVER|READY");
    return;
  }

  if (upper.startsWith("SAVGIFBEGIN|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);
    int third = command.indexOf('|', second + 1);
    int fourth = command.indexOf('|', third + 1);

    if (first < 0 ||
        second < 0 ||
        third < 0) {
      cdcPrintln("ERR|BAD_SAVGIFBEGIN");
      return;
    }

    uint32_t byteCount = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    String heightPart =
        fourth >= 0
            ? command.substring(third + 1, fourth)
            : command.substring(third + 1);

    String scalePart =
        fourth >= 0
            ? command.substring(fourth + 1)
            : String("FILL");

    GifScaleMode scaleMode =
        parseGifScaleMode(scalePart);

    if (!parseUnsignedLong(
            command.substring(first + 1, second),
            GIF_UPLOAD_LIMIT_BYTES,
            byteCount) ||
        !parseUnsigned(
            command.substring(second + 1, third),
            1024,
            width) ||
        !parseUnsigned(
            heightPart,
            1024,
            height) ||
        !gifDimensionsSupported(
            width,
            height)) {
      cdcPrintln("ERR|BAD_SAVGIFBEGIN");
      return;
    }

    size_t total =
        littleFsReady
            ? LittleFS.totalBytes()
            : 0;

    size_t used =
        littleFsReady
            ? LittleFS.usedBytes()
            : 0;

    size_t freeBytes =
        total > used
            ? total - used
            : 0;

    if (!littleFsReady) {
      cdcPrintln("ERR|FS_NOT_READY");
      return;
    }

    if (static_cast<size_t>(byteCount) + 4096 > freeBytes) {
      char out[96];
      snprintf(
          out,
          sizeof(out),
          "ERR|NO_SPACE|NEED=%lu|FREE=%lu|TOTAL=%lu",
          static_cast<unsigned long>(byteCount),
          static_cast<unsigned long>(freeBytes),
          static_cast<unsigned long>(total));
      cdcPrintln(out);
      return;
    }

    if (!beginGifUpload(
            byteCount,
            width,
            height,
            scaleMode)) {
      cdcPrintln("ERR|SAVGIFBEGIN_ALLOC");
      return;
    }

    cdcPrintln("OK|SAVGIFBEGIN");
    return;
  }

  if (upper.startsWith("SAVGIFDATA|")) {
    int first = command.indexOf('|');
    int second = command.indexOf('|', first + 1);

    if (first < 0 ||
        second < 0) {
      cdcPrintln("ERR|BAD_SAVGIFDATA");
      return;
    }

    uint32_t offset = 0;

    if (!parseUnsignedLong(
            command.substring(first + 1, second),
            gifUploadExpectedBytes,
            offset) ||
        !writeGifUploadChunk(
            offset,
            command.substring(second + 1))) {
      cdcPrintln("ERR|SAVGIFDATA");
      return;
    }

    char out[40];
    snprintf(
        out,
        sizeof(out),
        "OK|SAVGIFDATA|%lu",
        static_cast<unsigned long>(
            saverBytesReceived));

    cdcPrintln(out);
    return;
  }

  if (upper == "SAVGIFEND") {
    if (!finishGifUpload()) {
      cdcPrintln("ERR|SAVGIFEND");
      return;
    }

    lastUserActivityAt = millis();
    cdcPrintln("OK|SAVER|READY");
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

  if (upper.startsWith("GET_RGB_PROFILE|")) {
    int sep =
        command.indexOf('|');

    uint16_t profile = 0;

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(
                sep + 1),
            PROFILE_COUNT - 1,
            profile)) {
      cdcPrintln(
          "ERR|BAD_RGB_PROFILE");
      return;
    }

    cdcPrintln(
        serializeRgbProfile(
            static_cast<uint8_t>(
                profile)));
    return;
  }

  if (upper.startsWith("RGB_KEY|")) {
    int p1 =
        command.indexOf('|');
    int p2 =
        command.indexOf('|', p1 + 1);
    int p3 =
        command.indexOf('|', p2 + 1);
    int p4 =
        command.indexOf('|', p3 + 1);
    int p5 =
        command.indexOf('|', p4 + 1);

    uint16_t profile = 0;
    uint16_t key = 0;
    uint16_t r = 0;
    uint16_t g = 0;
    uint16_t b = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        p3 < 0 ||
        p4 < 0 ||
        p5 < 0 ||
        !parseUnsigned(
            command.substring(
                p1 + 1,
                p2),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsigned(
            command.substring(
                p2 + 1,
                p3),
            KEY_COUNT - 1,
            key) ||
        !parseUnsigned(
            command.substring(
                p3 + 1,
                p4),
            255,
            r) ||
        !parseUnsigned(
            command.substring(
                p4 + 1,
                p5),
            255,
            g) ||
        !parseUnsigned(
            command.substring(
                p5 + 1),
            255,
            b)) {
      cdcPrintln(
          "ERR|BAD_RGB_KEY");
      return;
    }

    rgbProfiles[profile][key][0] =
        static_cast<uint8_t>(r);
    rgbProfiles[profile][key][1] =
        static_cast<uint8_t>(g);
    rgbProfiles[profile][key][2] =
        static_cast<uint8_t>(b);

    saveRgbProfiles();

    if (profile ==
        activeProfile) {
      applyRgbProfile();
    }

    cdcPrintln(
        "OK|RGB_KEY");
    return;
  }

  if (upper.startsWith("RGB_ALL|")) {
    int p1 =
        command.indexOf('|');
    int p2 =
        command.indexOf('|', p1 + 1);
    int p3 =
        command.indexOf('|', p2 + 1);
    int p4 =
        command.indexOf('|', p3 + 1);

    uint16_t profile = 0;
    uint16_t r = 0;
    uint16_t g = 0;
    uint16_t b = 0;

    if (p1 < 0 ||
        p2 < 0 ||
        p3 < 0 ||
        p4 < 0 ||
        !parseUnsigned(
            command.substring(
                p1 + 1,
                p2),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsigned(
            command.substring(
                p2 + 1,
                p3),
            255,
            r) ||
        !parseUnsigned(
            command.substring(
                p3 + 1,
                p4),
            255,
            g) ||
        !parseUnsigned(
            command.substring(
                p4 + 1),
            255,
            b)) {
      cdcPrintln(
          "ERR|BAD_RGB_ALL");
      return;
    }

    for (uint8_t key = 0;
         key < KEY_COUNT;
         ++key) {
      rgbProfiles[profile][key][0] =
          static_cast<uint8_t>(r);
      rgbProfiles[profile][key][1] =
          static_cast<uint8_t>(g);
      rgbProfiles[profile][key][2] =
          static_cast<uint8_t>(b);
    }

    saveRgbProfiles();

    if (profile ==
        activeProfile) {
      applyRgbProfile();
    }

    cdcPrintln(
        "OK|RGB_ALL");
    return;
  }

  if (upper.startsWith("RGB_PROFILE_SET|")) {
    int first =
        command.indexOf('|');
    int second =
        command.indexOf(
            '|',
            first + 1);

    uint16_t profile = 0;

    if (first < 0 ||
        second < 0 ||
        !parseUnsigned(
            command.substring(
                first + 1,
                second),
            PROFILE_COUNT - 1,
            profile)) {
      cdcPrintln(
          "ERR|BAD_RGB_PROFILE");
      return;
    }

    uint8_t colors[KEY_COUNT][3] = {};

    if (!parseRgbProfileCsv(
            command.substring(
                second + 1),
            colors)) {
      cdcPrintln(
          "ERR|BAD_RGB_PROFILE");
      return;
    }

    memcpy(
        rgbProfiles[profile],
        colors,
        sizeof(colors));

    saveRgbProfiles();

    if (profile ==
        activeProfile) {
      applyRgbProfile();
    }

    cdcPrintln(
        "OK|RGB_PROFILE");
    return;
  }

  if (upper.startsWith("RGB_EFFECT|")) {
    int first =
        command.indexOf('|');
    int second =
        command.indexOf(
            '|',
            first + 1);

    uint16_t profile = 0;
    uint16_t effect = 0;

    if (first < 0 ||
        second < 0 ||
        !parseUnsigned(
            command.substring(
                first + 1,
                second),
            PROFILE_COUNT - 1,
            profile) ||
        !parseUnsigned(
            command.substring(
                second + 1),
            9,
            effect)) {
      cdcPrintln(
          "ERR|BAD_RGB_EFFECT");
      return;
    }

    rgbEffects[profile] =
        static_cast<uint8_t>(
            effect);

    saveRgbProfiles();

    if (profile ==
        activeProfile) {
      applyRgbProfile();
    }

    cdcPrintln(
        "OK|RGB_EFFECT");
    return;
  }

  if (upper.startsWith("RGB_SPEED|")) {
    int sep =
        command.indexOf('|');

    uint16_t speed = 0;

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(
                sep + 1),
            100,
            speed) ||
        speed < 10) {
      cdcPrintln(
          "ERR|BAD_RGB_SPEED");
      return;
    }

    rgbSpeedPercent =
        static_cast<uint8_t>(
            speed);

    saveRgbProfiles();
    rgbLastFrameAt = 0;

    cdcPrintln(
        "OK|RGB_SPEED");
    return;
  }

  if (upper.startsWith("RGB_ENABLE|")) {
    int sep =
        command.indexOf('|');

    uint16_t enabled = 0;

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(
                sep + 1),
            1,
            enabled)) {
      cdcPrintln(
          "ERR|BAD_RGB_ENABLE");
      return;
    }

    rgbEnabled =
        enabled != 0;

    saveRgbProfiles();
    applyRgbProfile();

    cdcPrintln(
        "OK|RGB_ENABLE");
    return;
  }

  if (upper.startsWith("RGB_BRIGHTNESS|")) {
    int sep =
        command.indexOf('|');

    uint16_t brightness = 0;

    if (sep < 0 ||
        !parseUnsigned(
            command.substring(
                sep + 1),
            100,
            brightness)) {
      cdcPrintln(
          "ERR|BAD_RGB_BRIGHTNESS");
      return;
    }

    rgbBrightnessPercent =
        static_cast<uint8_t>(
            brightness);

    saveRgbProfiles();
    applyRgbProfile();

    cdcPrintln(
        "OK|RGB_BRIGHTNESS");
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

    bool profileChanged =
        activeProfile != static_cast<uint8_t>(profile);

    activeProfile = static_cast<uint8_t>(profile);
    baseLayer = static_cast<uint8_t>(layer);
    momentaryLayer = -1;
    toggledLayerMask = 0;
    sendMappedReports();
    applyRgbProfile();

    if (profileChanged && !saverActive) {
      renderMainMenu();
    }

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

  if (upper == "ARM_BOOTLOADER") {
    // Normal CDC sessions have the Arduino-ESP32 DTR/RTS reboot hook disabled.
    // Only this explicit firmware-update command arms it, and only briefly.
    bootloaderArmed = true;
    bootloaderArmUntil =
        millis() + 5000UL;
    USBSerial.enableReboot(true);
    cdcPrintln("OK|BOOTLOADER_ARMED");
    USBSerial.flush();
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

    if (cdcLine.length() < 2048) {
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
  loadRgbProfiles();
  loadMainMenuConfig();

  rgbStrip.begin();
  rgbStrip.clear();
  applyRgbProfile();

  initKeys();
  initDisplay();

  littleFsReady = LittleFS.begin(true);
  if (littleFsReady) {
    loadPersistedMedia();
  }

  renderMainMenu();
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
  USB.firmwareVersion(0x0170);

  // Normal Lumi Macropad CDC traffic must never be interpreted as a request
  // to enter the ESP32-S2 bootloader. Firmware updates use the dedicated ROM
  // BOOT/esptool path instead.
  USBSerial.enableReboot(false);
  USBSerial.begin();
  Keyboard.begin();
  ConsumerControl.begin();

  USB.begin();

  delay(500);
  sendMappedReports();
  cdcPrintln("BOOT|PIXELPRO|1.7.0");
}

void loop() {
  pollKeys();
  pollCdc();
  pollRgbEffect();
  pollSaver();

  if (bootloaderArmed &&
      static_cast<int32_t>(
          millis() - bootloaderArmUntil) >= 0) {
    USBSerial.enableReboot(false);
    bootloaderArmed = false;
  }

  delay(1);
}
