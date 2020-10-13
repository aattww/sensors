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
 * ### NODE TYPE ###
 *
 * Define type of the node: Multical or pulse
 */
//#define NODE_TYPE_MULTICAL
#define NODE_TYPE_PULSE

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
 * Applies only if SEND_AND_FORGET is not defined.
 *
 * Limited to 0-4.
 */
#define RETRIES       1

/*
 * ### SEND AND FORGET ###
 *
 * Define whether node should send reliable or unreliable messages.
 * If SEND_AND_FORGET is defined, node sends a message every SEND_INTERVAL without waiting for acknowledgement.
 * If SEND_AND_FORGET is undefined, node waits for ack and, if necessary, retries transmission.
 */
#define SEND_AND_FORGET

/*
 * ### TARGET RSSI ###
 *
 * Define target RSSI for automatic power control (APC).
 * Lower value will make nodes send messages with less power, saving energy, but at the cost of possibly
 * causing more retransmits.
 */
#define TARGET_RSSI   -80 

/*
 * ### SEND INTERVAL ###
 *
 * Define how often to send message in seconds.
 */
#define SEND_INTERVAL   600

/*
 * ### MULTICAL 602/603 Modbus slave address ###
 *
 * Define the address of connected Multical 602/603 energy meter.
 * Applicable only if node type is NODE_TYPE_MULTICAL.
 */
#define MULTICAL_SLAVE_ADDRESS  15

/*
 * ####################
 * ### END SETTINGS ###
 * ####################
 */


#define VERSION 5

#if defined NODE_TYPE_MULTICAL
#include <SimpleModbusAsync.h>
#endif

#include <RH_RF95.h>
#include <RHDatagram.h>
#include <EEPROM.h>
#include <NTCSensor.h>

#ifdef ENCRYPT_KEY
#include <RHEncryptedDriver.h>
#include <Speck.h>
#endif

#define LED_PIN         A0
#define BTN_PIN         3
#define P1_PIN          6
#define P2_PIN          7
#define P3_PIN          A1
#define MAX_DE_PIN      8
#define JMP_PIN         A3

#if defined NODE_TYPE_MULTICAL
#define PAYLOAD_LEN     39
#elif defined NODE_TYPE_PULSE
#define PAYLOAD_LEN     15
#endif

#define GATEWAYID       254
#define TX_MAX_PWR      20
#define TX_MIN_PWR      2
#define PULSE_MIN       1000
#define EEPROM_SAVE     3600000

/* ### SETTINGS ### */
const float frequency = FREQUENCY; // Radio transmit frequency (depends on module in use and legislation)
#ifdef ENCRYPT_KEY
const uint8_t encryptKey[17] = ENCRYPT_KEY; // Encryption key for communication
#endif
uint8_t maxNrOfSends = RETRIES + 1; // Maximum amount of transmits after which to give in and try again after sleepTime (1-5)
uint16_t sendInterval = SEND_INTERVAL; // How often to send message in seconds
/* ### END SETTINGS ### */

// Radio and encryption instances
RH_RF95 rf95Driver;
#ifdef ENCRYPT_KEY
Speck cipherDriver;
RHEncryptedDriver encryptedDriver(rf95Driver, cipherDriver);
RHDatagram radioManager(encryptedDriver);
#else
RHDatagram radioManager(rf95Driver);
#endif

#if defined NODE_TYPE_MULTICAL
SimpleModbusAsync modbus;
#endif

NTCSensor sensorNTC(NTC_NO_ENABLE_PIN, P3_PIN);

// Payload buffer
uint8_t payloadBuffer[PAYLOAD_LEN];

uint32_t lastTransmittedMillis = 0; // Milliseconds from last transmitted packet
uint8_t transmitInterval; // How often should gateway expect transmit (in minutes)

bool previousTransmitOk = false; // Was previous transmit successful
bool hasFailedTransmit = false; // Is there at least one failed transmit (for decreasing transmit power faster after boot up)
bool isImportant = false; // When node is marked important, it triggers gateway to set external interrupt
bool isDebugMode = false;
volatile bool forceSend = false;
uint8_t nodeId; // Node ID

