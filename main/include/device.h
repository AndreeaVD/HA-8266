#ifndef __DEVICE_H_
#define __DEVICE_H_

#include "debug.h"

#include "drv/drvUART.h"
#include "drv/drvRGB.h"
#include "drv/drvRadio.h"
#include "drv/drvSDCard.h"
#include "drv/drvDHT22.h"
#include "drv/drvMQ135.h"
#include "drv/drvWiFi.h"
#include "drv/drvDS18B20.h"

#include "Wiring/WVector.h"
#include <Libraries/DHT/DHT.h>
#include <SmingCore/SmingCore.h>

#define LOCAL_TEMPHUMID_SENSOR_ID 0

#define INVALID_DEVICE_ID 0xffffffff

#define DEV_RADIO   0x0001
#define DEV_SDCARD  0x0002
#define DEV_RGB     0x0004
#define DEV_MQ135   0x0008
#define DEV_DHT22   0x0010
#define DEV_WIFI    0x0020
#define DEV_DSTEMP  0x0040
#define DEV_UART    0x0080

#define DISABLE 	0x01
#define ENABLE 		0x02
#define CONFIG 		0x04

#define DEV_ERR_OK 		0
#define DEV_OTHER_ERR 	1
#define DEV_DEVIO_ERR 	2
#define DEV_PARAM_ERR 	3

#define isDevEnabled(dev) ((dev) & gDevicesState)

void enableDev(unsigned short, uint8_t op);
void initDevices();

class CGenericDevice;
class CDeviceLight;
class CDeviceTempHumid;
class CDeviceHeater;

extern Vector<CGenericDevice*> g_activeDevices;

struct Watcher
{
	uint32_t id;
	CGenericDevice *device;
};

enum eDeviceType
{
	devTypeLight,
	devTypeTH,
	devTypeHeater,
};

enum eHeaterTypes
{
	heater_gasHeater,
	heater_electricHeater
};

enum eHeaterTurOnTriggers
{
	heatTrig_Manual 	= 0x01,
	heatTrig_TempSensor = 0x02,
};

enum eTriggers
{
	trigClapClap 	= 0x01,
	trigOnMovement 	= 0x02,
	trigOnTimer 	= 0x04, //Turn on in 5 minutes -> sends interval in seconds to light device
	trigOnManual	= 0x08,
};

enum eLightState
{
	lightOff,
	lightCustomIntensity,
	lightQuarterIntensity,
	lightHalfIntensity,
	lightFullIntensity,

	lightLastState,
};

enum eSensorLocation
{
	locLocal,
	locRemote
};

class CGenericDevice
{
public:
	CGenericDevice():m_ID(INVALID_DEVICE_ID), m_FriendlyName("notInit"),
					 m_LastUpdateTimestamp(~0){};

	bool isInit()
		{return m_ID != INVALID_DEVICE_ID;}

	virtual void requestUpdateState() = 0;
	virtual void triggerState(int reason, void* state) = 0;

	virtual bool radioPktReceivedFromDevice(char* pktData, uint16_t pktLen) = 0;

	CGenericDevice* findDevice(uint32_t deviceID)
	{
		int i = 0;
		for(; i < g_activeDevices.count(); i++)
		{
			if(deviceID == g_activeDevices[i]->m_ID)
			{
				return g_activeDevices[i];
			}
		}
		return NULL;
	}

	CGenericDevice* getDevice(Watcher& watcher)
	{
		if(watcher.device)
			return watcher.device;

		return findDevice(watcher.id);
	}

	void addWatcherDevice(uint32_t deviceID)
	{
		Watcher watcher;

		watcher.id = deviceID;
		watcher.device = findDevice(deviceID);

		m_devWatchersList.addElement(watcher);
	}

	virtual ~CGenericDevice(){}

	Vector<Watcher> m_devWatchersList;

	uint32_t m_ID;
	String m_FriendlyName;
	uint32_t m_LastUpdateTimestamp;
	eDeviceType m_deviceType;
	int m_updateInterval;
	Timer m_updateTimer;
};


struct tLightState
{

