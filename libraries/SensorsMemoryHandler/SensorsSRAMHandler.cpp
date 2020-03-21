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
 * SensorsSRAMHandler.cpp - Simple library for memory handling of internal ATmega328 SRAM to be used in Sensors
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

#include "SensorsSRAMHandler.h"

/*
 * Initializes the handler.
 *
 * Must be called before anything else can be done.
 *
 * returns: no
 */
void SensorsSRAMHandler::init() {
  for (uint8_t i = 0; i < POOL_CHUNKS; i++) {
    // Allocate memory for a chunk
    uint8_t* chunk = (uint8_t*)calloc(POOL_CHUNK_RAW_SIZE, sizeof(uint8_t));
    // Check that memory was actually allocated
    if (chunk != NULL) {
      // Add chunk to pool
      _dataPool[i] = chunk;
      _nrOfChunks++;
    }
    // If failed to allocate new memory, no reason to keep trying
    else {
      break;
    }
  }
  
  // If we have allocated chunks
  if (_nrOfChunks > 0) {
    _freeChunks = _nrOfChunks;
    _initialized = true;
  }
}

/*
 * Finds a free data chunk.
 *
 * returns: pointer to the newly allocated data chunk
 */
uint8_t* SensorsSRAMHandler::allocateDataChunk() {
  // Check that instance has been initialized
  if (!_initialized) {
    return NULL;
  }
  
  for (uint8_t i = 0; i < _nrOfChunks; i++) {
    // The first byte indicates node ID and can not be zero, meaning that chunk is currently not in use.
    if (_dataPool[i][0] == 0) {
      _freeChunks--;
      return _dataPool[i];
    }
  }
  // If did not find a free chunk, return NULL pointer
  return NULL;
}

/*
 * Deallocates (frees) a data chunk.
 *
 * chunk:   pointer to the chunk to be deallocated
 *
 * returns: no
 */
void SensorsSRAMHandler::deallocateDataChunk(uint8_t* chunk) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  for (uint8_t i = 0; i < _nrOfChunks; i++) {
    if (_dataPool[i] == chunk) {
      _freeChunks++;
      // Clear the first byte (node ID) to indicate free chunk
      _dataPool[i][0] = 0;
      break;
    }
  }
}

/*
 * Returns the amount of free data chunks.
 *
 * returns: amount of free data chunks
 */
uint8_t SensorsSRAMHandler::getFreeChunks() {
  return _freeChunks;
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
uint8_t SensorsSRAMHandler::saveNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  // Limit saveable data to 100 bytes
  if (length > 100) {
    length = 100;
  }
  
  // Delete old data and chunks for this node ID. This is easier than trying to reuse
  // old chunks (for example, node type and therefore data length can change).
  deleteNode(nodeId);
  
  // Calculate how many chunks are needed for the amount to be saved
  uint8_t neededChunks = ceil((double)length / POOL_CHUNK_DATA_SIZE);
  uint8_t start = 0;
  uint8_t end = 0;
  
  // If enough free chunks for the whole data
  if (getFreeChunks() >= neededChunks) {
    for (uint8_t i = 0; i < neededChunks; i++) {
      uint8_t* chunk = allocateDataChunk();
      // First byte of a chunk is node ID
      chunk[0] = nodeId;
      // Second byte of the chunk is the ordinal:
      // First chunk of long node data is 0, second chunk 1 and so on
      chunk[1] = i;
      
      // Calculate start and end byte of the data to be saved
      start = i * POOL_CHUNK_DATA_SIZE;
      end = (i + 1) * POOL_CHUNK_DATA_SIZE;
      if (end > length) {
        end = length;
      }
      
      // Save node data to data pool
      for (uint8_t i2 = start; i2 < end; i2++) {
        chunk[i2 + POOL_CHUNK_HEADER_SIZE - start] = buffer[i2];
      }
    }
  }
  // Return number of bytes saved
  return end;
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
uint8_t SensorsSRAMHandler::getNodeHeader(uint8_t nodeId) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  for (uint8_t i = 0; i < _nrOfChunks; i++) {
    if (_dataPool[i][0] == nodeId) {
      // If this is the first chunk of node data (ie. it has the header)
      if (_dataPool[i][1] == 0) {
        // First byte is node ID, second one ordinal, third one the actual header
        return _dataPool[i][2];
      }
    }
  }
  // Return zero to indicate node ID not found (header can not be zero)
  return 0;
}

/*
 * Deletes node from the memory.
 *
 * nodeId:  id of the node
 *
 * returns: no
 */
void SensorsSRAMHandler::deleteNode(uint8_t nodeId) {
  // Check that instance has been initialized
  if (!_initialized) {
    return;
  }
  
  // Search every chunk associated to node ID
  for (uint8_t i = 0; i < _nrOfChunks; i++) {
    if (_dataPool[i][0] == nodeId) {
      deallocateDataChunk(_dataPool[i]);
    }
  }
}

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
uint8_t SensorsSRAMHandler::getNodeData(uint8_t nodeId, uint8_t length, uint8_t* buffer, uint8_t offset) {
  // Check that instance has been initialized
  if (!_initialized) {
    return 0;
  }
  
  // Check that the ID exists
  if (getNodeHeader(nodeId) == 0) {
    return 0;
  }
  
  // Limit the maximum readable number of byte to 100 (technically 99)
  if ((length + offset) > 100) {
    length = 100 - offset;
  }
  
  // Calculate how many chunks the data has been divided to
  uint8_t neededChunks = ceil((double)(length + offset) / POOL_CHUNK_DATA_SIZE);
  uint8_t start = 0;
  uint8_t end = 0;
  uint8_t bytesWritten = 0;
  
  for (uint8_t chunkNumber = 0; chunkNumber < neededChunks; chunkNumber++) {
    for (uint8_t i = 0; i < _nrOfChunks; i++) {
      if (_dataPool[i][0] == nodeId) {
        if (_dataPool[i][1] == chunkNumber) {
          
          // Calculate start byte of the chunk
          start = POOL_CHUNK_HEADER_SIZE + offset;
          // If start byte would overflow chunk size...
          if (start >= POOL_CHUNK_RAW_SIZE) {
            // ... calculate remaining offset for the next chunk
            offset = offset - POOL_CHUNK_DATA_SIZE;
            // ... and discard this chunk completely
            continue;
          }
          // ... else clear offset to indicate we have handled it
          else {
            offset = 0;
          }
          // Assume we iterate through the whole chunk
          end = POOL_CHUNK_RAW_SIZE;
          
          for (uint8_t i2 = start; i2 < end; i2++) {
            // Copy byte to outside buffer
            buffer[bytesWritten] = _dataPool[i][i2];
            bytesWritten++;
            
            // If we have copied requested bytes, break off
            if (bytesWritten == length) {
              break;
            }
          }
          // No need to iterate the rest of the chunks anymore, instead go to the next chunk number
          break;
        }
      }
    }
  }
  return bytesWritten;
}
