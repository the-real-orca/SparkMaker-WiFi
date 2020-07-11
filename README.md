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
1. install VSCode with PlatformIO add ESP32 support
2. open the project in VSCode
3. upload the SPIFFS Image to your board (PIO -> Upload File System image)
4. compile and upload the application (PIO -> Upload)
5. optional: open a serial terminal and monitor the output
6. connect your mobile phone to *SparkMaker* WiFi network
7. open the captive portal (192.168.4.1, if not opened automatically)
8. set and connect to your local WiFi
9. open the SparkMaker Web application: http://sparkmaker.local

- You can also create a *private.json* file with your network credentials, using the same layout as *config.json*, in the *data* folder and add your private network credentials to this file. *private.json* will overwrite the settings from *config.json* but will not be uploaded to the repository.


Acknowledgments
---------------

This project is based on the work of Allen Frise and his project (https://github.com/afrise/spark).
