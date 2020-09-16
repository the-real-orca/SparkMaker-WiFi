

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


// DNS _httpServer
static const byte DNS_PORT = 53;
DNSServer _dnsServer;
static bool _dnsServerActive = false;

// Web
static const byte HTTP_PORT = 80;
WebServer _httpServer(HTTP_PORT);

// JSON
DynamicJsonDocument config(configJsonSize);
DynamicJsonDocument tempJson(tempJsonSize);

// Captive Portal defaults
const static struct
{
	uint16_t wifiClientConnectionTimeout = 5;
	uint16_t portalTimeout = 120;
	uint8_t softAP_IP[4] = {192, 168, 4, 1};
	uint8_t subnet[4] = {255, 255, 255, 0};
	String hostname = "ESP-" + String(ESP_getChipId());
	String portalPath = "portal.html";
} defaultConfig;
// parameters
static bool _portalActive = false;
static uint16_t _portalTimeout;
static uint16_t _portalStarted;
static uint16_t _wifiClientConnectionTimeout;

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
void startCaptivePortal()
{
	Serial.print("Start WiFi AP: " + config["hostname"].as<String>() + " ... ");
	WiFi.softAPdisconnect(true);

	IPAddress softAP_IP(defaultConfig.softAP_IP);
	if (config["CaptivePortal"]["ip"].is<const char *>())
		softAP_IP.fromString(config["CaptivePortal"]["ip"].as<const char *>());
	IPAddress subnet(defaultConfig.subnet);
	if (config["CaptivePortal"]["subnet"].is<const char *>())
		subnet.fromString(config["CaptivePortal"]["subnet"].as<const char *>());

	// create WiFi AP
	WiFi.softAPConfig(softAP_IP, softAP_IP, subnet);
	WiFi.softAP(config["hostname"].as<const char*>());
	_portalActive = true;
	_portalStarted = millis()/1000;
	Serial.println("OK");

	// redirecting all the domains to the ESP
	Serial.print("Start DNS ... ");
	_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
	_dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
	_dnsServerActive = true;
	Serial.println("OK");

}

/**
 * stop captive portal AP
 */
void stopCaptivePortal()
{
	Serial.print("Stop WiFi AP: " + config["hostname"].as<String>() + " ... ");
	WiFi.softAPdisconnect(true);

	// redirecting all the domains to the ESP
	_dnsServer.stop();
	_dnsServerActive = false;

	// switch to station only mode
	WiFi.mode(WIFI_MODE_STA);
	_portalActive = false;
	Serial.println("OK");
}


/**
 * connect as WiFi Client
 * use strongest available network in known credentials list
 */
void findAndConnectWifiNetwork()
{
	// scan for networks and connect to strongest one we have credentials
	WiFi.disconnect();
	WiFi.scanDelete();
	int n = WiFi.scanNetworks(false, false);

	// list networks
	for (int j = 0; j < n; j++)
	{
		Serial.print("SSID "); Serial.print( WiFi.SSID(j) );
		Serial.print(" RSSI "); Serial.println( WiFi.RSSI(j) );
	}

	// connect to first known network
	bool connected = false;
	for (int j = 0; j < n && !connected; j++)
	{
		String ssid = WiFi.SSID(j);
		Serial.print("Found Network: "); Serial.println(ssid);
		if (config["Credentials"].containsKey(ssid))
		{
			String pwd = config["Credentials"][ssid];
			Serial.print("Connect to "); Serial.print(ssid); Serial.print(" ... ");

			WiFi.begin(ssid.c_str(), pwd.c_str());
			// wait for connection
			for (int16_t i = _wifiClientConnectionTimeout * 10; (i > 0) && (WiFi.status() != WL_CONNECTED); i--)
			{
				delay(100);
			}
			if (WiFi.status() == WL_CONNECTED)
			{
				connected = true;
				Serial.println("OK");
			}
			else
				Serial.println("Failed");
		}
	}
}

/**
 * connect as WiFi Client
 * try given credentials, use strongest available network in known credentials list as fallback
 */
