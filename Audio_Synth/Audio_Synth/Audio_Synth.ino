#include <WS2812FX.h>

#define LED_PIN          4
#define LED_COUNT      144

#define AUDIO_PIN       34      // ADC1 pin (safe even if you later use Wi-Fi)
#define MIN_BRIGHTNESS   1
#define MAX_BRIGHTNESS 255
#define THRESHOLD        5

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint16_t quietLevel = 0;
uint16_t maxSample = 0;
uint16_t maxSampleEver = 0;
unsigned long timer = 0;

void setup() {
  Serial.begin(115200);

  // ESP32 ADC setup for 0..3.3V
  analogReadResolution(12);                          // 0..4095
  analogSetPinAttenuation(AUDIO_PIN, ADC_11db);      // best for ~0..3.3V range

  // establish baseline (quiet level)
  for (int i = 0; i < 20; i++) {
    quietLevel += analogRead(AUDIO_PIN);
    delay(25);
  }
  quietLevel /= 20;
  Serial.print("\nquietLevel is "); Serial.println(quietLevel);

  ws2812fx.init();
  ws2812fx.setBrightness(64);
  ws2812fx.setSegment(0, 0, LED_COUNT - 1, FX_MODE_STATIC, RED, 8000, NO_OPTIONS);
  ws2812fx.start();
}

void loop() {
  int raw = analogRead(AUDIO_PIN);
  uint16_t audioSample = abs(raw - (int)quietLevel);
  if (audioSample > maxSample) maxSample = audioSample;

  if (millis() > timer) {
    if (maxSample > THRESHOLD) {
      if (maxSample > maxSampleEver) maxSampleEver = maxSample;

      uint8_t newBrightness = map(maxSample, THRESHOLD, maxSampleEver,
                                  MIN_BRIGHTNESS, MAX_BRIGHTNESS);
      ws2812fx.setBrightness(newBrightness);
    } else {
      ws2812fx.setBrightness(MIN_BRIGHTNESS);
    }

    maxSample = 0;
    timer = millis() + 100;
  }

  ws2812fx.service();
}
