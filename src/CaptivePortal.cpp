

#include "CaptivePortal.h"

#ifdef ESP8266
extern "C"
{
#include "user_interface.h"
}
static inline uint32_t ESP_getChipId() { return ESP.getChipId(); }
#else //ESP32
#include <esp_wifi.h>
static inline uint32_t ESP_getChipId() { return (uint32_t)ESP.getEfuseMac(); }
#endif

#include <SPIFFS.h>

// DNS _httpServer
static const byte DNS_PORT = 53;
DNSServer _dnsServer;

// Web
static const byte HTTP_PORT = 80;
WebServer _httpServer(HTTP_PORT);

// JSON
static DynamicJsonDocument networks(1024);
static DynamicJsonDocument config(1024);
static DynamicJsonDocument tempJson(1024);

// Captive Portal defaults
const static struct
{
  uint16_t connectionTimeout = 5;
  uint8_t softAP_IP[4] = {192, 168, 4, 1};
  uint8_t subnet[4] = {255, 255, 255, 0};
  String hostname = "ESP-" + String(ESP_getChipId());
  String portalPath = "portal.html";
} defaultConfig;

/**
 * sanity check for strings
 */
String sanity(const String _str)
{
  String str = _str;
  str.replace("\"", "");
  str.replace("'", "");
  str.replace("\\", "");

  return str;
}

/**
 * start captive portal AP
 */
void startCaptiveAP()
{
  Serial.print("Start WiFi AP: " + config["hostname"].as<String>() + " ... ");

  IPAddress softAP_IP(defaultConfig.softAP_IP);
  if (config["captivePortal"]["ip"].is<const char *>())
    softAP_IP.fromString(config["captivePortal"]["ip"].as<const char *>());
  IPAddress subnet(defaultConfig.subnet);
  if (config["captivePortal"]["subnet"].is<const char *>())
    subnet.fromString(config["captivePortal"]["subnet"].as<const char *>());

  WiFi.softAPdisconnect(true);
  WiFi.setHostname(config["hostname"]);

  // create WiFi AP
  WiFi.softAPConfig(softAP_IP, softAP_IP, subnet);
  WiFi.softAP(config["hostname"]);
  Serial.println("OK");


  // redirecting all the domains to the ESP
  Serial.print("Start DNS ... ");
  _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("OK");  

  // enable mDNS
  Serial.print("Start mDNS ... ");
  if( MDNS.begin(config["hostname"]) )
  {
    MDNS.addService("http", "tcp", 80);
    Serial.println("OK");
  } else
  {
    Serial.println("Failed !");
  }    
  
}

/**
 * connect as WiFi Client
 * use strongest available network in known credentials list
 */
void connectWiFiClient()
{
  // scan for networks and connect to strongest one we have credentials
  uint16_t connectionTimeout = config["connectionTimeout"];
  WiFi.disconnect();
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, false);
  for (int j = 0; j < n && WiFi.status() != WL_CONNECTED; j++)
  {
    String ssid = WiFi.SSID(j);
    if (config["credentials"].containsKey(ssid))
    {
      String pwd = config["credentials"][ssid];
      Serial.print("Connect to ");
      Serial.print(ssid);
      Serial.print(" ... ");
      WiFi.begin(ssid.c_str(), pwd.c_str());
      // wait for connection
      for (int16_t i = connectionTimeout * 10; (i > 0) && (WiFi.status() != WL_CONNECTED); i--)
      {
        delay(100);
      }
    }
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("OK");
  else
    Serial.println("Failed");
}


/**
 * connect as WiFi Client
 * try given credentials, use strongest available network in known credentials list as fallback
 */
