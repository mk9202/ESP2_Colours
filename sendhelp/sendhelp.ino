#include <Arduino.h>
#include <FastLED.h>

// ======================================================
// ESP32 LED CUBE RECEIVER - DUAL UART + PI OVERRIDE
// NOW WITH OPTIONAL SECOND AND THIRD EFFECT LAYERS
//
// This ESP32 is the "LED Processor" side of the project.
// Its main job is to receive visual control packets from either:
//   1. The DSP ESP32, which processes the modular synthesizer signal
//   2. The Raspberry Pi GUI, which lets the user manually control the cube
//
// The LED processor then turns those packets into LED animations using FastLED.
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
//
// Important idea:
// The DSP is normally in control because it represents the live audio signal.
// However, the Pi GUI can temporarily override the DSP so the user can manually
// pick effects, colors, and brightness.
// ======================================================


// ------------------------------------------------------
// LED strip / cube configuration
// ------------------------------------------------------

// The LED cube uses WS2812B addressable LEDs.
// These are the common "NeoPixel-style" LEDs where one data line controls
// all LEDs in the chain.
#define LED_TYPE WS2812B

// The physical LED strip may use a different color byte order.
// If colors look wrong, for example red shows as blue, this is one of the
// first things to check. Common options are GRB, RGB, BRG, etc.
#define COLOR_ORDER BRG

// Data pin from this ESP32 to the WS2812B LED cube.
#define DATA_PIN 23

// Total number of LEDs in the cube.
// In this project, the cube has 9 LEDs per edge and 12 edges:
// 9 * 12 = 108 LEDs.
#define NUM_LEDS 108

// Maximum number of colors/hues that can be used in the palette.
// The code supports up to 5 hues at a time.
#define MAX_COLORS 5


// ------------------------------------------------------
// UART pin configuration
// ------------------------------------------------------

// UART connection from Raspberry Pi to this ESP32.
// The Pi is used for manual GUI control.
#define PI_RX_PIN 16
#define PI_TX_PIN 17

// UART connection from the DSP ESP32 to this ESP32.
// The DSP ESP32 sends packets based on the synthesizer audio analysis.
#define DSP_RX_PIN 25
#define DSP_TX_PIN 26

// Baud rate for both UART connections.
// Both the sender and receiver must use the same baud rate.
#define UART_BAUD 115200


// ------------------------------------------------------
// Pi override behavior
// ------------------------------------------------------

// If the Pi sends a visual packet, it temporarily takes control.
// If no Pi packet is received for this many milliseconds, the system
// automatically gives control back to the DSP ESP32.
#define PI_OVERRIDE_TIMEOUT_MS 3000


// ------------------------------------------------------
// LED buffers
// ------------------------------------------------------

// This is the final LED array that FastLED actually sends to the cube.
CRGB leds[NUM_LEDS];

// These are separate buffers for each effect layer.
// Instead of drawing everything directly into leds[], the code draws each
// effect into its own layer first. Then the layers are added together.
// This makes it possible to combine multiple effects at the same time.
CRGB layer1[NUM_LEDS];
CRGB layer2[NUM_LEDS];
CRGB layer3[NUM_LEDS];


// ------------------------------------------------------
// Effect modes
// ------------------------------------------------------

// Each effect is assigned a number so packets can send a compact command.
// For example, E=4 means EFFECT_RAINBOW.
enum EffectMode {
  EFFECT_STROBE = 0,
  EFFECT_FADE   = 1,
  EFFECT_FLASH  = 2,
  EFFECT_SMOOTH = 3,
  EFFECT_RAINBOW= 4,
  EFFECT_CHASE  = 5
};


// ------------------------------------------------------
// Packet source
// ------------------------------------------------------

// This enum is used so the code knows whether a packet came from:
//   - the Raspberry Pi
//   - the DSP ESP32
//
// This matters because only the Pi is allowed to send override commands.
enum PacketSource {
  SRC_PI,
  SRC_DSP
};


