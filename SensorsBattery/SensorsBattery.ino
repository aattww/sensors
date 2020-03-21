/*
 * Copyright (c) 2020 aattww (https://github.com/aattww/)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * ######################
 * ### BEGIN SETTINGS ###
 * ######################
 */

/*
 * ### ENCRYPTION ###
 * 
 * Define whether node should use encryption. To disable encryption, comment out ENCRYPT_KEY.
 * Key must be exactly 16 characters long.
 */
#define ENCRYPT_KEY   "sample16CharKey_"

/*
 * ### LOW RATE ###
 *
 * Define whether node should use low rate transmits.
 * Low rate transmits have better range, but also increase time on air and battery usage.
 */
//#define ENABLE_LOW_RATE

/*
 * ### FREQUENCY ###
 *
 * Define radio transmit frequency in MHz.
 * Usable frequency depends on module in use and legislation.
 */
#define FREQUENCY     867.6

/*
 * ### RETRIES ###
 *
 * Define how many times to resend message if first transmit fails.
 * After all retries have been exhausted, new transmit is tried after sleep time.
 *
 * Limited to 0-4.
 */
#define RETRIES       1

/*
 * ### TIMINGS ###
 *
 * Define timings.
 *
 * SLEEP_TIME: How long to sleep between wake ups in seconds.
 * FORCE_SEND: How often at least should a packet be transmitted regardless of threshold in minutes.
 */
#define SLEEP_TIME    600
#define FORCE_SEND    30

/*
 * ### THRESHOLDS ###
 *
 * In threshold mode node sends new values to gateway only if they differ more than threshold from previously sent values,
 * that is: abs(new_value - previous_value) > threshold
 *
 * Note that thresholds are in tenths of value, i.e. 10 = 1.0 °C.
 * You should always set either all to zero or all to a certain value. If one threshold is zero, that will trigger send every time node wakes up (=SLEEP_TIME).
 * If you don't want certain value to affect sends, set that threshold to some large value.
 *
 * TEMPERATURE_TH: Threshold of temperature in tenths of °C (0 = send always)
 * HUMIDITY_TH: Threshold of temperature in tenths of %RH (0 = send always)
 * PRESSURE_TH: Threshold of temperature in tenths of hPa (0 = send always)
 */
#define TEMPERATURE_TH  5
#define HUMIDITY_TH     30
#define PRESSURE_TH     10

/*
 * ####################
 * ### END SETTINGS ###
 * ####################
 */


#define VERSION 2

#include <RH_RF95.h>
#include <RHReliableDatagram.h>

#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>

#ifdef ENCRYPT_KEY
#include <RHEncryptedDriver.h>
#include <Speck.h>
#endif

#include <Wire.h>
#include <SI7021.h>
#include <SparkFunBME280.h>
#include <NTCSensor.h>

#define MODE_NO_SENSOR  0
#define MODE_SI7021     1
#define MODE_BME280     2
#define MODE_NTC        4

#define LED_PIN         8
#define NTC_ENABLE_PIN  0
#define NTC_PIN         A2
#define BTN_PIN         3
#define JMP_PIN         9

#define PAYLOAD_LEN     11
#define GATEWAYID       254
#define TX_MAX_PWR      23
#define TX_MIN_PWR      5

const float frequency = FREQUENCY; // Radio transmit frequency (depends on module in use and legislation)
#ifdef ENCRYPT_KEY
const uint8_t encryptKey[17] = ENCRYPT_KEY; // Encryption key for communication
#endif
uint8_t maxNrOfSends = RETRIES + 1; // Maximum amount of transmits after which to give in and try again after sleepTime (1-5)
uint16_t sleepTime = SLEEP_TIME; // Sleep time between wake ups in seconds
const uint16_t forceTransmitInterval = FORCE_SEND; // How often at least should a packet be transmitted regardless of threshold (minutes)

