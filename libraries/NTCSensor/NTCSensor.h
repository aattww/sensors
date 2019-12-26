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
 * NTCSensor.h - Simple library for NTC thermistor in battery powered environments
 *
 * NTCSensor implements a simple NTC type temperature sensor. Current flow through 
 * thermistor is controlled by a pin to save battery power.
 *
 * Remember to set correct values for your thermistor and series resistor in the header file.
 *
 * Parameters:
 *   enablePin - Enable voltage divider and power on thermistor
 *               255   = Not in use, thermistor is always pulled up by external resistor to Vcc
 *              !=255  = Pin connected to high side of divider
 *   sensorPin - Thermistor
 *
 * Schematic (enablePin != 255):
 * [GND]---[NTC]---|---[SERIES_RESISTOR]---[enablePin]
 *                 |
 *            [sensorPin]
 *
 * Schematic (enablePin == 255):
 * [GND]---[NTC]---|---[SERIES_RESISTOR]---[Vcc]
 *                 |
 *            [sensorPin]
 */

#ifndef NTCSENSOR_H
#define NTCSENSOR_H

#include "Arduino.h"

// Resistance at 25 C
#define NOMINAL_RESISTANCE 10000.0

// Temperature for nominal resistance
#define NOMINAL_TEMPERATURE 25.0

// The beta coefficient of the thermistor
#define BETA_COEFFICIENT 3380.0

// The value of the series resistor
#define SERIES_RESISTOR 10000.0

class NTCSensor {
  
  private:
  
    uint8_t _enablePin;  // Voltage divider enable pin (255 if not in use)
    uint8_t _sensorPin;  // Thermistor pin
    bool _initialised;   // Sensor has been initialized

  public:
  
    /*
     * Creates a new instance.
     *
     * enablePin: voltage divider enable pin (set to 255 if not in use)
     * sensorPin: thermistor pin
     *
     * returns:   no
     */
    NTCSensor(uint8_t enablePin, uint8_t sensorPin);
    
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
    bool init();
    
    /*
     * Reads current temperature.
     *
     * parameters: no
     *
     * returns:    temperature (tenfold) OR
     *             -990 if current temperature could not be measured
     */
    int16_t readTemperature();
};

#endif
