#include <Arduino.h>

// JSON
#include <ArduinoJson.h>
#include "AsyncJson.h"

// Captive Portal
#include "CaptivePortal.h"
CaptivePortal captivePortal;

// SparkMaker
#include "SparkMaker.h"
SparkMaker spark;

void handleStatus(AsyncWebServerRequest *request)
{
	tempJson.clear();
	tempJson["status"] = statusNames[spark.printer.status];
	tempJson["currentLayer"] = spark.printer.currentLayer;
	tempJson["totalLayers"] = spark.printer.totalLayers;
	tempJson["currentFile"] = spark.printer.currentFile;
	uint32_t printTime = 0;
	if ( !spark.printer.finishTime )
	{
		uint32_t time = millis() / 1000;
		printTime = time - spark.printer.startTime;
	}
	else
	{
		printTime = spark.printer.finishTime - spark.printer.startTime;
	}
	
	uint32_t estimatedTotalTime = 0;
	if ( spark.printer.currentLayer > 3 ) 
		estimatedTotalTime = (printTime * spark.printer.totalLayers) / spark.printer.currentLayer;
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
	request->send(200, "application/json", content);
// TODO	Serial.println(content);
}

void handleCmdDisconnect(AsyncWebServerRequest *request)
{
	spark.disconnect();
	handleStatus(request);
}

void handleCmdConnect(AsyncWebServerRequest *request)
{
	spark.connect();
	handleStatus(request);
}

void handleCmdPrint(AsyncWebServerRequest *request)
{
	String file = request->arg("file");
	spark.print(file);
	request->send(200, "text/plain", "OK");
}

void handleCmdMove(AsyncWebServerRequest *request)
{
	int16_t pos = request->arg("pos").toInt();
	if ( pos )
		spark.move(pos);
	request->send(200, "text/plain", "OK");
}


void setup()
{
	Serial.begin(115200);
	while (!Serial)
	{
		// wait for serial port to connect. Needed for native USB
	}
	Serial.println("\nSparkMaker BLE to WiFi interface");

	spark.setup();

	captivePortal.setup();

	// custom pages
	captivePortal.on("/status", handleStatus);
	captivePortal.on("/print", handleCmdPrint);

	captivePortal.on("/stop", [](AsyncWebServerRequest *request){ spark.stopPrint(); request->send(200, "text/plain", "OK"); });
	captivePortal.on("/pause", [](AsyncWebServerRequest *request){ spark.pausePrint(); request->send(200, "text/plain", "OK"); });
	captivePortal.on("/resume", [](AsyncWebServerRequest *request){ spark.resumePrint(); request->send(200, "text/plain", "OK"); });
	captivePortal.on("/emergencyStop", [](AsyncWebServerRequest *request){ spark.emergencyStop(); request->send(200, "text/plain", "OK"); });
	captivePortal.on("/requestStatus", [](AsyncWebServerRequest *request){ spark.requestStatus(); request->send(200, "text/plain", "OK"); });
	captivePortal.on("/home", [](AsyncWebServerRequest *request){ spark.home(); request->send(200, "text/plain", "OK"); });
	captivePortal.on("/move", handleCmdMove);
	captivePortal.on("/connect", handleCmdConnect);
	captivePortal.on("/disconnect", handleCmdDisconnect);

	captivePortal.begin();

	Serial.println("Sparkmaker WiFi started!");
}

void loop()
{
// TODO	captivePortal.loop();
	spark.loop();
}

/* FIXME
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  adc_power_off();
  //esp_wifi_stop(); // Doesn't work for me!
  esp_bt_controller_disable();
  */