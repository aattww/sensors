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
 * SimpleModbusAsync.cpp - Modbus library for use with Sensors: https://github.com/aattww/sensors
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

#include "SimpleModbusAsync.h"

/*
 * Sets communication parameters.
 *
 * serialPort:  hardware serial port
 * baud:        speed of the port
 * txEnablePin: TX enable pin for Maxim MAX(3)485 RS-485 transceiver chip (set to 255 if not in use)
 *
 * returns:     no
 */
void SimpleModbusAsync::setComms(HardwareSerial* serialPort, uint32_t baud, byte txEnablePin) {
  _ModbusPort = serialPort;
  _txEnablePin = txEnablePin;
  if (_txEnablePin != 255) {
    pinMode(_txEnablePin, OUTPUT);
    digitalWrite(_txEnablePin, LOW);
  }
  _ModbusPort->begin(baud, SERIAL_8N1);
  _onGoing = false;
  _isSending = false;
  _waitingResponseFrom = 0;
  _masterHasResponse = false;
  
  // Calculate correct timings based on Modbus standard
  if (baud > 19200) {
    _T1_5 = 750;
    _T3_5 = 1750;
  }
  else {
    _T1_5 = 15000000/baud;
    _T3_5 = 35000000/baud;
  }
}

/*
 * Sets slave address.
 *
 * address: Modbus slave address
 *
 * returns: no
 */
void SimpleModbusAsync::setAddress(byte address) {
  _address = address;
}

/*
 * Flushes Modbus port.
 *
 * Clears all flags and flushes serial buffer.
 *
 * returns: no
 */
void SimpleModbusAsync::flushPort() {
  _buffer = 0;
  _onGoing = false;
  _isSending = false;
  _waitingResponseFrom = 0;
  _masterHasResponse = false;
  _overflow = false;
  
  while (_ModbusPort->available()) {
    _ModbusPort->read();
  }
}

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
byte SimpleModbusAsync::modbusUpdate(uint16_t* startRegister, uint16_t* nrOfRegisters, uint8_t* functionCode) {
  
  // If currently sending
  if (_isSending) {
    // If sending has just finished
    if (!(bit_is_set(UCSR0B, UDRIE0) || bit_is_clear(UCSR0A, TXC0))) {
      finishSend();
      return FRAME_SENT;
    }
    return FRAME_SENDING;
  }
  
  // If expecting a response (master mode) and timeout has been exceeded, clear flag
  // This is in case a slave does not respond to a master request at all
  if (_waitingResponseFrom) {
    if ((millis() - _masterSentRequest) > MASTER_READ_TIMEOUT) {
      _waitingResponseFrom = 0;
    }
  }
  
  if (!_ModbusPort->available()) {
    if (!_onGoing) {
      return NO_FRAMES;
    }
    else {
      if ((micros() - _lastCharReceived) < _T1_5) {
        return FRAME_RECEIVING;
      }
      // If time from last character is over inter character timeout, fall through to processing frame
    }
  }
  else {
    // First character of the incoming frame
    if (!_onGoing) {
      _buffer = 0;
      _overflow = false;
      _onGoing = true;
      // Clear flag for having a response from a slave since the response will be lost (sharing the same buffer)
      _masterHasResponse = false;
    }
    // If buffer is full, just read serial buffer until it is empty
    if (_overflow) {
      _ModbusPort->read();
    }
    else {
      // If buffer has just become full
      if (_buffer == BUFFER_SIZE) {
        _overflow = true;
        _ModbusPort->read();
      }
      // Read character to buffer
      else {
        _frame[_buffer] = _ModbusPort->read();
        _buffer++;
      }
    }
    _lastCharReceived = micros();
    
    return FRAME_RECEIVING;
  }
  
  // If we get this far, there is a received message in the buffer waiting to be processed
  _onGoing = false;
  
  if (_overflow) {
    return ERROR_OVERFLOW;
  }
      
  // As a slave, the minimum request frame is 8 bytes. As a master, minimum response frame is 7 bytes.
  if (((_buffer >= 8) && !_waitingResponseFrom) || ((_buffer >= 7) && _waitingResponseFrom)) {
    // Combine CRC low and high bytes
    uint16_t crc = ((_frame[_buffer - 2] << 8) | _frame[_buffer - 1]);
    // If correct CRC
    if (calculateCRC(_buffer - 2) == crc) {
      // If correct address and not waiting for a response
      if ((_frame[0] == _address) && !_waitingResponseFrom) {
        // If correct function code (3, read holding registers or 4, read input registers)
        if ((_frame[1] == 3) || (_frame[1] == 4)) {
          
          // Set data to arguments
          
          if (startRegister) {
            *startRegister = ((_frame[2] << 8) | _frame[3]);
          }
          
          if (nrOfRegisters) {
            *nrOfRegisters = ((_frame[4] << 8) | _frame[5]);
          }
          
          if (functionCode) {
            *functionCode = _frame[1];
          }
          
          return FRAME_RECEIVED;
        }
        // Return illegal function response
        else {
          sendErrorResponse(_frame[1], ERROR_ILLEGAL_FUNCTION);
          return ERROR_ILLEGAL_FUNCTION;
        }
      }
      // Or response to a prior request - ie. acting as a Modbus master
      else if (_frame[0] == _waitingResponseFrom) {
        // Clear waiting response flag
        _waitingResponseFrom = 0;
        
        // If received normal response
        if ((_frame[1] == 3) || (_frame[1] == 4)) {
          // Set flag to indicate response in the buffer
          _masterHasResponse = true;
          
          return MASTER_RECEIVED;
        }
         // If received an error (in Modbus error the first bit of the function code is set)
        else if (_frame[1] & 0x80) {
          return MASTER_ERROR;
        }
        // Some undefined error
        else {
          return MASTER_ERROR;
        }
      }
    }
    // CRC does not match
    else {
      // Clear waiting response flag
      _waitingResponseFrom = 0;
      
      return ERROR_CRC_FAILED;
    }
  }
  // Frame is wrong length
  else {
    // Clear waiting response flag
    _waitingResponseFrom = 0;
    
    return ERROR_CORRUPTED;
  }
  
  // If we get this far, nothing is happening
  return NO_FRAMES;
}

