#include <Arduino.h>

// JSON
#include <ArduinoJson.h>

// Captive Portal
#include "CaptivePortal.h"
CaptivePortal captivePortal;

// SparkMaker
#include "SparkMaker.h"
SparkMaker spark;

void handleStatus()
{
	tempJson.clear();
	tempJson["status"] = statusNames[spark.printer.status];
	tempJson["currentLayer"] = spark.printer.currentLayer;
	tempJson["totalLayers"] = spark.printer.totalLayers;
	tempJson["currentFile"] = spark.printer.currentFile;
	uint32_t time = millis() / 1000;
	uint32_t printTime = time - spark.printer.startTime;
	uint32_t estimatedTotalTime = printTime * spark.printer.totalLayers / spark.printer.currentLayer;
	tempJson["printTime"] = printTime;
	tempJson["estimatedTotalTime"] = estimatedTotalTime;
	auto files = tempJson.createNestedArray("fileList");
	for (auto const &file: spark.printer.filenames)
	{
		files.add(file.first);
	}
	
	// send json data
	String content;
	serializeJsonPretty(tempJson, content);
	captivePortal.sendFinal(200, "application/json", content);
	Serial.println(content);
}

void handleCmdDisconnect()
{
	spark.disconnect();
	handleStatus();
}

void handleCmdConnect()
{
	spark.connect();
	handleStatus();
}

void handleCmdPrint()
{
	String file = captivePortal.getHttpServer().arg("file");
	spark.print(file);
	captivePortal.sendFinal(200, "text/plain", "OK");
}

void handleCmdMove()
{
	int16_t pos = captivePortal.getHttpServer().arg("pos").toInt();
	if ( pos )
		spark.move(pos);
	captivePortal.sendFinal(200, "text/plain", "OK");
}

/**
 * debug interfact to send direct commands
 */
static void handleCmdSend() // HACK
{
	String cmd = captivePortal.getHttpServer().arg("cmd");
	spark.send(cmd);
	captivePortal.sendFinal(200, "text/plain", "OK");
}

void setup()
{
	Serial.begin(115200);
	while (!Serial)
	{
		// wait for serial port to connect. Needed for native USB
	}
	Serial.println("\nSparkMaker BLE to WiFi interface");

	captivePortal.setup();

	// custom pages
	captivePortal.on("/status", handleStatus);
	captivePortal.on("/print", handleCmdPrint);

	captivePortal.on("/stop", [](){ spark.stopPrint(); captivePortal.sendFinal(200, "text/plain", "OK"); });
	captivePortal.on("/pause", [](){ spark.pausePrint(); captivePortal.sendFinal(200, "text/plain", "OK"); });
	captivePortal.on("/resume", [](){ spark.resumePrint(); captivePortal.sendFinal(200, "text/plain", "OK"); });
	captivePortal.on("/emergencyStop", [](){ spark.emergencyStop(); captivePortal.sendFinal(200, "text/plain", "OK"); });
	captivePortal.on("/requestStatus", [](){ spark.requestStatus(); captivePortal.sendFinal(200, "text/plain", "OK"); });
	captivePortal.on("/home", [](){ spark.home(); captivePortal.sendFinal(200, "text/plain", "OK"); });
	captivePortal.on("/move", handleCmdMove);


	captivePortal.on("/send", handleCmdSend);
	captivePortal.on("/connect", handleCmdConnect);
	captivePortal.on("/disconnect", handleCmdDisconnect);

	captivePortal.begin();

	Serial.println("Captive Portal started!");

	spark.setup();
}

void loop()
{
	captivePortal.loop();
	spark.loop();
}
