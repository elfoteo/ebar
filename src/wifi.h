#ifndef WIFI_H
#define WIFI_H

#include <glib.h>
#include <stddef.h>

typedef struct {
	char *ssid;
	char *security;
	int strength;
	gboolean active;
	gboolean secured;
} WifiNetwork;

#include "types.h"

void wifi_init(AppState *state);
void wifi_cleanup(AppState *state);
gboolean wifi_set_enabled(gboolean enabled);
GPtrArray *wifi_list_networks(void);
void wifi_connect(const char *ssid, const char *password);
void wifi_network_free(WifiNetwork *network);

#endif
