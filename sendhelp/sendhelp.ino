#include <Arduino.h>
#include <FastLED.h>

// ======================================================
// ESP32 LED CUBE RECEIVER - DUAL UART + PI OVERRIDE
// NOW WITH OPTIONAL SECOND AND THIRD EFFECT LAYERS
//
// Pi UART   : GPIO 16/17
// DSP UART  : GPIO 25/26 
//
// Supported packet fields:
//   E=0..5              primary effect
//   B=0..255            primary effect brightness
//   N=1..5,H=h1,h2,...  shared palette for effects
//
// Optional second effect layer:
//   E2=0..5             enable secondary effect
//   B2=0..255           secondary effect brightness
//   X2=1                disable secondary effect
//
// Optional third effect layer:
//   E3=0..5             enable third effect
//   B3=0..255           third effect brightness
//   X3=1                disable third effect
//
// Override control from Pi only:
//   OVR=1               Pi takes control
//   OVR=0               DSP takes control again
// ======================================================

#define LED_TYPE WS2812B
#define COLOR_ORDER BRG
#define DATA_PIN 23
#define NUM_LEDS 108
#define MAX_COLORS 5

#define PI_RX_PIN 16
#define PI_TX_PIN 17
#define DSP_RX_PIN 25
#define DSP_TX_PIN 26
#define UART_BAUD 115200

#define PI_OVERRIDE_TIMEOUT_MS 3000

CRGB leds[NUM_LEDS];
CRGB layer1[NUM_LEDS];
CRGB layer2[NUM_LEDS];
CRGB layer3[NUM_LEDS];

enum EffectMode {
  EFFECT_STROBE = 0,
  EFFECT_FADE   = 1,
  EFFECT_FLASH  = 2,
  EFFECT_SMOOTH = 3,
  EFFECT_RAINBOW= 4,
  EFFECT_CHASE  = 5
};

enum PacketSource {
  SRC_PI,
  SRC_DSP
};

struct EffectState {
  uint32_t lastTick = 0;
  bool on = false;
  uint8_t colorIndex = 0;
  int value = 0;
  int delta = 5;
  bool brightState = false;
  uint8_t lastV = 0;
  uint16_t offset = 0;
  int pos = 0;
};

struct LedParams {
  uint8_t hues[MAX_COLORS] = {0, 40, 96, 160, 200};
  uint8_t numColors = 5;

  EffectMode effect = EFFECT_RAINBOW;
  uint8_t brightness = 120;

  bool secondEnabled = false;
  EffectMode effect2 = EFFECT_FLASH;
  uint8_t brightness2 = 90;

  bool thirdEnabled = false;
  EffectMode effect3 = EFFECT_SMOOTH;
  uint8_t brightness3 = 70;
};

LedParams currentParams;
EffectState primaryState;
EffectState secondaryState;
EffectState tertiaryState;

bool piOverrideActive = false;
uint32_t lastPiPacketMs = 0;

String piRxLine = "";
String dspRxLine = "";

static inline uint8_t getPaletteHue(uint8_t idx) {
  return currentParams.hues[idx % currentParams.numColors];
}

static inline uint8_t scaleValue(uint8_t rawValue, uint8_t layerBrightness) {
  return scale8(rawValue, layerBrightness);
}

void clearBuffer(CRGB* buf) {
  fill_solid(buf, NUM_LEDS, CRGB::Black);
}

void processSerialStream(HardwareSerial& port, String& line, PacketSource src);
bool handleControlPacket(const String& packet, PacketSource src);
bool parseVisualPacket(String packet);
void readSerialPackets();
void updateOverrideTimeout();
void renderEffect(EffectMode mode, CRGB* target, EffectState& st, uint8_t layerBrightness);
void renderAndShow();

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, PI_RX_PIN, PI_TX_PIN);
  Serial1.begin(UART_BAUD, SERIAL_8N1, DSP_RX_PIN, DSP_TX_PIN);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  FastLED.clear();
  FastLED.show();

  Serial.println("Receiver ready.");
  Serial.println("Pi  on Serial2 GPIO16/17");
  Serial.println("DSP on Serial1 GPIO25/26");
}

void loop() {
  readSerialPackets();
  updateOverrideTimeout();
  renderAndShow();
}

void readSerialPackets() {
  processSerialStream(Serial2, piRxLine, SRC_PI);
  processSerialStream(Serial1, dspRxLine, SRC_DSP);
}

void updateOverrideTimeout() {
  if (piOverrideActive && (millis() - lastPiPacketMs >= PI_OVERRIDE_TIMEOUT_MS)) {
    piOverrideActive = false;
    Serial.println("Pi override timed out -> DSP restored.");
  }
}

