#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <user_config.h>
#include <SmingCore/SmingCore.h>

#include "debug.h"
#include "commWeb.h"

extern NtpClient *gNTPClient;
extern HttpServer gHttpServer;

void startWebServers();
void wsPruneConnections();

void wsSendAllExcept(WebSocket& socket, const char* msg, size_t size);

#endif
