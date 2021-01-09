#!/usr/bin/env python3

# MIT License
#
# Copyright (c) 2021 aattww (https://github.com/aattww/)
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


# Simple Python script to read data from Sensors gateway and send email alerts under defined conditions.
# Works with GMail using the default settings but should be quite easy to modify to work with other
# services as well.
#
# Remember to change the correct settings and alarm definitions (see comments below for more info).
# Also remember to install minimalmodbus library: https://pypi.org/project/minimalmodbus/
#
# Note that this script will not track active alarms so emails are sent as long as an alarm condition
# is true. That is, better not schedule the script to run every minute or there is a risk of spam.
#
# You can ran this script from shell or schedule using cron or similar.
# If run with -v or --verbose, script prints more information. Use this especially when testing.
# If run with -d or --dry, script will only check for alarms but not send email.


##################
# BEGIN SETTINGS #
##################

# Serial settings
serial_device = "/dev/serial0" # This should work with Raspberry if gateway is connected directly to headers
modbus_address = 1 # Gateway slave address

# Email settings (these work with GMail)
port = 465
smtp_server = "smtp.gmail.com"
sender_email = "example@gmail.com"
sender_friendly = "Sensors gateway"
sender_password = "password"
subject = "Alarms active"
receiver_email = "example@example.com"

alarms = []

# Alarm definitions
#
# Each line should define an alarm in the following format: register to read, condition, value, alarm text
# For examle, to read register 100 and raise an alarm if it is over 10 with text 'Alarm', define
#   alarms.append((100, ">", 10, "Alarm"))
# For examle, to read register 100 and raise an alarm if it is over the value in register 200 with text 'Alarm', define
#   alarms.append((100, ">", "R200", "Alarm"))
#
# Possible conditions are <, >, =, >=, <=, !=
# Remember that values read from gateway are often tenfold, so write your compares carefully!
alarms.append((105, ">", 100, "Node 1, temperature too high"))
alarms.append((100, ">", 60, "Node 1, node not sending within an hour"))
alarms.append((7, "<", "R8", "Gateway, node missing")) # Number of nodes during the last 12h is lower than during 24h
alarms.append((9, "=", 1, "Gateway, node low battery"))

# Should email be sent even if no alarms are active
send_empty = False

################
# END SETTINGS #
################

import smtplib, ssl
import minimalmodbus
import sys

gateway = minimalmodbus.Instrument(serial_device, modbus_address)
gateway.serial.baudrate = 38400

# Check for command line parameters
print_verbose = False
if (("-v" in sys.argv) or ("--verbose" in sys.argv)):
  print_verbose = True
  
dry_run = False
if (("-d" in sys.argv) or ("--dry" in sys.argv)):
  dry_run = True

# Build message body
message = """\
Subject: {sub}
From: {from_friendly} <{from_email}>
To: {to}

Currently active alarms:
"""
message = message.format(sub = subject, from_friendly = sender_friendly, from_email = sender_email, to = receiver_email)

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

# Function to check one alarm. Reads a modbus register and makes alarm state comparison.
#
# Comparison can be made against either a constant integer or another register. For example,
# check_alarm(105, ">", 100) will read modbus register 105 and compare it against a fixed value 60.
# On the other hand, check_alarm(7, "<", "R8") reads values from registers 6 and 8 and makes the comparison.
#
# register:   modbus register to read from
# condition:  comparison operator (<, >, =, >=, <=, !=)
# value:      either an integer to compare against, or a register to read value from
#             If defining a register, prefix with R.
#
# return:     -1 if unable to read from modbus slave
#              0 if alarm off
#              1 if alarm active
#
def check_alarm(register, condition, value):
  try:
    # Try to read the defined register
    mb_data = gateway.read_registers(register, 1)

    if (len(mb_data) != 1):
      return -1

    raw = to_signed(mb_data[0])
    
    # Read comparing value if it is a register
    if (str(value).startswith("R")):
      mb_data = gateway.read_registers(int(value.strip("R")), 1)
      
      if (len(mb_data) != 1):
        return -1

      value = to_signed(mb_data[0])

    if (print_verbose):
      print("... condition '"+str(raw)+condition+str(value)+"' ...")

    # Check for alarm state
    if (condition == "<"):
      if (raw < value):
        return 1
    elif (condition == ">"):
      if (raw > value):
        return 1
    elif (condition == "="):
      if (raw == value):
        return 1
    elif (condition == ">="):
      if (raw >= value):
        return 1
    elif (condition == "<="):
      if (raw <= value):
        return 1
    elif (condition == "!="):
      if (raw != value):
        return 1

    # Else (alarm not active)
    return 0
    
  except Exception:
    return -1


has_active_alarm = False

# Iterate through all alarm definitions, append alarm texts to the message body
for alarm in alarms:
  if (print_verbose):
    print("Checking alarm '"+alarm[3]+"' ...")
    
  value = check_alarm(alarm[0], alarm[1], alarm[2])
  if (value == 1):
    has_active_alarm = True
    message += "- "+alarm[3]+"\n"
    
    if (print_verbose):
      print("... result 'True'")
    
  elif (value == -1):
    has_active_alarm = True
    message += "- Error validating alarm '"+alarm[3]+"' (maybe gateway did not respond)\n"
    
    if (print_verbose):
      print("... result 'Error validating (maybe gateway did not respond)'")
      
  else:
    if (print_verbose):
      print("... result 'False'")

if (not has_active_alarm):
  message += "None\n"


if (print_verbose):
  print("\nMessage:\n\n"+message)
  if (dry_run):
    print("Running in dry run, not sending email")
  elif (not has_active_alarm):
    if (send_empty):
      print("No active alarms, but forcing send anyway ...")
    else:
      print("No active alarms, not sending email")
  else:
    print("Sending email ...")

# Send email if needed
if (not dry_run and (has_active_alarm or send_empty)):
  context = ssl.create_default_context()
  with smtplib.SMTP_SSL(smtp_server, port, context=context) as server:
    server.login(sender_email, sender_password)
    server.sendmail(sender_email, receiver_email, message.encode("utf8"))
    
  if (print_verbose):
    print("... sent")

