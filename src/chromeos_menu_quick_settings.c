#include "bluetooth.h"
#include "chromeos_menu.h"
#include "chromeos_menu_internal.h"
#include "icons.h"
#include "ipc.h"
#include "metrics.h"
#include "util.h"
#include "widgets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *get_battery_info_str(SystemData *d) {
	if (d->bat_percent < 0)
		return g_strdup("Battery: N/A");

	if (d->bat_time_remaining[0])
		return g_strdup_printf("%d%% - %s", d->bat_percent, d->bat_time_remaining);

	if (d->bat_charging)
		return g_strdup_printf("%d%% - Charging", d->bat_percent);

	return g_strdup_printf("%d%%", d->bat_percent);
}

typedef struct {
	BarWindow *bw;
	AppState *state;
	int index;
	char *layout;
} KeyboardLayoutCtx;

typedef struct {
	BarWindow *bw;
	AppState *state;
	char path[256];
	int connected;
} BluetoothDeviceCtx;

typedef struct {
	BarWindow *bw;
	AppState *state;
	char path[256];
} BluetoothConnectCtx;

static void free_keyboard_layout_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	KeyboardLayoutCtx *ctx = (KeyboardLayoutCtx *)data;
	g_free(ctx->layout);
	g_free(ctx);
}

static void free_bluetooth_device_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	g_free(data);
}

static char *layout_code_label(const char *layout) {
	char *copy = g_strdup(layout ? layout : "");
	g_strstrip(copy);
	for (char *p = copy; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return copy;
}

static char **get_keyboard_layouts(void) {
	char *opt = hyprctl_request("j/getoption input:kb_layout");
	if (!opt)
		return NULL;

	char layouts_str[256] = "";
	json_str(opt, "\"str\":", layouts_str, sizeof(layouts_str));
	free(opt);
	if (!layouts_str[0])
		return NULL;

	return g_strsplit(layouts_str, ",", -1);
}

static void on_keyboard_arrow_clicked(GtkWidget *widget, gpointer data);

static void on_keyboard_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	AppState *state = ctx->state;
	char *res = hyprctl_request("switchxkblayout all next");
	if (res)
		free(res);
	g_timeout_add(100, update_widgets_idle, state);
}

static void on_keyboard_layout_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	KeyboardLayoutCtx *ctx = (KeyboardLayoutCtx *)data;
	BarWindow *bw = ctx->bw;
	AppState *state = ctx->state;

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "switchxkblayout all %d", ctx->index);
	char *res = hyprctl_request(cmd);
	if (res)
		free(res);

	char *label = layout_code_label(ctx->layout);
	pthread_mutex_lock(&state->mutex);
	g_strlcpy(state->sys_data.kb_layout, label, sizeof(state->sys_data.kb_layout));
	pthread_mutex_unlock(&state->mutex);
	g_free(label);

	g_timeout_add(100, update_widgets_idle, state);
	on_keyboard_arrow_clicked(NULL, &(MenuCtx){.bw = bw, .state = state});
}