uint16_t sensor1Threshold = TEMPERATURE_TH; // Threshold for sending sensor1 in tenths of degree C (0 = send always)
uint16_t sensor2Threshold = HUMIDITY_TH; // Threshold for sending sensor2 in tenths of RH% (0 = send always)
uint16_t sensor3Threshold = PRESSURE_TH; // Threshold for sending sensor3 in tenths of hPa (0 = send always)

// Radio and encryption instances
RH_RF95 rf95Driver;
#ifdef ENCRYPT_KEY
Speck cipherDriver;
RHEncryptedDriver encryptedDriver(rf95Driver, cipherDriver);
RHReliableDatagram radioManager(encryptedDriver);
#else
RHReliableDatagram radioManager(rf95Driver);
#endif

SI7021 sensorSI7021; // SI7021 instance
BME280 sensorBME280; // BME280 instance
NTCSensor sensorNTC(NTC_ENABLE_PIN, NTC_PIN); // NTC instance

// Payload buffer
// Header | Battery_MSB | Battery_LSB | TransmitPower | TransmitInterval | Sensor1_MSB | Sensor1_LSB | Sensor2_MSB | Sensor2_LSB | Sensor3_MSB | Sensor3_LSB
uint8_t payloadBuffer[PAYLOAD_LEN];

uint16_t neededSleepCycles; // How many 8 second sleep cycles are needed for full sleep time
uint8_t transmitInterval; // How often should gateway expect transmit (in minutes)
uint16_t lastTransmittedCycles; // Sleep cycles from last transmitted packet
bool previousTransmitOk = false; // Was previous transmit successful
bool hasFailedTransmit = false; // Is there at least one failed transmit (for decreasing transmit power faster after boot up)
bool isImportant = false; // When node is marked important, it triggers gateway to set external interrupt

bool isDebugMode = false;
bool isThresholdMode = false;
volatile bool forceSend = false;
uint16_t neededForceCycles; // How many sleep cycles at max between force transmits
uint8_t nodeId; // Node ID
uint8_t sensorMode = MODE_NO_SENSOR;

int16_t sensor1LastTransmittedValue = 0; // Last transmitted sensor1 value
int16_t sensor2LastTransmittedValue = 0; // Last transmitted sensor2 value
int16_t sensor3LastTransmittedValue = 0; // Last transmitted sensor3 value
int16_t sensor1Value = 0;
int16_t sensor2Value = 0;
int16_t sensor3Value = 0;

uint16_t batteryVoltage = 0;
uint8_t transmitPower = (TX_MIN_PWR + TX_MAX_PWR) / 4; // Set initial transmit power to low medium
uint8_t transmitPowerRaw = 25;


void setup() {
  // LED
  pinMode(LED_PIN, OUTPUT);
  
  // Button
  pinMode(BTN_PIN, INPUT_PULLUP);
  
  // Jumper J1
  pinMode(JMP_PIN, INPUT_PULLUP);
  
  // Let inputs stabilize
  delay(10);
  
  // Enter programming mode (currently does nothing) if button is pressed. Also set debug mode.
  if (!digitalRead(BTN_PIN)) {
    enterProgMode();
    isDebugMode = true;
  }
  
  // Set node important if jumper is set
  if (!digitalRead(JMP_PIN)) {
    isImportant = true;
  }
  
  // Set pin to hi-Z to save power (otherwise jumper leaks current)
  pinMode(JMP_PIN, INPUT);
  
  // Blink led to indicate startup and fw version
  startUp();

  // Determine which sensor is connected
  // Possible options are SI7021, BME280, NTC or SI7021+NTC
  
  // Change expected BME280 address (common breakout boards use this)
  sensorBME280.setI2CAddress(0x76);
  
  // Try to initialize SI7021 sensor
  if (sensorSI7021.begin()) {
    sensorMode = MODE_SI7021;
  }
  // Try to initialize BME280 sensor if did not find SI7021
  else if (sensorBME280.beginI2C()) {
    sensorMode = MODE_BME280;
    sensorBME280.setMode(MODE_SLEEP);
  }
  // If not having BME280, try to also initialize NTC
  if ((sensorMode != MODE_BME280) && sensorNTC.init()) {
    sensorMode |= MODE_NTC;
    
    // If having SI7021+NTC, change sensor3 (now NTC) threshold to temperature instead of pressure
    if (sensorMode == (MODE_SI7021 | MODE_NTC)) {
      sensor3Threshold = TEMPERATURE_TH;
    }
  }
  
  // No valid sensor found, enter error blinking
  if (sensorMode == MODE_NO_SENSOR) {
    blinkLed(3, true);
  }
  
  // Calculate proper timings
  setTimings();

  readIds();
  
  // Initialize radio
  #ifdef ENCRYPT_KEY
  cipherDriver.setKey(encryptKey, sizeof(encryptKey)-1); // Discard null character at the end
  #endif
  radioManager.setThisAddress(nodeId);
  radioManager.setRetries(0);
  
  // If failed to init radio, start blinking led
  if (!radioManager.init()) {
    blinkLed(5, true);
  }
  
  rf95Driver.setFrequency(frequency);
  rf95Driver.setTxPower(transmitPower); // 5-23 dBm
  #ifdef ENABLE_LOW_RATE
  rf95Driver.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  radioManager.setTimeout(3500);
  #endif

  // Set button interrupt
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), wakeUpFromBtn, FALLING);
  
  // Set force send so that message is sent after startup and led blinked
  forceSend = true;
}

