#include "ClickButton.h"
#include <EEPROM.h>
#include "TinyMegaI2CMaster.h"
#include <tinyNeoPixel_Static.h>

#define LED_PIN PIN_PA6
#define MOTION_PIN PIN_PA7

#define POWER_BUTTON_PIN PIN_PA3
#define MODE_BUTTON_PIN PIN_PA2
#define COLOR_BUTTON_PIN PIN_PA1

ClickButton powerButton(POWER_BUTTON_PIN, LOW, true);
ClickButton modeButton(MODE_BUTTON_PIN, LOW, true);
ClickButton colorButton(COLOR_BUTTON_PIN, LOW, true);

#define DEBUG false
#define SERIAL_DEBUG true

#define MAX_BRIGHTNESS 150
//#define NUM_PIXELS 9
#define NUM_PIXELS 50
//#define NUM_PIXELS 16

#define SPECTRUM_MIN 70
#define SPECTRUM_THRESHOLD 350

#define EEPROM_MODE_ADDRESS 0 // uint8_t
#define EEPROM_COLOR_ADDRESS 1 // uint8_t
#define EEPROM_GRADIENT_ADDRESS 2 // uint8_t
#define EEPROM_BRIGHTNESS_ADDRESS 3 // uint8_t

unsigned long MOTION_TIMEOUT = (unsigned long) 1000 * 60 * 20; // 20 minutes

byte pixels[NUM_PIXELS * 3];
tinyNeoPixel leds = tinyNeoPixel(NUM_PIXELS, LED_PIN, NEO_GRB, pixels);

#define GRADIENT_SIZE 6
const PROGMEM uint16_t GRADIENTS[] = {
//  0, 255,  0, 255,  0, 255,  0, 255,  0, 255,  0, 255, // RED TEST
  
  0, 255,  13107, 255,  26214, 255,  39321, 255,  52428, 255,  65535, 255, // Rainbow Bold
  0, 220,  13107, 220,  26214, 220,  39321, 220,  52428, 220,  65535, 220, // Rainbow Pastel

  0, 240,  0,200,  3500,240,  8000,240,  3000,235,  0,220, // Autumn
  25300,255,  5461,210,  35239,246,  39028,245,  25300,255,  17000,220, // Beach
  0,255,  2556,255,  10362,245,  2500,240,  7000,225,  0,255, // Fire
  19311,245,  3500,245,  20968,245,  5461,235,  19311,230,  12000,240, // Forest
  33495,235,  42816,225,  20000,220,  33495,235, 28000,245,  18000,210, // Ocean
  64045,224,  46902,230,  54612,180,  64045,240,  50000,245,  65000,250, // Pink
  58000,199,  29854,228,  7459,215,  62849,181,  43253,199, 30000,199, // Sorbet
};

#define NUM_COLOR_STEPS 11
uint16_t COLORS[NUM_COLOR_STEPS];

enum Mode {
  COLOR,
  GRADIENT,
  GRADIENT_ANIMATED,
  SPECTRUM,

  MODE_LENGTH // last value is the length of this enum
};

// Default mode and color values
#define defaultMode SPECTRUM
#define defaultColorHue 0
#define defaultGradientIndex 0

// Config options
uint8_t currentMode = COLOR;
uint8_t currentColorHue = 0;
uint8_t currentGradientIndex = 0;
bool on = true;

uint8_t i, j;
uint8_t spectrum[16];

uint16_t cycle = 0;
uint8_t cycleCounter = 0;

uint8_t brightness = MAX_BRIGHTNESS;
uint8_t currentBrightness = 0;
int brightnessSpeed = -1;
uint8_t FADE_SPEED = 6;
bool firstUpdate = true;

// dumb hack to make sure that brightness fading works alright - the "clicks" value is being reset, so this keeps track of it
int lastClick = 0;

uint16_t avgDiff = SPECTRUM_THRESHOLD; // Instantly start spectrum 

unsigned long startTime, timeDiff, motionTime;