void processSerialStream(HardwareSerial& port, String& line, PacketSource src) {
  while (port.available()) {
    char c = (char)port.read();

    if (c == '\n') {
      line.trim();
      if (line.length() > 0) {
        if (!handleControlPacket(line, src)) {
          if (!(src == SRC_DSP && piOverrideActive)) {
            if (parseVisualPacket(line)) {
              if (src == SRC_PI) {
                piOverrideActive = true;
                lastPiPacketMs = millis();
              }
            }
          }
        }
      }
      line = "";
    } else if (c != '\r') {
      line += c;
      if (line.length() > 180) {
        line = "";
      }
    }
  }
}

bool handleControlPacket(const String& packet, PacketSource src) {
  if (src != SRC_PI) {
    return false;
  }

  String p = packet;
  p.trim();
  p.replace(" ", "");

  if (p.equalsIgnoreCase("OVR=1")) {
    piOverrideActive = true;
    lastPiPacketMs = millis();
    Serial.println("Pi override ENABLED");
    return true;
  }

  if (p.equalsIgnoreCase("OVR=0")) {
    piOverrideActive = false;
    Serial.println("Pi override DISABLED");
    return true;
  }

  return false;
}

bool parseVisualPacket(String packet) {
  packet.trim();
  packet.replace(" ", "");
  if (packet.length() == 0) return false;

  int i = 0;
  while (i < packet.length()) {
    int nextComma = packet.indexOf(',', i);
    if (nextComma == -1) nextComma = packet.length();

    String token = packet.substring(i, nextComma);

    if (token.startsWith("E3=")) {
      int effect3 = token.substring(3).toInt();
      if (effect3 < 0 || effect3 > 5) return false;
      currentParams.effect3 = (EffectMode)effect3;
      currentParams.thirdEnabled = true;
    }
    else if (token.startsWith("B3=")) {
      int b3 = token.substring(3).toInt();
      if (b3 < 0 || b3 > 255) return false;
      currentParams.brightness3 = (uint8_t)b3;
      currentParams.thirdEnabled = (b3 > 0) ? true : currentParams.thirdEnabled;
    }
    else if (token.startsWith("X3=")) {
      int x3 = token.substring(3).toInt();
      if (x3 == 1) currentParams.thirdEnabled = false;
    }
    else if (token.startsWith("E2=")) {
      int effect2 = token.substring(3).toInt();
      if (effect2 < 0 || effect2 > 5) return false;
      currentParams.effect2 = (EffectMode)effect2;
      currentParams.secondEnabled = true;
    }
    else if (token.startsWith("B2=")) {
      int b2 = token.substring(3).toInt();
      if (b2 < 0 || b2 > 255) return false;
      currentParams.brightness2 = (uint8_t)b2;
      currentParams.secondEnabled = (b2 > 0) ? true : currentParams.secondEnabled;
    }
    else if (token.startsWith("X2=")) {
      int x2 = token.substring(3).toInt();
      if (x2 == 1) currentParams.secondEnabled = false;
    }
    else if (token.startsWith("E=")) {
      int effectInt = token.substring(2).toInt();
      if (effectInt < 0 || effectInt > 5) return false;
      currentParams.effect = (EffectMode)effectInt;
    }
    else if (token.startsWith("B=")) {
      int brightness = token.substring(2).toInt();
      if (brightness < 0 || brightness > 255) return false;
      currentParams.brightness = (uint8_t)brightness;
    }
    else if (token.startsWith("N=")) {
      int numColors = token.substring(2).toInt();
      if (numColors < 1 || numColors > MAX_COLORS) return false;
      currentParams.numColors = (uint8_t)numColors;
    }
    else if (token.startsWith("H=")) {
      int pendingHues[MAX_COLORS] = {0};
      int pendingCount = 0;

      String firstHueStr = token.substring(2);
      if (firstHueStr.length() > 0) {
        int hue = firstHueStr.toInt();
        if (hue < 0 || hue > 255) return false;
        pendingHues[pendingCount++] = hue;
      }

      int j = nextComma + 1;
      while (j < packet.length() && pendingCount < MAX_COLORS) {
        int comma2 = packet.indexOf(',', j);
        if (comma2 == -1) comma2 = packet.length();
        String nextToken = packet.substring(j, comma2);

        if (nextToken.startsWith("E=") || nextToken.startsWith("B=") ||
            nextToken.startsWith("N=") || nextToken.startsWith("H=") ||
            nextToken.startsWith("E2=") || nextToken.startsWith("B2=") ||
            nextToken.startsWith("X2=") || nextToken.startsWith("E3=") ||
            nextToken.startsWith("B3=") || nextToken.startsWith("X3=")) {
          break;
        }

        int hue = nextToken.toInt();
        if (hue < 0 || hue > 255) return false;
        pendingHues[pendingCount++] = hue;
        j = comma2 + 1;
      }

      if (pendingCount < 1) return false;
      for (int k = 0; k < pendingCount; k++) currentParams.hues[k] = (uint8_t)pendingHues[k];
      currentParams.numColors = pendingCount;
      i = j;
      continue;
    }
    else {
      return false;
    }

    i = nextComma + 1;
  }

  Serial.print("Primary E=");
  Serial.print((int)currentParams.effect);
  Serial.print(" B=");
  Serial.print((int)currentParams.brightness);
  Serial.print(" | Second=");
  Serial.print(currentParams.secondEnabled ? "ON" : "OFF");
  if (currentParams.secondEnabled) {
    Serial.print(" E2=");
    Serial.print((int)currentParams.effect2);
    Serial.print(" B2=");
    Serial.print((int)currentParams.brightness2);
  }
  if (currentParams.thirdEnabled) {
    Serial.print(" | Third E3=");
    Serial.print((int)currentParams.effect3);
    Serial.print(" B3=");
    Serial.print((int)currentParams.brightness3);
  }
  Serial.println();

  return true;
}

