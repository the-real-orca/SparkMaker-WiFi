/*
    Captive Portal inclusive Web Server
*/
#ifndef _CAPTIVEPORTAL_h
#define _CAPTIVEPORTAL_h

#include <Arduino.h>
#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

// config
#include "config.h"
const size_t configJsonSize = 1024;
extern DynamicJsonDocument config;

// shared JSON buffer
const size_t tempJsonSize = 2048;
extern DynamicJsonDocument tempJson;

class CaptivePortal
{
  public:
	// captive portal core functions
	static void setup();
	static void begin();
	static void loop();

	// web server functions
	static AsyncWebServer &getHttpServer();

	static void on(const String &uri, ArRequestHandlerFunction handler);
	static void on(const String &uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler);
	static void on(const String &uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler, ArUploadHandlerFunction ufn);
};

#endif // _CAPTIVEPORTAL_h