#include <EEPROM.h>
#include "TinyMegaI2CMaster.h"
#include <tinyNeoPixel_Static.h>

#define LED_PIN 1
#define BUTTON_PIN 0
#define BRIGHTNESS_PIN 10

#define DEBUG false
#define DEBUG_MODE true

#define MAX_BRIGHTNESS 150
//#define NUM_PIXELS 9
#define NUM_PIXELS 51

#define EEPROM_MODE_ADDRESS 0 // uint8_t
#define EEPROM_COLOR_ADDRESS 1 // uint16_t
#define EEPROM_GRADIENT_ADDRESS 3 // uint8_t


byte pixels[NUM_PIXELS * 3];
tinyNeoPixel leds = tinyNeoPixel(NUM_PIXELS, LED_PIN, NEO_GRB, pixels);

#define GRADIENT_SIZE 6
const PROGMEM uint16_t GRADIENTS[] = {
  0, 255,  13107, 255,  26214, 255,  39321, 255,  52428, 255,  65535, 255, // Rainbow Bold
  0, 220,  13107, 220,  26214, 220,  39321, 220,  52428, 220,  65535, 220, // Rainbow Pastel
};

enum Mode {
  COLOR,
  GRADIENT,
  GRADIENT_ANIMATED,
  SPECTRUM,

  MODE_LENGTH // last value is the length of this enum
};

// Config options
uint8_t currentMode = GRADIENT;
uint16_t currentColorHue = 0;
uint8_t currentGradientIndex = 0;
bool on = true;

uint8_t i;
uint8_t spectrum[16];

uint16_t cycle = 0;
uint8_t cycleCounter = 0;

uint16_t lastAnalog = 0;
uint8_t lastAmbient = 0;

bool debugFlash = true;

void setup() {
  TinyMegaI2C.init();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  leds.setBrightness(10);
  if (DEBUG) {
    for (i = 0; i < NUM_PIXELS; i++) {
      leds.setPixelColor(i, leds.Color(0, 255, 0));
      delay(15);
      leds.show();
    }
  
    leds.show();
    delay(300);
  }
  
  leds.fill(leds.Color(0, 0, 0));
  leds.show();
  delay(300);

  EEPROM.get(EEPROM_MODE_ADDRESS, currentMode);
  currentMode = currentMode % MODE_LENGTH;
  
  EEPROM.get(EEPROM_COLOR_ADDRESS, currentColorHue);
  EEPROM.get(EEPROM_GRADIENT_ADDRESS, currentGradientIndex);
  if (currentGradientIndex == 255) currentGradientIndex = 0;
  
  _PROTECTED_WRITE(WDT.CTRLA,WDT_PERIOD_512CLK_gc); //enable the WDT, with a 0.512s timeout (WARNING: delay > 0.5 will reset)
}

void updateSpectrum() {
  const uint8_t amp = 25;
  const uint8_t fadeFactor = 15;

  uint16_t oldValue = 1000 - fadeFactor;
  uint16_t newValue = fadeFactor * amp;

  TinyMegaI2C.start(8, 16);

  for (i = 0; i < 16; i++) {
    spectrum[i] = max(100, (spectrum[i] * oldValue + TinyMegaI2C.read() * newValue) / 1000);
  }

  TinyMegaI2C.stop();
}

void checkButton() {
  uint16_t buttonValue = analogRead(BUTTON_PIN);

  if (abs(lastAnalog - buttonValue) > 20) {
    if (buttonValue > 500) { // Color
      on = true;

      if (currentMode == COLOR) {
        currentColorHue += 6553;
        EEPROM.put(EEPROM_COLOR_ADDRESS, currentColorHue);
      } else {
        currentGradientIndex = (currentGradientIndex + GRADIENT_SIZE * 2) % (sizeof(GRADIENTS) / 2);
        EEPROM.put(EEPROM_GRADIENT_ADDRESS, currentGradientIndex);
      }
    } else if (buttonValue > 250) { // Mode
      on = true;
      cycle = 0;
      currentMode = (currentMode + 1) % MODE_LENGTH;
      
      EEPROM.put(EEPROM_MODE_ADDRESS, currentMode);
    } else if (buttonValue > 100) { // On / Off
      on = !on;
    }
  }

  lastAnalog = buttonValue;
}

void checkBrightness() {
  uint8_t ambient = analogRead(BRIGHTNESS_PIN) >> 4;
  if (abs(ambient - lastAmbient) > 1) {
    leds.setBrightness(min(MAX_BRIGHTNESS, ambient));
    lastAmbient = ambient;
  }
}

void updateCycle() {
  if (on && currentMode == GRADIENT_ANIMATED) {
    cycleCounter++;
  
    if (cycleCounter > 5) {
      cycleCounter = 0;
      cycle++;
    }
  }
}

void updateColor(uint8_t i) {
  uint8_t value = 255;
  uint16_t hue = 255;
  uint8_t sat = 255;
  
  if (currentMode == SPECTRUM) {
    uint8_t spectrumIndex = map(i, 0, NUM_PIXELS, 0, 16);
    value = spectrum[spectrumIndex];
  }
  
  uint8_t iCycle = (i + cycle) % NUM_PIXELS;
  
  uint8_t iMapped = (iCycle * 100) / NUM_PIXELS;
  uint8_t gradIndex = (iMapped * (GRADIENT_SIZE - 1) / 100);
  uint8_t distance = (iMapped * (GRADIENT_SIZE - 1)) - (gradIndex * 100);
  
  hue = map(distance, 0, 100, pgm_read_word(GRADIENTS + currentGradientIndex + gradIndex * 2), pgm_read_word(GRADIENTS + currentGradientIndex + ((gradIndex + 1) * 2)));
  sat = map(distance, 0, 100, pgm_read_word(GRADIENTS + currentGradientIndex + gradIndex * 2 + 1), pgm_read_word(GRADIENTS + currentGradientIndex + ((gradIndex + 1) * 2 + 1)));
  
  leds.setPixelColor(i, leds.ColorHSV(hue, sat, value));
}

void loop() {
  //  reset watchdog timer
  __asm__ __volatile__ ("wdr"::);

  checkButton();
  checkBrightness();

  if (on) {
    if (currentMode == COLOR) {
      leds.fill(leds.ColorHSV(currentColorHue, 255, 255));
    } else {
      if (currentMode == SPECTRUM) updateSpectrum();
      
      for (i = 0; i < NUM_PIXELS; i++) {
        updateColor(i);
      }
    }

    if (DEBUG_MODE) {
      uint32_t lastPixel = leds.Color(255, 255, 0);
      if (currentMode == GRADIENT) lastPixel = leds.Color(255, 0, 0);
      if (currentMode == GRADIENT_ANIMATED) lastPixel = leds.Color(0, 255, 0);
      if (currentMode == SPECTRUM) lastPixel = leds.Color(0, 0, 255);
      
      if (debugFlash) leds.setPixelColor(NUM_PIXELS - 1, lastPixel);
      else leds.setPixelColor(NUM_PIXELS - 1, leds.Color(0, 0, 0));
      debugFlash = !debugFlash;
    }
  } else {
    leds.fill(leds.Color(0, 0, 0));
  }

  leds.show();

  updateCycle();
  
  delay(15);
}