/*
 * Calculates correct CRC for the first "bufferSize" bytes in the buffer.
 *
 * bufferSize: number of bytes in the buffer to include in CRC
 *
 * returns:    calculated 16 bit Modbus CRC
 */
uint16_t SimpleModbusAsync::calculateCRC(byte bufferSize) {
  uint16_t temp = 0xffff;
  uint16_t flag = 0;
  
  for (byte i = 0; i < bufferSize; i++) {
    temp = temp ^ _frame[i];
    for (byte j = 0; j < 8; j++) {
      flag = temp & 0x0001;
      temp >>= 1;
      if (flag) {
        temp ^= 0xa001;
      }
    }
  }
  // Reverse byte order
  return (((temp << 8) & 0xff00) | ((temp >> 8) & 0x00ff));
}

/*
 * Sends Modbus error response.
 *
 * originalFunctionCode: original function code of the related request
 * modbusErrorCode:      error code (see .h for codes)
 *
 * returns:              true if response was sent, else false
 */
bool SimpleModbusAsync::sendErrorResponse(uint8_t originalFunctionCode, uint8_t modbusErrorCode) {
  // Clear flag for having a response from a slave since the response will be lost (sharing the same buffer)
  _masterHasResponse = false;
  
  _frame[0] = _address;
  _frame[1] = originalFunctionCode | 0x80;
  
  switch (modbusErrorCode) {
    case ERROR_ILLEGAL_ADDRESS:
      _frame[2] = 0x02;
      break;
    case ERROR_ILLEGAL_FUNCTION:
      _frame[2] = 0x01;
      break;
    default:
      return false;
  }

  // Add CRC
  uint16_t crc = calculateCRC(3);
  _frame[3] = (crc >> 8);
  _frame[4] = crc;
  
  sendResponse(5);
  
  return true;
}

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
bool SimpleModbusAsync::sendNormalResponse(uint8_t originalFunctionCode, uint8_t* payload, uint8_t length, uint8_t offset) {
  // Clear flag for having a response from a slave since the response will be lost (sharing the same buffer)
  _masterHasResponse = false;
  
  // Validate some values
  if (((originalFunctionCode == 3) || (originalFunctionCode == 4)) && ((length + 5) <= BUFFER_SIZE)) {
    _frame[0] = _address;
    _frame[1] = originalFunctionCode;
    _frame[2] = length;
    
    // Copy payload to send buffer
    for (uint16_t i = 0; i < length; i++) {
      _frame[3 + i] = payload[i + offset];
    }
    
    // Add CRC
    uint16_t crc = calculateCRC(length + 3);
    _frame[length + 3] = (crc >> 8);
    _frame[length + 4] = crc;
    
    sendResponse(length + 5);
    
    return true;
  }
  else {
    return false;
  }
}

