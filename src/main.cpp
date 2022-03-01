#include <Arduino.h>
#include <FastLED.h>

#define DEBUG_TOUCH_SENSING false
// Testing with a logic analyzer shows that when the MCU is clocked at
// 8MHz a timeout value of 100000 ends up being about 182ms of wall time
#define TOUCH_SAMPLE_TIMEOUT 100000
// A touched sensing lead goes through a charge/discharge cycle in 17ms.
// Holding for several sequential samples to trigger the light sequence
// improves stability so my dogs don't trigger it by existing, but will
// be slower to respond when a person touches it.
#define SEQUENTIAL_TOUCH_SAMPLES_TO_TRIGGER 5

// ATTiny4313 physical pin 15
#define LED_DATA_PIN 12
#define LED_COUNT 5

// The LEDs I'm using take RGB values in GRB order
#define RGB_TO_GRB(r, g, b) \
  (((unsigned long)g << 16) | ((unsigned long)r << 8) | b)

// RGB values that might look good as shrine lighting
//  Orange = 0xFFA500
//  Harvest Gold = 0xCC8800
//  International Orange = 0xFF5500
#define SHRINE_ORANGE RGB_TO_GRB(0xFF, 0x55, 0x00)

CRGB leds[LED_COUNT];

//! Accumulate the amount of time it takes to charge and discharge
//! pin PB0 using pin PB1 through a 1MOhm resistor. This value will
//! change based on the capacitance of PB0 and so is used to tell if
//! it is being touched or not. Accumulator units are arbitrary, not
//! number of cycles or microseconds. Returns true if PB0 went through
//! a charge/discharge cycle before the value in accumulator exceeded
//! timeout, false otherwise.
static bool timeSensePinChargeDischarge(uint32_t& accumulator,
                                        uint32_t timeout) {
  // Setup conditions: PB1 low, PB0 in with no pullup
  DDRB |= 0x02;
  DDRB &= ~0x01;
  PORTB &= ~0x03;

#if DEBUG_TOUCH_SENSING
  // PB2 used for framing the entire function
  // PB4 used for measuring charge/discharge times
  DDRB |= 0x14;
  PORTB |= 0x04;
  PORTB &= ~0x10;
#endif

  cli();

  // Set PB1 low, delay to let PB0 discharge if necessary, then set PB1 high
  PORTB &= ~0x02;
  DDRB &= ~0x01;
  DDRB |= 0x01;
  PORTB &= ~0x01;
  delayMicroseconds(10);
  DDRB &= ~0x01;
  PORTB |= 0x02;

#if DEBUG_TOUCH_SENSING
  PORTB |= 0x10;
#endif

  // Count up while the voltage at PB0 is rising
  while ((PINB & 0x01) == 0 && accumulator < timeout) {
    accumulator++;
  }

#if DEBUG_TOUCH_SENSING
  PORTB &= ~0x10;
#endif

  // PB0 should be right around the Schmitt trigger so set it to output high and
  // delay to let it fully charge, then set it back to input and drive PB1 low.
  DDRB |= 0x01;
  PORTB |= 0x01;
  delayMicroseconds(10);
  DDRB &= ~0x01;
  PORTB &= ~0x03;

  PORTB |= 0x10;

  // Count up while the voltage at PB0 is falling
  while ((PINB & 0x01) != 0 && accumulator < timeout) {
    accumulator++;
  }

#if DEBUG_TOUCH_SENSING
  PORTB &= ~0x14;
#endif

  sei();

  return accumulator < timeout;
}

//! Return true when the pedestal has sensed a touch. This function does
//! not return until touched - not much else to do if it hasn't been.
bool waitForPedestalTouch() {
  uint8_t sequentialTouches = 0;
  while (sequentialTouches < SEQUENTIAL_TOUCH_SAMPLES_TO_TRIGGER) {
    uint32_t totalCounts = 0;
    timeSensePinChargeDischarge(totalCounts, TOUCH_SAMPLE_TIMEOUT);
    if (totalCounts < TOUCH_SAMPLE_TIMEOUT / 5) {
      sequentialTouches++;
    } else {
      sequentialTouches = 0;
    }
  }
  return true;
}

enum LightState {
  Inactive,
  OrangeSet,
  FadeOut,
  BetweenFades,
  FadeIn,
  BlueSet,
  IdleUntilTouchFinished
};
LightState lightState;

void setup() {
  FastLED.addLeds<WS2812B, LED_DATA_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(100);
  fill_solid(&(leds[0]), LED_COUNT, CRGB::Black);
  FastLED.show();
}

void loop() {
  static CEveryNMillis fadeTimer(20);
  static CRGB fadeInColor;
  for (;;) {
    switch (lightState) {
      case LightState::Inactive:
        waitForPedestalTouch();
        FastLED.setBrightness(100);
        fill_solid(leds, LED_COUNT, SHRINE_ORANGE);
        FastLED.show();
        lightState = LightState::OrangeSet;
        break;
      case LightState::OrangeSet:
        FastLED.delay(1000);
        fadeTimer.reset();
        lightState = LightState::FadeOut;
        break;
      case LightState::FadeOut:
        if (fadeTimer.ready()) {
          fadeToBlackBy(leds, LED_COUNT, 20);
          FastLED.show();
          if ((leds[0].r == 0 || leds[0].g == 0) && leds[0].b == 0) {
            lightState = LightState::BetweenFades;
          }
        }
        break;
      case LightState::BetweenFades:
        fill_solid(leds, LED_COUNT, CRGB::Black);
        FastLED.delay(250);
        fadeTimer.reset();
        fadeInColor = CRGB::Black;
        lightState = LightState::FadeIn;
        break;
      case LightState::FadeIn:
        if (fadeTimer.ready()) {
          constexpr uint8_t step = 20;
          fadeInColor.b += step;
          fill_solid(leds, LED_COUNT, fadeInColor);
          FastLED.show();
          if (leds[0].b >= (255 - step)) {
            fill_solid(leds, LED_COUNT, CRGB::Blue);
            FastLED.show();
            lightState = LightState::BlueSet;
          }
        }
        break;
      case LightState::BlueSet:
        FastLED.delay(5000);
        fill_solid(leds, LED_COUNT, CRGB::Black);
        FastLED.show();
        lightState = LightState::IdleUntilTouchFinished;
        break;
      case LightState::IdleUntilTouchFinished:
        lightState = LightState::Inactive;
        break;
    }
  }
}
