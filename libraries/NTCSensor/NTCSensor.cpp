/*
 * MIT License
 *
 * Original work Copyright (c) 2019 Adafruit
 * Modified work Copyright (c) 2019 aattww (https://github.com/aattww/)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * NTCSensor.cpp - Simple library for NTC thermistor in battery powered environments
 *
 * NTCSensor implements a simple NTC type temperature sensor. Current flow through 
 * thermistor is controlled by a pin to save battery power.
 *
 * Remember to set correct values for your thermistor and series resistor in the header file.
 *
 * Parameters:
 *   enablePin - Enable voltage divider and power on thermistor
 *               NTC_NO_ENABLE_PIN = Not in use, thermistor is always pulled up by external resistor to Vcc
 *               ARDUINO PIN       = Pin connected to high side of divider
 *   sensorPin - Thermistor
 *
 * Schematic (enablePin != NTC_NO_ENABLE_PIN):
 * [GND]---[NTC]---|---[SERIES_RESISTOR]---[enablePin]
 *                 |
 *            [sensorPin]
 *
 * Schematic (enablePin == NTC_NO_ENABLE_PIN):
 * [GND]---[NTC]---|---[SERIES_RESISTOR]---[Vcc]
 *                 |
 *            [sensorPin]
 */

/*
 * Version history
 * ---------------
 *
 * 1.2 2020-03-15 (CURRENT)
 *   - Fixed a possible division by zero error if sensor is missing.
 *
 * 1.1 2019-12-29
 *   - Fixed a bug when enablePin was not in use but placeholder pin was still controlled.
 *
 * 1.0 2019-12-26
 *   Initial public release
 */

#include "NTCSensor.h"

/*
 * Creates a new instance.
 *
 * enablePin: voltage divider enable pin (set to NTC_NO_ENABLE_PIN if not in use)
 * sensorPin: thermistor pin
 *
 * returns:   no
 */
NTCSensor::NTCSensor(uint8_t enablePin, uint8_t sensorPin) {
  _enablePin = enablePin;
  _sensorPin = sensorPin;
  _initialised = false;
}

/*
 * Initializes thermistor.
 *
 * This verifies that a thermistor is properly connected and usable.
 *
 * NOTE: Must be called before thermistor can be used.
 *
 * parameters: no
 *
 * returns:    true on success, false if thermistor was not found
 */
bool NTCSensor::init() {
  // Make sure Aref is on default
  analogReference(DEFAULT);
  analogRead(_sensorPin);
  
  // Go through different scenarios to determine if thermistor is connected properly
  
  // If thermistor does not have a separate enable pin (thermistor should always be powered)
  if (_enablePin == NTC_NO_ENABLE_PIN) {
    // Enable internal pullup resistor so that pin is not floating if there is no thermistor
    pinMode(_sensorPin, INPUT_PULLUP);
    delay(50);
    
    int value = analogRead(_sensorPin);
    
    // If raw value is between 400 and 923, there probably is NTC thermistor connected
    if ((value > 400) && (value < 923)) {
      _initialised = true;
    }
    else {
      _initialised = false;
    }
    
    // Reset pin mode to normal
    pinMode(_sensorPin, INPUT);
  }
  // Thermistor power is controlled by a pin
  else {
    // If raw value with enablePin as INPUT (so possible series resistor does not affect) is less than 20, there probably is thermistor connected (pulls pin to gnd)
    pinMode(_enablePin, INPUT);
    delay(50);
    int value = analogRead(_sensorPin);
    if (value < 20) {
      // Set enable pin back to output
      pinMode(_enablePin, OUTPUT);
      
      // Raw value between 200 and 823 would indicate connected series resistor and somewhat proper temperature
      digitalWrite(_enablePin, HIGH);
      delay(50);
      value = analogRead(_sensorPin);
      digitalWrite(_enablePin, LOW);
      if ((value > 200) && (value < 823)) {
        _initialised = true;
      }
      else {
        _initialised = false;
      }
    }
    else {
      _initialised = false;
    }
    
    // Make sure pin modes are correct
    pinMode(_enablePin, OUTPUT);
    digitalWrite(_enablePin, LOW);
  }
  
  return (_initialised);
}

/*
 * Reads current temperature in Celsius.
 *
 * parameters: no
 *
 * returns:    temperature (tenfold) OR
 *             -990 if current temperature could not be measured
 */
int16_t NTCSensor::readTemperature() {
  // If sensor is not initialised, break and return invalid value
  if (!_initialised) {
    return (-990);
  }
  
  // Make sure Aref is on default
  analogReference(DEFAULT);
  analogRead(_sensorPin);
  
  // Turn on analog voltage divider if it is in use
  if (_enablePin != NTC_NO_ENABLE_PIN) {
    digitalWrite(_enablePin, HIGH);
    delay(50);
  }
  
  float average = 0.0;
 
  // Take 5 samples in a row for averaging
  for (uint8_t i = 0; i < 5; i++) {
   average += analogRead(_sensorPin);
   delay(10);
  }
  
  // Turn off analog voltage if in use
  if (_enablePin != NTC_NO_ENABLE_PIN) {
    digitalWrite(_enablePin, LOW);
  }
 
  // Calculate average
  average /= 5.0;

  // In the unlikely event of measuring 0 resistance (sensor is shorted),
  // or 1023 (sensor is missing completely), return invalid value.
  // This also prevents division by zero error below.
  if ((average == 0.0) || (average == 1023.0)) {
    return (-990);
  }
  
  // Convert the value to resistance
  average = 1023.0 / average - 1;
  average = SERIES_RESISTOR / average;

  // Calculate temperature using B parameter version of Steinhart-Hart equation
  float steinhart;
  steinhart = average / NOMINAL_RESISTANCE;          // (R/Ro)
  steinhart = log(steinhart);                        // ln(R/Ro)
  steinhart /= BETA_COEFFICIENT;                     // 1/B * ln(R/Ro)
  steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15); // (1/To) + 1/B * ln(R/Ro)
  steinhart = 1.0 / steinhart;                       // Invert
  steinhart -= 273.15;                               // Convert to C

  return (round(steinhart * 10));
}