static void on_keyboard_arrow_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	BarWindow *bw = ctx->bw;
	AppState *state = ctx->state;

	chromeos_menu_clear(bw);

	chromeos_menu_create_subpage_header(bw, state, "Keyboard");

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(content), "cb-menu-content");
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), content, TRUE, TRUE, 0);

	char **layouts = get_keyboard_layouts();
	if (!layouts) {
		GtkWidget *fallback = gtk_label_new("No layouts configured");
		gtk_widget_set_halign(fallback, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(content), fallback, FALSE, FALSE, 0);
		gtk_widget_show_all(bw->cb_menu_main_box);
		return;
	}

	pthread_mutex_lock(&state->mutex);
	char active[32];
	g_strlcpy(active, state->sys_data.kb_layout, sizeof(active));
	pthread_mutex_unlock(&state->mutex);

	for (int i = 0; layouts[i]; i++) {
		char *label = layout_code_label(layouts[i]);
		GtkWidget *btn = gtk_button_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-wifi-list-btn");

		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		GtkWidget *icon = gtk_label_new(ICON_KEYBOARD);
		gtk_style_context_add_class(gtk_widget_get_style_context(icon), "icon");
		gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

		GtkWidget *name = gtk_label_new(label);
		gtk_widget_set_halign(name, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(row), name, TRUE, TRUE, 0);

		if (g_ascii_strcasecmp(active, label) == 0) {
			GtkWidget *check = gtk_label_new(ICON_CHECK);
			gtk_style_context_add_class(gtk_widget_get_style_context(check), "icon");
			gtk_box_pack_end(GTK_BOX(row), check, FALSE, FALSE, 0);
		}

		gtk_container_add(GTK_CONTAINER(btn), row);

		KeyboardLayoutCtx *layout_ctx = g_new0(KeyboardLayoutCtx, 1);
		layout_ctx->bw = bw;
		layout_ctx->state = state;
		layout_ctx->index = i;
		layout_ctx->layout = g_strdup(layouts[i]);
		g_signal_connect_data(btn, "clicked", G_CALLBACK(on_keyboard_layout_clicked), layout_ctx, (GClosureNotify)free_keyboard_layout_ctx,
							  0);

		gtk_box_pack_start(GTK_BOX(content), btn, FALSE, FALSE, 0);
		g_free(label);
	}
	g_strfreev(layouts);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

void chromeos_menu_on_back_to_main_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	chromeos_menu_show_main(ctx->bw, ctx->state);
}

static gboolean delayed_screenshot(gpointer data) {
	char *cmd = (char *)data;
	g_spawn_command_line_async(cmd, NULL);
	g_free(cmd);
	return G_SOURCE_REMOVE;
}

static void on_screenshot_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	AppState *state = (AppState *)data;
	char *cmd = g_malloc(512);
	const char *app = state->config.chromeos.screenshot_app;

	if (app[0] == '~') {
		snprintf(cmd, 512, "%s%s", getenv("HOME"), app + 1);
	} else {
		g_strlcpy(cmd, app, 512);
	}

	close_all_chromeos_menus(state);
	g_timeout_add(500, delayed_screenshot, cmd);
}

void chromeos_menu_refresh_bluetooth_state(AppState *state) { fetch_bluetooth(state); }

static void show_bluetooth_menu(BarWindow *bw, AppState *state);

static void on_bluetooth_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;

	pthread_mutex_lock(&ctx->state->mutex);
	int exists = ctx->state->sys_data.bluetooth_adapter_exists;
	int powered = ctx->state->sys_data.bluetooth_powered;
	pthread_mutex_unlock(&ctx->state->mutex);

	if (exists)
		bluetooth_set_powered(!powered);

	chromeos_menu_refresh_bluetooth_state(ctx->state);
	chromeos_menu_show_main(ctx->bw, ctx->state);
}

static void on_bluetooth_arrow_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	show_bluetooth_menu(ctx->bw, ctx->state);
}

static void on_bluetooth_toggled(GObject *object, GParamSpec *pspec, gpointer data) {
	(void)pspec;
	MenuCtx *ctx = (MenuCtx *)data;
	int active = gtk_switch_get_active(GTK_SWITCH(object));
	bluetooth_set_powered(active);
	chromeos_menu_refresh_bluetooth_state(ctx->state);
	show_bluetooth_menu(ctx->bw, ctx->state);
}

static void on_bluetooth_connect_finish(const char *path, int success, gpointer user_data) {
	(void)path;
	(void)success;
	BluetoothConnectCtx *ctx = (BluetoothConnectCtx *)user_data;
	AppState *state = ctx->state;

	pthread_mutex_lock(&state->mutex);
	state->sys_data.bluetooth_connecting[0] = '\0';
	int bw_alive = 0;
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		if (l->data == ctx->bw) {
			bw_alive = 1;
			break;
		}
	}
	pthread_mutex_unlock(&state->mutex);

	if (bw_alive) {
		chromeos_menu_refresh_bluetooth_state(state);
		if (ctx->bw->menu_window)
			show_bluetooth_menu(ctx->bw, state);
	}
	g_free(ctx);
}

