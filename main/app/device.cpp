#include "device.h"
#include "util.h"

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
	char devicesString[] = "3;1;0;IndoorTemp;22.1;0;1;1;2;1;Heater;200;50;100;1;0;0;2;LightHall;";

	int numDevices, iDev = 0, devID, devType, numWatchers;;

	#define MAX_FRIENDLY_NAME 64
	char friendlyName[MAX_FRIENDLY_NAME];

	if(!skipInt((char**)&devicesString, &numDevices))return;

	while(iDev < numDevices)
	{
		LOG_I("Loading dev %d of %d", iDev + 1, numDevices);
		if(!skipInt((char**)&devicesString, &devType))return;
		if(!skipInt((char**)&devicesString, &devID))return;
		if(!skipString((char**)&devicesString, (char*)friendlyName, MAX_FRIENDLY_NAME))return;

		switch(devType)
		{
			case devTypeLight:
			{
				LOG_I("LIGHT device - not impl ID:%d", devID);

			}
			break;

			case devTypeTH:
			{
				LOG_I("TH device ID:%d", devID);

				int tempSetPoint = 22.0f;
				if(!skipInt((char**)&devicesString, &tempSetPoint))return;

				tTempHumidState state(tempSetPoint);
				String name(friendlyName);

				int isLocal = 0;
				if(!skipInt((char**)&devicesString, &isLocal))return;

				CDeviceTempHumid *device = new CDeviceTempHumid();
				if(!device)
				{
					LOG_E("Fatal: heap");
					return;
				}

				device->initTempHumid(devID, name, state, (eSensorLocation)isLocal);

				if(!skipInt((char**)&devicesString, &numWatchers))return;

				while(numWatchers--)
				{
					if(!skipInt((char**)&devicesString, &devID))return;
					LOG_I("Add watcher ID:%d", devID);
					device->addWatcherDevice(devID);
				}

				g_activeDevices.addElement(device);
			}
			break;

			case devTypeHeater:
			{
				LOG_I("HEATER device ID:%d", devID);

				int gasHighThres = 200;
				int gasLowThres = 50;
				int gasMedThres = 100;

				if(!skipInt((char**)&devicesString, &gasHighThres))return;
				if(!skipInt((char**)&devicesString, &gasLowThres))return;
				if(!skipInt((char**)&devicesString, &gasMedThres))return;

				tHeaterState state(gasHighThres, gasLowThres, gasMedThres);
				String name(friendlyName);

				CDeviceHeater *device = new CDeviceHeater();
				if(!device)
				{
					LOG_E("Fatal: heap");
					return;
				}

				device->initHeater(devID, name, state);

				if(!skipInt((char**)&devicesString, &numWatchers))return;

				while(numWatchers--)
				{
					if(!skipInt((char**)&devicesString, &devID))return;
					LOG_I("Add watcher ID:%d", devID);
					device->addWatcherDevice(devID);
				}

				g_activeDevices.addElement(device);
			}
			break;

			default:
				LOG_I("UNKN device:%d ID:%d", devType, devID);
				break;
		};
	}

	LOG_I("Done adding %d devices", numDevices);

}


void initDevices()
{

	enableDev(DEV_UART, ENABLE | CONFIG);

	//setup SDCard and load custom system settings, then disable SDCard
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

	loadSavedDevices();
}


/* DEVICES logic */

void CDeviceTempHumid::onUpdateTimer()
{
	requestUpdateState();
	m_updateTimer.initializeMs(m_updateInterval, TimerDelegate(&CDeviceTempHumid::onUpdateTimer, this)).start(false);
}

void CDeviceTempHumid::requestUpdateState()
{
	uint8_t errValue;
	int i;
	if(locLocal == m_location)
	{
		errValue = devDHT22_read(m_state.lastTH);

		if(DEV_ERR_OK == errValue)
		{
			m_LastUpdateTimestamp = system_get_time();

			LOG_I("H:%f T:%f\n", m_state.lastTH.humid, m_state.lastTH.temp);

			/*devDHT22_heatIndex();
			devDHT22_dewPoint();
			devDHT22_comfortRatio();
			LOG_I( "\n");*/

			if(m_state.tempSetpoint > m_tempThreshold + m_state.lastTH.temp)
			{
				m_state.bNeedHeating = true;
				m_state.bNeedCooling = false;
			}
			else if(m_state.tempSetpoint < m_tempThreshold - m_state.lastTH.temp)
			{
				m_state.bNeedHeating = false;
				m_state.bNeedCooling = true;
			}

			if(m_state.bNeedHeating)
			{
				for(i=0; i < m_devWatchersList.count(); i++)
				{
					CGenericDevice* genDevice = getDevice(m_devWatchersList[i]);

					if(genDevice)
					{
						genDevice->triggerState(0, NULL);
					}
				}
			}
		}
		else
		{
			LOG_E( "DHT22 read FAIL:%d\n", errValue);
		}
	}
	else //request by radio
	{

	}
}

void CDeviceHeater::triggerState(int reason, void* state)
{
	bool bHeaterRequestOn = false;
	for(int i=0; i < m_devWatchersList.count(); i++)
	{
		CDeviceTempHumid* thDevice = (CDeviceTempHumid*)(getDevice(m_devWatchersList[i]));

		if(thDevice && thDevice->m_state.bNeedHeating)
		{
			bHeaterRequestOn = true;
			break;
		}
	}

	if(bHeaterRequestOn)
	{
		LOG_I("TurnOn: heater %d", m_ID);
		//send cmd to radio device
	}
	else
	{
		LOG_I("TurnOff: heater %d", m_ID);
		//send cmd to radio device
	}
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



