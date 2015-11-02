#include "CDeviceTempHumid.h"


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

			m_state.fLastTemp_1m = m_state.fAverageTemp_1m->feed(m_state.lastTH.temp);
			m_state.fLastTemp_8m = m_state.fAverageTemp_8m->feed(m_state.lastTH.temp);
			m_state.fLastRH_1m = m_state.fAverageRH_1m->feed(m_state.lastTH.humid);

			LOG_I("%s H:%.2f(%.1f) T:%.2f(%.1f %.1f) SetPt:%.2f Time:%u", m_FriendlyName.c_str(),
					m_state.lastTH.humid, m_state.fLastRH_1m, m_state.lastTH.temp, m_state.fLastTemp_1m ,
					m_state.fLastTemp_8m, m_state.tempSetpoint,
					m_LastUpdateTimestamp);

			/*devDHT22_heatIndex();
			devDHT22_dewPoint();
			devDHT22_comfortRatio();
			LOG_I( "\n");*/

			if(m_state.tempSetpoint > m_tempThreshold + m_state.fLastTemp_8m)
			{
				m_state.bNeedHeating = true;
				m_state.bNeedCooling = false;
			}
			else if(m_state.tempSetpoint < m_state.fLastTemp_1m - m_tempThreshold)
			{
				m_state.bNeedHeating = false;
				m_state.bNeedCooling = true;
			}

		//	if(m_state.bNeedHeating && m_state.bEnabled)
		//	{
				for(i=0; i < m_devWatchersList.count(); i++)
				{
					CDeviceHeater* genDevice = (CDeviceHeater*)getDevice(m_devWatchersList[i]);

					if(genDevice)
					{
						genDevice->triggerState(0, NULL);
					}
				}
		//	}

		}
		else
		{
			LOG_E( "DHT22 read FAIL:%d (%d)\n", errValue, (int)devDHT22_getLastError());
		}
	}
	else //request by radio
	{

	}
}

bool CDeviceTempHumid::deserialize(const char **devicesString)
{
	int devID, numWatchers;

	char friendlyName[MAX_FRIENDLY_NAME];

	LOG_I("TH device: %s", *devicesString);

	if(!skipInt(devicesString, &devID))return false;
	if(!skipString(devicesString, (char*)friendlyName, MAX_FRIENDLY_NAME))return false;

	LOG_I("TH device ID:%d NAME: %s", devID, friendlyName);

	float tempSetPoint = 22.5f;
	if(!skipFloat(devicesString, &(m_state.tempSetpoint)))return false;

	m_state.tempSetpointMin = 16;
	m_state.tempSetpointMax = 27;

	String name(friendlyName);

	if(!skipFloat(devicesString, &(m_state.tempSetpointMin)))return false;

	if(!skipFloat(devicesString, &(m_state.tempSetpointMax)))return false;

	int isLocal = 0;
	if(!skipInt(devicesString, &isLocal))return false;

	int iEnabled;
	if(!skipInt(devicesString, &(iEnabled)))return false;

	m_state.bEnabled = iEnabled;

	initTempHumid((uint32_t)devID, name, /*state,*/ (eSensorLocation)isLocal);

	if(!skipInt(devicesString, &numWatchers))return false;

	while(numWatchers--)
	{
		if(!skipInt(devicesString, &devID))return false;
		LOG_I("Add watcher ID:%d", devID);
		addWatcherDevice(devID);
	}

	return true;
}

uint32_t CDeviceTempHumid::serialize(char* buffer, uint32_t size)
{
	int i;
	int sz = snprintf(buffer, size, "%d;%d;%s;%f;%f;%f;%d;%d;%d;", devTypeTH, m_ID, m_FriendlyName.c_str(),
					m_state.tempSetpoint, m_state.tempSetpointMin, m_state.tempSetpointMax, (int)m_location,
					m_state.bEnabled ? 1:0,
					m_devWatchersList.count());

	for(i = 0; i < m_devWatchersList.count(); i++)
	{
		sz += snprintf(buffer + sz, size - sz, "%d;", m_devWatchersList[i].id);
	}

	return sz;
}