void setup() {
  if (SERIAL_DEBUG) Serial.begin(19200);
  if (SERIAL_DEBUG) Serial.println("Starting");
  
  TinyMegaI2C.init();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOTION_PIN, INPUT);

  attachInterrupt(MOTION_PIN, handleMotion, RISING);
  motionTime = millis();
  
  for (i = 0; i < 16; i++) {
    spectrum[i] = SPECTRUM_MIN;
  }
  
  COLORS[NUM_COLOR_STEPS - 1] = 0;
  for (i = 0; i < NUM_COLOR_STEPS - 1; i++) {
    COLORS[i] = map(i, 0, NUM_COLOR_STEPS - 1, 0, 65535);
  }

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
  if (currentMode == 255) currentMode = defaultMode;
  
  EEPROM.get(EEPROM_COLOR_ADDRESS, currentColorHue);
  if (currentColorHue == 255) currentColorHue = defaultColorHue;
  
  EEPROM.get(EEPROM_GRADIENT_ADDRESS, currentGradientIndex);
  if (currentGradientIndex == 255) currentGradientIndex = defaultGradientIndex;
  
  EEPROM.get(EEPROM_BRIGHTNESS_ADDRESS, brightness);
  if (brightness > MAX_BRIGHTNESS) brightness = MAX_BRIGHTNESS;
  
  _PROTECTED_WRITE(WDT.CTRLA,WDT_PERIOD_512CLK_gc); //enable the WDT, with a 0.512s timeout (WARNING: delay > 0.5 will reset)
}

void updateSpectrum() {
  float amp = 15;

  TinyMegaI2C.start(8, 16 + 3 * 2);

  int average = 0;
  uint8_t values[16];

  for (i = 0; i < 16; i++) {
    uint8_t rawValue = TinyMegaI2C.read();
    values[i] = rawValue;

    average += rawValue;
  }

  uint16_t avgValue = TinyMegaI2C.read() << 8;
  avgValue |= TinyMegaI2C.read();

  uint16_t minSample = TinyMegaI2C.read() << 8;
  minSample |= TinyMegaI2C.read();

  uint16_t maxSample = TinyMegaI2C.read() << 8;
  maxSample |= TinyMegaI2C.read();

  uint16_t diff = maxSample - minSample;

  float ratio = 0.005;
  avgDiff = (1 - ratio) * avgDiff + ratio * diff;

  if (SERIAL_DEBUG) Serial.println("avgDiff:" + String(avgDiff) + ",min:" + String(minSample) + ",max:" + String(maxSample) + ",diff:" + String(diff) + ",thresh:" + String(SPECTRUM_THRESHOLD));

  float mixFactor = 0.1;
  
  for (i = 0; i < 16; i++) {
    uint8_t rawValue = values[i];
    uint8_t final = map(rawValue, 1, 10, SPECTRUM_MIN, 255);

    spectrum[i] = max(SPECTRUM_MIN, mixFactor * final + (1 - mixFactor) * spectrum[i]);

    if (SERIAL_DEBUG) {
//      Serial.print(String(i) + ", " + String(rawValue) + ", " + String(final) + ", " + String(spectrum[i]) + "\t");
    }
  }

  if (SERIAL_DEBUG) Serial.println();

  TinyMegaI2C.stop();
}

void handleMotion() {
  motionTime = millis();
}

void handlePower() {
  if (powerButton.clicks == 1) {
    on = !on;
    lastClick = 1;
    motionTime = millis();
  }
    
  if (powerButton.clicks < 0 || (powerButton.depressed == true && lastClick < 0)) {
    motionTime = millis();
    
    if (powerButton.clicks < 0) lastClick = powerButton.clicks;
    
    brightness = constrain(brightness + brightnessSpeed, 0, MAX_BRIGHTNESS);
    if (brightness == 0 || brightness == MAX_BRIGHTNESS) brightnessSpeed *= -1;
  } else {
    // Only write to EEPROM when done holding down button to save writes
    uint8_t tmpBrightness;
    EEPROM.get(EEPROM_BRIGHTNESS_ADDRESS, tmpBrightness);

    if (brightness != tmpBrightness) {
      EEPROM.put(EEPROM_BRIGHTNESS_ADDRESS, brightness);
    }

    // Exit held state    
    lastClick = 0;
  }
}

