
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include "BLEUtils.h"

#include "SparkMaker.h"

// JSON
#include <ArduinoJson.h>
extern DynamicJsonDocument config;

// SparkMaker defaults
const static struct
{
	uint16_t statusRequestInterval = 20;	// periodic status request [s]
} defaultConfig;
static unsigned long statusRequestInterval;


// SparkMaker remote service
static BLEUUID SparkMakerServiceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID SparkMakerServiceRxUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID SparkMakerCharRxUUID("0000ffe4-0000-1000-8000-00805f9b34fb");
static BLEUUID SparkMakerServiceTxUUID("0000ffe5-0000-1000-8000-00805f9b34fb");
static BLEUUID SparkMakerCharTxUUID("0000ffe9-0000-1000-8000-00805f9b34fb");

static BLEAdvertisedDevice *sparkMakerBLEDevice = NULL;
static BLEClient *client = NULL;
static BLERemoteCharacteristic *txCharacteristic = NULL;
static BLERemoteCharacteristic *rxCharacteristic = NULL;
typedef enum
{
	NA,
	OFFLINE,
	SCANNING,
	FOUND,
	CONNECT,
	HANDSHAKE,
	ONLINE,
	READ_FILES
} BLESTATE;
static BLESTATE bleState = NA;
static uint32_t bleScantime = 0;
static BLEScan *pBLEScan = NULL;

extern int webServerActive;

/**
 * string names for WiFi encryption
 */
const char *statusNames[] = {
	"DISCONNECTED",
	"CONNECTING",
	"STANDBY",
	"FILELIST",
	"PRINTING",
	"PAUSE",
	"FINISHED",
	"STOPPING",
	"NO_CARD",
	"UPDATING"
};

/**
 * callback class for BLE advertised devices
 */
class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{

	/**
   * called on advertisement
   */
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		Serial.print("BLE Advertised Device found: ");
		Serial.print(advertisedDevice.toString().c_str()); Serial.print(", RSSI "); Serial.println( advertisedDevice.getRSSI() );

		// check for SparkMaker services
/* TODO		
		if (advertisedDevice.isAdvertisingService(SparkMakerServiceUUID))
		{
			// stop scanning
			if ( pBLEScan )
				pBLEScan->stop();

			// create device
			if (sparkMakerBLEDevice)
				delete sparkMakerBLEDevice;
			sparkMakerBLEDevice = new BLEAdvertisedDevice(advertisedDevice);

			bleState = FOUND;
		}
*/		
	}
};

/**
 * BLE callback
 * received subscribed data
 */