void connectWifiNetwork(String ssid, String pwd)
{
	WiFi.disconnect();
	Serial.print("Connect to ");
	Serial.print(ssid);
	Serial.print(" ... ");
	WiFi.begin(ssid.c_str(), pwd.c_str());
	// wait for connection
	for (int16_t i = _wifiClientConnectionTimeout * 10; (i > 0) && (WiFi.status() != WL_CONNECTED); i--)
	{
		delay(100);
	}

	if (WiFi.status() == WL_CONNECTED)
		Serial.println("OK");
	else
	{
		Serial.println("Failed");

		// fallback to known networks
		findAndConnectWifiNetwork();
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
	String portalPath = config["CaptivePortal"]["path"] | defaultConfig.portalPath;
	_httpServer.sendHeader("Location", "http://" + config["hostname"].as<String>() + ".local/" + portalPath, true);
	_httpServer.send(302, "text/html", "");
	_httpServer.client().stop();
}

/**
 * get MIME type from filename
 */
static String getContentType(String filename)
{
	if (filename.endsWith(".htm"))
		return "text/html";
	if (filename.endsWith(".html"))
		return "text/html";
	if (filename.endsWith(".css"))
		return "text/css";
	if (filename.endsWith(".js"))
		return "application/javascript";
	if (filename.endsWith(".png"))
		return "image/png";
	if (filename.endsWith(".gif"))
		return "image/gif";
	if (filename.endsWith(".jpg"))
		return "image/jpeg";
	if (filename.endsWith(".ico"))
		return "image/x-icon";
	if (filename.endsWith(".xml"))
		return "text/xml";
	if (filename.endsWith(".pdf"))
		return "application/x-pdf";
	if (filename.endsWith(".zip"))
		return "application/x-zip";
	if (filename.endsWith(".gz"))
		return "application/x-gzip";
	return "text/plain";
}

/**
 * send static file
 * 
 * @return true if file exist and was sent
 */
static bool handleFile(String path)
{
	Serial.print("handleFile: "); Serial.println(path);

	// security check
	if (path.indexOf("..") >= 0)
		return false;

	// prepend web server folder
	path = "/public" + path;

	// add default page
	if (path.endsWith("/"))
		path += "index.html";

	// MIME type
	String contentType = getContentType(path);

	String pathCompressed = path + ".gz";
	bool found = SPIFFS.exists(path);
	bool foundCompressed = SPIFFS.exists(pathCompressed);
	if (found || foundCompressed)
	{
		// use compressed version if exist
		if (foundCompressed)
			path = pathCompressed;

		// send file
		_httpServer.sendHeader("Cache-Control", "public, max-age=36000"); // enable cache
		_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
		File file = SPIFFS.open(path, "r");
		_httpServer.streamFile(file, contentType);
		_httpServer.client().setNoDelay(true);
		file.close();
		_httpServer.client().stop();

		Serial.println(String("Sent file: ") + path);
		return true;
	}

	Serial.println(String("File Not Found: ") + path);
	return false;
}

/**
 * handle HTTP generic request
 */
static void handleGenericHTTP()
{
	Serial.print("handleGenericHTTP ");Serial.println(_httpServer.uri());

	// test for captive portal request
	if (isCaptiveRequest())
	{
		handleCaptiveRequest();
		return;
	}

	// load from SPIFFS
	if (handleFile(_httpServer.uri()))
		return;

	// send not found page
	Serial.print("handleNotFound: ");
	Serial.print(_httpServer.hostHeader());
	Serial.println(_httpServer.uri());

	// HTML Header
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
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
	auto portal = tempJson.createNestedObject("CaptivePortal");
	portal["ssid"] = config["hostname"];
	portal["ip"] = WiFi.softAPIP().toString();
	auto client = tempJson.createNestedObject("client");
	client["ssid"] = WiFi.SSID();
	client["ip"] = WiFi.localIP().toString();

	// send json data
	String content;
	serializeJsonPretty(tempJson, content);
	_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
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
	tempJson.clear();
	WiFi.scanDelete();
	int n = WiFi.scanNetworks(false, false); //WiFi.scanNetworks(async, show_hidden)
	for (int i = 0; i < n; i++)
	{
		JsonObject ap = tempJson.createNestedObject();
		ap["ssid"] = WiFi.SSID(i);
		ap["rssi"] = WiFi.RSSI(i);
		ap["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

		// check for currently connected
		if (WiFi.SSID(i) == WiFi.SSID())
			ap["connected"] = true;
	}

	// augment known networks
	auto credentials = config["Credentials"].as<JsonObject>();
	for (const auto &kv : credentials)
	{
		// search in networks
		bool found = false;
		for (auto ap : tempJson.as<JsonArray>())
		{
			if (kv.key() == ap["ssid"])
			{
				// we have credentials for this network
				found = true;
				ap["known"] = true;
				break;
			}
		}
		if (!found)
		{
			// add to network list
			JsonObject newNet = tempJson.createNestedObject();
			newNet["ssid"] = kv.key();
			newNet["encrypted"] = (kv.value().as<String>().length() > 0);
			newNet["known"] = true;
		}
	}

	// send json data
	String content;
	serializeJsonPretty(tempJson, content);
	_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
	_httpServer.send(200, "application/json", content);
	_httpServer.client().stop();
	Serial.println(content);
}

/**
 * add AP to known networks
 */
static void handleWifiAdd()
{
	String ssid = sanity(_httpServer.arg("ssid"));
	String pwd = sanity(_httpServer.arg("pwd"));
	Serial.print("add '" + ssid + "' to known networks list");

	auto credentials = config["Credentials"].as<JsonObject>();
	credentials[ssid] = pwd;
	saveConfig(config);

	// send reply
	_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
	_httpServer.send(200, "application/json", "{\"status\": \"OK\"}");
	_httpServer.client().stop();

	// connect to WiFi
	if (ssid != WiFi.SSID())
	{
		delay(100);
		connectWifiNetwork(ssid, pwd);
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

	config["Credentials"].remove(ssid);
	saveConfig(config);

	// send reply
	_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
	_httpServer.send(200, "application/json", "{\"status\": \"OK\"}");
	_httpServer.client().stop();

	// connect to WiFi
	if (reconnect)
	{
		delay(100);
		findAndConnectWifiNetwork();
	}
}

/**
 * update host name
 */
static void handleUpdateHostname()
{
	String hostname = sanity(_httpServer.arg("hostname"));

	// update hostname
	if (!hostname.length())
	{
		// nothing to do, just send summary
		return handleInfo();
	}
	config["hostname"] = hostname;
	saveConfig(config);

	// answer with updated info
	handleInfo();

	// re-start AP
	startCaptivePortal();
}

// FIXME remove listDir() Debug only!!!
/*
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
*/

void CaptivePortal::setup()
{
	// file system
	if ( !SPIFFS.begin() )
	{
		// format filesystem if failed
		SPIFFS.format();
		SPIFFS.begin();
	}

//FIXME	listDir(SPIFFS, "/", 0);

	// load config
	loadConfig(config);
	loadConfig(tempJson, "/private.json", config.as<JsonObject>()); // overwrite with private config

	// sanity check for config
	if (!config["hostname"])
		config["hostname"] = defaultConfig.hostname;
	_wifiClientConnectionTimeout = config["CaptivePortal"]["wifiClientConnectionTimeout"] | defaultConfig.wifiClientConnectionTimeout;
	_portalTimeout = config["CaptivePortal"]["portalTimeout"] | defaultConfig.portalTimeout;

	// init WiFi
	WiFi.setAutoReconnect(false);
	WiFi.persistent(false);

	if ( config["CaptivePortal"]["wifiClientConnectionTimeout"].as<bool>() )
	{
		// start AP and Captive Portal
		WiFi.mode(WIFI_MODE_APSTA);
		startCaptivePortal();
	} else {
		// Captive Portal is disabled
		Serial.println("Captive Portal is disabled");		
		WiFi.mode(WIFI_MODE_STA);
	}

	// connect as WiFi Client
	findAndConnectWifiNetwork();

	Serial.print("IP Address: ");
	Serial.print(WiFi.softAPIP());
	Serial.print(", ");
	Serial.println(WiFi.localIP());

	// enable mDNS
	Serial.print("Start mDNS ... ");
	if (MDNS.begin(config["hostname"].as<const char*>()))
	{
		MDNS.addService("http", "tcp", 80);
		Serial.println("OK");
	}
	else
	{
		Serial.println("Failed !");
	}	

	// setup HTTP server
	Serial.print("Start WebServer ... ");

	_httpServer.on("/c/info", handleInfo);				 // send status info
	_httpServer.on("/c/hostname", handleUpdateHostname); // update
	_httpServer.on("/c/scan", handleWifiScan);			 // scan active WiFi networks
	_httpServer.on("/c/add", handleWifiAdd);			 // add credential for WiFi network
	_httpServer.on("/c/del", handleWifiDel);			 // remove known WiFi network

	_httpServer.on("/generate_204", handleCaptiveRequest); // Android captive portal.
	_httpServer.on("/fwlink", handleCaptiveRequest);	   // Microsoft captive portal.

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
	if ( _dnsServerActive )
		_dnsServer.processNextRequest();

	// Portal
	if ( _portalActive )
	{
		uint16_t time = millis() / 1000 - _portalStarted;
		if ( time > _portalTimeout )
		{
			// stop Captive Portal
			// 
			stopCaptivePortal();
		}
	}

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
	_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
	_httpServer.send(code, content_type, content);
	_httpServer.client().stop();
}
void CaptivePortal::sendFinal(int code, const String &content_type, const String &content)
{
	_httpServer.sendHeader("Access-Control-Allow-Origin", "*"); // allow CORS
	_httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");	// disable cache
	_httpServer.send(code, content_type, content);
	_httpServer.client().stop();
}
