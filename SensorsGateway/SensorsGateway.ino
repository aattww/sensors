/*
 * Copyright (c) 2019 aattww (https://github.com/aattww/)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License, version 2 along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
 * ### DELETE NODES ###
 *
 * Define after how many minutes delete nodes if they have not been seen.
 * Set to zero to never delete.
 */
#define DELETE_OLD_NODES  0

/*
 * ####################
 * ### END SETTINGS ###
 * ####################
 */


#define MAJOR_VERSION 1
#define MINOR_VERSION 2

#include <SimpleModbusAsync.h>
#include <RH_RF95.h>
#include <RHReliableDatagram.h>
#include <EEPROM.h>
#include <NTCSensor.h>
#include <SensorsMemoryHandler.h>

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
#define SRAM_NSS        9

#define GATEWAYID       254     // Gateway ID in radio network, DO NOT CHANGE!
#define TX_MAX_PWR      23      // Radio dependant, this is for RFM95
#define TX_MIN_PWR      5       // Radio dependant, this is for RFM95
#define MAX_PAYLOAD_BUF 50      // This needs to be at least 50 to be on the safe side!
#define PULSE_MIN       1000    // How many ms between pulses at least
#define EEPROM_SAVE     3600000 // How often in ms to save pulse values to EEPROM
#define MAX_NR_OF_NODES 100     // Absolute maximum number of nodes (SRAM may limit this even lower)

// Payload lengths for different nodes, DO NOT CHANGE!
#define NODE_TYPE_BATT_LENGTH    11
#define NODE_TYPE_PULSE_K_LENGTH 39
#define NODE_TYPE_PULSE_LENGTH   15

/* ### SETTINGS ### */
const float frequency = FREQUENCY; // Radio transmit frequency (depends on module in use and legislation)
#ifdef ENCRYPT_KEY
const uint8_t encryptKey[17] = ENCRYPT_KEY; // Encryption key for communication
#endif
const uint16_t deleteOldNodes = DELETE_OLD_NODES; // After how many minutes delete nodes not seen (0 = never delete)
/* ### END SETTINGS ### */


// Radio and encryption instances
RH_RF95 rf95Driver;
#ifdef ENCRYPT_KEY
Speck cipherDriver;
RHEncryptedDriver encryptedDriver(rf95Driver, cipherDriver);
RHReliableDatagram radioManager(encryptedDriver);
#else
RHReliableDatagram radioManager(rf95Driver);
#endif

// Modbus handler instance
SimpleModbusAsync modbus;

// NTC instance
NTCSensor sensorNTC(NTC_NO_ENABLE_PIN, P3_PIN);

// Memory handler instance
SensorsMemoryHandler memoryHandler(SRAM_NSS);

// Struct to hold gateway metadata
struct {
  uint16_t errors;
  uint16_t overflownFrames;
  uint16_t illegalFunctionReads;
  uint16_t illegalAddressReads;
  uint16_t framesReceived;
  uint16_t framesSent;
  uint8_t nodesDuringLastHour;
  uint8_t nodesDuringLast12Hours;
  uint8_t nodesDuringLast24Hours;
  bool lowBatteryVoltage;
  uint16_t version;
  volatile uint32_t pulse1;
  volatile uint32_t pulse2;
  volatile uint32_t pulse3;
  bool outOfMemory;
  uint16_t uptime;
} gwMetaData;

// Various variables
uint8_t nodeId; // Node ID for Modbus
uint8_t payloadBuffer[MAX_PAYLOAD_BUF];
uint8_t blinkMode = 0;
uint32_t blinkUpdated = 0;
bool hasNTC;
volatile uint32_t pulse1LastFall = 0;
volatile uint32_t pulse2LastFall = 0;
volatile uint32_t pulse3LastFall = 0;
volatile bool pulse1LastValue = true;
volatile bool pulse2LastValue = true;
volatile bool pulse3LastValue = true;
uint32_t lastSaveToEEPROM = 0;
uint32_t lastReceivedUpdated = 0;
uint8_t millisOverflows = 0;

