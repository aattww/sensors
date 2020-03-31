#!/usr/bin/env python3

# MIT License
#
# Copyright (c) 2020 aattww (https://github.com/aattww/)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# Simple Python script to read data from Sensors gateway. Use for debugging.
# Remember to change correct port and slave address in line 39.
# Also remember to install minimalmodbus library: https://pypi.org/project/minimalmodbus/

# Use with "read_modbus.py register_start nr_of_registers".
# For example,
#   "read_modbus.py 0 20" will read all gateway data, while
#   "read_modbus.py 100 8" will show data for node id 1.

import minimalmodbus
import sys

minimalmodbus.BAUDRATE = 38400
gateway = minimalmodbus.Instrument('/dev/serial0', 1) # port name, slave address (in decimal)
register_start = int(sys.argv[1])
registers_to_read = int(sys.argv[2])

# Read modbus data
try:
  mb_data = gateway.read_registers(register_start, registers_to_read)
except Exception:
  mb_data = 0

if mb_data != 0:
  if len(mb_data) == registers_to_read:
    print("Read Modbus data:")
    print()
    i = 0
    for register in mb_data:
      print("%i: %i" %(register_start + i, register))
      i = i + 1
  else:
    print("Wrong number of registers read: %i instead of %i" %(len(mb_data), registers_to_read))
else:
  print("Error reading Modbus")
