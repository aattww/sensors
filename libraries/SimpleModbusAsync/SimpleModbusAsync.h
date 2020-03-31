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
 * SimpleModbusAsync.h - Modbus library for use with Sensors: https://github.com/aattww/sensors
 *
 * SimpleModbusAsync is a simple library for handling basic Modbus protocol functionality.
 * The library has been designed to be used with Sensors but should be general enough to be used somewhere
 * else as well (at least with little modifications).
 *
 * Both slave and master parts are asynchronous, meaning that receiving and transmitting won't block.
 * The parts may not work well together but it does not make much sense for the same device to operate
 * as Modbus slave and master at the same time anyway.
 *
 * Currently supports only function codes 3 (read holding registers) and 4 (read input registers). 
 */

#ifndef SIMPLE_MODBUS_ASYNC_H
#define SIMPLE_MODBUS_ASYNC_H

#define NO_FRAMES               0
#define ERROR_OVERFLOW          1
#define ERROR_CRC_FAILED        2
#define ERROR_CORRUPTED         3
#define ERROR_ILLEGAL_FUNCTION  4
#define ERROR_ILLEGAL_ADDRESS   5
#define FRAME_SENDING           7
#define FRAME_SENT              8
#define FRAME_RECEIVING         9
#define FRAME_RECEIVED          10
#define MASTER_RECEIVED         11
#define MASTER_ERROR            12

#define BUFFER_SIZE 50

#define MASTER_READ_TIMEOUT     1000

#include "Arduino.h"


class SimpleModbusAsync {
  
  private:
  
    byte _frame[BUFFER_SIZE];         // Shared buffer for Modbus frames
    byte _txEnablePin;                // Maxim MAX(3)485 driver output pin (255 if not in use)
    byte _address;                    // Slave address 
    uint16_t _T1_5;               // Inter character time
    uint16_t _T3_5;               // Inter frame delay
    uint32_t _lastCharReceived;  // Time of the last character received
    HardwareSerial* _ModbusPort;      // Modbus serial port
    bool _onGoing;                    // Frame is being received
    byte _buffer;                     // Bytes read to the buffer
    bool _overflow;                   // Too many bytes received
    bool _isSending;                  // Response is being sent
    uint8_t _waitingResponseFrom;     // Waiting response from a slave (when operating as master)
    uint32_t _masterSentRequest; // When was a slave sent a request to
    bool _masterHasResponse;          // Response from a slave is in the buffer (when operating as master)
    
    /*
     * Calculates correct CRC for the first "bufferSize" bytes in the buffer.
     *
     * bufferSize: number of bytes in the buffer to include in CRC
     *
     * returns:    calculated 16 bit Modbus CRC
     */
    uint16_t calculateCRC(byte bufferSize);
    
    /*
     * Sends actual Modbus frame.
     *
     * bytes:   number of bytes in the buffer to send
     *
     * returns: no
     */
    void sendResponse(uint16_t bytes);
    
    /*
     * Finalizes sending.
     *
     * parameters: no
     *
     * returns:    no
     */
    void finishSend();
    
  public:
  
    /*
     * Sets communication parameters.
     *
     * serialPort:  hardware serial port
     * baud:        speed of the port
     * txEnablePin: TX enable pin for Maxim MAX(3)485 RS-485 transceiver chip (set to 255 if not in use)
     *
     * returns:     no
     */
    void setComms(HardwareSerial* serialPort, uint32_t baud, byte txEnablePin);
    
    /*
     * Sets slave address.
     *
     * address: Modbus slave address
     *
     * returns: no
     */
    void setAddress(byte address);
    
    /*
     * Flushes Modbus port.
     *
     * Clears all flags and flushes serial buffer.
     *
     * returns: no
     */
    void flushPort();
    
    /*
     * Updates current state.
     *
     * NOTE: Must be called frequently enough in order to respond to requests in time!
     *
     * startRegister: will be set to first requested register number. Call with NULL if not needed.
     * nrOfRegisters: will be set to number of registers requested. Call with NULL if not needed.
     * functionCode:  will be set to requested function code. Call with NULL if not needed.
     *
     * returns:       status code (see .h for codes)
     */
    byte modbusUpdate(uint16_t* startRegister, uint16_t* nrOfRegisters, uint8_t* functionCode);
    
    /*
     * Sends Modbus error response.
     *
     * originalFunctionCode: original function code of the related request
     * modbusErrorCode:      error code (see .h for codes)
     *
     * returns:              true if response was sent, else false
     */
    bool sendErrorResponse(uint8_t originalFunctionCode, uint8_t modbusErrorCode);
    
    /*
     * Sends normal Modbus response with payload.
     *
     * originalFunctionCode: function code of the related request
     * payload:              frame payload
     * length:               number of bytes in the payload to send
     * offset:               number of bytes to skip from the beginning
     *
     * returns:              true if response was sent, else false
     */
    bool sendNormalResponse(uint8_t originalFunctionCode, uint8_t* payload, uint8_t length, uint8_t offset);
    
    /*
     * Requests data from a slave.
     *
     * Sends a request and returns immediately. Call modbusUpdate() to update.
     * Once modbusUpdate() returns MASTER_RECEIVED, data can be retrieved using masterGetLastResponse().
     *
     * NOTE: You should probably implement timeout of some kind when using this function.
     *
     * node:          slave address
     * function:      Modbus function code
     * start:         first register
     * nrOfRegisters: number of registers to read
     *
     * returns:       true if request was sent, else false
     */
    bool masterRead(uint8_t node, uint8_t function, uint16_t start, uint16_t nrOfRegisters);
    
    /*
     * Returns the latest response from a slave after Modbus master read.
     *
     * Should be called right after modbusUpdate() has returned MASTER_RECEIVED. Else buffer may get overwritten.
     *
     * buffer:  will be set to received payload
     * length:  maximum size for the payload
     *
     * returns: number of bytes written to buffer
     */
    uint8_t masterGetLastResponse(uint8_t* buffer, uint8_t length);
};

#endif