int8_t transmitPower = ((TX_MAX_PWR - TX_MIN_PWR) / 4) + TX_MIN_PWR; // Set initial transmit power to low medium
uint8_t transmitPowerRaw = 25;
int8_t lastReportedRSSI;

volatile uint32_t pulse1 = 0;
volatile uint32_t pulse2 = 0;
volatile uint32_t pulse3 = 0;

#if defined NODE_TYPE_MULTICAL
uint32_t energy = 0;
uint32_t volume = 0;
uint32_t power = 0;
uint32_t flow = 0;
uint32_t inletTemp = 0;
uint32_t outletTemp = 0;
#endif

bool hasNTC;

volatile uint32_t pulse1LastFall = 0;
volatile uint32_t pulse2LastFall = 0;
volatile uint32_t pulse3LastFall = 0;
volatile bool pulse1LastValue = true;
volatile bool pulse2LastValue = true;
volatile bool pulse3LastValue = true;
uint32_t lastSaveToEEPROM = 0;
#if defined NODE_TYPE_MULTICAL
uint32_t lastModbusRead = 0;
#endif

void setup() {
  // Check if pulse 3 is NTC
  hasNTC = sensorNTC.init();

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
  
  // Blink led to indicate startup and fw version
  startUp();
  
  // Pulse inputs
  pinMode(P1_PIN, INPUT_PULLUP);
  pinMode(P2_PIN, INPUT_PULLUP);
  if (!hasNTC) {
    pinMode(P3_PIN, INPUT_PULLUP);
  }
  
  // Delay so that if button was pressed for debug mode but EEPROM is not to be cleared,
  // user has some time to release the button.
  delay(2000);
  
  // If jumper is set and button pressed, clear pulse values from EEPROM.
  if (!digitalRead(JMP_PIN) && !digitalRead(BTN_PIN)) {
    clearPulsesFromEEPROM();
  }
  
  // Set pin to hi-Z to cut leaking current through jumper
  pinMode(JMP_PIN, INPUT);

  // Calculate proper timings
  setTimings();

  readIds();
  
  #if defined NODE_TYPE_MULTICAL
  // Initialize Modbus
  modbus.setComms(&Serial, 38400, MAX_DE_PIN);
  modbus.setAddress(255);
  #endif
  
  // Initialize radio
  #ifdef ENCRYPT_KEY
  cipherDriver.setKey(encryptKey, sizeof(encryptKey)-1); // Discard null character at the end
  #endif
  radioManager.setThisAddress(nodeId);
  
  if (!radioManager.init()) {
    blinkLed(5, true);
  }
  rf95Driver.setFrequency(frequency);
  rf95Driver.setTxPower(transmitPower); // 5-23 dBm
  #ifdef ENABLE_LOW_RATE
  rf95Driver.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  #endif
  
  readPulsesFromEEPROM();
  
  // Set button interrupt
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), buttonPressed, FALLING);
  
  // Set pulse input interrupts
  PCICR |= B00000100; // P1 and P2 (port D)
  if (!hasNTC) {
    PCICR |= B00000010; // P3 (port C)
  }
  PCMSK2 |= B11000000;  // P1 and P2
  if (!hasNTC) {
    PCMSK1 |= B00000010;  // P3
  }
  
  // Set force send so that message is sent after startup
  forceSend = true;
}

void loop() {
  
  // Send packet if enough time from last one or force send is set
  if (((millis() - lastTransmittedMillis) > (uint32_t)(sendInterval * 1000UL)) || forceSend) {
    
    constructAndSendPacket();
    
    lastTransmittedMillis = millis();
  }
  
  #if defined NODE_TYPE_MULTICAL
  // Update values from Multical energy meter
  if ((millis() - lastModbusRead) > 10000UL) {
    bool result = updateModbus();
    
    if (result) {
      blinkLed(3, false);
    }
    else {
      blinkLed(4, false);
    }
    
    lastModbusRead = millis();
  }
  #endif

  // Save pulse values to EEPROM at regular intervals
  if ((millis() - lastSaveToEEPROM) > (uint32_t)EEPROM_SAVE) {
    savePulsesToEEPROM();
    lastSaveToEEPROM = millis();
  }
  
  // Update NTC temperature (if in use)
  if (hasNTC) {
    readNTC();
  }
}