void setup() {
  // Check if pulse 3 is NTC
  hasNTC = sensorNTC.init();
  
  // LED
  pinMode(LED_PIN, OUTPUT);
  
  // Blink led to indicate startup and fw version
  startUp();
  
  gwMetaData.version = (MAJOR_VERSION << 8) | MINOR_VERSION;
  
  // Button
  pinMode(BTN_PIN, INPUT_PULLUP);
  
  // Pulse inputs
  pinMode(P1_PIN, INPUT_PULLUP);
  pinMode(P2_PIN, INPUT_PULLUP);
  if (!hasNTC) {
    pinMode(P3_PIN, INPUT_PULLUP);
  }
  
  // Enter programming mode if jumper is set
  pinMode(JMP_PIN, INPUT_PULLUP);
  delay(5);
  if (!digitalRead(JMP_PIN)) {
    enterProgMode();
    
    // If also button is pressed, clear pulse values from EEPROM
    if (!digitalRead(BTN_PIN)) {
      clearPulsesFromEEPROM();
    }
  }
  pinMode(JMP_PIN, INPUT);
  
  // Initialize radio
  #ifdef ENCRYPT_KEY
  cipherDriver.setKey(encryptKey, sizeof(encryptKey)-1); // Discard null character at the end
  #endif
  radioManager.setThisAddress(GATEWAYID);
  radioManager.setRetries(0);
  
  if (!radioManager.init()) {
    enterError(5);
  }
  rf95Driver.setFrequency(frequency);
  rf95Driver.setTxPower(TX_MAX_PWR); // 5-23 dBm
  #ifdef ENABLE_LOW_RATE
  rf95Driver.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
  radioManager.setTimeout(3500);
  #endif
  
  readPulsesFromEEPROM();
  
  // Set pulse input interrupts
  PCICR |= B00000100; // P1 and P2 (port D)
  if (!hasNTC) {
    PCICR |= B00000010; // P3 (port C)
  }
  PCMSK2 |= B11000000;  // P1 and P2
  if (!hasNTC) {
    PCMSK1 |= B00000010;  // P3
  }
  
  // Initialize Modbus
  readIds();
  modbus.setComms(&Serial, 38400, MAX_DE_PIN);
  modbus.setAddress(nodeId);
  
  // Initialize memory handler
  memoryHandler.init();
}

void loop() {
  
  // Check radio status and handle possible messages
  checkRadio();
  
  // Check Modbus status and handle possible frames
  checkModbus();
  
  // Update led blink
  updateBlink();
  
  // Save current pulse values to EEPROM if enough time has passed
  if ((millis() - lastSaveToEEPROM) > (uint32_t)EEPROM_SAVE) {
    savePulsesToEEPROM();
    lastSaveToEEPROM = millis();
  }
  
  // Update last received times and check battery levels
  if ((millis() - lastReceivedUpdated) > 10000) {
    updateLastReceived();
    lastReceivedUpdated = millis();
  }
  
  // Read NTC temperature
  if (hasNTC) {
    readNTC();
  }
}