static void notifyCallback(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify)
{
	const size_t BUFFER_SIZE = 256;
	static char buffer[BUFFER_SIZE];
	static uint16_t buffer_pos = 0;

	// sanity check
	if (!txCharacteristic)
	{
		Serial.println("FAILURE: notification without txCharacteristic");
		bleState = SCANNING;
		return;
	}

	// append to buffer
	int16_t len = BUFFER_SIZE - buffer_pos - 2;
	if ( len < 0 )
	{
		buffer_pos = 0;
		buffer[buffer_pos] = 0x00;
	}
	if (length < len)
		len = length;
	memcpy(buffer + buffer_pos, data, len);
	buffer_pos = buffer_pos + len;
	buffer[buffer_pos] = 0x00;

	// do we have a complete line?
	char *ptr = strchr(buffer, '\n');
	if (!ptr)
	{
		// only partial line received, wait for more data
		return;
	}
	*ptr = 0x00;	// terminate string
	buffer_pos = 0; // buffer will be processed, next data will start a new line

	// heartbeat
	if (strcmp(buffer, "online") == 0)
	{
		SparkMaker::printer.heartbeat = millis();
		return;
	}

	// handshake
	if (strncmp(buffer, "P-", 2) == 0)
	{
		if (bleState < HANDSHAKE)
		{
			// send acknowledgement
			Serial.println("schedule handshake ... ");
			bleState = HANDSHAKE;
			SparkMaker::printer.status = CONNECTING;
		}
		return;
	}

	// selected file
	if (strncmp(buffer, "pf_", 3) == 0)
	{
		char *filename = buffer + 3;
		Serial.print("selected file: ");
		Serial.println(filename);
		SparkMaker::printer.currentFile = filename;
		return;
	}

	// file list
	if (strncmp(buffer, "f-", 2) == 0)
	{
		char *filename = buffer + 2;
		// read file index from message
		ptr = strrchr(filename, '.');
		if (ptr)
		{
			*ptr = 0x00;
			ptr++;
			uint16_t id = 0;
			id = atoi(ptr);
			Serial.print("file list: #");
			Serial.print(id);
			Serial.print(" ");
			Serial.println(filename);

			// add filename to file list
			SparkMaker::printer.filenames.insert(std::pair<std::string, uint16_t>(filename, id));
		}
		return;
	}

	// layer
	if (strncmp(buffer, "F/S=", 4) == 0)
	{
		Serial.print("layer: ");
		ptr = buffer + 4;
		SparkMaker::printer.currentLayer = atoi(ptr);
		ptr = strchr(ptr, '/');
		if ( ptr )
			SparkMaker::printer.totalLayers = atoi(++ptr);
		Serial.print(SparkMaker::printer.currentLayer); Serial.print('/'); Serial.println(SparkMaker::printer.totalLayers);
		return;
	}

	// standby
	if (strcmp(buffer, "standby_sts") == 0)
	{
		Serial.println("STANDBY");

		if ( SparkMaker::printer.status == NO_CARD )
		{
			// read SD Card
			bleState = READ_FILES;
		}
		SparkMaker::printer.status = STANDBY;
		return;
	}

	// printing
	if (strcmp(buffer, "printing_sts") == 0)
	{
		Serial.println("PRINTING");
		SparkMaker::printer.status = PRINTING;
		return;
	}

	// pause
	if (strcmp(buffer, "pause_sts") == 0)
	{
		Serial.println("PAUSE");
		SparkMaker::printer.status = PAUSE;
		return;
	}

	// resume
	if (strcmp(buffer, "pause-over") == 0)
	{
		Serial.println("PRINTING");
		SparkMaker::printer.status = PRINTING;
		return;
	}

	// STOP
	if (strcmp(buffer, "stop_sts") == 0)
	{
		Serial.println("STOPPING");
		SparkMaker::printer.status = STOPPING;
		return;
	}

	// finished
	if (strcmp(buffer, "printo_sts") == 0)
	{
		Serial.println("FINISHED");
		SparkMaker::printer.status = FINISHED;
		SparkMaker::printer.finishTime = millis() / 1000;
		return;
	}

	// no SD card
	if (strcmp(buffer, "nocard_sts") == 0)
	{
		Serial.println("NO_CARD");
		SparkMaker::printer.status = NO_CARD;
		SparkMaker::printer.filenames.clear();
		return;
	}

	// all files sent
	if (strcmp(buffer, "scan-finish") == 0)
	{
		Serial.println("scan-finish");
		// nothing to do
		return;
	}

	// update
	if (strcmp(buffer, "update_sts") == 0)
	{
		Serial.println("UPDATING");
		SparkMaker::printer.status = UPDATING;
		return;
	}

	// update
	if (strcmp(buffer, "OK") == 0)
	{
		Serial.println("OK");
		return;
	}	

	// unknown message
	Serial.print("UNKNOWN MESSAGE: ");
	Serial.println(buffer);
}

/**
 * BLE connection / disconnection callback
 */
class ConnectionCallback : public BLEClientCallbacks
{
	void onConnect(BLEClient *client)
	{
	}

	void onDisconnect(BLEClient *client)
	{
		Serial.println("onDisconnect");
		bleState = OFFLINE;
		SparkMaker::printer.status = DISCONNECTED;
	}
};

/**
 * disconnect from SparkMaker
 */
