
#include <Arduino.h>

// Captive Portal
#include "CaptivePortal.h"
CaptivePortal captivePortal;

void handleRoot()
{
    // TODO
    captivePortal.sendFinal(200, "text/plain", "Hello World");
}



void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        // wait for serial port to connect. Needed for native USB
    }
    Serial.println("\nSparkMaker BLE to WiFi interface");

    //  JsonObject config = loadConfig();
    captivePortal.setup();

    // TODO add custom pages
    captivePortal.on("/", handleRoot);

    captivePortal.begin();

    Serial.println("Captive Portal started!");    
}

void loop()
{
    captivePortal.loop();
}
