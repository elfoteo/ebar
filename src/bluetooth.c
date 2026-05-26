#include "bluetooth.h"
#include <gio/gio.h>
#include <string.h>

static GDBusConnection *bluetooth_bus(void) {
	GError *error = NULL;
	GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (error)
		g_error_free(error);
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
