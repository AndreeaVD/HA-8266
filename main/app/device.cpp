#include "device.h"

bool devicesLoadFromDisk();
bool deviceReadFromDisk(char* path);
bool deviceWriteToDisk(CGenericDevice *dev);

 Vector<CGenericDevice*> g_activeDevices;

unsigned short gDevicesState = 0x0000;

struct devCtl
{
	unsigned short dev;
	uint8_t (*initFunc)(uint8_t);
};

devCtl gDevices[] =
{
	{DEV_RADIO, devRadio_init},
	{DEV_SDCARD, devSDCard_init},
	{DEV_RGB, devRGB_init},
	{DEV_MQ135, devMQ135_init},
	{DEV_DHT22, devDHT22_init},
	{DEV_WIFI, devWiFi_init},
	{DEV_DSTEMP, devDSTemp_init},
	{DEV_UART, devUART_init}
};

#define NUM_DEVICES (sizeof(gDevices)/sizeof(gDevices[0]))

void loadSavedDevices()
{

	//0;2;LightHall;
	const char *devicesString = "2;1;0;IndoorTemp;29.2;16.0;26.5;0;1;1;1;2;1;Heater\\;;200;50;100;1;0;";

	int numDevices, iDev = 0, devType, numWatchers;

	LOG_I("Loading saved devs\n");
	if(!skipInt(&devicesString, &numDevices))return;

	while(iDev++ < numDevices)
	{
		LOG_I("Loading dev %d of %d", iDev, numDevices);
		if(!skipInt(&devicesString, &devType))return;


		switch(devType)
		{
			case devTypeLight:
			{
				LOG_I("LIGHT");
				CDeviceLight *device = new CDeviceLight();
				if(!device)
				{
					LOG_E("Fatal: heap(%d)", iDev);
					return;
				}
				LOG_I("deserialize");
				if( device->deserialize(&devicesString))
				{
					g_activeDevices.addElement(device);
				}
				else
				{
					LOG_E("Fatal: deserial(%d)", iDev);
				}
			}
			break;

			case devTypeTH:
			{
				LOG_I("TH");
				CDeviceTempHumid *device = new CDeviceTempHumid();
				if(!device)
				{
					LOG_E("Fatal: heap(%d)", iDev);
					return;
				}

				if( device->deserialize(&devicesString))
				{
					g_activeDevices.addElement(device);
				}
				else
				{
					LOG_E("Fatal: deserial(%d)", iDev);
				}
			}
			break;

			case devTypeHeater:
			{
				LOG_I("HEATER");
				CDeviceHeater *device = new CDeviceHeater();
				if(!device)
				{
					LOG_E("Fatal: heap(%d)", iDev);
					return;
				}

				if( device->deserialize(&devicesString))
				{
					g_activeDevices.addElement(device);
				}
				else
				{
					LOG_E("Fatal: deserial(%d)", iDev);
				}
			}
			break;

			default:
				LOG_I("UNKN device:%d", devType);
				break;
		};
	}

	LOG_I("Done adding %d devices", numDevices);


}


void initDevices()
{
	enableDev(DEV_UART, ENABLE | CONFIG);

	//setup SDCard and load custom system settings
	enableDev(DEV_SDCARD, ENABLE | CONFIG);
	//enableDev(DEV_SDCARD, DISABLE);

	//DHT22 periodically enabled to read data
	enableDev(DEV_DHT22, ENABLE | CONFIG);

	enableDev(DEV_MQ135, ENABLE | CONFIG);

	//RGB periodically enabled to send data
	enableDev(DEV_RGB, DISABLE);

	//enable and config Radio
	enableDev(DEV_RADIO, ENABLE | CONFIG);

	//start listening for incoming packets
	if(Radio)
		Radio->startListening();

	//setup Wifi
	enableDev(DEV_WIFI, ENABLE | CONFIG);

	//loadSavedDevices();

	devicesLoadFromDisk();
}


