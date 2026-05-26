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

gboolean wifi_get_status(int *exists, int *enabled, int *connected, char *ssid, size_t ssid_len, int *strength);
gboolean wifi_set_enabled(gboolean enabled);
GPtrArray *wifi_list_networks(void);
void wifi_connect(const char *ssid, const char *password);
void wifi_network_free(WifiNetwork *network);

#endif
