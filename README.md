PiRadio-20x4LCD
=======

Python based internet radio project for RaspberryPi to play internet radio streams and control it using custom atmega328p-based board with 
connected 20x4 character display, rotary encoder, potentiometer, 2xMSGEQ7 board and PT2322 audio processor board.

All hardware-specific EAGLE projects are located under 'schematic' subfolder. 
All required non-standard libraries are available upon request. Some of them are self-made from scratch, some of them googled in the internet.

**Prerequesties:**

1. You should have a working raspberry pi with latest raspbian (at least 2012-12-18)
2. Also we assumes that your network connection (wired or wireless) is also running well
3. Your arduino-based board is connected via internal on-board serial port  
   (assuming arduino/PyRadioInterface sketch is already uploaded there, all external hardware is connected to the arduino - 
   such as rotary encoder, potentiometer, PT2322 board, MSGEQ7 board)
4. You are logged in as user 'pi' with it's default home at /home/pi/ 

**Installation:**

1. sudo apt-get update && sudo apt-get upgrade
2. sudo apt-get install mpd mpc python-serial python-mpd2 git
3. cd && git clone https://github.com/andykarpov/PiRadio-20x4LCD.git PiRadio
4. cd /home/pi/PiRadio
5. sudo ln -s /home/pi/PiRadio/etc/init.d/pi-radio /etc/init.d/pi-radio
6. chmod -R a+rwx data/ 
7. cd /home/pi/PiRadio/init.d/
8. sudo chmod a+x pi-radio
9. sudo nano /etc/mpd - change "localhost" to "any"
10. sudo alsamixer - set a desired output level
11. sudo alsactl store
12. sudo /etc/init.d/pi-radio start
13. sudo update-rc.d pi-radio defaults
14. sudo reboot