/*
 * Sends actual Modbus frame.
 *
 * bytes:   number of bytes in the buffer to send
 *
 * returns: no
 */
void SimpleModbusAsync::sendResponse(uint16_t bytes) {
  _isSending = true;
  
  // Wait for quiet time between frames
  while ((micros() - _lastCharReceived) < _T3_5);
  
  // If using MAX(3)485 transceiver, enable driver output and wait for it to stabilize
  if (_txEnablePin != 255) {
    digitalWrite(_txEnablePin, HIGH);
    delayMicroseconds(100);
  }
  
  // Write frame to serial and return immediately
  _ModbusPort->write(_frame, bytes);
}

/*
 * Finalizes sending.
 *
 * parameters: no
 *
 * returns:    no
 */
void SimpleModbusAsync::finishSend() {
  _isSending = false;
  
  // Disable MAX3485 driver output (if in use)
  if (_txEnablePin != 255) {
    digitalWrite(_txEnablePin, LOW);
  }
}

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
bool SimpleModbusAsync::masterRead(uint8_t node, uint8_t function, uint16_t start, uint16_t nrOfRegisters) {
  
  // Validate some values
  if (!((node >= 1) && (node <= 254) && ((function == 3) || (function == 4)) && (nrOfRegisters > 0))) {
    return false;
  }
  
  // Make sure that the response will fit frame buffer
  if ((nrOfRegisters * 2 + 5) > BUFFER_SIZE) {
    return false;
  }
  
  // Make sure we are not sending or receiving
  if (_onGoing || _isSending || _waitingResponseFrom) {
    return false;
  }
  
  // Flush port in case previous request is hanging
  flushPort();
  
  _frame[0] = node;
  _frame[1] = function;
  _frame[2] = (start >> 8);
  _frame[3] = start;
  _frame[4] = (nrOfRegisters >> 8);
  _frame[5] = nrOfRegisters;
  
  uint16_t crc = calculateCRC(6);
  _frame[6] = (crc >> 8);
  _frame[7] = crc;
  
  // Set flag that we are expecting a response
  _waitingResponseFrom = node;
  
  // Send data
  sendResponse(8);
  
  // Save request send time
  _masterSentRequest = millis();
  
  return true;
}

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
 uint8_t SimpleModbusAsync::masterGetLastResponse(uint8_t* buffer, uint8_t length) {
  // If there is a response in the buffer
  if (_masterHasResponse) {
    // If there is space for the response
    if ((_buffer - 5) <= length) {
      // Sanity check - make sure received frame length matches the one in the frame
      if ((_buffer - 5) == _frame[2]) {
        // Copy frame payload to buffer
        for (uint8_t i = 0; i < _frame[2]; i++) {
          buffer[i] = _frame[3 + i];
        }
        
        return _frame[2];
      }
    }
  }

  return 0;
}