void loop() {

  // Read sensor values
  if (sensorMode & MODE_SI7021) {
    readSI7021();
  }
  else if (sensorMode & MODE_BME280) {
    readBME280();
  }
  if (sensorMode & MODE_NTC) {
    readNTC();
  }

  
  // If send threshold mode is active and send is not forced...
  if (isThresholdMode && !forceSend) {
    
    // If it has been too long time from last transmit ...
    if (lastTransmittedCycles >= neededForceCycles) {
      constructAndSendPacket();
    }
    // ... or sensor value differs enough from last sent value
    else if ((abs(sensor1LastTransmittedValue - sensor1Value) > sensor1Threshold) || 
             (abs(sensor2LastTransmittedValue - sensor2Value) > sensor2Threshold) || 
             (abs(sensor3LastTransmittedValue - sensor3Value) > sensor3Threshold)) {
      constructAndSendPacket();
    }
  }
  // ... else always send
  else {
    constructAndSendPacket();
  }
  
  sleepNode();
}

bool constructAndSendPacket() {
  // Read battery voltage
  readBatteryVoltage();
  
  // Construct payload
  
  if (sensorMode == MODE_SI7021) {
    payloadBuffer[0] = B00010001;
  }
  else if (sensorMode == MODE_BME280) {
    payloadBuffer[0] = B00010100;
  }
  else if (sensorMode == MODE_NTC) {
    payloadBuffer[0] = B00010101;
  }
  else if (sensorMode == (MODE_SI7021 | MODE_NTC)) {
    payloadBuffer[0] = B00010110;
  }
  
  if (isImportant) {
    payloadBuffer[0] |= B00100000;
  }
  
  payloadBuffer[1] = batteryVoltage >> 8;
  payloadBuffer[2] = batteryVoltage;
  payloadBuffer[3] = transmitPowerRaw;
  payloadBuffer[4] = transmitInterval;
  payloadBuffer[5] = sensor1Value >> 8;
  payloadBuffer[6] = sensor1Value;
  payloadBuffer[7] = sensor2Value >> 8;
  payloadBuffer[8] = sensor2Value;
  payloadBuffer[9] = sensor3Value >> 8;
  payloadBuffer[10] = sensor3Value;

  // If in force mode, set maximum transmit power
  if (forceSend) {
    rf95Driver.setTxPower(TX_MAX_PWR);
    payloadBuffer[3] = 100;
  }
  
  bool transmitOk = false;
  for (uint8_t i = 0; i < maxNrOfSends; i++) {
    // Send packet
    transmitOk = radioManager.sendtoWait(payloadBuffer, PAYLOAD_LEN, GATEWAYID);
    
    // Update transmit power based on transmit result if not in force send (force sends are always sent at full power and don't count for APC)
    if (!forceSend) {
      updateTransmitPower(transmitOk);
      previousTransmitOk = transmitOk;
    }
    
    // If transmit was successful, reset and break from retransmit loop
    if (transmitOk) {
      lastTransmittedCycles = 0;
      sensor1LastTransmittedValue = sensor1Value;
      sensor2LastTransmittedValue = sensor2Value;
      sensor3LastTransmittedValue = sensor3Value;
      break;
    }
  }
  
  // Return to normal transmit power after force send
  if (forceSend) {
    rf95Driver.setTxPower(transmitPower);
    payloadBuffer[3] = transmitPowerRaw;
  }
  
  if (isDebugMode || forceSend) {
    if (transmitOk) {
      blinkLed(1, false);
    }
    else {
      blinkLed(2, false);
    }
  }
  
  forceSend = false;
  
  return transmitOk;
}

