/*
 * MIT License
 *
 * Copyright (c) 2019 aattww (https://github.com/aattww/)
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
 * Sensors23K256Handler.h - Simple library for 23K256 SRAM chip to be used in Sensors
 *
 * Sensors23K256Handler implements a simple class to use 23K256 SRAM chip. This class handles
 * interfacing with 23K256 by providing abstraction to save and restore node data.
 */

#ifndef SENSORS23K256HANDLER_H
#define SENSORS23K256HANDLER_H

#include "Arduino.h"
#include <SPI.h>

// Instruction set
#define SMH_READ  B00000011
#define SMH_WRITE B00000010
#define SMH_RDSR  B00000101
#define SMH_WRSR  B00000001

// Operating modes
#define SMH_BYTE_MODE       B00000001
#define SMH_PAGE_MODE       B10000001
#define SMH_SEQUENTIAL_MODE B01000001


class Sensors23K256Handler {
  
  private:
  
    uint8_t _slaveSelectPin;       // 23K256 chip select pin
    uint8_t _currentOperatingMode; // Current 23K256 operating mode
    bool _initialized;             // SRAM has been initialized
    
    /*
     * Sets new operating mode.
     *
     * newMode: new mode for the chip
     *
     * returns: no
     */
    void setOperatingMode(uint8_t newMode);
    
    /*
     * Reads a byte.
     *
     * address: address of the byte to be read
     *
     * returns: read byte
     */
    uint8_t readByte(uint16_t address);
    
    /*
     * Writes a byte.
     *
     * address:   address to write byte to
     * writeByte: byte to be written
     *
     * returns:   no
     */
    void writeByte(uint16_t address, uint8_t writeByte);
    
    /*
     * Reads a sequence.
     *
     * address: start address
     * length:  number of bytes to read
     * buffer:  buffer for read bytes
     *
     * returns: no
     */
    void readSequence(uint16_t address, uint16_t length, uint8_t* buffer);
    
    /*
     * Writes a sequence.
     *
     * address: start address
     * length:  number of bytes to write
     * buffer:  buffer for write bytes. If NULL, writes zeros.
     *
     * returns: no
     */
    void writeSequence(uint16_t address, uint16_t length, uint8_t* buffer);
    
    /*
     * Clears (sets to 0) all registers.
     *
     * returns: no
     */
    void clearRegisters();

  public:
  
    /*
     * Creates a new instance.
     *
     * returns: no
     */
    Sensors23K256Handler();
    
    /*
     * Sets slave select pin.
     *
     * slaveSelectPin: 23K256 SRAM chip slave select pin
     *
     * returns:        no
     */
    void setSlaveSelectPin(uint8_t slaveSelectPin);
    
    /*
     * Initializes the handler.
     *
     * Must be called before anything else can be done.
     *
     * returns: true if valid 23K256 chip was initialized
     */
    bool init();
    
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
    uint8_t getNodeHeader(uint8_t nodeId);
    
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
    uint8_t getNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer, uint8_t offset);
    
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
    uint8_t saveNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer);
    
    /*
     * Deletes node. That is, sets its header to 0.
     *
     * nodeId:  id of the node
     *
     * returns: no
     */
    void deleteNode(uint8_t nodeId);
};

#endif
