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


# Simple example (very simplified) Python script to read data from a Sensors gateway and save
# it to MySQL database. Use this as a base for your own needs.
#
# Remember to change the correct port and slave address and database settings below before use.
#
# Also remember to install the needed libraries:
#   minimalmodbus: https://pypi.org/project/minimalmodbus/
#   mysql.connector: https://dev.mysql.com/doc/connector-python/en/


##################
# BEGIN SETTINGS #
##################

# Serial settings
serial_device = "/dev/serial0" # This should work with Raspberry if gateway is connected directly to headers
modbus_address = 1 # Gateway slave address

# Database settings
db_user = "user"
db_password = "password"
db_host = "localhost"
db_database = "database"

################
# END SETTINGS #
################

import minimalmodbus
import mysql.connector
from datetime import datetime

# Helper function to convert 16 bit unsigned values from gateway to signed 16 bit values
# so that negative values are handled correctly.
#
# value:    unsigned value to be converted
#
# returns:  signed value
#
def to_signed(value):
  if (value >= 2**15):
    return (value - 2**16)
  else:
    return (value)

# Function to save one value to database.
#
# Automatically adds current date and time.
#
# table:   database table name
# column:  database column
# value:   value to be saved
#
# returns: no
#
def save_to_db(table, column, value):
  try:
    cnx = mysql.connector.connect(user=db_user, password=db_password, host=db_host, database=db_database)
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

gateway = minimalmodbus.Instrument(serial_device, modbus_address)
gateway.serial.baudrate = 38400

# Read node 1 modbus data
try:
  mb_data = gateway.read_registers(100, 8)
  
  # If we had correct amount of data, save it to database
  if len(mb_data) == 8:
    save_to_db("node_1", "temperature", to_signed(mb_data[5]))
    save_to_db("node_1", "humidity", to_signed(mb_data[6]))
    save_to_db("node_1", "pressure", to_signed(mb_data[7]))
except Exception:
  print("Error saving node 1 data to database")