void renderAndShow() {
  clearBuffer(layer1);
  clearBuffer(layer2);
  clearBuffer(layer3);

  renderEffect(currentParams.effect, layer1, primaryState, currentParams.brightness);

  if (currentParams.secondEnabled && currentParams.brightness2 > 0) {
    renderEffect(currentParams.effect2, layer2, secondaryState, currentParams.brightness2);
  }

  if (currentParams.thirdEnabled && currentParams.brightness3 > 0) {
    renderEffect(currentParams.effect3, layer3, tertiaryState, currentParams.brightness3);
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = layer1[i];
    if (currentParams.secondEnabled && currentParams.brightness2 > 0) {
      leds[i] += layer2[i];
    }
    if (currentParams.thirdEnabled && currentParams.brightness3 > 0) {
      leds[i] += layer3[i];
    }
  }

  FastLED.show();
}

void renderEffect(EffectMode mode, CRGB* target, EffectState& st, uint8_t layerBrightness) {
  uint32_t now = millis();

  switch (mode) {
    case EFFECT_STROBE: {
      if (now - st.lastTick >= 100) {
        st.lastTick = now;
        st.on = !st.on;
        if (st.on) st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
      }
      if (st.on) {
        fill_solid(target, NUM_LEDS, CHSV(getPaletteHue(st.colorIndex), 255, layerBrightness));
      }
      break;
    }

    case EFFECT_FADE: {
      if (now - st.lastTick >= 20) {
        st.lastTick = now;
        st.value += st.delta;
        if (st.value >= 255) {
          st.value = 255;
          st.delta = -st.delta;
        } else if (st.value <= 0) {
          st.value = 0;
          st.delta = -st.delta;
          st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
        }
      }
      fill_solid(target, NUM_LEDS, CHSV(getPaletteHue(st.colorIndex), 255, scaleValue((uint8_t)st.value, layerBrightness)));
      break;
    }

    case EFFECT_FLASH: {
      if (now - st.lastTick >= 250) {
        st.lastTick = now;
        st.brightState = !st.brightState;
        if (st.brightState) st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
      }
      uint8_t v = st.brightState ? layerBrightness : scaleValue(40, layerBrightness);
      fill_solid(target, NUM_LEDS, CHSV(getPaletteHue(st.colorIndex), 255, v));
      break;
    }

    case EFFECT_SMOOTH: {
      uint8_t vRaw = beatsin8(18, 20, 255);
      uint8_t v = scaleValue(vRaw, layerBrightness);
      fill_solid(target, NUM_LEDS, CHSV(getPaletteHue(st.colorIndex), 255, v));
      if (vRaw < st.lastV && st.lastV > 240) {
        st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
      }
      st.lastV = vRaw;
      break;
    }

    case EFFECT_RAINBOW: {
      if (now - st.lastTick >= 20) {
        st.lastTick = now;
        st.offset++;
      }
      for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t paletteIndex = ((i + st.offset) / (NUM_LEDS / currentParams.numColors + 1)) % currentParams.numColors;
        target[i] = CHSV(getPaletteHue(paletteIndex), 255, layerBrightness);
      }
      break;
    }

    case EFFECT_CHASE: {
      if (now - st.lastTick >= 25) {
        st.lastTick = now;
        st.pos = (st.pos + 1) % NUM_LEDS;
      }
      for (int t = 0; t < 10; t++) {
        int idx = (st.pos - t + NUM_LEDS) % NUM_LEDS;
        int tailBrightness = 255 - (t * 22);
        if (tailBrightness < 0) tailBrightness = 0;
        target[idx] = CHSV(getPaletteHue(t), 255, scaleValue((uint8_t)tailBrightness, layerBrightness));
      }
      break;
    }
  }
}