void checkRadio() {
  if (radioManager.available()) {
    uint8_t len = MAX_PAYLOAD_BUF;
    uint8_t from;
    // If received a message sent to us (automatically acks)
    if (radioManager.recvfromAck(payloadBuffer, &len, &from)) {
      // Process packet
      
      // DEBUG - WHAT HAPPENS IF RECEIVED MESSAGE ENCRYPTED WITH WRONG KEY? (can header and length still match?)
      // Length doesn't seem to match (at least not consistently)
      // Add CRC8 checksum byte to payload?
      
      // Check that ID is valid
      if (from > MAX_NR_OF_NODES) {
        return;
      }
      
      // Check which type node the message is from to determine data length that is saved to SRAM.
      // Length is payload plus 2 bytes (last received is added by gateway).
      
      uint8_t length = 0;
      
      // If length matches battery type message
      if (len == NODE_TYPE_BATT_LENGTH) {
        // Sanity check: if header matches any battery type node
        uint8_t type = payloadBuffer[0] & B00000111;
        if ((type == B00000001) || (type == B00000100) || (type == B00000101) || (type == B00000110)) {
          length = NODE_TYPE_BATT_LENGTH + 2;
        }
      }
      // If length matches pulse with Kamstrup type message
      else if (len == NODE_TYPE_PULSE_K_LENGTH) {
      // Sanity check: if header matches node type pulse with Kamstrup message
        if ((payloadBuffer[0] & B00000111) == B00000010) {
          length = NODE_TYPE_PULSE_K_LENGTH + 2;
        }
      }
      // If length matches pulse type message
      else if (len == NODE_TYPE_PULSE_LENGTH) {
      // Sanity check: if header matches node type pulse message
        if ((payloadBuffer[0] & B00000111) == B00000011) {
          length = NODE_TYPE_PULSE_LENGTH + 2;
        }
      }
      
      // If the message was from a known type node, save it to memory
      if (length != 0) {

        // Reuse the same payloadBuffer to save SRAM
        
        // If too much data (sanity check, this should never be possible)
        if (length > MAX_PAYLOAD_BUF) {
          return;
        }
        
        // Move data two indices forward
        for (uint8_t i = length-1; i > 2; i--) {
          payloadBuffer[i] = payloadBuffer[i-2];
        }
        
        // Add received time
        uint16_t tempTime = millis() / 60000; // Convert into minutes
        payloadBuffer[1] = (tempTime >> 8);
        payloadBuffer[2] = tempTime;
        
        // Save data to memory
        uint8_t savedBytes = memoryHandler.saveNodeData(from, length, payloadBuffer);
        
        // If saved data differs from the actual data, gateway is out of memory so flag it
        // Note that this is only updated every time a message is received.
        if (savedBytes != length) {
          gwMetaData.outOfMemory = true;
          
          // Blink led to indicate received but not saved message
          setBlink(2);
        }
        else {
          gwMetaData.outOfMemory = false;
          
          // Blink led to indicate received and successfully saved message
          setBlink(1);
        }
      }
    }
  }
}

