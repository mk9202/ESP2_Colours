#include <Arduino.h>
#include <FastLED.h>

// ======================================================
// ESP32 LED CUBE RECEIVER - PARTIAL UPDATE VERSION
//
// Supported UART messages:
//
// E=0
// B=180
// N=3,H=20,80,140
// E=5,N=3,H=20,80,140,B=180
//
// Fields:
// E = effect
// N = number of active colors
// H = hue list
// B = brightness
//
// Effect map:
// 0 = STROBE
// 1 = FADE
// 2 = FLASH
// 3 = SMOOTH
// 4 = RAINBOW
// 5 = CHASE
// ======================================================

// ================= LED CONFIG =================
#define LED_TYPE           WS2812B
#define COLOR_ORDER        BRG
#define DATA_PIN           23
#define NUM_LEDS           108
#define DEFAULT_BRIGHTNESS 120
#define MAX_COLORS         5

CRGB leds[NUM_LEDS];

// ================= UART CONFIG =================
#define RXD2 16
#define TXD2 17
#define UART_BAUD 115200

// ================= EFFECT ENUM =================
enum EffectMode {
  EFFECT_STROBE = 0,
  EFFECT_FADE   = 1,
  EFFECT_FLASH  = 2,
  EFFECT_SMOOTH = 3,
  EFFECT_RAINBOW= 4,
  EFFECT_CHASE  = 5
};

// ================= PARAM STRUCT =================
struct LedParams {
  uint8_t hues[MAX_COLORS];
  uint8_t numColors;
  uint8_t brightness;
  EffectMode effect;
};

LedParams currentParams = {
  {0, 40, 96, 160, 200},
  5,
  DEFAULT_BRIGHTNESS,
  EFFECT_RAINBOW
};

// ================= FUNCTION PROTOTYPES =================
void readSerialPacket();
void parsePacket(String packet);
void runCurrentEffect();

void effectStrobe(uint16_t intervalMs);
void effectFade(uint16_t stepMs);
void effectFlash(uint16_t intervalMs);
void effectSmooth(uint8_t bpm);
void effectRainbow(uint16_t stepMs);
void effectChase(uint16_t stepMs);

uint8_t getPaletteHue(uint8_t index);

// ======================================================
// SETUP
// ======================================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, RXD2, TXD2);

  delay(500);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(currentParams.brightness);
  FastLED.clear();
  FastLED.show();

  Serial.println("ESP32 LED cube receiver ready.");
  Serial.println("Examples:");
  Serial.println("E=0");
  Serial.println("B=180");
  Serial.println("N=3,H=20,80,140");
  Serial.println("E=5,N=3,H=20,80,140,B=180");
}

// ======================================================
// MAIN LOOP
// ======================================================
void loop() {
  readSerialPacket();
  runCurrentEffect();
}

// ======================================================
// UART RECEIVE
// ======================================================
void readSerialPacket() {
  static String rxLine = "";

  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == '\n') {
      if (rxLine.length() > 0) {
        parsePacket(rxLine);
        rxLine = "";
      }
    }
    else if (c != '\r') {
      rxLine += c;

      if (rxLine.length() > 120) {
        rxLine = "";
        Serial.println("UART line too long, clearing buffer.");
      }
    }
  }
}

