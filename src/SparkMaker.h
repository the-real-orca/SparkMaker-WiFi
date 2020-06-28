/*
	SparkMaker BLE Interface
*/
#ifndef _SPARKMAKER_h
#define _SPARKMAKER_h

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <map>

typedef enum
{
	DISCONNECTED,
	CONNECTING,
	STANDBY,
	FILELIST,
	PRINTING,
	PAUSE,
	FINISHED,
	STOPPING,
	NO_CARD,
	UPDATING
} PRINTERSTATUS;

/**
 * string names for WiFi encryption
 */
extern const char *statusNames[];

typedef struct
{
	PRINTERSTATUS status = DISCONNECTED;
	int32_t currentLayer = 0;
	int32_t totalLayers = 0;
	std::string currentFile;
	unsigned long heartbeat = 0;
	unsigned long lastStatusRequest = 0;
	std::map<uint16_t, std::string> filenames;
} Printer;

class SparkMaker
{
  public:
	static void setup();
	static void loop();

	static void connect();
	static void disconnect();
	static void send(String cmd);

	static void print(String filename);
	static void stopPrint();
	static void pausePrint();
	static void resumePrint();
	static void emergencyStop();

	static void requestStatus();

	static void move(int16_t pos);
	static void home();

	static Printer printer;

};

#endif // _SPARKMAKER_h