// ------------------------------------------------------
// EffectState
// ------------------------------------------------------
//
// This struct stores the animation state for one effect layer.
//
// This is important because animations need to remember things between loop()
// calls. For example:
//   - where the chase effect currently is
//   - whether the strobe is on or off
//   - which color is currently active
//   - when the effect last updated
//
// There is one EffectState for each layer:
//   primaryState   -> layer 1
//   secondaryState -> layer 2
//   tertiaryState  -> layer 3
// ------------------------------------------------------
struct EffectState {
  // Used with millis() so effects can update without using delay().
  // This keeps the ESP32 responsive to UART packets.
  uint32_t lastTick = 0;

  // Used by blinking/strobing effects to remember if the LEDs are currently on.
  bool on = false;

  // Tracks which hue in the palette is currently being used.
  uint8_t colorIndex = 0;

  // Generic animation value.
  // In the fade effect, this is the current brightness level from 0 to 255.
  int value = 0;

  // Direction and speed of change for fade-style effects.
  // Positive means getting brighter, negative means getting dimmer.
  int delta = 5;

  // Used by the flash effect to alternate between bright and dim states.
  bool brightState = false;

  // Used by the smooth effect to detect when the sine wave brightness cycle
  // has completed, so it knows when to switch to the next color.
  uint8_t lastV = 0;

  // Used by the rainbow effect to shift the colors over time.
  uint16_t offset = 0;

  // Used by the chase effect to remember the current LED position.
  int pos = 0;
};


// ------------------------------------------------------
// LedParams
// ------------------------------------------------------
//
// This struct stores the current settings for the LED cube.
// Packets from the Pi or DSP update these values.
//
// The settings include:
//   - shared color palette
//   - primary effect and brightness
//   - optional second effect and brightness
//   - optional third effect and brightness
//
// The second and third layers allow the cube to show more complex visuals,
// like a rainbow base layer plus a chase effect on top.
// ------------------------------------------------------
struct LedParams {
  // Default hue palette.
  // FastLED CHSV hue values range from 0 to 255.
  // These are not RGB values. They represent positions around the color wheel.
  uint8_t hues[MAX_COLORS] = {0, 40, 96, 160, 200};

  // Number of active colors in the palette.
  // This can be changed by packet fields like N=3,H=20,80,140.
  uint8_t numColors = 5;

  // Primary layer effect.
  // This is always active.
  EffectMode effect = EFFECT_RAINBOW;

  // Brightness for the primary layer.
  uint8_t brightness = 120;

  // Whether the second effect layer is currently enabled.
  bool secondEnabled = false;

  // Default secondary effect.
  EffectMode effect2 = EFFECT_FLASH;

  // Brightness for the secondary layer.
  uint8_t brightness2 = 90;

  // Whether the third effect layer is currently enabled.
  bool thirdEnabled = false;

  // Default third effect.
  EffectMode effect3 = EFFECT_SMOOTH;

  // Brightness for the third layer.
  uint8_t brightness3 = 70;
};


// ------------------------------------------------------
// Global state objects
// ------------------------------------------------------

// Stores the currently selected effects, colors, and brightnesses.
LedParams currentParams;

// Separate animation state for each visual layer.
EffectState primaryState;
EffectState secondaryState;
EffectState tertiaryState;


// ------------------------------------------------------
// Pi override state
// ------------------------------------------------------

// True means the Pi GUI is currently controlling the LED cube.
// When this is true, packets from the DSP are ignored so the Pi settings
// do not immediately get overwritten.
bool piOverrideActive = false;

// Stores the time when the last valid Pi packet was received.
// This is used to time out the Pi override after a few seconds.
uint32_t lastPiPacketMs = 0;


// ------------------------------------------------------
// UART receive buffers
// ------------------------------------------------------

// These strings collect characters from each UART until a newline is received.
// Packets are expected to end with '\n'.
String piRxLine = "";
String dspRxLine = "";


// ------------------------------------------------------
// Helper function: getPaletteHue
// ------------------------------------------------------
//
// Returns a hue from the current palette.
//
// The modulo makes this safe even if the effect asks for a color index
// larger than the number of colors. For example, if there are 3 colors and
// idx is 5, it wraps around instead of going out of bounds.
// ------------------------------------------------------
static inline uint8_t getPaletteHue(uint8_t idx) {
  return currentParams.hues[idx % currentParams.numColors];
}


