#include "bluetooth.h"
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

static GDBusConnection *bluetooth_bus(void) {
	GError *error = NULL;
	GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (error) {
		fprintf(stderr, "Bluetooth: failed to get system bus: %s\n", error->message);
		g_error_free(error);
	}
	return bus;
}

static GVariant *bluez_managed_objects(GDBusConnection *bus) {
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_sync(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
												  "GetManagedObjects", NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
												  G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &error);
	if (error)
		g_error_free(error);
	return reply;
}

static gboolean prop_bool(GVariant *props, const char *name) {
	GVariant *value = g_variant_lookup_value(props, name, NULL);
	if (!value)
		return FALSE;
	gboolean result = g_variant_get_boolean(value);
	g_variant_unref(value);
	return result;
}

static void prop_string(GVariant *props, const char *name, char *out, size_t out_size) {
	GVariant *value = g_variant_lookup_value(props, name, NULL);
	if (!value)
		return;
	const char *str = g_variant_get_string(value, NULL);
	g_strlcpy(out, str, out_size);
	g_variant_unref(value);
}

static char *find_adapter_path(GDBusConnection *bus) {
	GVariant *reply = bluez_managed_objects(bus);
	if (!reply)
		return NULL;

	char *adapter_path = NULL;
	GVariant *objects = g_variant_get_child_value(reply, 0);
	GVariantIter iter;
	g_variant_iter_init(&iter, objects);

	GVariant *entry = NULL;
	while ((entry = g_variant_iter_next_value(&iter))) {
		GVariant *path_value = g_variant_get_child_value(entry, 0);
		GVariant *ifaces = g_variant_get_child_value(entry, 1);
		const char *path = g_variant_get_string(path_value, NULL);
		GVariant *props = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", G_VARIANT_TYPE("a{sv}"));
		if (props) {
			adapter_path = g_strdup(path);
			g_variant_unref(props);
			g_variant_unref(ifaces);
			g_variant_unref(path_value);
			g_variant_unref(entry);
			break;
		}
		g_variant_unref(ifaces);
		g_variant_unref(path_value);
		g_variant_unref(entry);
	}

	g_variant_unref(objects);
	g_variant_unref(reply);
	return adapter_path;
}

void bluetooth_get_status(int *adapter_exists, int *powered, int *connected, char *device_name, size_t device_name_size) {
	if (adapter_exists)
		*adapter_exists = 0;
	if (powered)
		*powered = 0;
	if (connected)
		*connected = 0;
	if (device_name && device_name_size > 0)
		device_name[0] = '\0';

	GDBusConnection *bus = bluetooth_bus();
	if (!bus)
		return;

	GVariant *reply = bluez_managed_objects(bus);
	if (!reply) {
		g_object_unref(bus);
		return;
	}

	GVariant *objects = g_variant_get_child_value(reply, 0);
	GVariantIter iter;
	g_variant_iter_init(&iter, objects);

	GVariant *entry = NULL;
	while ((entry = g_variant_iter_next_value(&iter))) {
		GVariant *ifaces = g_variant_get_child_value(entry, 1);
		GVariant *adapter_props = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", G_VARIANT_TYPE("a{sv}"));
		if (adapter_props) {
			if (adapter_exists)
				*adapter_exists = 1;
			if (powered)
				*powered = prop_bool(adapter_props, "Powered");
			g_variant_unref(adapter_props);
		}

		GVariant *device_props = g_variant_lookup_value(ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
		if (device_props) {
			if (prop_bool(device_props, "Connected")) {
				if (connected)
					*connected = 1;
				if (device_name && device_name_size > 0 && !device_name[0]) {
					prop_string(device_props, "Alias", device_name, device_name_size);
					if (!device_name[0])
						prop_string(device_props, "Name", device_name, device_name_size);
				}
			}
			g_variant_unref(device_props);
		}

		g_variant_unref(ifaces);
		g_variant_unref(entry);
	}

	g_variant_unref(objects);
	g_variant_unref(reply);
	g_object_unref(bus);
}

int bluetooth_set_powered(int powered) {
	GDBusConnection *bus = bluetooth_bus();
	if (!bus)
		return -1;

	char *adapter_path = find_adapter_path(bus);
	if (!adapter_path) {
		g_object_unref(bus);
		return -1;
	}

	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_sync(bus, "org.bluez", adapter_path, "org.freedesktop.DBus.Properties", "Set",
												  g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered",
																powered ? g_variant_new_boolean(TRUE) : g_variant_new_boolean(FALSE)),
												  NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &error);
	g_free(adapter_path);
	g_object_unref(bus);

	if (error) {
		g_error_free(error);
		return -1;
	}
	if (reply)
		g_variant_unref(reply);
	return 0;
}

static guint16 prop_uint16(GVariant *props, const char *name) {
	GVariant *value = g_variant_lookup_value(props, name, NULL);
	if (!value)
		return 0;
	guint16 result = g_variant_get_uint16(value);
	g_variant_unref(value);
	return result;
}