static void on_bluetooth_device_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	BluetoothDeviceCtx *ctx = (BluetoothDeviceCtx *)data;
	BarWindow *bw = ctx->bw;
	AppState *state = ctx->state;

	BluetoothConnectCtx *cb_ctx = g_new0(BluetoothConnectCtx, 1);
	cb_ctx->bw = bw;
	cb_ctx->state = state;
	g_strlcpy(cb_ctx->path, ctx->path, sizeof(cb_ctx->path));

	pthread_mutex_lock(&state->mutex);
	g_strlcpy(state->sys_data.bluetooth_connecting, ctx->path, sizeof(state->sys_data.bluetooth_connecting));
	pthread_mutex_unlock(&state->mutex);

	show_bluetooth_menu(bw, state);

	if (ctx->connected)
		bluetooth_disconnect_device_async(ctx->path, on_bluetooth_connect_finish, cb_ctx);
	else
		bluetooth_connect_device_async(ctx->path, on_bluetooth_connect_finish, cb_ctx);
}

static int bluetooth_device_cmp(gconstpointer a, gconstpointer b) {
	const BluetoothDevice *da = *(const BluetoothDevice **)a;
	const BluetoothDevice *db = *(const BluetoothDevice **)b;
	int pa = da->connected || da->paired;
	int pb = db->connected || db->paired;
	return pb - pa;
}