// ------------------------------------------------------
// Helper function: scaleValue
// ------------------------------------------------------
//
// This scales a raw brightness value by a layer brightness.
//
// rawValue is usually the brightness produced by the effect itself.
// layerBrightness is the user-selected brightness for that layer.
//
// scale8() is a FastLED helper that scales an 8-bit value efficiently.
// Example:
//   rawValue = 255, layerBrightness = 120 -> output around 120
//   rawValue = 128, layerBrightness = 120 -> output around 60
// ------------------------------------------------------
static inline uint8_t scaleValue(uint8_t rawValue, uint8_t layerBrightness) {
  return scale8(rawValue, layerBrightness);
}


// ------------------------------------------------------
// Helper function: clearBuffer
// ------------------------------------------------------
//
// Sets an entire LED buffer to black.
// This is used before rendering each frame so old pixels from the last frame
// do not stay on the cube accidentally.
// ------------------------------------------------------
void clearBuffer(CRGB* buf) {
  fill_solid(buf, NUM_LEDS, CRGB::Black);
}


// ------------------------------------------------------
// Function prototypes
// ------------------------------------------------------
//
// These let the compiler know these functions exist before setup() and loop().
// This is not always required in Arduino, but it helps keep the file organized.
// ------------------------------------------------------
void processSerialStream(HardwareSerial& port, String& line, PacketSource src);
bool handleControlPacket(const String& packet, PacketSource src);
bool parseVisualPacket(String packet);
void readSerialPackets();
void updateOverrideTimeout();
void renderEffect(EffectMode mode, CRGB* target, EffectState& st, uint8_t layerBrightness);
void renderAndShow();


// ------------------------------------------------------
// setup()
// ------------------------------------------------------
//
// Runs once when the ESP32 boots.
//
// This function:
//   1. Starts the USB serial monitor for debugging
//   2. Starts UART communication with the Pi
//   3. Starts UART communication with the DSP ESP32
//   4. Initializes FastLED
//   5. Clears the LED cube
// ------------------------------------------------------
void setup() {
  // USB serial monitor for debug messages.
  Serial.begin(115200);

  // Serial2 is used for Raspberry Pi communication.
  // The Pi sends GUI/manual control packets to this port.
  Serial2.begin(UART_BAUD, SERIAL_8N1, PI_RX_PIN, PI_TX_PIN);

  // Serial1 is used for DSP ESP32 communication.
  // The DSP sends audio-reactive visual packets to this port.
  Serial1.begin(UART_BAUD, SERIAL_8N1, DSP_RX_PIN, DSP_TX_PIN);

  // Initialize the LED strip/cube with the selected LED type, data pin,
  // color order, LED array, and number of LEDs.
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);

  // Global FastLED brightness is kept at full brightness.
  // Individual layer brightness is handled inside the effect rendering.
  FastLED.setBrightness(255);

  // Make sure the cube starts off.
  FastLED.clear();
  FastLED.show();

  // Startup debug messages so we know the ESP32 booted correctly.
  Serial.println("Receiver ready.");
  Serial.println("Pi  on Serial2 GPIO16/17");
  Serial.println("DSP on Serial1 GPIO25/26");
}


// ------------------------------------------------------
// loop()
// ------------------------------------------------------
//
// Runs repeatedly forever.
//
// The loop is intentionally simple:
//   1. Read any incoming packets
//   2. Check if Pi override expired
//   3. Render the current LED effects
//
// There are no delay() calls because delays would make the ESP32 slow to
// respond to new UART packets.
// ------------------------------------------------------
void loop() {
  readSerialPackets();
  updateOverrideTimeout();
  renderAndShow();
}


// ------------------------------------------------------
// readSerialPackets()
// ------------------------------------------------------
//
// Checks both UART ports for incoming data.
//
// Pi packets and DSP packets are processed separately because they come from
// different UART hardware and have different control permissions.
// ------------------------------------------------------
void readSerialPackets() {
  processSerialStream(Serial2, piRxLine, SRC_PI);
  processSerialStream(Serial1, dspRxLine, SRC_DSP);
}


