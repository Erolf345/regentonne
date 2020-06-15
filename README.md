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