/*generic local device logic */
//TODO: test if GPIO pins correspond to HW layout
void enableDev(unsigned short dev, uint8_t op)
{
	uint8_t i = 0;
	uint8_t retVal;
	do
	{
		if(op & DISABLE)
		{
			if(!isDevEnabled(dev))
			{
				LOG_I( "DEV %x is DIS\n", dev);
				break;
			}
		}
		else
		{
			if(isDevEnabled(dev))
			{
				LOG_I( "DEV %x is ENA\n", dev);
				break;
			}
		}

		for(; i<NUM_DEVICES; i++)
		{
			if(dev == gDevices[i].dev)
			{
				retVal = gDevices[i].initFunc(op);
				if(DEV_ERR_OK != retVal)
				{
					LOG_E( "DEV %x,%x FAIL: %d\n", dev, op, retVal);
				}
				break;
			}
		}

		if( NUM_DEVICES == i )
		{
			LOG_E( "DEV %x unknown\n", dev);
			break;
		}

		//All went well, update gDevicesState
		if(op & DISABLE)
		{
			gDevicesState &= ~dev;
		}
		else
		{
			gDevicesState |= dev;
		}
	}
	while(0);
}

bool devicesLoadFromDisk()
{
	bool bRet = false;

	FRESULT res;
	FILINFO fno;
	DIR dir;
	int i;
	char *fn;   /* This function assumes non-Unicode configuration */

	res = f_opendir(&dir, DEV_PATH_ON_DISK);                       /* Open the directory */
	if (res == FR_OK)
	{
		for (;;)
		{
			res = f_readdir(&dir, &fno);                   /* Read a directory item */
			if (res != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
			if (fno.fname[0] == '.') continue;             /* Ignore dot entry */

			deviceReadFromDisk(fno.fname);
		}
		f_closedir(&dir);
	}

	if(FR_OK != res)
	{
		LOG_E( "devicesLoadFromDisk: err %d", (int)res);
	}
	else bRet = true;


	return bRet;
}

bool deviceWriteToDisk(CGenericDevice *dev)
{
	FIL file;
	FRESULT fRes;

	//2. Open file, write a few bytes, close reopen and read
	//Serial.print("\n2. Open file \"test\" and write some data...\n");
	fRes = f_open(&file, "test", FA_WRITE | FA_CREATE_ALWAYS);

	if (fRes == FR_OK)
	{
		//you can write directly
		f_write(&file, "hello ", 5, &actual);

		//or using printf for convenience
		f_printf(&file, " has %d letters\r\n", actual);

		if (actual != 5)
		{
			Serial.printf("Only written %d bytes\n", actual);
		}
		f_close(&file);
	}
	else
	{
		Serial.printf("fopen FAIL: %d \n", (unsigned int)fRes);
	}
}

bool deviceReadFromDisk(char* path)
{
	FIL file;
	FRESULT fRes;
	uint32_t fSize;

	char *devicesString;

	fr = f_stat(fname, &fno);
	if(fr != FR_OK)
	{
		LOG_I("devReadDisk err %d", (int)fRes);
		break;
	}

	if(fno.fsize > MAX_DEVICE_STRING)

	LOG_I("Loading dev %s", path);



	fRes = f_open(&file, path, FA_READ);

	if (fRes == FR_OK)
	{
		//read back file contents
		char buffer[64];

		f_read(&file, buffer, sizeof(buffer), &actual);
		buffer[actual] = 0;

		Serial.printf("Read: %s \n", buffer);

		f_close(&file);
	}
	else
	{
		LOG_E("fopen FAIL: %d \n", (int)fRes);
	}


			if(!skipInt(&devicesString, &devType))return;


			switch(devType)
			{
				case devTypeLight:
				{
					LOG_I("LIGHT");
					CDeviceLight *device = new CDeviceLight();
					if(!device)
					{
						LOG_E("Fatal: heap(%d)", iDev);
						return;
					}
					LOG_I("deserialize");
					if( device->deserialize(&devicesString))
					{
						g_activeDevices.addElement(device);
					}
					else
					{
						LOG_E("Fatal: deserial(%d)", iDev);
					}
				}
				break;

				case devTypeTH:
				{
					LOG_I("TH");
					CDeviceTempHumid *device = new CDeviceTempHumid();
					if(!device)
					{
						LOG_E("Fatal: heap(%d)", iDev);
						return;
					}

					if( device->deserialize(&devicesString))
					{
						g_activeDevices.addElement(device);
					}
					else
					{
						LOG_E("Fatal: deserial(%d)", iDev);
					}
				}
				break;

				case devTypeHeater:
				{
					LOG_I("HEATER");
					CDeviceHeater *device = new CDeviceHeater();
					if(!device)
					{
						LOG_E("Fatal: heap(%d)", iDev);
						return;
					}

					if( device->deserialize(&devicesString))
					{
						g_activeDevices.addElement(device);
					}
					else
					{
						LOG_E("Fatal: deserial(%d)", iDev);
					}
				}
				break;

				default:
					LOG_I("UNKN device:%d", devType);
					break;
			};



}
