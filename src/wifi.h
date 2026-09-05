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

typedef void (*WifiChangedCallback)(AppState *state);
typedef void (*WifiConnectCallback)(gboolean success, gpointer user_data);

void wifi_init(AppState *state);
void wifi_cleanup(AppState *state);
void wifi_set_changed_callback(WifiChangedCallback cb);
gboolean wifi_set_enabled(gboolean enabled);
GPtrArray *wifi_list_networks(void);
gboolean wifi_has_saved_connection(const char *ssid);
void wifi_connect(const char *ssid, const char *password, WifiConnectCallback callback, gpointer user_data);
void wifi_disconnect(void);
void wifi_network_free(WifiNetwork *network);

#endif
