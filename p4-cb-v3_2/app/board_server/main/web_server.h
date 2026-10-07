#ifndef __WEB_SERVER_H
#define __WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

/* Early init: register the LAN-IP event catcher (call BEFORE WiFi init,
 * so the IP event is not missed). */
void web_server_init(void);

/* Start the e_server HTTP API (page + JSON endpoints + MJPEG stream). */
esp_err_t web_server_start(void);

/* LAN IP cached from IP_EVENT_STA_GOT_IP ("0.0.0.0" before connect). */
const char *web_server_lan_ip(void);

#endif
