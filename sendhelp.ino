#include <FastLED.h>

#define NUM_LEDS 1
#define DATA_PIN 23   // Try 23 first, then 18

CRGB leds[NUM_LEDS];   // Global LED array

void setup() {
  delay(1000);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
}

void loop() {
  leds[0] = CRGB::Red;
  FastLED.show();
  delay(400);

  leds[0] = CRGB::Green;
  FastLED.show();
  delay(400);

  leds[0] = CRGB::Blue;
  FastLED.show();
  delay(400);

  leds[0] = CRGB::Black;
  FastLED.show();
  delay(400);
}