static guint32 prop_uint32(GVariant *props, const char *name) {
	GVariant *value = g_variant_lookup_value(props, name, NULL);
	if (!value)
		return 0;
	guint32 result = g_variant_get_uint32(value);
	g_variant_unref(value);
	return result;
}

static void set_device_icon(BluetoothDevice *device, GVariant *props) {
	char freedesktop_icon[32] = "";
	prop_string(props, "Icon", freedesktop_icon, sizeof(freedesktop_icon));

	if (freedesktop_icon[0]) {
		if (strcmp(freedesktop_icon, "phone") == 0)
			g_strlcpy(device->icon, "󰄋", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "computer") == 0 || strcmp(freedesktop_icon, "video-display") == 0)
			g_strlcpy(device->icon, "󰌢", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "audio-headset") == 0)
			g_strlcpy(device->icon, "󰋎", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "audio-headphones") == 0 || strcmp(freedesktop_icon, "audio-card") == 0)
			g_strlcpy(device->icon, "󰋋", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "input-keyboard") == 0)
			g_strlcpy(device->icon, "󰌌", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "input-mouse") == 0)
			g_strlcpy(device->icon, "󰍽", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "input-gaming") == 0)
			g_strlcpy(device->icon, "󰮂", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "printer") == 0)
			g_strlcpy(device->icon, "󰐪", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "camera-photo") == 0 || strcmp(freedesktop_icon, "camera-video") == 0)
			g_strlcpy(device->icon, "󰄀", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "multimedia-player") == 0)
			g_strlcpy(device->icon, "󰦚", sizeof(device->icon));
		else if (strcmp(freedesktop_icon, "modem") == 0 || strcmp(freedesktop_icon, "network-wireless") == 0)
			g_strlcpy(device->icon, "󰑩", sizeof(device->icon));
		else
			g_strlcpy(device->icon, "󰂯", sizeof(device->icon));
		return;
	}

	guint16 appearance = prop_uint16(props, "Appearance");
	if (appearance) {
		if (appearance >= 0x0040 && appearance <= 0x007f)
			g_strlcpy(device->icon, "󰄋", sizeof(device->icon));
		else if (appearance >= 0x0080 && appearance <= 0x00bf)
			g_strlcpy(device->icon, "󰌢", sizeof(device->icon));
		else if (appearance >= 0x00c0 && appearance <= 0x00ff)
			g_strlcpy(device->icon, "󰖉", sizeof(device->icon));
		else if (appearance >= 0x0280 && appearance <= 0x02bf)
			g_strlcpy(device->icon, "󰦚", sizeof(device->icon));
		else if (appearance >= 0x0900 && appearance <= 0x093f)
			g_strlcpy(device->icon, "󰋋", sizeof(device->icon));
		else if (appearance >= 0x0940 && appearance <= 0x097f)
			g_strlcpy(device->icon, appearance == 0x0942 ? "󰋎" : "󰋋", sizeof(device->icon));
		else if (appearance >= 0x0300 && appearance <= 0x053f)
			g_strlcpy(device->icon, "󰗶", sizeof(device->icon));
		else if (appearance >= 0x03c0 && appearance <= 0x03ff) {
			if (appearance == 0x03c1)
				g_strlcpy(device->icon, "󰌌", sizeof(device->icon));
			else if (appearance == 0x03c2)
				g_strlcpy(device->icon, "󰍽", sizeof(device->icon));
			else if (appearance == 0x03c3 || appearance == 0x03c4)
				g_strlcpy(device->icon, "󰮂", sizeof(device->icon));
			else
				g_strlcpy(device->icon, "󰂯", sizeof(device->icon));
		} else
			g_strlcpy(device->icon, "󰂯", sizeof(device->icon));
		return;
	}

	guint32 cod = prop_uint32(props, "Class");
	if (cod) {
		guint32 major = (cod >> 8) & 0x1f;
		guint32 minor = cod & 0xff;
		switch (major) {
		case 0x01:
			g_strlcpy(device->icon, "󰌢", sizeof(device->icon));
			return;
		case 0x02:
			g_strlcpy(device->icon, "󰄋", sizeof(device->icon));
			return;
		case 0x04: {
			const char *ico = "󰋋";
			if (minor == 0x01 || minor == 0x02)
				ico = "󰋎";
			else if (minor == 0x04)
				ico = "󰦚";
			g_strlcpy(device->icon, ico, sizeof(device->icon));
			return;
		}
		case 0x05: {
			const char *ico = "󰂯";
			if (minor == 0x01)
				ico = "󰌌";
			else if (minor == 0x02)
				ico = "󰍽";
			else if (minor == 0x04 || minor == 0x05)
				ico = "󰮂";
			g_strlcpy(device->icon, ico, sizeof(device->icon));
			return;
		}
		case 0x06:
			g_strlcpy(device->icon, "󰄀", sizeof(device->icon));
			return;
		case 0x07:
			g_strlcpy(device->icon, minor == 0x01 ? "󰖉" : "󰂯", sizeof(device->icon));
			return;
		case 0x09:
			g_strlcpy(device->icon, "󰗶", sizeof(device->icon));
			return;
		default:
			break;
		}
	}

	g_strlcpy(device->icon, "󰂯", sizeof(device->icon));
}

