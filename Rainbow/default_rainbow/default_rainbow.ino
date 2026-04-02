#include <FastLED.h>

#define LED_TYPE    WS2812B
#define COLOR_ORDER BRG
#define DATA_PIN    23
#define NUM_LEDS    108
#define BRIGHTNESS  80

CRGB leds[NUM_LEDS];

void setup() {
  delay(500);
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
}

void loop() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
  delay(1000);

  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(1000);

  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(1000);

  FastLED.clear();
  FastLED.show();
  delay(1000);
}