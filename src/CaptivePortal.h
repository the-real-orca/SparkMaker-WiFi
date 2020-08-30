/*
    Captive Portal inclusive Web Server
*/
#ifndef _CAPTIVEPORTAL_h
#define _CAPTIVEPORTAL_h

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
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
	static WebServer &getHttpServer();
	static void on(const String &uri, WebServer::THandlerFunction handler);
	static void on(const String &uri, HTTPMethod method, WebServer::THandlerFunction handler);
	static void on(const String &uri, HTTPMethod method, WebServer::THandlerFunction handler, WebServer::THandlerFunction ufn);
	static void sendHeader(const String &name, const String &value, bool first = false);
	static void sendFinal(int code, char *content_type, const String &content);
	static void sendFinal(int code, const String &content_type, const String &content);
};

#endif // _CAPTIVEPORTAL_h