	tLightState(int minInt,
				int maxInt = 255,
				int dimSpd = 5,
				uint32_t trigger = trigClapClap | trigOnMovement,
				uint32_t capa = trigClapClap | trigOnMovement | trigOnTimer | trigOnManual,
				int onTime = 15):
			   curIntensity(minInt), minIntensity(minInt),
			   maxIntensity(maxInt), dimSpeed(dimSpd),
			   turnOnTriggers(trigger),
			   capabilities(capa),
			   lightState(lightOff), movement_keepOnTimeSeconds(onTime),
			   movement_lastMovementTimestamp(~0){};

	tLightState()
	{
		tLightState(50);
	}

	int curIntensity, minIntensity, maxIntensity,
		dimSpeed, movement_keepOnTimeSeconds;
	uint32_t turnOnTriggers, capabilities, movement_lastMovementTimestamp;
	eLightState lightState;
};

class CDeviceLight : public CGenericDevice
{
public:

	CDeviceLight()
	{
		m_deviceType = devTypeLight;
	}

	void initLight(uint32_t lightID, String& friendlyName, tLightState& state)
	{
		m_ID = lightID;
		m_FriendlyName = friendlyName;
		m_state = state;
	}

	virtual void requestUpdateState(){}
	virtual void triggerState(int reason, void* state){}
	virtual bool radioPktReceivedFromDevice(char* pktData, uint16_t pktLen){}

	tLightState m_state;
};

struct tTempHumidState
{
	tTempHumidState(float setpoint):
		tempSetpoint(setpoint),
		bNeedHeating(false),
		bNeedCooling(false)
	{
		lastTH.temp = -99.0f;
		lastTH.humid = -99.0f;
	}

	tTempHumidState()
	{
		tTempHumidState(22.0f);
	}

	TempAndHumidity lastTH;
	float tempSetpoint;

	bool bNeedHeating, bNeedCooling;

	//confort related stuff
};



class CDeviceTempHumid : public CGenericDevice
{
public:

	CDeviceTempHumid()
	{
		m_deviceType = devTypeTH;
	}

	void initTempHumid(	uint32_t deviceID,
						String& friendlyName,
						tTempHumidState& state,
						eSensorLocation location,
						int updateInterval = 5000)
	{
		m_ID = deviceID;
		m_FriendlyName = friendlyName;
		m_state = state;
		m_location = location;

		m_updateInterval = updateInterval;

		m_tempThreshold = 0.1;

		m_updateTimer.initializeMs(m_updateInterval, TimerDelegate(&CDeviceTempHumid::onUpdateTimer, this)).start(false);
	}

	void onUpdateTimer();

	virtual void requestUpdateState();

	//no implementation required for sensors
	virtual void triggerState(int reason, void* state){};

	virtual bool radioPktReceivedFromDevice(char* pktData, uint16_t pktLen){}

	tTempHumidState m_state;

	eSensorLocation m_location;

	float m_tempThreshold;

};

struct tHeaterState
{
	tHeaterState(int highWarn,
				int lowWarn = 50,
				int medWarn = 100,
				int lastRead = 0):
		gasLevel_lastReading(lastRead),
		gasLevel_LowWarningThres(lowWarn),
		gasLevel_MedWarningThres(medWarn),
		gasLevel_HighWarningThres(highWarn),
		isOn(false), isFault(false)
	{}

	tHeaterState()
	{
		tHeaterState(200);
	}

	int gasLevel_lastReading, gasLevel_LowWarningThres,
		gasLevel_MedWarningThres, gasLevel_HighWarningThres;

	bool isOn, isFault;
};


class CDeviceHeater : public CGenericDevice
{
public:

	CDeviceHeater()
	{
		m_deviceType = devTypeHeater;
	}

	void initHeater(uint32_t heaterID, String& friendlyName, tHeaterState& state)
	{
		m_ID = heaterID;
		m_FriendlyName = friendlyName;
		m_state = state;
	}



	virtual void requestUpdateState()
	{
		//send state request from radio device
	}

	virtual void triggerState(int reason, void* state);

	virtual bool radioPktReceivedFromDevice(char* pktData, uint16_t pktLen)
	{
		//update state
	}

	eHeaterTypes type;
	tHeaterState m_state;
};

#endif /*__DEVICE_H_*/