void checkModbus() {
  uint16_t startRegister;
  uint16_t nrOfRegisters;
  uint8_t functionCode;
  
  byte response = modbus.modbusUpdate(&startRegister, &nrOfRegisters, &functionCode);
  
  if (response == ERROR_CRC_FAILED || response == ERROR_CORRUPTED) {
    gwMetaData.errors++;
  }
  else if (response == ERROR_OVERFLOW) {
    gwMetaData.overflownFrames++;
  }
  else if (response == ERROR_ILLEGAL_FUNCTION) {
    gwMetaData.illegalFunctionReads++;
    
    // Blink led to indicate failed Modbus read
    setBlink(4);
  }
  else if (response == FRAME_RECEIVED) {
    
    // Calculate node id from requested register address
    uint8_t requestedId = startRegister / 100;
    
    // Check what type of node the requested id is
    uint8_t requestedType = 255;
    
    // Gateway
    if (requestedId == 0) {
      requestedType = 0;
    }
    // ID over maximum supported number of nodes
    else if (requestedId > MAX_NR_OF_NODES) {
      requestedType = 255;
    }
    // Is at least legal ID
    else {
      uint8_t type = memoryHandler.getNodeHeader(requestedId) & B00000111;
      
      // Any battery type
      if ((type == B00000001) || (type == B00000100) || (type == B00000101) || (type == B00000110)) {
        requestedType = 1;
      }
      // Pulse with Kamstrup
      else if (type == B00000010) {
        requestedType = 2;
      }
      // Pulse
      else if (type == B00000011) {
        requestedType = 3;
      }
    }
    
    // Calculate how many registers it is possible to read based on type
    uint8_t maxNrOfRegistersToRead = 0;
    if (requestedType == 0) {
      maxNrOfRegistersToRead = 20;
    }
    else if (requestedType == 1) {
      maxNrOfRegistersToRead = 8;
    }
    else if (requestedType == 2) {
      maxNrOfRegistersToRead = 22;
    }
    else if (requestedType == 3) {
      maxNrOfRegistersToRead = 10;
    }
    
    // Construct Modbus payload
    
    // Gateway meta data
    if (requestedType == 0) {
      payloadBuffer[0] = (gwMetaData.errors >> 8);
      payloadBuffer[1] = gwMetaData.errors;
      payloadBuffer[2] = (gwMetaData.overflownFrames >> 8);
      payloadBuffer[3] = gwMetaData.overflownFrames;
      payloadBuffer[4] = (gwMetaData.illegalFunctionReads >> 8);
      payloadBuffer[5] = gwMetaData.illegalFunctionReads;
      payloadBuffer[6] = (gwMetaData.illegalAddressReads >> 8);
      payloadBuffer[7] = gwMetaData.illegalAddressReads;
      payloadBuffer[8] = (gwMetaData.framesReceived >> 8);
      payloadBuffer[9] = gwMetaData.framesReceived;
      payloadBuffer[10] = (gwMetaData.framesSent >> 8);
      payloadBuffer[11] = gwMetaData.framesSent;
      payloadBuffer[12] = 0;
      payloadBuffer[13] = gwMetaData.nodesDuringLastHour;
      payloadBuffer[14] = 0;
      payloadBuffer[15] = gwMetaData.nodesDuringLast12Hours;
      payloadBuffer[16] = 0;
      payloadBuffer[17] = gwMetaData.nodesDuringLast24Hours;
      payloadBuffer[18] = 0;
      payloadBuffer[19] = gwMetaData.lowBatteryVoltage;
      payloadBuffer[20] = 0;
      payloadBuffer[21] = gwMetaData.outOfMemory;
      payloadBuffer[22] = (gwMetaData.uptime >> 8);
      payloadBuffer[23] = gwMetaData.uptime;
      payloadBuffer[24] = (gwMetaData.version >> 8);
      payloadBuffer[25] = gwMetaData.version;
      payloadBuffer[26] = 0;
      payloadBuffer[27] = 0 | (memoryHandler.hasExternalSRAM() ? B00000001 : B00000000);
      payloadBuffer[28] = (gwMetaData.pulse1 >> 24);
      payloadBuffer[29] = (gwMetaData.pulse1 >> 16);
      payloadBuffer[30] = (gwMetaData.pulse1 >> 8);
      payloadBuffer[31] = gwMetaData.pulse1;
      payloadBuffer[32] = (gwMetaData.pulse2 >> 24);
      payloadBuffer[33] = (gwMetaData.pulse2 >> 16);
      payloadBuffer[34] = (gwMetaData.pulse2 >> 8);
      payloadBuffer[35] = gwMetaData.pulse2;
      payloadBuffer[36] = (gwMetaData.pulse3 >> 24);
      payloadBuffer[37] = (gwMetaData.pulse3 >> 16);
      payloadBuffer[38] = (gwMetaData.pulse3 >> 8);
      payloadBuffer[39] = gwMetaData.pulse3;
    }
    // Battery type
    else if (requestedType == 1) {
      uint8_t tempBuffer[NODE_TYPE_BATT_LENGTH + 2];
      uint8_t readBytes = memoryHandler.getNodeData(requestedId, NODE_TYPE_BATT_LENGTH + 2, tempBuffer, 0);
      
      if (readBytes == NODE_TYPE_BATT_LENGTH + 2) {
        uint16_t lastSeen = (millis() / 60000) - ((tempBuffer[1] << 8) | tempBuffer[2]);
        
        payloadBuffer[0] = (lastSeen >> 8);
        payloadBuffer[1] = lastSeen;
        payloadBuffer[2] = tempBuffer[3];
        payloadBuffer[3] = tempBuffer[4];
        payloadBuffer[4] = 0;
        payloadBuffer[5] = tempBuffer[5];
        payloadBuffer[6] = 0;
        payloadBuffer[7] = tempBuffer[6];
        payloadBuffer[8] = 0;
        payloadBuffer[9] = tempBuffer[0];
        payloadBuffer[10] = tempBuffer[7];
        payloadBuffer[11] = tempBuffer[8];
        payloadBuffer[12] = tempBuffer[9];
        payloadBuffer[13] = tempBuffer[10];
        payloadBuffer[14] = tempBuffer[11];
        payloadBuffer[15] = tempBuffer[12];
      }
      else {
        maxNrOfRegistersToRead = 0;
      }
    }
    // Pulse with Kamstrup
    else if (requestedType == 2) {
      uint8_t tempBuffer[NODE_TYPE_PULSE_K_LENGTH + 2];
      uint8_t readBytes = memoryHandler.getNodeData(requestedId, NODE_TYPE_PULSE_K_LENGTH + 2, tempBuffer, 0);
      
      if (readBytes == NODE_TYPE_PULSE_K_LENGTH + 2) {
        uint16_t lastSeen = (millis() / 60000) - ((tempBuffer[1] << 8) | tempBuffer[2]);
        
        payloadBuffer[0] = (lastSeen >> 8);
        payloadBuffer[1] = lastSeen;
        payloadBuffer[2] = 0;
        payloadBuffer[3] = tempBuffer[3];
        payloadBuffer[4] = 0;
        payloadBuffer[5] = tempBuffer[4];
        payloadBuffer[6] = 0;
        payloadBuffer[7] = tempBuffer[0];
        payloadBuffer[8] = tempBuffer[5];
        payloadBuffer[9] = tempBuffer[6];
        payloadBuffer[10] = tempBuffer[7];
        payloadBuffer[11] = tempBuffer[8];
        payloadBuffer[12] = tempBuffer[9];
        payloadBuffer[13] = tempBuffer[10];
        payloadBuffer[14] = tempBuffer[11];
        payloadBuffer[15] = tempBuffer[12];
        payloadBuffer[16] = tempBuffer[13];
        payloadBuffer[17] = tempBuffer[14];
        payloadBuffer[18] = tempBuffer[15];
        payloadBuffer[19] = tempBuffer[16];
        payloadBuffer[20] = tempBuffer[17];
        payloadBuffer[21] = tempBuffer[18];
        payloadBuffer[22] = tempBuffer[19];
        payloadBuffer[23] = tempBuffer[20];
        payloadBuffer[24] = tempBuffer[21];
        payloadBuffer[25] = tempBuffer[22];
        payloadBuffer[26] = tempBuffer[23];
        payloadBuffer[27] = tempBuffer[24];
        payloadBuffer[28] = tempBuffer[25];
        payloadBuffer[29] = tempBuffer[26];
        payloadBuffer[30] = tempBuffer[27];
        payloadBuffer[31] = tempBuffer[28];
        payloadBuffer[32] = tempBuffer[29];
        payloadBuffer[33] = tempBuffer[30];
        payloadBuffer[34] = tempBuffer[31];
        payloadBuffer[35] = tempBuffer[32];
        payloadBuffer[36] = tempBuffer[33];
        payloadBuffer[37] = tempBuffer[34];
        payloadBuffer[38] = tempBuffer[35];
        payloadBuffer[39] = tempBuffer[36];
        payloadBuffer[40] = tempBuffer[37];
        payloadBuffer[41] = tempBuffer[38];
        payloadBuffer[42] = tempBuffer[39];
        payloadBuffer[43] = tempBuffer[40];
      }
      else {
        maxNrOfRegistersToRead = 0;
      }
    }
    // Pulse
    else if (requestedType == 3) {
      uint8_t tempBuffer[NODE_TYPE_PULSE_LENGTH + 2];
      uint8_t readBytes = memoryHandler.getNodeData(requestedId, NODE_TYPE_PULSE_LENGTH + 2, tempBuffer, 0);
      
      if (readBytes == NODE_TYPE_PULSE_LENGTH + 2) {
        uint16_t lastSeen = (millis() / 60000) - ((tempBuffer[1] << 8) | tempBuffer[2]);
        
        payloadBuffer[0] = (lastSeen >> 8);
        payloadBuffer[1] = lastSeen;
        payloadBuffer[2] = 0;
        payloadBuffer[3] = tempBuffer[3];
        payloadBuffer[4] = 0;
        payloadBuffer[5] = tempBuffer[4];
        payloadBuffer[6] = 0;
        payloadBuffer[7] = tempBuffer[0];
        payloadBuffer[8] = tempBuffer[5];
        payloadBuffer[9] = tempBuffer[6];
        payloadBuffer[10] = tempBuffer[7];
        payloadBuffer[11] = tempBuffer[8];
        payloadBuffer[12] = tempBuffer[9];
        payloadBuffer[13] = tempBuffer[10];
        payloadBuffer[14] = tempBuffer[11];
        payloadBuffer[15] = tempBuffer[12];
        payloadBuffer[16] = tempBuffer[13];
        payloadBuffer[17] = tempBuffer[14];
        payloadBuffer[18] = tempBuffer[15];
        payloadBuffer[19] = tempBuffer[16];
      }
      else {
        maxNrOfRegistersToRead = 0;
      }
    }
    
    uint16_t startAddress = startRegister - requestedId * 100;
    
    if (((startAddress + nrOfRegisters) <= maxNrOfRegistersToRead) && (requestedType != 255)) {
      bool result = modbus.sendNormalResponse(functionCode, payloadBuffer, nrOfRegisters * 2, startAddress * 2);
      
      if (result) {
        gwMetaData.framesReceived++;
        
        // Blink led to indicate successful Modbus read
        setBlink(3);
      }
      else {
        modbus.sendErrorResponse(functionCode, ERROR_ILLEGAL_ADDRESS);
        gwMetaData.illegalAddressReads++;
        
        // Blink led to indicate failed Modbus read
        setBlink(4);
      }
    }
    else {
      modbus.sendErrorResponse(functionCode, ERROR_ILLEGAL_ADDRESS);
      gwMetaData.illegalAddressReads++;
      
      // Blink led to indicate failed Modbus read
      setBlink(4);
    }
  }
  else if (response == FRAME_SENT) {
    gwMetaData.framesSent++;
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
    enterError(1);
  }
}

