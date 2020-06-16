# Rain barrel/Irrigation control using ESP32

## Overview
This project contains Arduino/Platformio code for controlling a ESP32 with MQTT to:

- Measure water level of two rain barrels

- Pump water from catcher barrel to storage barrel if levels are okay

- Open switch and activate pump to irrigate garden

This project aims to be powered by a single solar cell.

## Sensors

- GY63: Pressure Sensor used for measuring water level

- BMP280: Reference pressure (Also useful for smarthome application)

## Pumps
One pump is used to move water from rain catcher to storage, the other pump is used to increase irrigation reach. Both are powered by 12V battery and are controlled via a Mosfet that only works with PWM.

Aditionally to the irrigation pump a switch has to be activated before the irrigation hose can be used.

