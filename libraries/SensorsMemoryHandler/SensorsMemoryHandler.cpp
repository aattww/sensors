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
 * SensorsMemoryHandler.h - Simple library for memory handling to be used in Sensors
 *
 * SensorsMemoryHandler implements a simple class to use either 23K256 SRAM chip or
 * the internal SRAM of ATmega328. This class handles interfacing with 23K256 and
 * internal SRAM by providing abstraction to save and restore node data regardless
 * of the actual memory in use. Checks first if there is a 23K256 chip connected
 * and uses it, otherwise falls back to using internal SRAM.
 */

#include "SensorsMemoryHandler.h"

/*
 * Creates a new instance.
 *
 * slaveSelectPin: 23K256 SRAM chip slave select pin
 *
 * returns:        no
 */
SensorsMemoryHandler::SensorsMemoryHandler(uint8_t slaveSelectPin) {
  _slaveSelectPin = slaveSelectPin;
  _initialized = false;
}

/*
 * Initializes the handler.
 *
 * Must be called before anything else can be done.
 *
 * returns: true if memory was initialized
 */
bool SensorsMemoryHandler::init() {
  // Try to init 23K256...
  _23K256Handler.setSlaveSelectPin(_slaveSelectPin);
  if (_23K256Handler.init()) {
    _hasExternalSRAM = true;
  }
  // ... else fall back to internal SRAM
  else {
    _SRAMHandler.init();
    _hasExternalSRAM = false;
  }
  
  _initialized = true;
  
  return _initialized;
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
uint8_t SensorsMemoryHandler::getNodeHeader(uint8_t nodeId) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  if (_hasExternalSRAM) {
    return _23K256Handler.getNodeHeader(nodeId);
  }
  else {
    return _SRAMHandler.getNodeHeader(nodeId);
  }
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
uint8_t SensorsMemoryHandler::getNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer, uint8_t offset) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  if (_hasExternalSRAM) {
    return _23K256Handler.getNodeData(nodeId, length, buffer, offset);
  }
  else {
    return _SRAMHandler.getNodeData(nodeId, length, buffer, offset);
  }
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
uint8_t SensorsMemoryHandler::saveNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  if (_hasExternalSRAM) {
    return _23K256Handler.saveNodeData(nodeId, length, buffer);
  }
  else {
    return _SRAMHandler.saveNodeData(nodeId, length, buffer);
  }
}

/*
 * Deletes node. That is, sets its header to 0.
 *
 * nodeId:  id of the node
 *
 * returns: no
 */
void SensorsMemoryHandler::deleteNode(uint8_t nodeId) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  if (_hasExternalSRAM) {
    return _23K256Handler.deleteNode(nodeId);
  }
  else {
    return _SRAMHandler.deleteNode(nodeId);
  }
}

/*
 * Check whether gateway has external SRAM connected or not.
 *
 * returns: true if using external SRAM, false otherwise
 */
bool SensorsMemoryHandler::hasExternalSRAM() {
  return _hasExternalSRAM;
}