// ------------------------------------------------------
// updateOverrideTimeout()
// ------------------------------------------------------
//
// If the Pi is controlling the cube but stops sending packets, this function
// gives control back to the DSP after PI_OVERRIDE_TIMEOUT_MS.
//
// This prevents the cube from getting stuck in Pi mode forever if the GUI
// closes, the Pi disconnects, or the Pi stops transmitting.
// ------------------------------------------------------
void updateOverrideTimeout() {
  if (piOverrideActive && (millis() - lastPiPacketMs >= PI_OVERRIDE_TIMEOUT_MS)) {
    piOverrideActive = false;
    Serial.println("Pi override timed out -> DSP restored.");
  }
}


// ------------------------------------------------------
// processSerialStream()
// ------------------------------------------------------
//
// Reads characters from one UART port and builds a full packet line.
//
// Parameters:
//   port -> the UART port to read from, either Serial1 or Serial2
//   line -> the String buffer for that UART
//   src  -> tells us whether this packet came from the Pi or DSP
//
// Packet format expectation:
//   Packets should end with '\n'.
//   Example:
//     E=4,N=3,H=0,80,160,B=120\n
//
// Important logic:
//   - If a newline is received, the packet is complete.
//   - If the packet is a control packet, handle it first.
//   - If it is not a control packet, try parsing it as a visual packet.
//   - If the DSP sends a packet while Pi override is active, ignore it.
// ------------------------------------------------------
void processSerialStream(HardwareSerial& port, String& line, PacketSource src) {
  while (port.available()) {
    char c = (char)port.read();

    // Newline means the packet is complete and ready to parse.
    if (c == '\n') {
      line.trim();

      // Ignore empty lines.
      if (line.length() > 0) {

        // First, check if this is a special control packet.
        // Right now, only the Pi can send OVR=1 or OVR=0.
        if (!handleControlPacket(line, src)) {

          // If this packet came from the DSP while Pi override is active,
          // ignore it so the DSP does not overwrite Pi GUI settings.
          if (!(src == SRC_DSP && piOverrideActive)) {

            // Try to parse the packet as a normal visual/effect packet.
            if (parseVisualPacket(line)) {

              // Any valid visual packet from the Pi automatically enables
              // Pi override and resets the timeout timer.
              if (src == SRC_PI) {
                piOverrideActive = true;
                lastPiPacketMs = millis();
              }
            }
          }
        }
      }

      // Clear the line buffer so the next packet can be received.
      line = "";
    }

    // Ignore carriage returns.
    // Some systems send "\r\n" instead of just "\n".
    else if (c != '\r') {
      line += c;

      // Safety check:
      // If the line gets too long, something probably went wrong
      // such as a missing newline or corrupted data.
      // Clearing it prevents memory issues or bad packets from building up.
      if (line.length() > 180) {
        line = "";
      }
    }
  }
}


// ------------------------------------------------------
// handleControlPacket()
// ------------------------------------------------------
//
// Handles special control packets.
// These are different from visual packets because they do not directly set
// colors or effects.
//
// Only the Raspberry Pi is allowed to send control packets.
// The DSP should not be able to force override mode on or off.
//
// Supported control packets:
//   OVR=1 -> Pi takes control
//   OVR=0 -> DSP gets control back
//
// Returns:
//   true  -> packet was handled as a control packet
//   false -> packet was not a control packet
// ------------------------------------------------------
bool handleControlPacket(const String& packet, PacketSource src) {
  // If the packet did not come from the Pi, it cannot be a valid control packet.
  if (src != SRC_PI) {
    return false;
  }

  // Make a cleaned-up copy of the packet.
  // This removes spaces so "OVR = 1" can still work after cleanup.
  String p = packet;
  p.trim();
  p.replace(" ", "");

  // Pi explicitly takes control.
  if (p.equalsIgnoreCase("OVR=1")) {
    piOverrideActive = true;
    lastPiPacketMs = millis();
    Serial.println("Pi override ENABLED");
    return true;
  }

  // Pi explicitly gives control back to the DSP.
  if (p.equalsIgnoreCase("OVR=0")) {
    piOverrideActive = false;
    Serial.println("Pi override DISABLED");
    return true;
  }

  // Not a recognized control packet.
  return false;
}