void updateTransmitPower(bool lastTransmitOk) {
  uint8_t subConstant = 1;
  uint8_t addConstant = 10;
  
  if (lastTransmitOk) {
    // If there are no failed transmits yet, decrease power faster (to speed up power decrease after boot up)
    if (!hasFailedTransmit) {
      subConstant = 5;
    }
    if ((transmitPowerRaw - subConstant) < 0) {
      transmitPowerRaw = 0;
    }
    else {
      transmitPowerRaw = transmitPowerRaw - subConstant;
    }
  }
  else {
    hasFailedTransmit = true;
    
    // If this was the first failed transmit, increase power only by half
    if (previousTransmitOk) {
      addConstant = addConstant / 2;
    }
    if ((transmitPowerRaw + addConstant) > 100) {
      transmitPowerRaw = 100;
    }
    else {
      transmitPowerRaw = transmitPowerRaw + addConstant;
    }
  }
  
  // Map raw transmit power to real usable range
  transmitPower = map(transmitPowerRaw, 0, 100, TX_MIN_PWR, TX_MAX_PWR);
  
  // Set radio transmit power
  rf95Driver.setTxPower(transmitPower);
  
  payloadBuffer[3] = transmitPowerRaw;
}

void sleepNode() {
  
  // Put radio to sleep
  #ifdef ENCRYPT_KEY
  encryptedDriver.sleep();
  #else
  rf95Driver.sleep();
  #endif
  
  // Make sure force send flag is cleared
  forceSend = false;
  
  // Sleep uC for sufficient number of 8 second sleep cycles
  for(uint16_t i = 0; i < neededSleepCycles; i++) {
    sleepMCU();
    lastTransmittedCycles++;
    
    // If button pressed, fall through to main loop
    if (forceSend) {
      return;
    }
  }
}

void sleepMCU() {
  // Disable ADC
  ADCSRA &= ~_BV(ADEN);

  // Set watchdog timer to 8s and interrupt only mode
  wdt_enable(WDTO_8S);
  WDTCSR |= _BV(WDIE);
  
  // Initiate actual sleep
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  sleep_disable();
  sei();

  // Restore ADC
  ADCSRA |= _BV(ADEN);
}

// Handle watchdog interrupt
ISR (WDT_vect) {
  // Disable watchdog for now
  wdt_disable();
}

void wakeUpFromBtn() {
  forceSend = true;
}

void setTimings() {
  // Change sleepTime to one sleep cycle if in debug mode, else use provided value
  if (isDebugMode) {
    sleepTime = 8;
  }
  
  // Calculate needed 8 second sleep cycles for full sleepTime
  neededSleepCycles = round(sleepTime / 8.0);
  if (neededSleepCycles < 1) {
    neededSleepCycles = 1;
  }
  
  // Check if we are in threshold mode
  isThresholdMode = (((sensor1Threshold > 0) || (sensor2Threshold > 0) || (sensor3Threshold > 0)) && !isDebugMode);

  // Calculate advertised transmit interval
  if (isThresholdMode) {
    transmitInterval = forceTransmitInterval;
  }
  else {
    transmitInterval = ceil(sleepTime / 60.0);
  }
  
  // Calculate needed cycles for forced transmit in threshold mode
  neededForceCycles = round(forceTransmitInterval*60 / 8.0);
  
  // Constrain maximum number of transmits value
  if (maxNrOfSends < 1) {
    maxNrOfSends = 1;
  }
  else if (maxNrOfSends > 5) {
    maxNrOfSends = 5;
  }
  
  lastTransmittedCycles = 0;
  
}