static void show_bluetooth_menu(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *back_ctx = g_new0(MenuCtx, 1);
	back_ctx->bw = bw;
	back_ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(chromeos_menu_on_back_to_main_clicked), back_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title_lbl = gtk_label_new("Bluetooth");
	gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title_lbl, FALSE, FALSE, 0);

	pthread_mutex_lock(&state->mutex);
	gboolean bt_on = state->sys_data.bluetooth_powered;
	pthread_mutex_unlock(&state->mutex);

	GtkWidget *toggle = gtk_switch_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(toggle), "cb-menu-led-toggle");
	gtk_switch_set_active(GTK_SWITCH(toggle), bt_on);
	gtk_widget_set_halign(toggle, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
	MenuCtx *toggle_ctx = g_new0(MenuCtx, 1);
	toggle_ctx->bw = bw;
	toggle_ctx->state = state;
	g_signal_connect_data(toggle, "notify::active", G_CALLBACK(on_bluetooth_toggled), toggle_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_end(GTK_BOX(header), toggle, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(content), "cb-menu-content");
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), content, TRUE, TRUE, 0);

	pthread_mutex_lock(&state->mutex);
	int exists = state->sys_data.bluetooth_adapter_exists;
	int powered = state->sys_data.bluetooth_powered;
	char connecting[256];
	g_strlcpy(connecting, state->sys_data.bluetooth_connecting, sizeof(connecting));
	pthread_mutex_unlock(&state->mutex);

	if (!exists || !powered) {
		GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
		gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
		gtk_widget_set_vexpand(empty_box, TRUE);

		GtkWidget *empty_icon = gtk_label_new(ICON_BLUETOOTH_OFF);
		gtk_style_context_add_class(gtk_widget_get_style_context(empty_icon), "icon");
		gtk_widget_set_margin_bottom(empty_icon, 8);
		gtk_box_pack_start(GTK_BOX(empty_box), empty_icon, FALSE, FALSE, 0);

		GtkWidget *empty_lbl = gtk_label_new(!exists ? "No adapter" : "Bluetooth is off");
		gtk_style_context_add_class(gtk_widget_get_style_context(empty_lbl), "subtitle");
		gtk_box_pack_start(GTK_BOX(empty_box), empty_lbl, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(content), empty_box, FALSE, FALSE, 0);
		gtk_widget_show_all(bw->cb_menu_main_box);
		return;
	}

	GPtrArray *devices = bluetooth_list_devices();
	if (devices->len == 0) {
		GtkWidget *fallback = gtk_label_new("No devices");
		gtk_widget_set_halign(fallback, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(content), fallback, FALSE, FALSE, 0);
		g_ptr_array_unref(devices);
		gtk_widget_show_all(bw->cb_menu_main_box);
		return;
	}

	g_ptr_array_sort(devices, (GCompareFunc)bluetooth_device_cmp);

	int show_paired = 1;
	for (guint i = 0; i < devices->len; i++) {
		BluetoothDevice *device = g_ptr_array_index(devices, i);
		int is_paired = device->connected || device->paired;

		if (is_paired && show_paired) {
			GtkWidget *lbl = gtk_label_new("Paired devices");
			gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "cb-menu-section-label");
			gtk_widget_set_halign(lbl, GTK_ALIGN_START);
			gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 0);
			show_paired = 0;
		} else if (!is_paired && !show_paired) {
			GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_widget_set_margin_top(sep, 6);
			gtk_widget_set_margin_bottom(sep, 6);
			gtk_box_pack_start(GTK_BOX(content), sep, FALSE, FALSE, 0);
			GtkWidget *lbl = gtk_label_new("Available devices");
			gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "cb-menu-section-label");
			gtk_widget_set_halign(lbl, GTK_ALIGN_START);
			gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 0);
			show_paired = -1;
		} else if (!is_paired && show_paired == 1) {
			GtkWidget *lbl = gtk_label_new("Available devices");
			gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "cb-menu-section-label");
			gtk_widget_set_halign(lbl, GTK_ALIGN_START);
			gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 0);
			show_paired = -1;
		}
		int is_connecting = connecting[0] && strcmp(device->path, connecting) == 0;
		GtkWidget *btn = gtk_button_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-wifi-list-btn");

		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		GtkWidget *icon = gtk_label_new(device->icon[0] ? device->icon : ICON_BLUETOOTH);
		gtk_style_context_add_class(gtk_widget_get_style_context(icon), "icon");
		gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

		GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *name = gtk_label_new(device->name);
		chromeos_menu_ellipsize_label(name, 48);
		gtk_widget_set_halign(name, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text), name, FALSE, FALSE, 0);

		const char *status;
		if (is_connecting)
			status = device->connected ? "Disconnecting…" : "Connecting…";
		else
			status = device->connected ? "Connected" : (device->paired ? "Paired" : "Available");

		GtkWidget *subtitle = gtk_label_new(status);
		gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "subtitle");
		gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text), subtitle, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);

		if (device->connected && !is_connecting) {
			GtkWidget *check = gtk_label_new(ICON_CHECK);
			gtk_style_context_add_class(gtk_widget_get_style_context(check), "icon");
			gtk_box_pack_end(GTK_BOX(row), check, FALSE, FALSE, 0);
		}

		gtk_container_add(GTK_CONTAINER(btn), row);
		gtk_widget_set_sensitive(btn, !is_connecting);

		BluetoothDeviceCtx *device_ctx = g_new0(BluetoothDeviceCtx, 1);
		device_ctx->bw = bw;
		device_ctx->state = state;
		device_ctx->connected = device->connected;
		g_strlcpy(device_ctx->path, device->path, sizeof(device_ctx->path));
		g_signal_connect_data(btn, "clicked", G_CALLBACK(on_bluetooth_device_clicked), device_ctx,
							  (GClosureNotify)free_bluetooth_device_ctx, 0);

		gtk_box_pack_start(GTK_BOX(content), btn, FALSE, FALSE, 0);
	}
	g_ptr_array_unref(devices);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

static void power_menu_free_cmd(gpointer data, GClosure *closure) {
	(void)closure;
	g_free(data);
}

static void power_menu_on_option_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	const char *cmd = (const char *)data;
	g_spawn_command_line_async(cmd, NULL);
}

static void power_menu_on_btn_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	GtkPopover *popover = GTK_POPOVER(data);
	if (gtk_widget_get_visible(GTK_WIDGET(popover)))
		gtk_popover_popdown(popover);
	else
		gtk_popover_popup(popover);
}

static void on_settings_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	chromeos_menu_show_leds(ctx->bw, ctx->state);
}