void handleMode() {
  if (modeButton.clicks == 1) {
    on = true;
    motionTime = millis();
    
    cycle = 0;
    currentMode = (currentMode + 1) % MODE_LENGTH;
    
    EEPROM.put(EEPROM_MODE_ADDRESS, currentMode);
  }
}

void handleColor() {
  if (colorButton.clicks == 1) {
    on = true;
    motionTime = millis();
  
    if (currentMode == COLOR) {
      currentColorHue = (currentColorHue + 1) % NUM_COLOR_STEPS;
      EEPROM.put(EEPROM_COLOR_ADDRESS, currentColorHue);
    } else {
      currentGradientIndex = (currentGradientIndex + GRADIENT_SIZE * 2) % (sizeof(GRADIENTS) / 2);
      EEPROM.put(EEPROM_GRADIENT_ADDRESS, currentGradientIndex);
    }
  }
}

void checkButtons() {
  powerButton.Update();
  modeButton.Update();
  colorButton.Update();

  // Don't cause weird behavior on boot
  if (!firstUpdate) {
    handlePower();  
    handleMode();
    handleColor();
  }
  
  firstUpdate = false;
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
    uint8_t spectrumIndex = map(i, 0, NUM_PIXELS, 0, 14);

    if (avgDiff > SPECTRUM_THRESHOLD) {
      value = spectrum[spectrumIndex];
    } else {
      value = SPECTRUM_MIN;
    }
  }
  
  uint8_t iCycle = (i + cycle) % NUM_PIXELS;
  
  uint8_t iMapped = (iCycle * 100) / NUM_PIXELS;
  uint8_t gradIndex = (iMapped * (GRADIENT_SIZE - 1) / 100);
  uint8_t distance = (iMapped * (GRADIENT_SIZE - 1)) - (gradIndex * 100);

  hue = map(distance, 0, 100, pgm_read_word(GRADIENTS + currentGradientIndex + gradIndex * 2), pgm_read_word(GRADIENTS + currentGradientIndex + ((gradIndex + 1) * 2)));
  sat = map(distance, 0, 100, pgm_read_word(GRADIENTS + currentGradientIndex + gradIndex * 2 + 1), pgm_read_word(GRADIENTS + currentGradientIndex + ((gradIndex + 1) * 2 + 1)));
  
  leds.setPixelColor(i, leds.ColorHSV(hue, sat, value));
}

void updateLEDs() {
  if (currentMode == COLOR) {
    leds.fill(leds.ColorHSV(COLORS[currentColorHue], currentColorHue == NUM_COLOR_STEPS - 1 ? 0 : 255, 255));
  } else {
    if (currentMode == SPECTRUM) updateSpectrum();
    
    for (i = 0; i < NUM_PIXELS; i++) {
      updateColor(i);
    }
  }

  leds.show();

  bool off = !on || (millis() - motionTime > MOTION_TIMEOUT);

  if (!off && currentBrightness < brightness) currentBrightness = constrain(currentBrightness + FADE_SPEED, 0, MAX_BRIGHTNESS);
  if (off || currentBrightness > brightness) currentBrightness = constrain(currentBrightness - FADE_SPEED, 0, MAX_BRIGHTNESS);
  
  leds.setBrightness(min(MAX_BRIGHTNESS, currentBrightness));
}

void loop() {
  //  reset watchdog timer
  __asm__ __volatile__ ("wdr"::);

  uint8_t frameTime = currentMode == SPECTRUM ? 50 : 15;
  if (millis() - startTime >= frameTime) {
    startTime = millis();
  
    checkButtons();
    updateLEDs();
    updateCycle(); 
  }
}
