// file system
// http://www.instructables.com/id/Using-ESP8266-SPIFFS/
#include <Arduino.h>
#include <SPIFFS.h>
#include "config.h"

const JsonObject JsonObjectNull;

bool loadConfig(DynamicJsonDocument& configJson, const String& filename)
{
  Serial.println("loadConfig:");
  configJson.clear();

  // handle config file
  File configFile = SPIFFS.open(filename, FILE_READ);
  if (!configFile)
  {
    Serial.println("Failed to open config file");
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

  // parse JSON
  DeserializationError error = deserializeJson(configJson, configFile);
  configFile.close();
  if (error)
  {
    Serial.println("Failed to parse config file");
    configJson.clear();
    return false;
  }

  // TODO
  Serial.print("JSON load: ");
  serializeJsonPretty(configJson, Serial);
  Serial.println();

  return true;
}

bool saveConfig(const DynamicJsonDocument& config, const String& filename)
{
  Serial.println("saveConfig:");

  if ( !config.isNull() )
  {
    configJson.clear();
    configJson = config;
  }

  // file handling
  File configFile = SPIFFS.open(filename, FILE_WRITE);
  if (!configFile)
  {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  // Serialize JSON to file
  size_t size = serializeJsonPretty(configJson, configFile);
  configFile.close();
  if (size == 0)
  {
    Serial.println(F("Failed to write to file"));
    return false;
  }

  // TODO
  Serial.print("JSON save: ");
  serializeJsonPretty(configJson, Serial);
  Serial.println();

  return true;
}
