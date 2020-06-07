#ifndef _CONFIG_h
#define _CONFIG_h

#include <ArduinoJson.h>
extern DynamicJsonDocument configJson;
extern const JsonObject JsonObjectNull;

JsonObject loadConfig(const String& filename = "/config.json");
bool saveConfig(const JsonObject& config = JsonObjectNull, const String& filename = "/config.json");

#endif // _CONFIG_h