#if defined NODE_TYPE_MULTICAL
bool updateModbus() {
  
  // We will request 12 registers which is 24 bytes
  uint8_t tempRegisters[24] = {0};
  
  // Send Modbus master request (function code 4, start address 276, number of registers 12).
  // See Multical Modbus RTU module datasheet for more info.
  bool result = modbus.masterRead(MULTICAL_SLAVE_ADDRESS, 4, 276, 12);
  
  if (result) {
    uint32_t requestSent = millis();
    
    // Wait for response with timeout
    byte returnCode = 0;
    do {
      returnCode = modbus.modbusUpdate(NULL, NULL, NULL);
    } while ((returnCode != MASTER_RECEIVED) && ((millis() - requestSent) < 1500));
    
    // If we got a valid response, parse it
    if (returnCode == MASTER_RECEIVED) {
      uint8_t readBytes = modbus.masterGetLastResponse(tempRegisters, 24);
      if (readBytes == 24) {
        energy = ((uint32_t)tempRegisters[0] << 24) | ((uint32_t)tempRegisters[1] << 16) | ((uint32_t)tempRegisters[2] << 8) | (uint32_t)tempRegisters[3];
        flow = ((uint32_t)tempRegisters[4] << 24) | ((uint32_t)tempRegisters[5] << 16) | ((uint32_t)tempRegisters[6] << 8) | (uint32_t)tempRegisters[7];
        volume = ((uint32_t)tempRegisters[8] << 24) | ((uint32_t)tempRegisters[9] << 16) | ((uint32_t)tempRegisters[10] << 8) | (uint32_t)tempRegisters[11];
        power = ((uint32_t)tempRegisters[12] << 24) | ((uint32_t)tempRegisters[13] << 16) | ((uint32_t)tempRegisters[14] << 8) | (uint32_t)tempRegisters[15];
        inletTemp = ((uint32_t)tempRegisters[16] << 24) | ((uint32_t)tempRegisters[17] << 16) | ((uint32_t)tempRegisters[18] << 8) | (uint32_t)tempRegisters[19];
        outletTemp = ((uint32_t)tempRegisters[20] << 24) | ((uint32_t)tempRegisters[21] << 16) | ((uint32_t)tempRegisters[22] << 8) | (uint32_t)tempRegisters[23];
        
        return true;
      }
    }
  }
  
  return false;
}
#endif

