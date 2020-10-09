SparkMaker-WiFi
===============

Web application for SparkMaker FHD 3D printer.

Use a ESP32 to connect to the Bluetooth interface of the SparkMaker FHD and provide a nice Web application to remotely manage and monitor your prints.

Features
--------
- Captive Portal for easy WiFi configuration
  - connect to existing WiFi network or create stand alone Access Point
- connect to SparkMaker FHD via Bluetooth LE
- Web application
  - monitor current status of printer
  - select file to print
  - start / stop / pause prints
  - nice front-end (*basic functionality*)
- REST API for printer functions

Hardware
--------
- ESP32 DevKit (esp32doit-devkit) or any other ESP32 based board


Installation
------------
1. install VSCode with PlatformIO add ESP32 support
2. open the project in VSCode
3. *optional*: open data/config.json and add your WiFi network to "Credentials"
    - You can also create a *private.json* file with your network credentials, using the same layout as *config.json*, in the *data* folder and add your private network credentials to this file. *private.json* will overwrite the settings from *config.json* but will not be uploaded to the repository.
4. upload the SPIFFS Image to your board (PIO Tasks -> env:esp32... -> Platform -> Upload File System image)
5. compile and upload the application (PIO -> Upload All)
6. open a serial terminal and monitor the output
7. check serial output for IP address of ESP32, or open your router DHCP page to get the IP address
8. connect to http://*IP-address* or http://sparkmaker.local

**Captive Portal:**
1. connect your mobile phone to *SparkMaker* WiFi network
2.   open the captive portal (192.168.4.1, if not opened automatically)
3.  set and connect to your local WiFi
4.  open the SparkMaker Web application: http://sparkmaker.local




Acknowledgments
---------------

This project is based on the work of Allen Frise and his project (https://github.com/afrise/spark).