// ======================================================
// PACKET PARSE - PARTIAL UPDATE
// Supports:
// E=0
// B=180
// N=3,H=20,80,140
// E=5,N=3,H=20,80,140,B=180
// ======================================================
void parsePacket(String packet) {
  packet.trim();
  if (packet.length() == 0) return;

  // Remove spaces
  packet.replace(" ", "");

  bool sawHueField = false;
  int pendingHues[MAX_COLORS] = {0};
  int pendingHueCount = 0;

  int i = 0;
  while (i < packet.length()) {
    int nextComma = packet.indexOf(',', i);
    if (nextComma == -1) nextComma = packet.length();

    String token = packet.substring(i, nextComma);

    // ---------- E=effect ----------
    if (token.startsWith("E=")) {
      int effectInt = token.substring(2).toInt();
      if (effectInt < 0 || effectInt > 5) {
        Serial.print("Invalid effect: ");
        Serial.println(effectInt);
        return;
      }
      currentParams.effect = (EffectMode)effectInt;
    }

    // ---------- B=brightness ----------
    else if (token.startsWith("B=")) {
      int brightness = token.substring(2).toInt();
      if (brightness < 0 || brightness > 255) {
        Serial.print("Invalid brightness: ");
        Serial.println(brightness);
        return;
      }
      currentParams.brightness = (uint8_t)brightness;
      FastLED.setBrightness(currentParams.brightness);
    }

    // ---------- N=numColors ----------
    else if (token.startsWith("N=")) {
      int numColors = token.substring(2).toInt();
      if (numColors < 1 || numColors > MAX_COLORS) {
        Serial.print("Invalid numColors: ");
        Serial.println(numColors);
        return;
      }
      currentParams.numColors = (uint8_t)numColors;
    }

    // ---------- H=h1,h2,h3... ----------
    else if (token.startsWith("H=")) {
      sawHueField = true;
      pendingHueCount = 0;

      // First hue is in this token after H=
      String firstHueStr = token.substring(2);
      if (firstHueStr.length() > 0) {
        int hue = firstHueStr.toInt();
        if (hue < 0 || hue > 255) {
          Serial.print("Invalid hue: ");
          Serial.println(hue);
          return;
        }
        pendingHues[pendingHueCount++] = hue;
      }

      // Additional hues are in following comma-separated tokens
      int j = nextComma + 1;
      while (j < packet.length() && pendingHueCount < MAX_COLORS) {
        int comma2 = packet.indexOf(',', j);
        if (comma2 == -1) comma2 = packet.length();

        String nextToken = packet.substring(j, comma2);

        // Stop if next field begins
        if (nextToken.startsWith("E=") || nextToken.startsWith("N=") || nextToken.startsWith("B=") || nextToken.startsWith("H=")) {
          break;
        }

        int hue = nextToken.toInt();
        if (hue < 0 || hue > 255) {
          Serial.print("Invalid hue: ");
          Serial.println(hue);
          return;
        }

        pendingHues[pendingHueCount++] = hue;
        j = comma2 + 1;
      }

      if (pendingHueCount < 1) {
        Serial.println("H field provided but no hues found.");
        return;
      }

      // Apply hues immediately
      for (int k = 0; k < pendingHueCount; k++) {
        currentParams.hues[k] = (uint8_t)pendingHues[k];
      }

      // If no N= was sent, infer numColors from H=
      currentParams.numColors = pendingHueCount;

      // Move parser index to where hue parsing stopped
      i = j;
      continue;
    }

    else {
      Serial.print("Unknown token: ");
      Serial.println(token);
      return;
    }

    i = nextComma + 1;
  }

  // Safety check
  if (currentParams.numColors < 1 || currentParams.numColors > MAX_COLORS) {
    Serial.println("Internal numColors invalid after parse.");
    return;
  }

  Serial.print("Updated -> Effect: ");
  Serial.print((int)currentParams.effect);
  Serial.print(" NumColors: ");
  Serial.print(currentParams.numColors);
  Serial.print(" Hues: ");
  for (int k = 0; k < currentParams.numColors; k++) {
    Serial.print(currentParams.hues[k]);
    if (k < currentParams.numColors - 1) Serial.print(",");
  }
  Serial.print(" Brightness: ");
  Serial.println(currentParams.brightness);
}

// ======================================================
// HELPER
// ======================================================
uint8_t getPaletteHue(uint8_t index) {
  return currentParams.hues[index % currentParams.numColors];
}