bool disconnectBLE()
{
	Serial.println("disconnect BLE");

	// delete old connections
	if (client)
	{
		Serial.println("disconnect previous client");
		client->disconnect();
	}

	txCharacteristic = NULL;
	rxCharacteristic = NULL;

	bleState = OFFLINE;
	
	return true;
}

/**
 * connect to SparkMaker and subscribe to status
 */
bool connectBLE()
{
	Serial.println("connect BLE");

	// delete old connections
	disconnectBLE();

	if (!sparkMakerBLEDevice)
		return false;

	// use do-while(false) as poor-mans exception handling
	do
	{
		Serial.print("connecting to ");
		Serial.print(sparkMakerBLEDevice->getAddress().toString().c_str());
		Serial.println(" ...");

		// create new BLE client
		client = BLEDevice::createClient();

		client->setClientCallbacks(new ConnectionCallback());
		client->connect(sparkMakerBLEDevice);
		if (!client->isConnected())
			break;

		Serial.println("registering to SparkMakerServiceRxUUID ...");
		auto rxService = client->getService(SparkMakerServiceRxUUID);
		if (!rxService)
			break;
		rxCharacteristic = rxService->getCharacteristic(SparkMakerCharRxUUID);
		if (!rxCharacteristic || !rxCharacteristic->canNotify())
			break;
		rxCharacteristic->registerForNotify(notifyCallback);

		Serial.println("connect SparkMakerServiceTxUUID ...");
		auto txService = client->getService(SparkMakerServiceTxUUID);
		if (!txService)
			break;
		txCharacteristic = txService->getCharacteristic(SparkMakerCharTxUUID);
		if (!txCharacteristic)
			break;

		Serial.println("connected to device");
		bleState = CONNECT;
		return true;

	} while (false);

	// broke out of connection process
	client->disconnect();
	return false;
}

Printer SparkMaker::printer;

/**
 * SparkMaker BLE Interface setup
 */
void SparkMaker::setup()
{
	// start Bluetooth Low Energy
	BLEDevice::init(config["hostname"]);

	// parse config
	statusRequestInterval = ( config["SparkMaker"]["statusRequestInterval"] | defaultConfig.statusRequestInterval ) * 1000;

	// get BLE scanner object
	pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks);
	pBLEScan->setWindow(2000);
	pBLEScan->setInterval(200);
	pBLEScan->setActiveScan(true);
	pBLEScan->clearResults();
	pBLEScan->start(2);


	// SparkMaker state handling
	bleState = SCANNING;
	SparkMaker::printer.status = DISCONNECTED;
}

/**
 * SparkMaker BLE Interface loop function
 */
void SparkMaker::loop()
{
	uint32_t time = millis();
	switch (bleState)
	{
	case NA:
	case OFFLINE:
		// nothing to do
		break;
		
	case SCANNING:
	default:
		// scan for BLE devices
		printer.status = DISCONNECTED;
		SparkMaker::printer.filenames.clear();
		if ( (time - bleScantime) > 3500 && pBLEScan)
		{
			Serial.println("scan BLE");
			pBLEScan->start(1);
			bleScantime = time;
		}
		break;

	case FOUND:
		// connect to SparkMaker device
		if ( connectBLE() && pBLEScan)
		{
			Serial.println("Connecting to SparkMaker");
			printer.status = CONNECTING;
		}
		else
		{
			Serial.println("FAILURE: Cannot connect to SparkMaker");
			bleState = SCANNING;
		}
		break;

	case CONNECT:
		break;

	case HANDSHAKE:
		// send handshake acknowledgement
		Serial.print("send handshake ... ");
		if ( txCharacteristic )
		{
			SparkMaker::printer.lastStatusRequest = 0;
			SparkMaker::requestStatus();
			bleState = READ_FILES;
		}
		else
		{
			bleState = SCANNING;
			Serial.println("Failed");
		}
		break;

	case ONLINE:
		break;

	case READ_FILES:
		// query files from printer
		Serial.print("read files ... ");
		if ( txCharacteristic )
		{
			SparkMaker::printer.filenames.clear();
			txCharacteristic->writeValue("scan-file\n");
			Serial.println("OK");
			bleState = ONLINE;
		}
		else
		{
			bleState = SCANNING;
			Serial.println("Failed");
		}
		break;
	}

	if (bleState >= CONNECT)
	{
		// trigger status messages
		unsigned long time = millis();
		if ((time - printer.lastStatusRequest) > statusRequestInterval)
		{
			if ( txCharacteristic )
			{
				SparkMaker::requestStatus();
			}
			else
			{
				bleState = SCANNING;
				Serial.println("Failed");
			}
		}
	}
}