void chromeos_menu_show_main(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);
	g_object_set_data(G_OBJECT(bw->cb_menu_main_box), "current-view", "main");

	GtkWidget *grid = gtk_grid_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(grid), "cb-menu-grid");
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_object_set_data_full(G_OBJECT(grid), "menu-ctx", ctx, g_free);

	pthread_mutex_lock(&state->mutex);
	SystemData d = state->sys_data;
	pthread_mutex_unlock(&state->mutex);

	const char *w_icon = ICON_WIFI_OFF; /* Off / No Adapter */
	const char *w_subtitle = "Off";
	int wifi_active = 0;
	int wifi_pill_sensitive = 1;
	int wifi_arrow_sensitive = 0;

	if (!d.wifi_adapter_exists) {
		w_subtitle = "No adapter";
		wifi_pill_sensitive = 0;
	} else {
		wifi_arrow_sensitive = 1;
		if (d.wifi_enabled) {
			wifi_active = 1;
			if (d.wifi_connected) {
				w_icon = get_wifi_icon(d.wifi_strength);
				w_subtitle = d.wifi_ssid[0] ? d.wifi_ssid : "Connected";
			} else {
				w_icon = ICON_WIFI_0;
				w_subtitle = "Disconnected";
			}
		}
	}

	bw->cb_menu_wifi_pill = chromeos_menu_create_pill(
		w_icon, "WiFi", w_subtitle, wifi_active, &bw->cb_menu_wifi_subtitle, &bw->cb_menu_wifi_icon, &bw->cb_menu_wifi_arrow,
		G_CALLBACK(chromeos_menu_on_wifi_clicked), G_CALLBACK(chromeos_menu_on_wifi_arrow_clicked), ctx);
	gtk_widget_set_sensitive(bw->cb_menu_wifi_pill, wifi_pill_sensitive);
	if (bw->cb_menu_wifi_arrow)
		gtk_widget_set_sensitive(bw->cb_menu_wifi_arrow, wifi_arrow_sensitive);

	g_signal_connect(bw->cb_menu_wifi_pill, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->cb_menu_wifi_pill);
	if (bw->cb_menu_wifi_icon)
		g_signal_connect(bw->cb_menu_wifi_icon, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->cb_menu_wifi_icon);
	if (bw->cb_menu_wifi_subtitle)
		g_signal_connect(bw->cb_menu_wifi_subtitle, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->cb_menu_wifi_subtitle);
	if (bw->cb_menu_wifi_arrow)
		g_signal_connect(bw->cb_menu_wifi_arrow, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->cb_menu_wifi_arrow);
	gtk_grid_attach(GTK_GRID(grid), bw->cb_menu_wifi_pill, 0, 0, 2, 1);

	GtkWidget *screenshot = chromeos_menu_create_pill(ICON_CAMERA, "Screen capture", NULL, FALSE, NULL, NULL, NULL,
													  G_CALLBACK(on_screenshot_clicked), NULL, state);
	gtk_grid_attach(GTK_GRID(grid), screenshot, 2, 0, 2, 1);

	pthread_mutex_lock(&state->mutex);
	int bluetooth_exists = state->sys_data.bluetooth_adapter_exists;
	int bluetooth_powered = state->sys_data.bluetooth_powered;
	int bluetooth_connected = state->sys_data.bluetooth_connected;
	char bluetooth_device[64];
	g_strlcpy(bluetooth_device, state->sys_data.bluetooth_device, sizeof(bluetooth_device));
	pthread_mutex_unlock(&state->mutex);

	const char *bluetooth_subtitle = "No adapter";
	if (bluetooth_exists) {
		if (!bluetooth_powered)
			bluetooth_subtitle = "Off";
		else if (bluetooth_connected)
			bluetooth_subtitle = bluetooth_device[0] ? bluetooth_device : "Connected";
		else
			bluetooth_subtitle = "On";
	}

	GtkWidget *bluetooth =
		chromeos_menu_create_pill(ICON_BLUETOOTH, "Bluetooth", bluetooth_subtitle, bluetooth_exists && bluetooth_powered, NULL, NULL, NULL,
								  G_CALLBACK(on_bluetooth_clicked), G_CALLBACK(on_bluetooth_arrow_clicked), ctx);
	gtk_grid_attach(GTK_GRID(grid), bluetooth, 0, 1, 2, 1);

	GtkWidget *keyboard = chromeos_menu_create_pill(
		ICON_KEYBOARD, "Keyboard", state->sys_data.kb_layout[0] ? state->sys_data.kb_layout : "US", FALSE, &bw->cb_menu_kb_label, NULL,
		NULL, G_CALLBACK(on_keyboard_clicked), G_CALLBACK(on_keyboard_arrow_clicked), ctx);
	if (bw->cb_menu_kb_label)
		g_signal_connect(bw->cb_menu_kb_label, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->cb_menu_kb_label);
	gtk_grid_attach(GTK_GRID(grid), keyboard, 2, 1, 2, 1);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), grid, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), chromeos_menu_create_volume_slider(ctx), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), chromeos_menu_create_brightness_nightlight_slider(ctx), FALSE, FALSE, 0);

	GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(bottom_box), "cb-menu-bottom");

	GtkWidget *power_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(power_btn), "cb-menu-power");
	gtk_container_add(GTK_CONTAINER(power_btn), gtk_label_new(ICON_POWER " " ICON_POWER_PLUG));

	GtkWidget *power_popover = gtk_popover_new(power_btn);
	gtk_popover_set_position(GTK_POPOVER(power_popover), GTK_POS_TOP);
	gtk_popover_set_modal(GTK_POPOVER(power_popover), TRUE);

	GtkWidget *power_menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(power_popover), power_menu_box);

	struct {
		const char *icon;
		const char *label;
		const char *cmd;
	} power_opts[] = {
		{ICON_POWER, "Shut down", "systemctl poweroff"},
		{ICON_SLEEP, "Suspend", "systemctl suspend"},
		{ICON_RESTART, "Restart", "systemctl reboot"},
	};
	for (int i = 0; i < 3; i++) {
		GtkWidget *opt = gtk_button_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(opt), "cb-menu-wifi-list-btn");
		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		GtkWidget *icon = gtk_label_new(power_opts[i].icon);
		gtk_style_context_add_class(gtk_widget_get_style_context(icon), "icon");
		gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
		GtkWidget *lbl = gtk_label_new(power_opts[i].label);
		gtk_widget_set_halign(lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);
		gtk_container_add(GTK_CONTAINER(opt), row);

		char *cmd = g_strdup(power_opts[i].cmd);
		g_signal_connect_data(opt, "clicked", G_CALLBACK(power_menu_on_option_clicked), cmd, (GClosureNotify)power_menu_free_cmd, 0);
		gtk_box_pack_start(GTK_BOX(power_menu_box), opt, FALSE, FALSE, 0);
	}

	g_signal_connect(power_btn, "clicked", G_CALLBACK(power_menu_on_btn_clicked), power_popover);
	gtk_widget_show_all(power_menu_box);
	gtk_box_pack_start(GTK_BOX(bottom_box), power_btn, FALSE, FALSE, 0);

	char *bat_info = get_battery_info_str(&state->sys_data);
	GtkWidget *battery_lbl = gtk_label_new(bat_info);
	g_free(bat_info);
	bw->cb_menu_bat_label = battery_lbl;
	g_signal_connect(bw->cb_menu_bat_label, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->cb_menu_bat_label);
	gtk_style_context_add_class(gtk_widget_get_style_context(battery_lbl), "cb-menu-battery");
	chromeos_menu_ellipsize_label(battery_lbl, 22);
	gtk_widget_set_halign(battery_lbl, GTK_ALIGN_END);
	gtk_box_pack_start(GTK_BOX(bottom_box), battery_lbl, TRUE, TRUE, 12);

	GtkWidget *settings_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(settings_btn), "cb-menu-settings");
	gtk_widget_set_valign(settings_btn, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(settings_btn), gtk_label_new(ICON_SETTINGS));
	{
		MenuCtx *sctx = g_new0(MenuCtx, 1);
		sctx->bw = bw;
		sctx->state = state;
		g_signal_connect_data(settings_btn, "clicked", G_CALLBACK(on_settings_clicked), sctx,
							  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	}
	gtk_box_pack_end(GTK_BOX(bottom_box), settings_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), bottom_box, FALSE, FALSE, 0);

	gtk_widget_show_all(bw->cb_menu_main_box);
}
