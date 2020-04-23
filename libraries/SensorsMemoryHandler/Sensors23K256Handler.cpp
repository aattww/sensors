/*
 * MIT License
 *
 * Copyright (c) 2020 aattww (https://github.com/aattww/)
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
 * Sensors23K256Handler.cpp - Simple library for 23K256 SRAM chip to be used in Sensors
 *
 * Sensors23K256Handler implements a simple class to use 23K256 SRAM chip. This class handles
 * interfacing with 23K256 by providing abstraction to save and restore node data.
 */

/*
 * Version history
 * ---------------
 *
 * 1.1 2020-04-21 (CURRENT)
 *   - Add beginTransaction() and endTransaction() to prevent interrupts interfering
 *     with transmissions.
 *   - Stop using transfer16() and use transfer() instead.
 *
 * 1.0 2019-12-26
 *   Initial public release
 */

#include "Sensors23K256Handler.h"

/*
 * Creates a new instance.
 *
 * returns: no
 */
Sensors23K256Handler::Sensors23K256Handler() {
  _initialized = false;
  _slaveSelectPin = 255;
}

/*
 * Sets slave select pin.
 *
 * slaveSelectPin: 23K256 SRAM chip slave select pin
 *
 * returns:        no
 */
void Sensors23K256Handler::setSlaveSelectPin(uint8_t slaveSelectPin) {
  _slaveSelectPin = slaveSelectPin;
}

/*
 * Initializes the handler.
 *
 * Must be called before anything else can be done.
 *
 * returns: true if valid 23K256 chip was initialized
 */
bool Sensors23K256Handler::init() {
  if (_slaveSelectPin == 255) {
    _initialized = false;
    return false;
  }
  
  digitalWrite(_slaveSelectPin, HIGH);
  pinMode(_slaveSelectPin, OUTPUT);
  
  setOperatingMode(SMH_BYTE_MODE);
  
  _initialized = true;
  
  // Write a known byte...
  uint8_t testByte = B10101010;
  writeByte(0, testByte);
  
  // ... and read it back. If they do not match, there probably is no 23K256 chip connected
  if (readByte(0) != testByte) {
    _initialized = false;
  }
  else {
    clearRegisters();
  }
  
  return _initialized;
  
}

/*
 * Sets new operating mode.
 *
 * newMode: new mode for the chip
 *
 * returns: no
 */
void Sensors23K256Handler::setOperatingMode(uint8_t newMode) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  if (newMode != _currentOperatingMode) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_slaveSelectPin, LOW);
    SPI.transfer(SMH_WRSR);
    SPI.transfer(newMode);
    digitalWrite(_slaveSelectPin, HIGH);
    SPI.endTransaction();
    _currentOperatingMode = newMode;
  }
}

/*
 * Reads a byte.
 *
 * address: address of the byte to be read
 *
 * returns: read byte
 */
uint8_t Sensors23K256Handler::readByte(uint16_t address) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  // Make sure we are in correct operating mode
  setOperatingMode(SMH_BYTE_MODE);
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(_slaveSelectPin, LOW);
  SPI.transfer(SMH_READ);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  uint8_t readByte = SPI.transfer(0);
  digitalWrite(_slaveSelectPin, HIGH);
  SPI.endTransaction();
  
  return readByte;
}

/*
 * Writes a byte.
 *
 * address:   address to write byte to
 * writeByte: byte to be written
 *
 * returns:   no
 */
void Sensors23K256Handler::writeByte(uint16_t address, uint8_t writeByte) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  // Make sure we are in correct operating mode
  setOperatingMode(SMH_BYTE_MODE);
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(_slaveSelectPin, LOW);
  SPI.transfer(SMH_WRITE);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  SPI.transfer(writeByte);
  digitalWrite(_slaveSelectPin, HIGH);
  SPI.endTransaction();
}

/*
 * Reads a sequence.
 *
 * address: start address
 * length:  number of bytes to read
 * buffer:  buffer for read bytes
 *
 * returns: no
 */
void Sensors23K256Handler::readSequence(uint16_t address, uint16_t length, uint8_t* buffer) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  // Make sure we are in correct operating mode
  setOperatingMode(SMH_SEQUENTIAL_MODE);
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(_slaveSelectPin, LOW);
  SPI.transfer(SMH_READ);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  
  for (uint16_t i = 0; i < length; i++) {
    buffer[i] = SPI.transfer(0);
  }
  
  digitalWrite(_slaveSelectPin, HIGH);
  SPI.endTransaction();
}

/*
 * Writes a sequence.
 *
 * address: start address
 * length:  number of bytes to write
 * buffer:  buffer for write bytes. If NULL, writes zeros.
 *
 * returns: no
 */
void Sensors23K256Handler::writeSequence(uint16_t address, uint16_t length, uint8_t* buffer) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  // Make sure we are in correct operating mode
  setOperatingMode(SMH_SEQUENTIAL_MODE);
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(_slaveSelectPin, LOW);
  SPI.transfer(SMH_WRITE);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  
  for (uint16_t i = 0; i < length; i++) {
    if (buffer) {
      SPI.transfer(buffer[i]);
    }
    else {
      SPI.transfer(0);
    }
  }
  
  digitalWrite(_slaveSelectPin, HIGH);
  SPI.endTransaction();
}

/*
 * Clears (sets to 0) all registers.
 *
 * returns: no
 */
void Sensors23K256Handler::clearRegisters() {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  writeSequence(0, 32768, NULL);
}

/*
 * Gets header of a node.
 *
 * WARNING: Does not validate ID or if it exists. In this case returns 0.
 *          This can be used to check if node exists.
 *
 * nodeId:  id of the node
 *
 * returns: header
 */
uint8_t Sensors23K256Handler::getNodeHeader(uint8_t nodeId) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  return readByte((uint16_t)(nodeId * 100));
}

/*
 * Gets data for a node.
 *
 * WARNING: Does not validate ID or if it exists. In this case returns 0.
 *          Silently limits bytes to be read to 100. Returns 0 for bytes
 *          not in use.
 *
 * nodeId:  id of the node
 * length:  number of bytes to read
 * buffer:  buffer to write read bytes
 * offset:  how many bytes to skip in the beginning
 *
 * returns: number of bytes read
 */
uint8_t Sensors23K256Handler::getNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer, uint8_t offset) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  // Check that the ID exists
  if (getNodeHeader(nodeId) == 0) {
    return 0;
  }
  
  if ((length + offset) > 100) {
    length = 100 - offset;
  }
  
  readSequence((uint16_t)(nodeId * 100 + offset), (uint16_t)length, buffer);
  
  return length;
}

/*
 * Saves data for a node.
 *
 * WARNING: Silently limits bytes to be written to 100.
 *
 * nodeId:  id of the node
 * length:  number of bytes to write
 * buffer:  bytes to write
 *
 * returns: number of bytes written
 */
uint8_t Sensors23K256Handler::saveNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  if (length > 100) {
    length = 100;
  }
  
  writeSequence((uint16_t)(nodeId * 100), (uint16_t)length, buffer);
  
  return length;
}

/*
 * Deletes node. That is, sets its header to 0.
 *
 * nodeId:  id of the node
 *
 * returns: no
 */
void Sensors23K256Handler::deleteNode(uint8_t nodeId) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  writeByte((uint16_t)(nodeId * 100), 0);
}
