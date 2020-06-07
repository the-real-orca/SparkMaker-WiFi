

#include "CaptivePortal.h"

// DNS _httpServer
static const byte DNS_PORT = 53;
DNSServer _dnsServer;

// Web
static const byte HTTP_PORT = 80;
WebServer _httpServer(HTTP_PORT);

// JSON
DynamicJsonDocument jsonDoc(2048);

// AP network configuration
IPAddress apIP(192, 168, 4, 1); // TODO
IPAddress netMask(255, 255, 255, 0); // TODO


String _hostname = "ESP_Captive";

/**
 * handle captive portal requests
 * 
 * @return: true if handled
 */
static boolean handleCaptiveRequest()
{
    // test for local requests
    String host = _httpServer.hostHeader();
    if ( host == WiFi.softAPIP().toString() )
    {
      // request to access point IP
      return false;    
    }

    if ( host == WiFi.localIP().toString() )
    {
      // request to client IP
      return false;    
    }

    if ( host == _hostname + ".local" )
    {
      // request to local host name
      return false;    
    }

    // redirect
    Serial.println("request captured and redirected");
    _httpServer.sendHeader("Location", "http://" + _hostname + ".local/portal", true);
    _httpServer.send(302, "text/html", "");
    _httpServer.client().stop();    
    return true;
}

/**
 * handle HTTP generic request
 */
static void handleGenericHTTP()
{
    // test for captive portal request
    if ( handleCaptiveRequest() )
        return;

    // send not found page
    Serial.print("handleNotFound: ");
    Serial.print(_httpServer.hostHeader()); Serial.println(_httpServer.uri());

    // HTML Header
    _httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _httpServer.sendHeader("Pragma", "no-cache");
    _httpServer.sendHeader("Expires", "-1");
    _httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    // HTML Content
    static String html;
    html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>" + _hostname + "</title></head><body>";
    html += "<i>" + _httpServer.uri() + "</i> not found";
    html += "</body></html>";
    _httpServer.send(404, "text/html", html);
    _httpServer.client().stop();
}

/**
 * string names for WiFi encryption
 */
const char* encryptionTypeNames[] = {
    "OPEN",
    "WEP",
    "WPA",
    "WPA2",
    "WPA & WPA2",
    "WPA2 Enterprise",
    "WPA3",
    "WPA3 Enterprise",
    "WIFI_AUTH_MAX"
};

/**
 * perform WiFi scan and send detected networks
 */
static void handleWifiScan()
{
  Serial.print("WiFi scan: ... ");

  jsonDoc.clear();
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, false); //WiFi.scanNetworks(async, show_hidden)
  for (int i = 0; i < n; i++) 
  {
    JsonObject network = jsonDoc.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = encryptionTypeNames[WiFi.encryptionType(i)];
  }

  // send json data
  String content;
  serializeJson(jsonDoc, content);
  _httpServer.send(200, "application/json", content);
  _httpServer.client().stop();  
  Serial.println(content);
}


/**
 * add AP to known APs
 */
static void handleWifiAdd()
{
// TODO
}

void CaptivePortal::setup()
{
  JsonObject config = loadConfig();
  CaptivePortal::setup(config);
}

void CaptivePortal::setup(const JsonObject& config)
{
    // copy config
    _hostname = config["hostname"] | _hostname;

    // init WiFi
WiFi.printDiag(Serial); // TODO
Serial.setDebugOutput(true);
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAPdisconnect(true);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.setHostname(_hostname.c_str());

    // create WiFi AP
    Serial.print("Start WiFi AP ... ");
    WiFi.disconnect();
    WiFi.softAPConfig(apIP, apIP, netMask);
    WiFi.softAP(_hostname.c_str());
    Serial.println("OK");

    // connect as Client
    String ssid = config["knownAP"]["ssid"];
    String pwd = config["knownAP"]["pwd"];
    uint16_t connectionTimeout = config["connectionTimeout"] | 5;
    Serial.print("Connect to "); Serial.print(ssid); Serial.print(" ... ");
    WiFi.begin(ssid.c_str(), pwd.c_str());
    // wait for connection
    for(int16_t i = connectionTimeout * 10; (i > 0) &&  (WiFi.status() != WL_CONNECTED); i--) {
        delay(100);
    }
    if ( WiFi.status() == WL_CONNECTED)
        Serial.println("OK");
    else
        Serial.println("Failed");

    // redirecting all the domains to the ESP
    Serial.print("Start DNS ... ");
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("OK");

    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println(WiFi.localIP());

    // setup HTTP server
    Serial.print("Start WebServer ... ");
    _httpServer.on("/c/scan", handleWifiScan);
    _httpServer.on("/c/add", handleWifiAdd);
// TODO    _httpServer.on("/c/del", handleWifiDel);

//    _httpServer.on("/generate_204", handleRoot); // Android captive portal. Maybe not needed. Might be handled by notFound handler.
//    _httpServer.on("/fwlink", handleRoot);       // Microsoft captive portal.


    // generic not found
    _httpServer.onNotFound(handleGenericHTTP);
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
WebServer& CaptivePortal::getHttpServer()
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
void CaptivePortal::sendHeader(const String& name, const String& value, bool first)
{
  _httpServer.sendHeader(name, value, first);
}
void CaptivePortal::sendFinal(int code, char* content_type, const String& content)
{
  _httpServer.send(code, content_type, content);
  _httpServer.client().stop();  
}
void CaptivePortal::sendFinal(int code, const String& content_type, const String& content)
{
  _httpServer.send(code, content_type, content);
  _httpServer.client().stop();  
}
