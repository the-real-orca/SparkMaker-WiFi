/*
    Captive Portal inclusive Web Server
*/
#ifndef _CAPTIVEPORTAL_h
#define _CAPTIVEPORTAL_h

#include <Arduino.h>
#ifdef ESP32
	#include <WiFi.h>
	#include <WebServer.h>
	#include <ESPmDNS.h>
	#include <SPIFFS.h>	
#endif
#ifdef ESP8266
	#include <ESP8266WiFi.h>
	#define WIFI_MODE_OFF WIFI_OFF
	#define WIFI_MODE_STA WIFI_STA
	#define WIFI_MODE_AP WIFI_AP
	#define WIFI_MODE_APSTA WIFI_AP_STA
	#define WIFI_AUTH_OPEN ENC_TYPE_NONE

	#include <ESP8266WebServer.h>
	typedef ESP8266WebServer WebServer;

	#include <ESP8266mDNS.h>
	#include <FS.h>
#endif

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