/**
 * connect printer
 */
void SparkMaker::connect()
{
	disconnectBLE();
	
	// start BLE scanning
	bleState = SCANNING;
	SparkMaker::printer.status = DISCONNECTED;	
}

/**
 * disconnect printer
 */
void SparkMaker::disconnect()
{
	disconnectBLE();
	printer.status = DISCONNECTED;
}

/**
 * send command
 */
void SparkMaker::send(const String &cmd)
{
	Serial.println("send command: "); Serial.println(cmd);
	if ( txCharacteristic )
	{
		txCharacteristic->writeValue(cmd.c_str());
	}
}

/**
 * send status request
 */
void SparkMaker::requestStatus()
{
	Serial.println("send status request");
	if ( txCharacteristic )
	{
		SparkMaker::printer.lastStatusRequest = millis();
		txCharacteristic->writeValue("PWD-OK\n");
	}
}

/**
 * relative move Z position
 */
void SparkMaker::move(int16_t pos)
{
	if ( pos == 0 || pos < -50 || pos > 50 )
		return;

	if ( printer.status == STANDBY || printer.status == FINISHED || printer.status == PAUSE )
	{
		Serial.println("move Z position");
		String cmd = "G1 Z" + String(pos) + ";";
		if ( txCharacteristic )
			txCharacteristic->writeValue(cmd.c_str());
	}
}

/**
 * move Z to home position
 */
void SparkMaker::home()
{
	if ( printer.status == STANDBY || printer.status == FINISHED )
	{
		Serial.println("home Z");
		if ( txCharacteristic )
			txCharacteristic->writeValue("G28 Z0;");
	}
}

/**
 * start print
 */
void SparkMaker::print(const String &filename)
{
	if ( printer.status == STANDBY || printer.status == FINISHED  )
	{
		if ( !txCharacteristic )
			return;

		Serial.print("select file: "); Serial.println(filename);
		if ( !filename.isEmpty() )
		{
			// search for filename
			auto it = SparkMaker::printer.filenames.find(filename.c_str());
			if ( it == SparkMaker::printer.filenames.end() )
				return;
			uint16_t id = it->second;
			
			// select file to print
			String cmd = "file-" + String(id);
			txCharacteristic->writeValue(cmd.c_str());
			delay(100);

		}

		Serial.println("start printing");
		txCharacteristic->writeValue("Start Printing;");
		SparkMaker::printer.startTime = millis() / 1000;
		SparkMaker::printer.finishTime = 0;
	}
}

/**
 * stop print
 */
void SparkMaker::stopPrint()
{
	Serial.println("stop printing");
	if ( txCharacteristic )
		txCharacteristic->writeValue("Stop Printing;");
}

/**
 * pause print
 */
void SparkMaker::pausePrint()
{
	if ( printer.status == PRINTING  )
	{
		Serial.println("pause printing");
		if ( txCharacteristic )
			txCharacteristic->writeValue("Pause Printing;");
	}
}

/**
 * resume print
 */
void SparkMaker::resumePrint()
{
	if ( printer.status == PAUSE  )
	{
		Serial.println("resume printing");
		if ( txCharacteristic )
			txCharacteristic->writeValue("Keep Printing;");
	}
}

/**
 * emergency stop
 */
void SparkMaker::emergencyStop()
{
	Serial.println("emergency stop");
	if ( txCharacteristic )
		txCharacteristic->writeValue("Emergency;");
}
