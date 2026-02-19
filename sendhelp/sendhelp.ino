// Use Arduino IDE to code this because VSCode hates FastLEDs or maybe vscode has a extention that lets you use arduino ide
// This is the brain of the LEDs, so this gets the variables from the ESP connected from the synth
// Synth -> ESP1 -> "ESP2" <- PI
// ESPLED will take the frequency, amplitude, and the shape of the wave and process it accordingly
// guys there is a library for the effects actually

#include <FastLED.h>

#define NUM_LEDS 1
#define DATA_PIN 23 // Try 23 first, then 18

CRGB leds[NUM_LEDS]; // Global LED array

// using FastLED to initalize the LEDs
void setup()
{
  delay(1000);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
}
// base program that works to switch LED on (do not delete until we have functional code for processing)
void loop()
{
  leds[0] = CRGB::Red; // LED at index 0 will be RED,
  FastLED.show();      // the RED gets show on the LED (using FastLED)
  delay(400);          // standard LED

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

// function prototypes to implement

// void process_sent_data(string Uart_rx){
// intialize the uart recieving function for this board
// parse the data so it can be used for the colour processing

//}

// void process_shape(WaveformType waveform)
// would probably have to get the header file from the other project to detect that
// if the if statements get larger i would def break them down into smaller functions
// I have not yet looked that deeply into how the effects work yet
// if (SINE)
// smooth fade in and out (using fast leds)
// if (SAWTOOTH)
//  colour sweeping up and down
// if (SQUARE)
//  hard on and off
// if (TRIANGLE)
//  fade up and down like a triangle
//  I imagine there is going to be something with timing and LED chasing
// if OTHER
//  Live laugh love do whatever maybe default preset

// void process_colour(float frequency)
// This one is a little more straightforward because its just coding the formula for hue
// Code the formula for hue and then based on that set the

// ChatGPT generated code
// CRGB freqToColor(float freq) {

//   // Clamp range
//   freq = constrain(freq, MIN_FREQ, MAX_FREQ);

//   // Normalize 0.0 → 1.0
//   float norm = (freq - MIN_FREQ) / (MAX_FREQ - MIN_FREQ);

//   // Map to hue (0–160 works well visually)
//   // 0 = red, 96 = green, 160 = blue
//   uint8_t hue = (uint8_t)(norm * 160.0f);

//   // Full saturation & brightness
//   CHSV hsv(hue, 255, 255);

//   CRGB rgb;
//   hsv2rgb_rainbow(hsv, rgb);

//   return rgb;
// }

// void GUI_interaction (string PI_rx){
// get the information from the PI gui and process that
//}

// void ESP1_control(){
//  I dont exactly know how this should be implemented but this should do something to make sure the automatic processing program is not
//  constantly overwriting the new updates from the pi (this could maybe just be an error checking function so there is no backflow)

//}