void updateLastReceived() {
  uint16_t currentTime = millis() / 60000; // Convert into minutes
  gwMetaData.nodesDuringLastHour = 0;
  gwMetaData.nodesDuringLast12Hours = 0;
  gwMetaData.nodesDuringLast24Hours = 0;
  gwMetaData.lowBatteryVoltage = false;
  
  // Calculate gateway uptime with millis() overflow handling
  if (millis() < lastReceivedUpdated) {
    millisOverflows++;
  }
  gwMetaData.uptime = (currentTime / 60) + (millisOverflows * 1193); // millis() overflows every 1193 hours
  
  uint8_t tempBuffer[5];
  
  // Iterate through all IDs
  for (uint8_t i = 1; i <= MAX_NR_OF_NODES; i++) {
    // The first 5 bytes has all the data we need here
    uint8_t readBytes = memoryHandler.getNodeData(i, 5, tempBuffer, 0);
    
    // If the ID is in use
    if (readBytes == 5) {
      
      // Calculate nodes which have been seen during the last...
      uint16_t lastReceived = (tempBuffer[1] << 8) | tempBuffer[2];
      // ... hour
      if ((currentTime - lastReceived) <= 60) {
        gwMetaData.nodesDuringLastHour++;
      }
      // ... 12 hours
      if ((currentTime - lastReceived) <= 720) {
        gwMetaData.nodesDuringLast12Hours++;
      }
      // ... 24 hours
      if ((currentTime - lastReceived) <= 1440) {
        gwMetaData.nodesDuringLast24Hours++;
      }
      
      // Check battery levels for battery nodes
      uint8_t type = tempBuffer[0] & B00000111;
      if ((type == B00000001) || (type == B00000100) || (type == B00000101) || (type == B00000110)) {
        uint16_t voltage = (tempBuffer[3] << 8) | tempBuffer[4];
        if (voltage < 2100) {
          gwMetaData.lowBatteryVoltage = true;
        }
      }
      
      // Delete nodes not seen for a while
      if (deleteOldNodes != 0) {
        if ((currentTime - lastReceived) > deleteOldNodes) {
          memoryHandler.deleteNode(i);
        }
      }
    }
  }
}

