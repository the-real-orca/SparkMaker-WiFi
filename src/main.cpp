
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

// Captive Portal
#include "CaptivePortal.h"
CaptivePortal captivePortal;

void handleRoot()
{
    // TODO
    captivePortal.sendFinal(200, "text/plain", "Hello World");
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.name(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

#define FORMAT_SPIFFS_IF_FAILED true

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        // wait for serial port to connect. Needed for native USB
    }
    Serial.println("\nSparkMaker BLE to WiFi interface");

    // file system
    SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED);
    listDir(SPIFFS, "/", 0);

    //  JsonObject config = loadConfig();
    captivePortal.setup();

    // TODO add custom pages
    captivePortal.on("/", handleRoot);

    captivePortal.begin();

    Serial.println("OK");    
}

void loop()
{
    captivePortal.loop();
}
