// file system
// http://www.instructables.com/id/Using-ESP8266-SPIFFS/
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "config.h"

const uint16_t maxConfigSize = 2048;
DynamicJsonDocument configJson(maxConfigSize);
const JsonObject JsonObjectNull;

JsonObject loadConfig(const String& filename)
{
  Serial.println("loadConfig:");
  configJson.clear();

  // handle config file
  File configFile = SPIFFS.open(filename, FILE_READ);
  if (!configFile)
  {
    Serial.println("Failed to open config file");
    return configJson.as<JsonObject>();
  }

  size_t size = configFile.size();
  if (size > maxConfigSize)
  {
    Serial.println("Config file size is too large");
    configFile.close();
    return configJson.as<JsonObject>();
  }

  // parse JSON
  DeserializationError error = deserializeJson(configJson, configFile);
  configFile.close();
  if (error)
  {
    Serial.println("Failed to parse config file");
    configJson.clear();
  }

  // TODO
  Serial.print("JSON load: ");
  serializeJsonPretty(configJson, Serial);
  Serial.println();

  return configJson.as<JsonObject>();
}

bool saveConfig(const JsonObject& config, const String& filename)
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
