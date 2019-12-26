#!/usr/bin/python3

# MIT License
#
# Copyright (c) 2019 aattww (https://github.com/aattww/)
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


# Simple example (very simplified) Python script to read data from Sensors gateway and save it to MySQL database.
# Remember to change correct database settings in line 46.
# Remember to change correct port and slave address in line 63.
# Also remember to install needed libraries:
#   minimalmodbus: https://pypi.org/project/minimalmodbus/
#   mysql.connector: https://dev.mysql.com/doc/connector-python/en/

import minimalmodbus
import mysql.connector
from datetime import datetime

# Helper function to convert 16 bit unsigned values from gateway to signed 16 bit values.
def to_signed(value):
  if (value >= 2**15):
    return (value - 2**16)
  else:
    return (value)

def save_to_db(table, column, value):
  try:
    cnx = mysql.connector.connect(user='user', password='password', host='localhost', database='database')
    cursor = cnx.cursor()
  
    dt = datetime.now().replace(second=0, microsecond=0)
  
    add_reading = ("INSERT INTO " + table + "(time, " + column + ") VALUES (%s, %s) ON DUPLICATE KEY UPDATE time=VALUES(time), " + column + "=VALUES(" + column + ")")
    data_reading = (dt, str(value))
    cursor.execute(add_reading, data_reading)
    cnx.commit()
    
  except Exception as e:
    print(e)
  finally:
    cursor.close()
    cnx.close()

minimalmodbus.BAUDRATE = 38400
gateway = minimalmodbus.Instrument('/dev/serial0', 1)

# Read node 1 modbus data
try:
  mb_data = gateway.read_registers(100, 8)
except Exception:
  mb_data = 0

# If we had correct amount of data, save it to database
if mb_data != 0:
  if len(mb_data) == 8:
    save_to_db("out", "temperature", to_signed(mb_data[5]))
    save_to_db("out", "humidity", to_signed(mb_data[6]))
    save_to_db("out", "pressure", to_signed(mb_data[7]))