GPtrArray *bluetooth_list_devices(void) {
	GPtrArray *devices = g_ptr_array_new_with_free_func(g_free);
	GDBusConnection *bus = bluetooth_bus();
	if (!bus)
		return devices;

	GVariant *reply = bluez_managed_objects(bus);
	if (!reply) {
		g_object_unref(bus);
		return devices;
	}

	GVariant *objects = g_variant_get_child_value(reply, 0);
	GVariantIter iter;
	g_variant_iter_init(&iter, objects);

	GVariant *entry = NULL;
	while ((entry = g_variant_iter_next_value(&iter))) {
		GVariant *path_value = g_variant_get_child_value(entry, 0);
		GVariant *ifaces = g_variant_get_child_value(entry, 1);
		const char *path = g_variant_get_string(path_value, NULL);
		GVariant *props = g_variant_lookup_value(ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
		if (props) {
			BluetoothDevice *device = g_new0(BluetoothDevice, 1);
			g_strlcpy(device->path, path, sizeof(device->path));
			prop_string(props, "Alias", device->name, sizeof(device->name));
			if (!device->name[0])
				prop_string(props, "Name", device->name, sizeof(device->name));
			if (!device->name[0])
				g_strlcpy(device->name, "Bluetooth device", sizeof(device->name));
			device->paired = prop_bool(props, "Paired");
			device->connected = prop_bool(props, "Connected");
			set_device_icon(device, props);
			g_ptr_array_add(devices, device);
			g_variant_unref(props);
		}
		g_variant_unref(ifaces);
		g_variant_unref(path_value);
		g_variant_unref(entry);
	}

	g_variant_unref(objects);
	g_variant_unref(reply);
	g_object_unref(bus);
	return devices;
}

static int bluetooth_device_call(const char *path, const char *method) {
	if (!path || !path[0])
		return -1;

	GDBusConnection *bus = bluetooth_bus();
	if (!bus)
		return -1;

	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_sync(bus, "org.bluez", path, "org.bluez.Device1", method, NULL, NULL,
												  G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
	g_object_unref(bus);

	if (error) {
		g_error_free(error);
		return -1;
	}
	if (reply)
		g_variant_unref(reply);
	return 0;
}

int bluetooth_connect_device(const char *path) {
	return bluetooth_device_call(path, "Connect");
}

int bluetooth_disconnect_device(const char *path) {
	return bluetooth_device_call(path, "Disconnect");
}

typedef struct {
	char path[256];
	BluetoothConnectCallback cb;
	gpointer user_data;
} AsyncConnectCtx;

static void on_async_call_finish(GObject *source, GAsyncResult *res, gpointer data) {
	AsyncConnectCtx *ctx = (AsyncConnectCtx *)data;
	GDBusConnection *bus = G_DBUS_CONNECTION(source);
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_finish(bus, res, &error);
	int success = (error == NULL);
	if (error)
		g_error_free(error);
	if (reply)
		g_variant_unref(reply);
	g_object_unref(bus);
	if (ctx->cb)
		ctx->cb(ctx->path, success, ctx->user_data);
	g_free(ctx);
}

static void bluetooth_device_call_async(const char *path, const char *method, BluetoothConnectCallback cb, gpointer user_data) {
	if (!path || !path[0]) {
		if (cb)
			cb(path, 0, user_data);
		return;
	}

	GDBusConnection *bus = bluetooth_bus();
	if (!bus) {
		if (cb)
			cb(path, 0, user_data);
		return;
	}

	AsyncConnectCtx *ctx = g_new0(AsyncConnectCtx, 1);
	g_strlcpy(ctx->path, path, sizeof(ctx->path));
	ctx->cb = cb;
	ctx->user_data = user_data;

	g_dbus_connection_call(bus, "org.bluez", path, "org.bluez.Device1", method, NULL, NULL,
						   G_DBUS_CALL_FLAGS_NONE, 10000, NULL, on_async_call_finish, ctx);
}

void bluetooth_connect_device_async(const char *path, BluetoothConnectCallback cb, gpointer user_data) {
	bluetooth_device_call_async(path, "Connect", cb, user_data);
}

void bluetooth_disconnect_device_async(const char *path, BluetoothConnectCallback cb, gpointer user_data) {
	bluetooth_device_call_async(path, "Disconnect", cb, user_data);
}

void fetch_bluetooth(AppState *state) {
	int exists = 0;
	int powered = 0;
	int connected = 0;
	char device[64] = "";

	bluetooth_get_status(&exists, &powered, &connected, device, sizeof(device));

	pthread_mutex_lock(&state->mutex);
	state->sys_data.bluetooth_adapter_exists = exists;
	state->sys_data.bluetooth_powered = powered;
	state->sys_data.bluetooth_connected = connected;
	g_strlcpy(state->sys_data.bluetooth_device, device, sizeof(state->sys_data.bluetooth_device));
	pthread_mutex_unlock(&state->mutex);
}
