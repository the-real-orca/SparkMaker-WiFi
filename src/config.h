#ifndef _CONFIG_h
#define _CONFIG_h

#include <ArduinoJson.h>
extern DynamicJsonDocument configJson;
extern const JsonObject JsonObjectNull;

bool loadConfig(DynamicJsonDocument& configJson, const String& filename = "/config.json");
bool saveConfig(const DynamicJsonDocument& config, const String& filename = "/config.json");

#endif // _CONFIG_h
