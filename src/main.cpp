
#include <Arduino.h>

// JSON
#include <ArduinoJson.h>

// Captive Portal
#include "CaptivePortal.h"
CaptivePortal captivePortal;

// SparkMaker
#include "SparkMaker.h"
SparkMaker spark;

void handleRoot()
{
	// TODO
	captivePortal.sendFinal(200, "text/plain", "Hello World");
}

void handleStatus()
{
	tempJson.clear();
//	tempJson["status"] = statusNames[spark.printer.status];
	tempJson["currentLayer"] = spark.printer.currentLayer;
	tempJson["totalLayers"] = spark.printer.totalLayers;
//	tempJson["currentFile"] = spark.printer.currentFile;
//	tempJson["filenames"] = spark.printer.filenames;
	
	// send json data
	String content;
	serializeJsonPretty(tempJson, content);
	captivePortal.sendFinal(200, "application/json", content);
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

	// custom pages
	captivePortal.on("/", handleRoot);
	captivePortal.on("/status", handleStatus);

	captivePortal.begin();

	Serial.println("Captive Portal started!");

	spark.setup();
}

void loop()
{
	captivePortal.loop();

	spark.loop();
	delay(1000);
	Serial.print("Status: ");
	Serial.println(spark.printer.status);
}
