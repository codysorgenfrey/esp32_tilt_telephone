# Tilt Telephone for ESP32

## V0.1 (MVP)

### Procedure
1. Wake from sleep
2. Connect to WiFi
3. Scan for Tilts
4. Send tilt data to cloud service
5. Sleep 

### Notes
* WiFi SSID and password are hard coded. 
* Tilt UUIDs are hard coded.
* Checking interval is hard coded.
* No web interface.
* No error handling/reporting.
* No calibration support.


## V0.2

### Procedure
1. Setup
    1. Read in preferences
        1. If none:
            1. Setup Wifi
            2. Set up web server
            3. Do FRE
        2. If some:
            1. Connect to WiFi
2. Loop
    1. Wake from sleep?
    2. Connect to WiFi
    3. Scan for Tilts
    4. Calibrate tilt data
    5. Send tilt data to cloud service
    6. Sleep?

### Notes
* Can web server run during sleep? Or will this cause more energy consumption?
* Tilts are discovered on the fly?
* We need persistent storage... SD Card?