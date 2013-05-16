#!/bin/bash

cd /home/pi/PiRadio
exec python -u run-radio.py > /dev/null 2>&1