void enterError(uint8_t blinks) {
  while (true) {
    for (uint8_t i = 0; i < blinks; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
    delay(2000);
  }
}

void setBlink(uint8_t blinks) {
  blinkMode = blinks;
  blinkUpdated = millis();

  digitalWrite(LED_PIN, HIGH);
}

void updateBlink() {
  if (blinkMode > 0) {
    if (digitalRead(LED_PIN)) {
      if ((millis() - blinkUpdated) > 50) {
        digitalWrite(LED_PIN, LOW);
        blinkUpdated = millis();
        blinkMode--;
      }
    }
    else {
      if ((millis() - blinkUpdated) > 150) {
        digitalWrite(LED_PIN, HIGH);
        blinkUpdated = millis();
      }
    }
  }
}

void enterProgMode() {
  return;
}

ISR(PCINT1_vect) {
  if (!digitalRead(P3_PIN) && pulse3LastValue) {
    if ((millis() - pulse3LastFall) > (uint32_t)PULSE_MIN) {
      gwMetaData.pulse3++;
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
      gwMetaData.pulse1++;
      pulse1LastFall = millis();
    }
    pulse1LastValue = false;
  }
  else if (digitalRead(P1_PIN) && !pulse1LastValue) {
    pulse1LastValue = true;
  }
  
  if (!digitalRead(P2_PIN) && pulse2LastValue) {
    if ((millis() - pulse2LastFall) > (uint32_t)PULSE_MIN) {
      gwMetaData.pulse2++;
      pulse2LastFall = millis();
    }
    pulse2LastValue = false;
  }
  else if (digitalRead(P2_PIN) && !pulse2LastValue) {
    pulse2LastValue = true;
  }
}

void savePulsesToEEPROM() {
  EEPROM.put(10, gwMetaData.pulse1);
  EEPROM.put(20, gwMetaData.pulse2);
  if (!hasNTC) {
    EEPROM.put(30, gwMetaData.pulse3);
  }
}

void readPulsesFromEEPROM() {
  EEPROM.get(10, gwMetaData.pulse1);
  EEPROM.get(20, gwMetaData.pulse2);
  if (!hasNTC) {
    EEPROM.get(30, gwMetaData.pulse3);
  }
}

void clearPulsesFromEEPROM() {
  uint32_t zero = 0;
  
  EEPROM.put(10, zero);
  EEPROM.put(20, zero);
  EEPROM.put(30, zero);
}

void readNTC() {
  gwMetaData.pulse3 = sensorNTC.readTemperature();
}

void startUp() {
  digitalWrite(LED_PIN, HIGH);
  if (MINOR_VERSION & B00001000) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  delay(200);
  
  digitalWrite(LED_PIN, HIGH);
  if (MINOR_VERSION & B00000100) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  delay(200);
  
  digitalWrite(LED_PIN, HIGH);
  if (MINOR_VERSION & B00000010) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  delay(200);
  
  digitalWrite(LED_PIN, HIGH);
  if (MINOR_VERSION & B00000001) {
    delay(200);
  }
  else {
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  
  delay(1000);
}