// ======================================================
// EFFECT DISPATCHER
// ======================================================
void runCurrentEffect() {
  switch (currentParams.effect) {
    case EFFECT_STROBE:
      effectStrobe(100);
      break;

    case EFFECT_FADE:
      effectFade(20);
      break;

    case EFFECT_FLASH:
      effectFlash(250);
      break;

    case EFFECT_SMOOTH:
      effectSmooth(18);
      break;

    case EFFECT_RAINBOW:
      effectRainbow(20);
      break;

    case EFFECT_CHASE:
      effectChase(25);
      break;

    default:
      FastLED.clear();
      FastLED.show();
      break;
  }
}

// ======================================================
// EFFECTS
// ======================================================
void effectStrobe(uint16_t intervalMs) {
  static uint32_t lastToggle = 0;
  static bool on = false;
  static uint8_t colorIndex = 0;

  if (millis() - lastToggle >= intervalMs) {
    lastToggle = millis();
    on = !on;

    if (on) {
      uint8_t hue = getPaletteHue(colorIndex);
      fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
      colorIndex = (colorIndex + 1) % currentParams.numColors;
    } else {
      FastLED.clear();
    }

    FastLED.show();
  }
}

void effectFade(uint16_t stepMs) {
  static uint32_t lastStep = 0;
  static int value = 0;
  static int delta = 5;
  static uint8_t colorIndex = 0;

  if (millis() - lastStep >= stepMs) {
    lastStep = millis();
    value += delta;

    if (value >= 255) {
      value = 255;
      delta = -delta;
    } else if (value <= 0) {
      value = 0;
      delta = -delta;
      colorIndex = (colorIndex + 1) % currentParams.numColors;
    }

    fill_solid(leds, NUM_LEDS, CHSV(getPaletteHue(colorIndex), 255, (uint8_t)value));
    FastLED.show();
  }
}

void effectFlash(uint16_t intervalMs) {
  static uint32_t lastToggle = 0;
  static bool brightState = false;
  static uint8_t colorIndex = 0;

  if (millis() - lastToggle >= intervalMs) {
    lastToggle = millis();
    brightState = !brightState;

    uint8_t v = brightState ? 255 : 40;
    fill_solid(leds, NUM_LEDS, CHSV(getPaletteHue(colorIndex), 255, v));

    if (brightState) {
      colorIndex = (colorIndex + 1) % currentParams.numColors;
    }

    FastLED.show();
  }
}

void effectSmooth(uint8_t bpm) {
  static uint8_t colorIndex = 0;
  static uint8_t lastV = 0;

  uint8_t v = beatsin8(bpm, 20, 255);
  fill_solid(leds, NUM_LEDS, CHSV(getPaletteHue(colorIndex), 255, v));
  FastLED.show();

  if (v < lastV && lastV > 240) {
    colorIndex = (colorIndex + 1) % currentParams.numColors;
  }
  lastV = v;

  FastLED.delay(1000 / 120);
}

void effectRainbow(uint16_t stepMs) {
  static uint32_t lastStep = 0;
  static uint16_t offset = 0;

  if (millis() - lastStep >= stepMs) {
    lastStep = millis();
    offset++;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t paletteIndex = ((i + offset) / (NUM_LEDS / currentParams.numColors + 1)) % currentParams.numColors;
    leds[i] = CHSV(getPaletteHue(paletteIndex), 255, 255);
  }

  FastLED.show();
}

void effectChase(uint16_t stepMs) {
  static uint32_t lastStep = 0;
  static int pos = 0;

  if (millis() - lastStep >= stepMs) {
    lastStep = millis();
    pos = (pos + 1) % NUM_LEDS;

    FastLED.clear();

    for (int t = 0; t < 10; t++) {
      int idx = (pos - t + NUM_LEDS) % NUM_LEDS;
      int tailBrightness = 255 - (t * 22);
      if (tailBrightness < 0) tailBrightness = 0;

      uint8_t hue = getPaletteHue(t);
      leds[idx] = CHSV(hue, 255, (uint8_t)tailBrightness);
    }

    FastLED.show();
  }
}