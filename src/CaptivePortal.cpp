

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
static bool _dnsServerActive = false;

// Web
static const byte HTTP_PORT = 80;
AsyncWebServer _httpServer(HTTP_PORT);

// JSON
DynamicJsonDocument config(configJsonSize);
DynamicJsonDocument tempJson(tempJsonSize);

// Captive Portal defaults
const static struct
{
	uint16_t wifiClientConnectionTimeout = 5;
	uint8_t softAP_IP[4] = {192, 168, 4, 1};
	uint8_t subnet[4] = {255, 255, 255, 0};
	String hostname = "ESP-" + String(ESP_getChipId());
	String portalPath = "portal.html";
} defaultConfig;
static uint16_t wifiClientConnectionTimeout;

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
	WiFi.softAPdisconnect(true);

	IPAddress softAP_IP(defaultConfig.softAP_IP);
	if (config["CaptivePortal"]["ip"].is<const char *>())
		softAP_IP.fromString(config["CaptivePortal"]["ip"].as<const char *>());
	IPAddress subnet(defaultConfig.subnet);
	if (config["CaptivePortal"]["subnet"].is<const char *>())
		subnet.fromString(config["CaptivePortal"]["subnet"].as<const char *>());

	// create WiFi AP
	WiFi.softAPConfig(softAP_IP, softAP_IP, subnet);
	WiFi.softAP(config["hostname"]);
	Serial.println("OK");

	// redirecting all the domains to the ESP
	Serial.print("Start DNS ... ");
	_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
	_dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
	_dnsServerActive = true;
	Serial.println("OK");
}

/**
 * connect as WiFi Client
 * use strongest available network in known credentials list
 */
void connectWiFiClient()
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
			for (int16_t i = wifiClientConnectionTimeout * 10; (i > 0) && (WiFi.status() != WL_CONNECTED); i--)
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
void connectWiFiClient(String ssid, String pwd)
{
	WiFi.disconnect();
	Serial.print("Connect to ");
	Serial.print(ssid);
	Serial.print(" ... ");
	WiFi.begin(ssid.c_str(), pwd.c_str());
	// wait for connection
	for (int16_t i = wifiClientConnectionTimeout * 10; (i > 0) && (WiFi.status() != WL_CONNECTED); i--)
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
static boolean isCaptiveRequest(AsyncWebServerRequest *request)
{
	// test for local requests
	String host = request->host();
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
static void handleCaptiveRequest(AsyncWebServerRequest *request)
{
	// redirect
	Serial.println("request captured and redirected");
	String portalPath = config["CaptivePortal"]["path"] | defaultConfig.portalPath;
	request->redirect("http://" + config["hostname"].as<String>() + ".local/" + portalPath);
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
static bool handleFile(AsyncWebServerRequest *request, String path)
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
		auto file = SPIFFS.open(path);
		int32_t size = file.size();
		file.close();
		AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path);
/*		
		AsyncWebServerResponse *response = request->beginResponse(contentType, size, [path, size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
		AsyncWebServerResponse *response = request->beginChunkedResponse(contentType, [path, size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
			if ( index >= size )
			{
				Serial.print("END "); Serial.println(path);
				return 0;
			}

Serial.print("  "); Serial.print(path); Serial.print(" "); Serial.print(index); Serial.print(" / "); Serial.print(size); Serial.print(" maxLen: "); Serial.print(maxLen); // TODO debut output
// TODO			if ( maxLen > 2000 ) maxLen = 2000;
Serial.print(" -> "); Serial.print(maxLen); // TODO debut output
			if ( maxLen == 0 ) return 0;
			auto file = SPIFFS.open(path);
			file.seek(index);
			size_t len = file.read(buffer, maxLen);
			file.close();
Serial.print(" -> "); Serial.println(len); // TODO debut output
			// last part to send
			if ( index + len >= size )
			{
				Serial.print("File finished: "); Serial.println(path);
			}
			return len;
		});
*/			

		if (foundCompressed)
			response->addHeader("Content-Encoding", "gzip");
		request->send(response);
		return true;
	}

	Serial.println(String("File Not Found: ") + path);
	return false;
}

/**
 * handle HTTP generic request
 */
static void handleGenericHTTP(AsyncWebServerRequest *request)
{
	// test for captive portal request
	if ( isCaptiveRequest(request) )
	{
		handleCaptiveRequest(request);
		return;
	}

	// load from SPIFFS
	if (handleFile(request, request->url()))
		return;

	// send not found page
	Serial.print("handleNotFound: ");
	Serial.print(request->host());
	Serial.println(request->url());

	// HTML Content
	static String html;
	html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><title>" + config["hostname"].as<String>() + "</title></head><body>";
	html += "<i>" + request->url() + "</i> not found";
	html += "</body></html>";
	AsyncWebServerResponse *response = request->beginResponse(404, "text/html", "Hello World!");

	// HTML Header
	response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	response->addHeader("Pragma", "no-cache");
	response->addHeader("Expires", "-1");
	request->send(response);
}


