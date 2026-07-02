#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <glib.h>
#include <stddef.h>

typedef struct {
	char path[256];
	char name[128];
	char icon[16];
	int paired;
	int connected;
} BluetoothDevice;

void bluetooth_get_status(int *adapter_exists, int *powered, int *connected, char *device_name, size_t device_name_size);
int bluetooth_set_powered(int powered);
GPtrArray *bluetooth_list_devices(void);
int bluetooth_connect_device(const char *path);
int bluetooth_disconnect_device(const char *path);

typedef void (*BluetoothConnectCallback)(const char *path, int success, gpointer user_data);
void bluetooth_connect_device_async(const char *path, BluetoothConnectCallback cb, gpointer user_data);
void bluetooth_disconnect_device_async(const char *path, BluetoothConnectCallback cb, gpointer user_data);

#endif