void readIds() {
  // Change correct pinmodes
  pinMode(1, INPUT_PULLUP);
  pinMode(A1, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  pinMode(A3, INPUT_PULLUP);
  
  delay(5);
  
  // Read node ID
  nodeId = 0;
  bitWrite(nodeId, 0, !digitalRead(1));
  bitWrite(nodeId, 1, !digitalRead(A1));
  bitWrite(nodeId, 2, !digitalRead(6));
  bitWrite(nodeId, 3, !digitalRead(5));
  bitWrite(nodeId, 4, !digitalRead(4));
  bitWrite(nodeId, 5, !digitalRead(A3));
  
  // Change pinmodes back to INPUT to save power
  pinMode(1, INPUT);
  pinMode(A1, INPUT);
  pinMode(6, INPUT);
  pinMode(5, INPUT);
  pinMode(4, INPUT);
  pinMode(A3, INPUT);
  
  // Check for correct ids
  if ((nodeId >= 1) && (nodeId < 254)) {
    // All good
    return;
  }
  else {
    // Wrong ids set, enter error mode and start blinking led
    blinkLed(1, true);
  }
}

void readSI7021() {
  si7021_env data = sensorSI7021.getHumidityAndTemperature();
  sensor1Value = round(data.celsiusHundredths / 10.0);
  sensor2Value = round(data.humidityBasisPoints / 10.0);
}

void readBME280() {
  sensorBME280.setMode(MODE_FORCED); // Wake BME280 and start measurement
  delay(8); // According to datasheet, typical temp, hum and pressure conversion is 8ms
  
  uint32_t timeout = millis();
  while (sensorBME280.isMeasuring() && ((millis() - timeout) < 100)) { // Wait more (with timeout) if conversion is still in progress
    delay(1);
  }

  sensor1Value = round(sensorBME280.readTempC() * 10.0);
  sensor2Value = round(sensorBME280.readFloatHumidity() * 10.0);
  sensor3Value = round(sensorBME280.readFloatPressure() / 10.0);
}

void readNTC() {
  if (sensorMode == MODE_NTC) {
    sensor1Value = sensorNTC.readTemperature();
  }
  else if (sensorMode == (MODE_SI7021 | MODE_NTC)) {
    sensor3Value = sensorNTC.readTemperature();
  }
}

void readBatteryVoltage() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  
 
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring
 
  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both
 
  uint16_t result = 1125300L / ((high<<8) | low); // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  
  if (batteryVoltage == 0)
    batteryVoltage = result;
  else
    batteryVoltage = (batteryVoltage * 3 + result) / 4; // Apply IIR filter to smooth voltage spikes
}

void blinkLed(uint8_t blinks, bool loop) {
  do {
    for (uint8_t i = 0; i < blinks; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      if (i != (blinks - 1)) {
        delay(200);
      }
    }
    if (loop) {
      delay(2200);
    }
  } while (loop);
}

void enterProgMode() {
  return;
}

void startUp() {
  digitalWrite(LED_PIN, HIGH);
  if (VERSION & B00001000) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  delay(200);
  
  digitalWrite(LED_PIN, HIGH);
  if (VERSION & B00000100) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  delay(200);
  
  digitalWrite(LED_PIN, HIGH);
  if (VERSION & B00000010) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  delay(200);
  
  digitalWrite(LED_PIN, HIGH);
  if (VERSION & B00000001) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  
  delay(1000);
}