/**
 * handle info request
 */
static void handleInfo(AsyncWebServerRequest *request)
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
	request->send(200, "application/json", content);
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
static void handleWifiScan(AsyncWebServerRequest *request)
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
	request->send(200, "application/json", content);
	Serial.println(content);
}

/**
 * add AP to known networks
 */
static void handleWifiAdd(AsyncWebServerRequest *request)
{
	String ssid = sanity(request->arg("ssid"));
	String pwd = sanity(request->arg("pwd"));
	Serial.print("add '" + ssid + "' to known networks list");

	auto credentials = config["Credentials"].as<JsonObject>();
	credentials[ssid] = pwd;
	saveConfig(config);

	// send reply
	request->send(200, "application/json", "{\"status\": \"OK\"}");

	// connect to WiFi
	if (ssid != WiFi.SSID())
	{
		delay(100);
		connectWiFiClient(ssid, pwd);
	}
}

/**
 * remove AP from known networks
 */
static void handleWifiDel(AsyncWebServerRequest *request)
{
	String ssid = request->arg("ssid");
	Serial.print("remove '" + ssid + "' from known networks list: ... ");

	bool reconnect = (ssid == WiFi.SSID());

	config["Credentials"].remove(ssid);
	saveConfig(config);

	// send reply
	request->send(200, "application/json", "{\"status\": \"OK\"}");

	// connect to WiFi
	if (reconnect)
	{
		delay(100);
		connectWiFiClient();
	}
}

/**
 * update host name
 */
static void handleUpdateHostname(AsyncWebServerRequest *request)
{
	String hostname = sanity(request->arg("hostname"));

	// update hostname
	if (!hostname.length())
	{
		// nothing to do, just send summary
		return handleInfo(request);
	}
	config["hostname"] = hostname;
	saveConfig(config);

	// answer with updated info
	handleInfo(request);

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
// FIX number of files limited	
	SPIFFS.begin(true, "", 10); // format filesystem if failed
	listDir(SPIFFS, "/", 0);

	// load config
	loadConfig(config);
	loadConfig(tempJson, "/private.json", config.as<JsonObject>()); // overwrite with private config

	// sanity check for config
	if (!config["hostname"])
		config["hostname"] = defaultConfig.hostname;
	wifiClientConnectionTimeout = config["CaptivePortal"]["wifiClientConnectionTimeout"] | defaultConfig.wifiClientConnectionTimeout;

	// init WiFi
// TODO station only	WiFi.mode(WIFI_MODE_APSTA);
	WiFi.mode(WIFI_MODE_STA);
	WiFi.setAutoReconnect(false);
	WiFi.persistent(false);
	WiFi.setHostname(config["hostname"]);

	// start AP
// TODO startCaptiveAP();

	// connect as WiFi Client
	connectWiFiClient();

	Serial.print("IP Address: ");
	Serial.print(WiFi.softAPIP());
	Serial.print(", ");
	Serial.println(WiFi.localIP());

	// enable mDNS
	Serial.print("Start mDNS ... ");
	if (MDNS.begin(config["hostname"]))
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
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");	// enable CORS

	_httpServer.on("/c/info", handleInfo);				 // send status info
	_httpServer.on("/c/hostname", handleUpdateHostname); // update
	_httpServer.on("/c/scan", handleWifiScan);			 // scan active WiFi networks
	_httpServer.on("/c/add", handleWifiAdd);			 // add credential for WiFi network
	_httpServer.on("/c/del", handleWifiDel);			 // remove known WiFi network

	_httpServer.on("/generate_204", handleCaptiveRequest); // Android captive portal.
	_httpServer.on("/fwlink", handleCaptiveRequest);	   // Microsoft captive portal.

	// generic not found
// TODO	_httpServer.serveStatic("/", SPIFFS, "/public/").setDefaultFile("index.html");
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
}

/*******************************************************************************************************************************
 * WebServer wrapper functions
 */
AsyncWebServer &CaptivePortal::getHttpServer()
{
	return _httpServer;
}
void CaptivePortal::on(const String &uri, ArRequestHandlerFunction handler)
{
	_httpServer.on(uri.c_str(), handler);
}
void CaptivePortal::on(const String &uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler)
{
	_httpServer.on(uri.c_str(), method, handler);
}
void CaptivePortal::on(const String &uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler, ArUploadHandlerFunction ufn)
{
	_httpServer.on(uri.c_str(), method, handler, ufn);
}