void connectWiFiClient(String ssid, String pwd)
{
  WiFi.disconnect();
  Serial.print("Connect to ");
  Serial.print(ssid);
  Serial.print(" ... ");
  WiFi.begin(ssid.c_str(), pwd.c_str());
  // wait for connection
  uint16_t connectionTimeout = config["connectionTimeout"];  
  for (int16_t i = connectionTimeout * 10; (i > 0) && (WiFi.status() != WL_CONNECTED); i--)
  {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
    Serial.println("OK");
  else
  {
    Serial.println("Failed");

    // fallback to known networks
    connectWiFiClient();
  }
}

/**
 * test for captive portal requests
 * 
 * @return: true if handled
 */
static boolean isCaptiveRequest()
{
  // test for local requests
  String host = _httpServer.hostHeader();
  if (host == WiFi.softAPIP().toString())
  {
    // request to access point IP
    return false;
  }

  if (host == WiFi.localIP().toString())
  {
    // request to client IP
    return false;
  }

  if (host.endsWith(".local") || host.endsWith(".home"))
  {
    // request to local host name
    return false;
  }

  return true;
}

/**
 * handle captive portal requests
 */
static void handleCaptiveRequest()
{
  // redirect
  Serial.println("request captured and redirected");
  String portalPath = config["captivePortal"]["path"] | defaultConfig.portalPath;
  _httpServer.sendHeader("Location", "http://" + config["hostname"].as<String>() + ".local/" + portalPath, true);
  _httpServer.send(302, "text/html", "");
  _httpServer.client().stop();
}

/**
 * get MIME type from filename
 */
static String getContentType(String filename) 
{
  if(filename.endsWith(".htm")) return "text/html";
  if(filename.endsWith(".html")) return "text/html";
  if(filename.endsWith(".css")) return "text/css";
  if(filename.endsWith(".js")) return "application/javascript";
  if(filename.endsWith(".png")) return "image/png";
  if(filename.endsWith(".gif")) return "image/gif";
  if(filename.endsWith(".jpg")) return "image/jpeg";
  if(filename.endsWith(".ico")) return "image/x-icon";
  if(filename.endsWith(".xml")) return "text/xml";
  if(filename.endsWith(".pdf")) return "application/x-pdf";
  if(filename.endsWith(".zip")) return "application/x-zip";
  if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

/**
 * send static file
 * 
 * @return true if file exist and was sent
 */
static bool handleFile(String path)
{
  Serial.println("handleFile: " + path);

  // security check
  if ( path.indexOf("..") >= 0 )
    return false;

  // prepend web server folder
  path = "/public" + path;

  // add default page
  if ( path.endsWith("/") )
    path += "index.html";                                

  // MIME type
  String contentType = getContentType(path);
  
  String pathCompressed = path + ".gz";
  bool found = SPIFFS.exists(path);
  bool foundCompressed = SPIFFS.exists(pathCompressed);
  if ( found || foundCompressed )
  {  
    // use compressed version if exist
    if ( foundCompressed )
      path = pathCompressed;

    // send file
    File file = SPIFFS.open(path, "r");
    _httpServer.streamFile(file, contentType);
    file.close();

Serial.println(String("Sent file: ") + path);
    return true;
  }

Serial.println(String("\tFile Not Found: ") + path);
  return false;
}

/**
 * handle HTTP generic request
 */
static void handleGenericHTTP()
{
  // test for captive portal request
  if (isCaptiveRequest())
  {
    handleCaptiveRequest();
    return;
  }

  // load from SPIFFS
  if ( handleFile(_httpServer.uri()) )
    return;

  // send not found page
  Serial.print("handleNotFound: ");
  Serial.print(_httpServer.hostHeader());
  Serial.println(_httpServer.uri());

  // HTML Header
  _httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  _httpServer.sendHeader("Pragma", "no-cache");
  _httpServer.sendHeader("Expires", "-1");
  _httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  // HTML Content
  static String html;
  html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>" + config["hostname"].as<String>() + "</title></head><body>";
  html += "<i>" + _httpServer.uri() + "</i> not found";
  html += "</body></html>";
  _httpServer.send(404, "text/html", html);
  _httpServer.client().stop();
}

/**
 * handle info request
 */
static void handleInfo()
{
  Serial.println("send info");

  // scan available networks
  tempJson.clear();
  tempJson["name"] = config["hostname"];
  auto portal = tempJson.createNestedObject("captivePortal");
  portal["ssid"] = config["hostname"];
  portal["ip"] = WiFi.softAPIP().toString();
  auto client = tempJson.createNestedObject("client");
  client["ssid"] = WiFi.SSID();
  client["ip"] = WiFi.localIP().toString();

  // send json data
  String content;
  serializeJsonPretty(tempJson, content);
  _httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  _httpServer.send(200, "application/json", content);
  _httpServer.client().stop();
}

/**
 * string names for WiFi encryption
 */
const char *encryptionTypeNames[] = {
    "OPEN",
    "WEP",
    "WPA",
    "WPA2",
    "WPA & WPA2",
    "WPA2 Enterprise",
    "WPA3",
    "WPA3 Enterprise",
    "WIFI_AUTH_MAX"};

/**
 * perform WiFi scan and send detected networks
 */
static void handleWifiScan()
{
  Serial.print("WiFi scan: ... ");

  // scan available networks
  networks.clear();
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, false); //WiFi.scanNetworks(async, show_hidden)
  for (int i = 0; i < n; i++)
  {
    JsonObject ap = networks.createNestedObject();
    ap["ssid"] = WiFi.SSID(i);
    ap["rssi"] = WiFi.RSSI(i);
    ap["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

    // check for currently connected
    if ( WiFi.SSID(i) == WiFi.SSID() )
      ap["connected"] = true;
  }

  // augment known networks
  auto credentials = config["credentials"].as<JsonObject>();
  for (const auto& kv : credentials) 
  {
    // search in networks
    bool found = false;
    for (auto ap : networks.as<JsonArray>())
    {
      if ( kv.key() == ap["ssid"] )
      {
        // we have credentials for this network
        found = true;
        ap["known"] = true;
        break;
      }
    }
    if ( !found )
    {
      // add to network list
      JsonObject newNet = networks.createNestedObject();
      newNet["ssid"] = kv.key();
      newNet["encrypted"] = (kv.value().as<String>().length() > 0);
      newNet["known"] = true;
    }
  }

  // send json data
  String content;
  serializeJsonPretty(networks, content);
  _httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  _httpServer.send(200, "application/json", content);
  _httpServer.client().stop();
  Serial.println(content); // TODO
}

/**
 * add AP to known networks
 */
static void handleWifiAdd()
{
  String ssid = sanity(_httpServer.arg("ssid"));
  String pwd = sanity(_httpServer.arg("pwd"));
  Serial.print("add '" + ssid + "' to known networks list");

  auto credentials = config["credentials"].as<JsonObject>();
  credentials[ssid] = pwd;
  saveConfig(config);

  // send reply
  _httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  _httpServer.send(200, "application/json", "{\"status\": \"OK\"}");
  _httpServer.client().stop();

  // connect to WiFi
  if ( ssid != WiFi.SSID() )
  {
    delay(100);  
    connectWiFiClient(ssid, pwd);
  }
}

/**
 * remove AP from known networks
 */
static void handleWifiDel()
{
  String ssid = _httpServer.arg("ssid");
  Serial.print("remove '" + ssid + "' from known networks list: ... ");

  bool reconnect = (ssid == WiFi.SSID());

  config["credentials"].remove(ssid);
  saveConfig(config);

  // send reply
  _httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  _httpServer.send(200, "application/json", "{\"status\": \"OK\"}");
  _httpServer.client().stop();

  // connect to WiFi
  if ( reconnect )
  {
    delay(100);
    connectWiFiClient();
  }
}

/**
 * update host name
 */
static void handleUpdateHostname()
{
  String hostname = sanity(_httpServer.arg("hostname"));

  // update hostname
  if ( !hostname.length() )
  {
    // nothing to do, just send summary
    return handleInfo();
  }
  config["hostname"] = hostname;
  saveConfig(config);

  // answer with updated info
  handleInfo();

  // re-start AP
  startCaptiveAP();

}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.name(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void CaptivePortal::setup()
{
  // file system
  SPIFFS.begin(true); // format filesystem if failed
  listDir(SPIFFS, "/", 0);

  loadConfig(config);

  // sanity check for config
  if (!config["hostname"])
    config["hostname"] = defaultConfig.hostname;
  if (!config["connectionTimeout"])
    config["connectionTimeout"] | defaultConfig.connectionTimeout;

  // init WiFi
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  // start AP
  startCaptiveAP();

  // connect as WiFi Client
  connectWiFiClient();
  
  Serial.print("IP Address: ");
  Serial.print(WiFi.softAPIP());
  Serial.print(", ");
  Serial.println(WiFi.localIP());


  // setup HTTP server
  Serial.print("Start WebServer ... ");
  _httpServer.on("/c/info", handleInfo);     // send status info
  _httpServer.on("/c/hostname", handleUpdateHostname); // update
  _httpServer.on("/c/scan", handleWifiScan); // scan active WiFi networks
  _httpServer.on("/c/add", handleWifiAdd);   // add credential for WiFi network
  _httpServer.on("/c/del", handleWifiDel);   // remove known WiFi network

  _httpServer.on("/generate_204", handleCaptiveRequest); // Android captive portal.
  _httpServer.on("/fwlink", handleCaptiveRequest);       // Microsoft captive portal.

  // generic not found
  _httpServer.onNotFound(handleGenericHTTP);
  Serial.println("OK");

}

void CaptivePortal::begin()
{
  // start _httpServer
  _httpServer.begin();
}

void CaptivePortal::loop()
{
  //DNS
  _dnsServer.processNextRequest();

  //HTTP
  _httpServer.handleClient();
}

/*******************************************************************************************************************************
 * WebServer wrapper functions
 */
WebServer &CaptivePortal::getHttpServer()
{
  return _httpServer;
}
void CaptivePortal::on(const String &uri, WebServer::THandlerFunction handler)
{
  _httpServer.on(uri, handler);
}
void CaptivePortal::on(const String &uri, HTTPMethod method, WebServer::THandlerFunction handler)
{
  _httpServer.on(uri, method, handler);
}
void CaptivePortal::on(const String &uri, HTTPMethod method, WebServer::THandlerFunction handler, WebServer::THandlerFunction ufn)
{
  _httpServer.on(uri, method, handler, ufn);
}
void CaptivePortal::sendHeader(const String &name, const String &value, bool first)
{
  _httpServer.sendHeader(name, value, first);
}
void CaptivePortal::sendFinal(int code, char *content_type, const String &content)
{
  _httpServer.send(code, content_type, content);
  _httpServer.client().stop();
}
void CaptivePortal::sendFinal(int code, const String &content_type, const String &content)
{
  _httpServer.send(code, content_type, content);
  _httpServer.client().stop();
}
