#include "wifi.h"
#include <NetworkManager.h>
#include <string.h>

static NMDeviceWifi *find_wifi_device(NMClient *client) {
	const GPtrArray *devices = nm_client_get_devices(client);
	if (!devices)
		return NULL;

	for (guint i = 0; i < devices->len; i++) {
		NMDevice *device = g_ptr_array_index(devices, i);
		if (NM_IS_DEVICE_WIFI(device))
			return NM_DEVICE_WIFI(device);
	}

	return NULL;
}

static char *ap_ssid_to_utf8(NMAccessPoint *ap) {
	GBytes *ssid = nm_access_point_get_ssid(ap);
	if (!ssid)
		return NULL;

	gsize len = 0;
	const guint8 *data = g_bytes_get_data(ssid, &len);
	if (!data || len == 0)
		return NULL;

	return nm_utils_ssid_to_utf8(data, len);
}

static gboolean ap_is_secured(NMAccessPoint *ap) {
	return (nm_access_point_get_flags(ap) & NM_802_11_AP_FLAGS_PRIVACY) ||
		   nm_access_point_get_wpa_flags(ap) != NM_802_11_AP_SEC_NONE ||
		   nm_access_point_get_rsn_flags(ap) != NM_802_11_AP_SEC_NONE;
}

static void set_property_cb(GObject *object, GAsyncResult *result, gpointer user_data) {
	GError *error = NULL;
	nm_client_dbus_set_property_finish(NM_CLIENT(object), result, &error);
	if (error) {
		g_warning("Failed to update WiFi radio state: %s", error->message);
		g_error_free(error);
	}
	g_object_unref(user_data);
}

static char *ap_security_label(NMAccessPoint *ap) {
	NM80211ApSecurityFlags rsn = nm_access_point_get_rsn_flags(ap);
	NM80211ApSecurityFlags wpa = nm_access_point_get_wpa_flags(ap);

	if ((rsn | wpa) & NM_802_11_AP_SEC_KEY_MGMT_SAE)
		return g_strdup("WPA3");
	if (rsn != NM_802_11_AP_SEC_NONE)
		return g_strdup("WPA2");
	if (wpa != NM_802_11_AP_SEC_NONE)
		return g_strdup("WPA");
	if (nm_access_point_get_flags(ap) & NM_802_11_AP_FLAGS_PRIVACY)
		return g_strdup("WEP");
	return g_strdup("--");
}

void wifi_network_free(WifiNetwork *network) {
	if (!network)
		return;

	g_free(network->ssid);
	g_free(network->security);
	g_free(network);
}

gboolean wifi_get_status(int *exists, int *enabled, int *connected, char *ssid, size_t ssid_len, int *strength) {
	GError *error = NULL;
	NMClient *client = nm_client_new(NULL, &error);
	if (!client) {
		g_clear_error(&error);
		return FALSE;
	}

	NMDeviceWifi *wifi = find_wifi_device(client);
	if (exists)
		*exists = wifi != NULL;
	if (enabled)
		*enabled = nm_client_wireless_get_enabled(client) && nm_client_wireless_hardware_get_enabled(client);
	if (connected)
		*connected = 0;
	if (strength)
		*strength = -1;
	if (ssid && ssid_len > 0)
		ssid[0] = '\0';

	if (wifi) {
		NMAccessPoint *active_ap = nm_device_wifi_get_active_access_point(wifi);
		if (active_ap) {
			if (connected)
				*connected = 1;
			if (strength)
				*strength = nm_access_point_get_strength(active_ap);
			if (ssid && ssid_len > 0) {
				char *active_ssid = ap_ssid_to_utf8(active_ap);
				if (active_ssid) {
					g_strlcpy(ssid, active_ssid, ssid_len);
					g_free(active_ssid);
				}
			}
		} else if (nm_device_get_state(NM_DEVICE(wifi)) == NM_DEVICE_STATE_ACTIVATED) {
			NMActiveConnection *active = nm_device_get_active_connection(NM_DEVICE(wifi));
			if (connected)
				*connected = 1;
			if (active && ssid && ssid_len > 0)
				g_strlcpy(ssid, nm_active_connection_get_id(active), ssid_len);
		}
	}

	g_object_unref(client);
	return TRUE;
}

gboolean wifi_set_enabled(gboolean enabled) {
	GError *error = NULL;
	NMClient *client = nm_client_new(NULL, &error);
	if (!client) {
		g_clear_error(&error);
		return FALSE;
	}

	nm_client_dbus_set_property(client, NM_DBUS_PATH, NM_DBUS_INTERFACE, "WirelessEnabled",
								g_variant_new_boolean(enabled), -1, NULL, set_property_cb, g_object_ref(client));
	g_object_unref(client);
	return TRUE;
}

static void request_scan_cb(GObject *object, GAsyncResult *result, gpointer user_data) {
	GError *error = NULL;
	nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(object), result, &error);
	g_clear_error(&error);
	g_object_unref(user_data);
}

static gint compare_networks(gconstpointer a, gconstpointer b) {
	const WifiNetwork *na = *(WifiNetwork * const *)a;
	const WifiNetwork *nb = *(WifiNetwork * const *)b;

	if (na->active != nb->active)
		return nb->active - na->active;
	if (na->strength != nb->strength)
		return nb->strength - na->strength;
	return g_ascii_strcasecmp(na->ssid, nb->ssid);
}