// ------------------------------------------------------
// parseVisualPacket()
// ------------------------------------------------------
//
// Parses visual control packets and updates currentParams.
//
// Example packets:
//   E=4,B=120
//   E=5,N=3,H=20,80,140,B=180
//   E=4,B=100,E2=5,B2=80
//   E=3,B=120,E2=5,B2=90,E3=1,B3=70
//
// Fields:
//   E  -> primary effect
//   B  -> primary brightness
//   N  -> number of palette colors
//   H  -> hue list
//   E2 -> second effect
//   B2 -> second brightness
//   X2 -> disable second effect
//   E3 -> third effect
//   B3 -> third brightness
//   X3 -> disable third effect
//
// Returns:
//   true  -> packet was valid and settings were updated
//   false -> packet was invalid
//
// Note:
// The H field is special because it can contain several comma-separated
// hue values after it, like H=20,80,140.
// Because of that, H needs extra parsing logic compared to fields like E=4.
// ------------------------------------------------------
bool parseVisualPacket(String packet) {
  // Clean up packet formatting.
  packet.trim();
  packet.replace(" ", "");

  // Do not try to parse an empty packet.
  if (packet.length() == 0) return false;

  // i is the current position in the packet string.
  int i = 0;

  // Walk through the packet one comma-separated token at a time.
  while (i < packet.length()) {
    int nextComma = packet.indexOf(',', i);

    // If there is no next comma, this token goes to the end of the packet.
    if (nextComma == -1) nextComma = packet.length();

    // Extract the current token.
    // Example token: "E=4" or "B2=90"
    String token = packet.substring(i, nextComma);


    // --------------------------------------------------
    // Third layer effect select
    // --------------------------------------------------
    if (token.startsWith("E3=")) {
      int effect3 = token.substring(3).toInt();

      // Only effect numbers 0 through 5 are valid.
      if (effect3 < 0 || effect3 > 5) return false;

      currentParams.effect3 = (EffectMode)effect3;

      // Setting E3 automatically enables the third layer.
      currentParams.thirdEnabled = true;
    }

    // --------------------------------------------------
    // Third layer brightness
    // --------------------------------------------------
    else if (token.startsWith("B3=")) {
      int b3 = token.substring(3).toInt();

      if (b3 < 0 || b3 > 255) return false;

      currentParams.brightness3 = (uint8_t)b3;

      // If brightness is greater than 0, enable the layer.
      // If brightness is 0, this does not force-disable it because X3=1
      // is the explicit disable command.
      currentParams.thirdEnabled = (b3 > 0) ? true : currentParams.thirdEnabled;
    }

    // --------------------------------------------------
    // Third layer disable command
    // --------------------------------------------------
    else if (token.startsWith("X3=")) {
      int x3 = token.substring(3).toInt();

      // X3=1 turns off the third layer.
      if (x3 == 1) currentParams.thirdEnabled = false;
    }

    // --------------------------------------------------
    // Second layer effect select
    // --------------------------------------------------
    else if (token.startsWith("E2=")) {
      int effect2 = token.substring(3).toInt();

      if (effect2 < 0 || effect2 > 5) return false;

      currentParams.effect2 = (EffectMode)effect2;

      // Setting E2 automatically enables the second layer.
      currentParams.secondEnabled = true;
    }

    // --------------------------------------------------
    // Second layer brightness
    // --------------------------------------------------
    else if (token.startsWith("B2=")) {
      int b2 = token.substring(3).toInt();

      if (b2 < 0 || b2 > 255) return false;

      currentParams.brightness2 = (uint8_t)b2;

      // Same behavior as B3:
      // brightness > 0 enables the layer, but brightness = 0 does not
      // automatically disable it unless X2=1 is sent.
      currentParams.secondEnabled = (b2 > 0) ? true : currentParams.secondEnabled;
    }

    // --------------------------------------------------
    // Second layer disable command
    // --------------------------------------------------
    else if (token.startsWith("X2=")) {
      int x2 = token.substring(3).toInt();

      // X2=1 turns off the second layer.
      if (x2 == 1) currentParams.secondEnabled = false;
    }

    // --------------------------------------------------
    // Primary effect select
    // --------------------------------------------------
    else if (token.startsWith("E=")) {
      int effectInt = token.substring(2).toInt();

      if (effectInt < 0 || effectInt > 5) return false;

      currentParams.effect = (EffectMode)effectInt;
    }

    // --------------------------------------------------
    // Primary brightness
    // --------------------------------------------------
    else if (token.startsWith("B=")) {
      int brightness = token.substring(2).toInt();

      if (brightness < 0 || brightness > 255) return false;

      currentParams.brightness = (uint8_t)brightness;
    }

    // --------------------------------------------------
    // Number of colors in the palette
    // --------------------------------------------------
    else if (token.startsWith("N=")) {
      int numColors = token.substring(2).toInt();

      // Must have at least 1 color and cannot exceed MAX_COLORS.
      if (numColors < 1 || numColors > MAX_COLORS) return false;

      currentParams.numColors = (uint8_t)numColors;
    }

    // --------------------------------------------------
    // Hue list
    // --------------------------------------------------
    else if (token.startsWith("H=")) {
      // Temporary storage for the new hue list.
      // The code does not immediately write to currentParams.hues
      // until it knows the hue list is valid.
      int pendingHues[MAX_COLORS] = {0};
      int pendingCount = 0;

      // The first hue is attached directly to H=.
      // Example: in H=20,80,140, the first token is H=20.
      String firstHueStr = token.substring(2);

      if (firstHueStr.length() > 0) {
        int hue = firstHueStr.toInt();

        if (hue < 0 || hue > 255) return false;

        pendingHues[pendingCount++] = hue;
      }

      // Now continue reading following comma-separated values as hues
      // until another recognized field begins.
      //
      // Example:
      //   E=5,N=3,H=20,80,140,B=180
      //
      // Once the parser sees B=180, it knows the hue list is finished.
      int j = nextComma + 1;

      while (j < packet.length() && pendingCount < MAX_COLORS) {
        int comma2 = packet.indexOf(',', j);
        if (comma2 == -1) comma2 = packet.length();

        String nextToken = packet.substring(j, comma2);

        // Stop collecting hues if the next token is another command field.
        if (nextToken.startsWith("E=") || nextToken.startsWith("B=") ||
            nextToken.startsWith("N=") || nextToken.startsWith("H=") ||
            nextToken.startsWith("E2=") || nextToken.startsWith("B2=") ||
            nextToken.startsWith("X2=") || nextToken.startsWith("E3=") ||
            nextToken.startsWith("B3=") || nextToken.startsWith("X3=")) {
          break;
        }

        // If it is not another command field, treat it as another hue value.
        int hue = nextToken.toInt();

        if (hue < 0 || hue > 255) return false;

        pendingHues[pendingCount++] = hue;

        // Move to the next comma-separated token.
        j = comma2 + 1;
      }

      // At least one hue must be received.
      if (pendingCount < 1) return false;

      // Copy the validated hue list into the actual current settings.
      for (int k = 0; k < pendingCount; k++) {
        currentParams.hues[k] = (uint8_t)pendingHues[k];
      }

      // The number of colors becomes however many hues were actually received.
      currentParams.numColors = pendingCount;

      // Since H parsing may have consumed multiple comma-separated values,
      // manually move i to the next unprocessed token.
      i = j;
      continue;
    }

    // --------------------------------------------------
    // Unknown token
    // --------------------------------------------------
    else {
      // If the packet has a token the parser does not understand,
      // reject the whole packet.
      return false;
    }

    // Move to the token after the comma.
    i = nextComma + 1;
  }


  // ------------------------------------------------------
  // Debug printout
  // ------------------------------------------------------
  //
  // After a successful packet, print the active settings.
  // This is helpful when testing through the Arduino Serial Monitor because
  // it confirms that the ESP32 actually understood the packet.
  // ------------------------------------------------------
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


// ------------------------------------------------------
// renderAndShow()
// ------------------------------------------------------
//
// Builds one complete frame and sends it to the LEDs.
//
// Process:
//   1. Clear all layer buffers
//   2. Render the primary effect into layer1
//   3. Render the second effect into layer2 if enabled
//   4. Render the third effect into layer3 if enabled
//   5. Add the layers together into leds[]
//   6. Send leds[] to the physical cube using FastLED.show()
//
// Layer blending:
//   leds[i] += layer2[i]
// This adds the RGB values together. FastLED handles saturation so the color
// values do not overflow in a weird way.
// ------------------------------------------------------
void renderAndShow() {
  // Start each frame with blank layer buffers.
  clearBuffer(layer1);
  clearBuffer(layer2);
  clearBuffer(layer3);

  // Primary effect is always rendered.
  renderEffect(currentParams.effect, layer1, primaryState, currentParams.brightness);

  // Render the second layer only if it is enabled and has visible brightness.
  if (currentParams.secondEnabled && currentParams.brightness2 > 0) {
    renderEffect(currentParams.effect2, layer2, secondaryState, currentParams.brightness2);
  }

  // Render the third layer only if it is enabled and has visible brightness.
  if (currentParams.thirdEnabled && currentParams.brightness3 > 0) {
    renderEffect(currentParams.effect3, layer3, tertiaryState, currentParams.brightness3);
  }

  // Combine all active layers into the final LED output buffer.
  for (int i = 0; i < NUM_LEDS; i++) {
    // Start with the primary layer.
    leds[i] = layer1[i];

    // Add second layer on top if active.
    if (currentParams.secondEnabled && currentParams.brightness2 > 0) {
      leds[i] += layer2[i];
    }

    // Add third layer on top if active.
    if (currentParams.thirdEnabled && currentParams.brightness3 > 0) {
      leds[i] += layer3[i];
    }
  }

  // Actually update the physical LEDs.
  FastLED.show();
}


// ------------------------------------------------------
// renderEffect()
// ------------------------------------------------------
//
// Draws one selected effect into one target buffer.
//
// Parameters:
//   mode            -> which effect to render
//   target          -> which LED layer buffer to draw into
//   st              -> animation state for this layer
//   layerBrightness -> brightness limit for this layer
//
// This function does NOT call FastLED.show().
// It only fills the target buffer. The final show happens in renderAndShow().
//
// The use of millis() allows the effects to animate without blocking.
// This is better than delay() because the ESP32 can still read UART packets
// while animations are running.
// ------------------------------------------------------
void renderEffect(EffectMode mode, CRGB* target, EffectState& st, uint8_t layerBrightness) {
  uint32_t now = millis();

  switch (mode) {

    // --------------------------------------------------
    // EFFECT_STROBE
    // --------------------------------------------------
    //
    // Turns the LEDs on and off quickly.
    // Every time the strobe turns on, it advances to the next color.
    // --------------------------------------------------
    case EFFECT_STROBE: {
      if (now - st.lastTick >= 100) {
        st.lastTick = now;

        // Flip between on and off.
        st.on = !st.on;

        // Only change color when the strobe turns on.
        if (st.on) {
          st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
        }
      }

      // If the strobe is currently on, fill the entire cube with one color.
      // If it is off, leave the target buffer black.
      if (st.on) {
        fill_solid(
          target,
          NUM_LEDS,
          CHSV(getPaletteHue(st.colorIndex), 255, layerBrightness)
        );
      }

      break;
    }


    // --------------------------------------------------
    // EFFECT_FADE
    // --------------------------------------------------
    //
    // Smoothly fades brightness up and down.
    // When the brightness reaches zero, it switches to the next palette color.
    // --------------------------------------------------
    case EFFECT_FADE: {
      if (now - st.lastTick >= 20) {
        st.lastTick = now;

        // Move brightness value up or down.
        st.value += st.delta;

        // If brightness hits the top, clamp it and reverse direction.
        if (st.value >= 255) {
          st.value = 255;
          st.delta = -st.delta;
        }

        // If brightness hits the bottom, clamp it, reverse direction,
        // and move to the next color.
        else if (st.value <= 0) {
          st.value = 0;
          st.delta = -st.delta;
          st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
        }
      }

      // Draw the whole cube using the current fade brightness.
      fill_solid(
        target,
        NUM_LEDS,
        CHSV(
          getPaletteHue(st.colorIndex),
          255,
          scaleValue((uint8_t)st.value, layerBrightness)
        )
      );

      break;
    }


    // --------------------------------------------------
    // EFFECT_FLASH
    // --------------------------------------------------
    //
    // Alternates between a bright state and a dim state.
    // This is slower and less harsh than the strobe effect.
    // --------------------------------------------------
    case EFFECT_FLASH: {
      if (now - st.lastTick >= 250) {
        st.lastTick = now;

        // Toggle between bright and dim.
        st.brightState = !st.brightState;

        // Advance color whenever the flash becomes bright.
        if (st.brightState) {
          st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
        }
      }

      // Bright state uses full layer brightness.
      // Dim state uses a small value scaled by the layer brightness.
      uint8_t v = st.brightState ? layerBrightness : scaleValue(40, layerBrightness);

      fill_solid(
        target,
        NUM_LEDS,
        CHSV(getPaletteHue(st.colorIndex), 255, v)
      );

      break;
    }


    // --------------------------------------------------
    // EFFECT_SMOOTH
    // --------------------------------------------------
    //
    // Uses FastLED's beatsin8() function to create a smooth pulsing brightness.
    //
    // beatsin8(18, 20, 255) creates a sine-wave-like brightness value that
    // moves between 20 and 255 at a tempo of 18 beats per minute.
    //
    // This effect is useful for calm, smooth, breathing-style visuals.
    // --------------------------------------------------
    case EFFECT_SMOOTH: {
      // Raw brightness from sine wave.
      uint8_t vRaw = beatsin8(18, 20, 255);

      // Scale the sine wave brightness by the layer brightness setting.
      uint8_t v = scaleValue(vRaw, layerBrightness);

      fill_solid(
        target,
        NUM_LEDS,
        CHSV(getPaletteHue(st.colorIndex), 255, v)
      );

      // This detects when the brightness wave has just passed its peak.
      // When that happens, switch to the next color.
      if (vRaw < st.lastV && st.lastV > 240) {
        st.colorIndex = (st.colorIndex + 1) % currentParams.numColors;
      }

      // Save current brightness so next loop can compare against it.
      st.lastV = vRaw;

      break;
    }


    // --------------------------------------------------
    // EFFECT_RAINBOW
    // --------------------------------------------------
    //
    // Creates a moving block-style rainbow based on the current palette.
    //
    // This is not a full HSV rainbow. It only uses the selected palette hues.
    // The offset shifts the colors across the LED cube over time.
    // --------------------------------------------------
    case EFFECT_RAINBOW: {
      if (now - st.lastTick >= 20) {
        st.lastTick = now;

        // Move the color pattern forward.
        st.offset++;
      }

      for (int i = 0; i < NUM_LEDS; i++) {
        // Choose which palette color this LED should use.
        //
        // NUM_LEDS / currentParams.numColors determines roughly how wide
        // each color band should be.
        //
        // The +1 helps avoid divide-by-zero style issues and keeps the
        // bands from being too small in some cases.
        uint8_t paletteIndex =
          ((i + st.offset) / (NUM_LEDS / currentParams.numColors + 1))
          % currentParams.numColors;

        target[i] = CHSV(
          getPaletteHue(paletteIndex),
          255,
          layerBrightness
        );
      }

      break;
    }


    // --------------------------------------------------
    // EFFECT_CHASE
    // --------------------------------------------------
    //
    // Creates a moving dot with a fading tail.
    // The dot moves around the LED chain and leaves a short trail behind it.
    // --------------------------------------------------
    case EFFECT_CHASE: {
      if (now - st.lastTick >= 25) {
        st.lastTick = now;

        // Move the chase position forward and wrap around at the end.
        st.pos = (st.pos + 1) % NUM_LEDS;
      }

      // Draw a 10-LED tail behind the current position.
      for (int t = 0; t < 10; t++) {
        // Calculate the LED index for this part of the tail.
        // Adding NUM_LEDS before modulo prevents negative index problems.
        int idx = (st.pos - t + NUM_LEDS) % NUM_LEDS;

        // Tail gets dimmer as t increases.
        int tailBrightness = 255 - (t * 22);

        if (tailBrightness < 0) {
          tailBrightness = 0;
        }

        // getPaletteHue(t) uses t to select different colors along the tail.
        // Because getPaletteHue wraps with modulo, this is safe even when
        // t is greater than the number of active colors.
        target[idx] = CHSV(
          getPaletteHue(t),
          255,
          scaleValue((uint8_t)tailBrightness, layerBrightness)
        );
      }

      break;
    }
  }
}