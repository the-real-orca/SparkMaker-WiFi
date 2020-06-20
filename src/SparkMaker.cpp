
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
	uint16_t statusRequestInterval = 60;	// periodic status request [s]
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
	SCANNING,
	FOUND,
	CONNECT,
	HANDSHAKE,
	ONLINE,
	READ_FILES
} BLESTATE;
static BLESTATE bleState = NA;


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
		Serial.println(advertisedDevice.toString().c_str());

		// check for SparkMaker services
		if (advertisedDevice.isAdvertisingService(SparkMakerServiceUUID))
		{
			// stop scanning
			BLEDevice::getScan()->stop();

			// create device
			if (sparkMakerBLEDevice)
				delete sparkMakerBLEDevice;
			sparkMakerBLEDevice = new BLEAdvertisedDevice(advertisedDevice);

			bleState = FOUND;
		}
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
	uint16_t len = BUFFER_SIZE - buffer_pos - 1;
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
		uint16_t id = 0;
		ptr = strrchr(filename, '.');
		if (ptr)
		{
			*ptr = 0x00;
			ptr++;
			id = strtol(ptr, NULL, 10);
			Serial.print("file list: #");
			Serial.print(id);
			Serial.print(" ");
			Serial.println(filename);
		}
		// add filename to file list
		SparkMaker::printer.filenames.insert(std::pair<uint16_t, std::string>(id, filename));
		return;
	}

	// layer
	if (strncmp(buffer, "F/S=", 4) == 0)
	{
		Serial.println("layer");
Serial.println(buffer);
		// TODO
		return;
	}

	// standby
	if (strcmp(buffer, "standby_sts") == 0)
	{
		Serial.println("STANDBY");
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
		// TODO
		return;
	}

	// no SD card
	if (strcmp(buffer, "nocard_sts") == 0)
	{
		Serial.println("NO_CARD");
		SparkMaker::printer.status = NO_CARD;
		// TODO
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
		bleState = SCANNING;
		SparkMaker::printer.status = DISCONNECTED;
	}
};

/**
 * disconnect from SparkMaker
 */
bool disconnectBLE()
{
	Serial.println("connectBLE");

	// delete old connections
	if (client)
	{
		Serial.println("disconnect previous client");
		client->disconnect();
	}

	txCharacteristic = NULL;
	rxCharacteristic = NULL;

	bleState = SCANNING;
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
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks);
	pBLEScan->setInterval(1000);
	pBLEScan->setWindow(500);
	// start BLE scan
	pBLEScan->setActiveScan(true);

	// SparkMaker state handling
	bleState = SCANNING;
	SparkMaker::printer.status = DISCONNECTED;
}

/**
 * SparkMaker BLE Interface loop function
 */
void SparkMaker::loop()
{
	static std::string cmd;

	switch (bleState)
	{
	case SCANNING:
	default:
		// scan for BLE devices
		printer.status = DISCONNECTED;
		Serial.println("scan BLE");
		BLEDevice::getScan()->start(1);
		break;

	case FOUND:
		// connect to SparkMaker device
		if ( connectBLE() )
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
			cmd = "PWD-OK\n";
			txCharacteristic->writeValue(cmd);
			Serial.println("OK");
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
			cmd = "scan-file\n";
			txCharacteristic->writeValue(cmd);
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
				SparkMaker::printer.lastStatusRequest = time;
				Serial.print("get status ... ");
				cmd = "PWD-OK\n";
				txCharacteristic->writeValue(cmd);
				Serial.println("OK");
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
void SparkMaker::send(String cmd)
{
	Serial.print("send command: "); Serial.println(cmd);
	if ( txCharacteristic )
	{
		cmd += "\n";
		txCharacteristic->writeValue(cmd.c_str());
	}
}

/**
 * start print
 */
void SparkMaker::startPrint()
{
	static std::string cmd;

	if ( printer.status == STANDBY || printer.status == FINISHED  )
	{
		Serial.print("start printing ... ");
		if ( txCharacteristic )
		{
			std::string cmd = "Start Printing;\n";
			txCharacteristic->writeValue(cmd);
			Serial.println("OK");
			printer.status = PRINTING;
		}
		else
		{
			bleState = SCANNING;
			Serial.println("Failed");
		}		
	}
}