GPtrArray *wifi_list_networks(void) {
	GPtrArray *networks = g_ptr_array_new_with_free_func((GDestroyNotify)wifi_network_free);
	GError *error = NULL;
	NMClient *client = nm_client_new(NULL, &error);
	if (!client) {
		g_clear_error(&error);
		return networks;
	}

	NMDeviceWifi *wifi = find_wifi_device(client);
	if (!wifi) {
		g_object_unref(client);
		return networks;
	}

	nm_device_wifi_request_scan_async(wifi, NULL, request_scan_cb, g_object_ref(client));

	NMAccessPoint *active_ap = nm_device_wifi_get_active_access_point(wifi);
	const GPtrArray *aps = nm_device_wifi_get_access_points(wifi);
	GHashTable *by_ssid = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	for (guint i = 0; aps && i < aps->len; i++) {
		NMAccessPoint *ap = g_ptr_array_index(aps, i);
		char *ssid = ap_ssid_to_utf8(ap);
		if (!ssid || ssid[0] == '\0') {
			g_free(ssid);
			continue;
		}

		WifiNetwork *network = g_hash_table_lookup(by_ssid, ssid);
		gboolean active = ap == active_ap;
		int strength = nm_access_point_get_strength(ap);

		if (!network) {
			network = g_new0(WifiNetwork, 1);
			network->ssid = g_strdup(ssid);
			g_ptr_array_add(networks, network);
			g_hash_table_insert(by_ssid, g_strdup(ssid), network);
		}

		if (active || strength >= network->strength) {
			g_free(network->security);
			network->security = ap_security_label(ap);
			network->strength = strength;
			network->secured = ap_is_secured(ap);
		}
		network->active = network->active || active;

		g_free(ssid);
	}

	g_hash_table_destroy(by_ssid);
	g_ptr_array_sort(networks, compare_networks);
	g_object_unref(client);
	return networks;
}

static void activate_connection_cb(GObject *object, GAsyncResult *result, gpointer user_data) {
	GError *error = NULL;
	NMActiveConnection *active = nm_client_add_and_activate_connection_finish(NM_CLIENT(object), result, &error);
	if (active)
		g_object_unref(active);
	if (error) {
		g_warning("Failed to connect to WiFi: %s", error->message);
		g_error_free(error);
	}
	g_object_unref(user_data);
}

static NMAccessPoint *find_access_point_by_ssid(NMDeviceWifi *wifi, const char *ssid) {
	const GPtrArray *aps = nm_device_wifi_get_access_points(wifi);
	for (guint i = 0; aps && i < aps->len; i++) {
		NMAccessPoint *ap = g_ptr_array_index(aps, i);
		char *ap_ssid = ap_ssid_to_utf8(ap);
		gboolean match = ap_ssid && strcmp(ap_ssid, ssid) == 0;
		g_free(ap_ssid);
		if (match)
			return ap;
	}
	return NULL;
}

void wifi_connect(const char *ssid, const char *password) {
	if (!ssid || ssid[0] == '\0')
		return;

	GError *error = NULL;
	NMClient *client = nm_client_new(NULL, &error);
	if (!client) {
		g_clear_error(&error);
		return;
	}

	NMDeviceWifi *wifi = find_wifi_device(client);
	if (!wifi) {
		g_object_unref(client);
		return;
	}

	NMAccessPoint *ap = find_access_point_by_ssid(wifi, ssid);
	NMConnection *connection = nm_simple_connection_new();

	NMSettingConnection *s_con = NM_SETTING_CONNECTION(nm_setting_connection_new());
	g_object_set(s_con,
				 NM_SETTING_CONNECTION_ID, ssid,
				 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
				 NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
				 NULL);
	nm_connection_add_setting(connection, NM_SETTING(s_con));

	NMSettingWireless *s_wifi = NM_SETTING_WIRELESS(nm_setting_wireless_new());
	GBytes *ssid_bytes = g_bytes_new(ssid, strlen(ssid));
	g_object_set(s_wifi,
				 NM_SETTING_WIRELESS_SSID, ssid_bytes,
				 NM_SETTING_WIRELESS_MODE, NM_SETTING_WIRELESS_MODE_INFRA,
				 NULL);
	g_bytes_unref(ssid_bytes);
	nm_connection_add_setting(connection, NM_SETTING(s_wifi));

	if (password && password[0] != '\0') {
		NMSettingWirelessSecurity *s_sec = NM_SETTING_WIRELESS_SECURITY(nm_setting_wireless_security_new());
		if (ap && (nm_access_point_get_flags(ap) & NM_802_11_AP_FLAGS_PRIVACY) &&
			nm_access_point_get_wpa_flags(ap) == NM_802_11_AP_SEC_NONE &&
			nm_access_point_get_rsn_flags(ap) == NM_802_11_AP_SEC_NONE) {
			g_object_set(s_sec,
						 NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none",
						 NM_SETTING_WIRELESS_SECURITY_WEP_KEY0, password,
						 NULL);
		} else {
			g_object_set(s_sec,
						 NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
						 NM_SETTING_WIRELESS_SECURITY_PSK, password,
						 NULL);
		}
		nm_connection_add_setting(connection, NM_SETTING(s_sec));
	}

	nm_client_add_and_activate_connection_async(client, connection, NM_DEVICE(wifi),
											   ap ? nm_object_get_path(NM_OBJECT(ap)) : NULL,
											   NULL, activate_connection_cb, g_object_ref(client));
	g_object_unref(connection);
	g_object_unref(client);
}
