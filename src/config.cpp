// file system
// http://www.instructables.com/id/Using-ESP8266-SPIFFS/
#include <Arduino.h>
#ifdef ESP32
	#include <SPIFFS.h>	
#endif
#ifdef ESP8266
	#include <FS.h> 	
	#define FILE_READ "r"
	#define FILE_WRITE "w"
#endif

#include "config.h"

const JsonObject JsonObjectNull;

static void merge(JsonVariant dst, JsonVariantConst src)
{
  if (src.is<JsonObject>())
  {
    for (auto kvp : src.as<JsonObject>())
    {
      merge(dst.getOrAddMember(kvp.key()), kvp.value());
    }
  }
  else
  {
    dst.set(src);
  }
}

bool loadConfig(DynamicJsonDocument &configJson, const String &filename, JsonObject mergeObj)
{
	Serial.print("loadConfig:");	Serial.println(filename);

	// handle config file
	File configFile = SPIFFS.open(filename, FILE_READ);
	if (!configFile)
	{
		Serial.print("Failed to open config file: "); Serial.println(filename);
		return false;
	}

	size_t maxConfigSize = configJson.capacity();
	size_t size = configFile.size();
	if (size > maxConfigSize)
	{
		Serial.println("Config file size is too large");
		configFile.close();
		return false;
	}
	if (!size)
	{
		Serial.print("Failed to read config file: "); Serial.println(filename);
		configFile.close();
		return false;
	}

	// parse JSON
	DeserializationError error = deserializeJson(configJson, configFile);
	configFile.close();
	if (error)
	{
		Serial.println("Failed to parse config file");
		return false;
	}

	// merge to destination object
	if ( !mergeObj.isNull() )
	{
		merge(mergeObj, configJson);
	}

	return true;
}

bool saveConfig(const DynamicJsonDocument &config, const String &filename)
{
	Serial.println("saveConfig:");

	// file handling
	File configFile = SPIFFS.open(filename, FILE_WRITE);
	if (!configFile)
	{
		Serial.println("Failed to open config file for writing");
		return false;
	}

	// Serialize JSON to file
	size_t size = serializeJsonPretty(config, configFile);
	configFile.close();
	if (size == 0)
	{
		Serial.println(F("Failed to write to file"));
		return false;
	}

	return true;
}