bool constructAndSendPacket() {
  
  // Construct payload
  #if defined NODE_TYPE_MULTICAL
  payloadBuffer[0] = B00000010;
  #elif defined NODE_TYPE_PULSE
  payloadBuffer[0] = B00000011;
  #endif
  
  if (isImportant) {
    payloadBuffer[0] |= B00100000;
  }
  
  // Set expect ack bit if needed
  #if not defined SEND_AND_FORGET
  payloadBuffer[0] |= B01000000;
  #endif
  
  payloadBuffer[1] = transmitPowerRaw;
  payloadBuffer[2] = transmitInterval;
  
  payloadBuffer[3] = pulse1 >> 24;
  payloadBuffer[4] = pulse1 >> 16;
  payloadBuffer[5] = pulse1 >> 8;
  payloadBuffer[6] = pulse1;
  
  payloadBuffer[7] = pulse2 >> 24;
  payloadBuffer[8] = pulse2 >> 16;
  payloadBuffer[9] = pulse2 >> 8;
  payloadBuffer[10] = pulse2;
  
  payloadBuffer[11] = pulse3 >> 24;
  payloadBuffer[12] = pulse3 >> 16;
  payloadBuffer[13] = pulse3 >> 8;
  payloadBuffer[14] = pulse3;
  
  #if defined NODE_TYPE_MULTICAL
  payloadBuffer[15] = energy >> 24;
  payloadBuffer[16] = energy >> 16;
  payloadBuffer[17] = energy >> 8;
  payloadBuffer[18] = energy;
  
  payloadBuffer[19] = flow >> 24;
  payloadBuffer[20] = flow >> 16;
  payloadBuffer[21] = flow >> 8;
  payloadBuffer[22] = flow;
  
  payloadBuffer[23] = volume >> 24;
  payloadBuffer[24] = volume >> 16;
  payloadBuffer[25] = volume >> 8;
  payloadBuffer[26] = volume;
  
  payloadBuffer[27] = power >> 24;
  payloadBuffer[28] = power >> 16;
  payloadBuffer[29] = power >> 8;
  payloadBuffer[30] = power;
  
  payloadBuffer[31] = inletTemp >> 24;
  payloadBuffer[32] = inletTemp >> 16;
  payloadBuffer[33] = inletTemp >> 8;
  payloadBuffer[34] = inletTemp;
  
  payloadBuffer[35] = outletTemp >> 24;
  payloadBuffer[36] = outletTemp >> 16;
  payloadBuffer[37] = outletTemp >> 8;
  payloadBuffer[38] = outletTemp;
  #endif
  
  // If in force mode, set maximum transmit power
  if (forceSend) {
    rf95Driver.setTxPower(TX_MAX_PWR);
    payloadBuffer[1] = 100;
  }
  
  bool transmitOk = false;
  for (uint8_t i = 0; i < maxNrOfSends; i++) {
    // Send packet
    transmitOk = sendPacket();
    
    // Update transmit power based on transmit result if not in force send (force sends are always sent at full power and don't count for APC)
    if (!forceSend) {
      updateTransmitPower(transmitOk);
      previousTransmitOk = transmitOk;
    }
    
    // If transmit was successful, reset and break from retransmit loop
    if (transmitOk) {
      break;
    }
  }
  
  // Return to normal transmit power after force send
  if (forceSend) {
    rf95Driver.setTxPower(transmitPower);
    payloadBuffer[1] = transmitPowerRaw;
  }
  
  if (transmitOk) {
    blinkLed(1, false);
  }
  else {
    blinkLed(2, false);
  }
  
  forceSend = false;
  
  return transmitOk;
}

