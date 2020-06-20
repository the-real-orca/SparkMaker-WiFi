SparkMaker-WiFi
===============

Web application for SparkMaker FHD 3D printer.

Use a ESP32 to connect to the Bluetooth interface of the SparkMaker FHD and provide a nice Web application to remotely manage and monitor your prints.

Features
--------
- Captive Portal for easy WiFi configuration
  - connect to existing WiFi network or create stand alone Access Point
- connect to SparkMaker FHD via Bluetooth LE
- Web application (partially)
  - monitor current status of printer (partially)
  - select file to print (ToDo)
  - start / stop / pause prints (ToDo)
  - nice front-end (ToDo)
- REST API for printer functions (partially)

Hardware
--------
- ESP32 DevKit (esp32doit-devkit) or any other ESP32 based board


Installation
------------
- install VSCode with PlatformIO add ESP32 support
- open the project in VSCode
- upload the SPIFFS Image to your board (PIO -> Upload File System image)
- compile and upload the application (PIO -> Upload)
- optional: open a serial terminal and monitor the output
- connect your mobile phone to *SparkMaker* WiFi network
- open the captive portal (192.168.4.1, if not opened automatically)
- set and connect to your local WiFi
- open the SparkMaker Web application: http://sparkmaker.local


Acknowledgments
---------------

This project is based on the work of Allen Frise and his project (https://github.com/afrise/spark).
