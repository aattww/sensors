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
 * SensorsSRAMHandler.h - Simple library for memory handling of internal ATmega328 SRAM to be used in Sensors
 *
 * SensorsSRAMHandler implements a simple class to use internal SRAM of the microcontroller.
 * This class handles memory by providing abstraction to save and restore node data.
 */

/*
 * Version history
 * ---------------
 *
 * 1.1 2020-03-15 (CURRENT)
 *   - Fixed a possible problem caused by not checking memory allocation for success.
 *
 * 1.0 2019-12-26
 *   Initial public release
 */

#ifndef SENSORSSRAMHANDLER_H
#define SENSORSSRAMHANDLER_H

#include "Arduino.h"

// Data pool settings - BE CAREFUL WHEN ADJUSTING OR YOU MAY RUN OUT OF SRAM!

// How many chunks in the pool
#define POOL_CHUNKS            10
// How many data bytes one chunk holds
// 13 can handle one battery node and so does not waste memory in networks with mostly battery nodes.
#define POOL_CHUNK_DATA_SIZE   13

// With above settings, battery node takes 1 chunk and pulse node 2 chunks.
// So, for example, above settings allow gateway to handle 10 battery nodes OR
// 5 pulse nodes OR 6 battery and 2 pulse nodes and so on. 10 chunks is on the
// high side and increasing that starts to be pushing the limits.

// Bytes added by this library to every chunk (node ID and chunk ordinal for now)
// DO NOT CHANGE!
#define POOL_CHUNK_HEADER_SIZE 2
// Size of a complete chunk
// DO NOT CHANGE!
#define POOL_CHUNK_RAW_SIZE    (POOL_CHUNK_DATA_SIZE + POOL_CHUNK_HEADER_SIZE)

class SensorsSRAMHandler {
  
  private:

    uint8_t* _dataPool[POOL_CHUNKS]; // Memory pool for data
    uint8_t _freeChunks = 0;         // Number of free chunks
    uint8_t _nrOfChunks = 0;         // Number of chunks in total
    bool _initialized = false;       // SRAM handler has been initialized

    /*
     * Finds a free data chunk.
     *
     * returns: pointer to the newly allocated data chunk
     */
    uint8_t* allocateDataChunk();
    
    /*
     * Deallocates (frees) a data chunk.
     *
     * chunk:   pointer to the chunk to be deallocated
     *
     * returns: no
     */
    void deallocateDataChunk(uint8_t* chunk);
    
    /*
     * Returns the amount of free data chunks.
     *
     * returns: amount of free data chunks
     */
    uint8_t getFreeChunks();
    
  public:
    
    /*
     * Initializes the handler.
     *
     * Must be called before anything else can be done.
     *
     * returns: no
     */
    void init();

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
     * Deletes node from the memory.
     *
     * nodeId:  id of the node
     *
     * returns: no
     */
    void deleteNode(uint8_t nodeId);

    /*
     * Gets data for a node.
     *
     * WARNING: Does not validate ID or if it exists. In this case returns 0.
     *          Silently limits bytes to be read to 100. Returns indeterminate
     *          values for bytes not in use.
     *
     * nodeId:  id of the node
     * length:  number of bytes to read
     * buffer:  buffer to write read bytes
     * offset:  how many bytes to skip in the beginning
     *
     * returns: number of bytes read
     */
    uint8_t getNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer, uint8_t offset);
};

#endif