bool sendPacket() {
  // If packet accepted by radio
  if (radioManager.sendto(payloadBuffer, PAYLOAD_LEN, GATEWAYID)) {
    
    // Wait for packet to be sent
    radioManager.waitPacketSent();

    #ifdef ENABLE_LOW_RATE
    uint16_t timeout = 3500;
    #else
    uint16_t timeout = 200;
    #endif

    // Wait ack
    if (radioManager.waitAvailableTimeout(timeout)) {
      uint8_t tempBuffer[2];
      uint8_t len = 2;
      uint8_t from;
      
      // If received packet from gateway
      if (radioManager.recvfrom(tempBuffer, &len, &from)) {
        if (from == GATEWAYID) {
          if (len == 2) {
            // If this really is ack
            if (tempBuffer[0] & B00000001) {
              // Save RSSI reported by gateway for future use
              lastReportedRSSI = tempBuffer[1];
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

void updateTransmitPower(bool lastTransmitOk) {
  
  // Default power change. Increase to make APC more aggressive.
  int8_t defaultChange = 1;
  
  // If transmit was successful, increase/decrease power based on reported RSSI
  if (lastTransmitOk) {
    
    if (lastReportedRSSI > TARGET_RSSI) {
      // If there are no failed transmits yet, decrease power faster (to speed up power decrease after boot up)
      if (!hasFailedTransmit) {
        defaultChange *= -4;
      }
      else {
        defaultChange *= -1;
      }
    }
    else if (lastReportedRSSI < TARGET_RSSI) {
      defaultChange = defaultChange;
    }
    else {
      defaultChange = 0;
    }
      
  }
  // If transmit was unsuccessful, increase always power
  else {
    hasFailedTransmit = true;
    
    // If this was the first failed transmit, increase power only by half
    if (previousTransmitOk) {
      defaultChange *= 4;
    }
    else {
      defaultChange *= 8;
    }
  }
  
  // Add/subtract change
  transmitPowerRaw = constrain((transmitPowerRaw + defaultChange), 0, 100);
  
  // Map raw transmit power to real usable range
  transmitPower = map(transmitPowerRaw, 0, 100, TX_MIN_PWR, TX_MAX_PWR);
  
  // Set radio transmit power
  rf95Driver.setTxPower(transmitPower);
  
  payloadBuffer[1] = transmitPowerRaw;
}

void buttonPressed() {
  forceSend = true;
}

// Functions to calculate pulses

ISR(PCINT1_vect) {
  if (!digitalRead(P3_PIN) && pulse3LastValue) {
    if ((millis() - pulse3LastFall) > (uint32_t)PULSE_MIN) {
      pulse3++;
      pulse3LastFall = millis();
    }
    pulse3LastValue = false;
  }
  else if (digitalRead(P3_PIN) && !pulse3LastValue) {
    pulse3LastValue = true;
  }
}

ISR(PCINT2_vect) {
  if (!digitalRead(P1_PIN) && pulse1LastValue) {
    if ((millis() - pulse1LastFall) > (uint32_t)PULSE_MIN) {
      pulse1++;
      pulse1LastFall = millis();
    }
    pulse1LastValue = false;
  }
  else if (digitalRead(P1_PIN) && !pulse1LastValue) {
    pulse1LastValue = true;
  }
  
  if (!digitalRead(P2_PIN) && pulse2LastValue) {
    if ((millis() - pulse2LastFall) > (uint32_t)PULSE_MIN) {
      pulse2++;
      pulse2LastFall = millis();
    }
    pulse2LastValue = false;
  }
  else if (digitalRead(P2_PIN) && !pulse2LastValue) {
    pulse2LastValue = true;
  }
}

void savePulsesToEEPROM() {
  EEPROM.put(10, pulse1);
  EEPROM.put(20, pulse2);
  if (!hasNTC) {
    EEPROM.put(30, pulse3);
  }
}

void readPulsesFromEEPROM() {
  EEPROM.get(10, pulse1);
  EEPROM.get(20, pulse2);
  if (!hasNTC) {
    EEPROM.get(30, pulse3);
  }
}

void clearPulsesFromEEPROM() {
  uint32_t zero = 0;
  
  EEPROM.put(10, zero);
  EEPROM.put(20, zero);
  EEPROM.put(30, zero);
}

void readNTC() {
  pulse3 = sensorNTC.readTemperature();
}

void setTimings() {
  // Change sendInterval to 8 seconds if in debug mode, else use provided value
  if (isDebugMode) {
    sendInterval = 8;
  }
  
  // Calculate advertised transmit interval
  transmitInterval = ceil(sendInterval / 60.0);
  
  // Constrain maximum number of transmits value
  if (maxNrOfSends < 1) {
    maxNrOfSends = 1;
  }
  else if (maxNrOfSends > 5) {
    maxNrOfSends = 5;
  }
}

void readIds() {
  // Change correct pinmodes
  pinMode(A2, INPUT_PULLUP);
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  
  delay(5);
  
  // Read node ID
  nodeId = 0;
  bitWrite(nodeId, 0, !digitalRead(A2));
  bitWrite(nodeId, 1, !digitalRead(A4));
  bitWrite(nodeId, 2, !digitalRead(A5));
  bitWrite(nodeId, 3, !digitalRead(5));
  bitWrite(nodeId, 4, !digitalRead(4));
  
  // Change pinmodes back to INPUT to save power
  pinMode(A2, INPUT);
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  pinMode(5, INPUT);
  pinMode(4, INPUT);
  
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

void blinkLed(uint8_t blinks, bool loop) {
  do {
    for (uint8_t i = 0; i < blinks; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      digitalWrite(LED_PIN, LOW);
      if (i != (blinks - 1)) {
        delay(150);
      }
    }
    if (loop) {
      delay(2150);
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
