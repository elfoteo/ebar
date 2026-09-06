#include "chromeos_menu_internal.h"
#include "icons.h"
#include "wifi.h"

#include <stdlib.h>

typedef struct {
	BarWindow *bw;
	AppState *state;
	char *ssid;
	char *security;
	gboolean active;
	gboolean hidden;
} WifiNetCtx;

typedef struct {
	BarWindow *bw;
	AppState *state;
	char *ssid;
	GtkWidget *password_entry;
} WifiConnectCtx;

typedef struct {
	BarWindow *bw;
	AppState *state;
	GtkWidget *ssid_entry;
	GtkWidget *password_entry;
	GtkWidget *error_label;
	GtkWidget *spinner;
	GtkWidget *connect_btn;
	GtkWidget *cancel_btn;
	gboolean connecting;
	gpointer pending_td; /* WifiConnectThreadData* while a connect is in flight */
} WifiHiddenConnectCtx;

typedef struct WifiConnectThreadData {
	WifiHiddenConnectCtx *ctx;
	BarWindow *bw;
	char *ssid;
	char *password;
	char *error;
	gboolean cancel; /* set when the menu/bar window is destroyed while connecting */
} WifiConnectThreadData;

typedef struct {
	GtkWidget *entry;
	GtkWidget *button;
} PasswordToggleCtx;

static void wifi_cancel_pending_thread(WifiHiddenConnectCtx *ctx) {
	if (ctx->pending_td)
		((WifiConnectThreadData *)ctx->pending_td)->cancel = TRUE;
}

static void free_wifi_net_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	WifiNetCtx *ctx = (WifiNetCtx *)data;
	g_free(ctx->ssid);
	g_free(ctx->security);
	g_free(ctx);
}

static void free_wifi_connect_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	WifiConnectCtx *ctx = (WifiConnectCtx *)data;
	g_free(ctx->ssid);
	g_free(ctx);
}

static void free_wifi_hidden_connect_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	WifiHiddenConnectCtx *ctx = (WifiHiddenConnectCtx *)data;
	wifi_cancel_pending_thread(ctx);
	g_free(ctx);
}

static int wifi_signal_level(int strength) {
	if (strength >= 75)
		return 4;
	if (strength >= 50)
		return 3;
	if (strength >= 25)
		return 2;
	if (strength > 0)
		return 1;
	return 0;
}

static const char *wifi_signal_icon(int strength, gboolean secured) {
	int level = wifi_signal_level(strength);

	if (secured) {
		static const char *secured_icons[] = {ICON_WIFI_SEC_0, ICON_WIFI_SEC_1, ICON_WIFI_SEC_2, ICON_WIFI_SEC_3, ICON_WIFI_SEC_4};
		return secured_icons[level];
	}

	static const char *open_icons[] = {ICON_WIFI_0, ICON_WIFI_1, ICON_WIFI_2, ICON_WIFI_3, ICON_WIFI_4};
	return open_icons[level];
}

void chromeos_menu_on_wifi_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	AppState *state = ctx->state;
	pthread_mutex_lock(&state->mutex);
	int enabled = state->sys_data.wifi_enabled;
	pthread_mutex_unlock(&state->mutex);

	wifi_set_enabled(!enabled);
	chromeos_menu_show_main(ctx->bw, state);
}

static void chromeos_menu_on_wifi_toggled(GObject *object, GParamSpec *pspec, gpointer data) {
	(void)pspec;
	MenuCtx *ctx = (MenuCtx *)data;
	int active = gtk_switch_get_active(GTK_SWITCH(object));
	wifi_set_enabled(active);
	(void)ctx;
}

void chromeos_menu_on_wifi_arrow_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	chromeos_menu_show_wifi_networks(ctx->bw, ctx->state);
}

static void on_cancel_wifi_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	chromeos_menu_show_wifi_networks(ctx->bw, ctx->state);
}

static void on_connect_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	WifiConnectCtx *ctx = (WifiConnectCtx *)data;
	const char *password = gtk_entry_get_text(GTK_ENTRY(ctx->password_entry));
	wifi_connect(ctx->ssid, password, NULL, NULL);
	chromeos_menu_show_wifi_networks(ctx->bw, ctx->state);
}

static char *strip_ansi(const char *str) {
	if (!str)
		return NULL;
	GString *out = g_string_new(NULL);
	const char *p = str;
	while (*p) {
		if (*p == '\033') {
			p++;
			while (*p && *p != 'm')
				p++;
			if (*p == 'm')
				p++;
		} else {
			g_string_append_c(out, *p);
			p++;
		}
	}
	char *result = g_string_free_and_steal(out);
	return result;
}

static guint wifi_refresh_pending = 0;

static gboolean nmcli_connect_hidden(const char *ssid, const char *passphrase, char **error_out) {
	gint exit_status = 0;
	gchar *stdout_str = NULL;
	gchar *stderr_str = NULL;
	GError *err = NULL;

	const char *argv[] = {
		"nmcli", "device", "wifi", "connect", ssid,
		"password", passphrase,
		"hidden", "yes",
		NULL
	};

	g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &stdout_str, &stderr_str, &exit_status, &err);

	if (err) {
		if (error_out)
			*error_out = strip_ansi(err->message);
		g_error_free(err);
	} else if (exit_status != 0) {
		const char *msg = (stderr_str && stderr_str[0] != '\0') ? stderr_str :
						  (stdout_str && stdout_str[0] != '\0') ? stdout_str : "Unknown error";
		if (error_out)
			*error_out = strip_ansi(msg);
	}

	g_free(stdout_str);
	g_free(stderr_str);
	return (exit_status == 0 && !err);
}

static gboolean on_connect_done(gpointer user_data) {
	WifiConnectThreadData *td = (WifiConnectThreadData *)user_data;

	if (!td->cancel) {
		WifiHiddenConnectCtx *ctx = td->ctx;
		gtk_spinner_stop(GTK_SPINNER(ctx->spinner));
		gtk_widget_hide(ctx->spinner);
		gtk_widget_set_sensitive(ctx->connect_btn, TRUE);
		gtk_widget_set_sensitive(ctx->cancel_btn, TRUE);
		gtk_widget_set_sensitive(ctx->ssid_entry, TRUE);
		gtk_widget_set_sensitive(ctx->password_entry, TRUE);
		ctx->connecting = FALSE;
		ctx->pending_td = NULL;

		if (td->error) {
			char *display = g_strdup_printf("Connection failed: %s", td->error);
			gtk_label_set_text(GTK_LABEL(ctx->error_label), display);
			gtk_widget_show(ctx->error_label);
			g_free(display);
		} else {
			if (wifi_refresh_pending) {
				g_source_remove(wifi_refresh_pending);
				wifi_refresh_pending = 0;
			}
			if (ctx->bw->cb_menu_main_box)
				chromeos_menu_show_wifi_networks(ctx->bw, ctx->state);
		}
	}

	g_free(td->error);
	g_free(td->ssid);
	g_free(td->password);
	g_free(td);
	return G_SOURCE_REMOVE;
}

static gpointer connect_thread_func(gpointer user_data) {
	WifiConnectThreadData *td = (WifiConnectThreadData *)user_data;
	char *error_msg = NULL;
	nmcli_connect_hidden(td->ssid, td->password, &error_msg);
	td->error = error_msg;
	g_idle_add(on_connect_done, td);
	return NULL;
}

static void on_hidden_connect_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	WifiHiddenConnectCtx *ctx = (WifiHiddenConnectCtx *)data;
	const char *ssid = gtk_entry_get_text(GTK_ENTRY(ctx->ssid_entry));
	const char *password = gtk_entry_get_text(GTK_ENTRY(ctx->password_entry));
	if (!ssid || ssid[0] == '\0' || ctx->connecting)
		return;

	ctx->connecting = TRUE;
	gtk_widget_hide(ctx->error_label);
	gtk_spinner_start(GTK_SPINNER(ctx->spinner));
	gtk_widget_show(ctx->spinner);
	gtk_widget_set_sensitive(ctx->connect_btn, FALSE);
	gtk_widget_set_sensitive(ctx->cancel_btn, FALSE);
	gtk_widget_set_sensitive(ctx->ssid_entry, FALSE);
	gtk_widget_set_sensitive(ctx->password_entry, FALSE);

	WifiConnectThreadData *td = g_new0(WifiConnectThreadData, 1);
	td->ctx = ctx;
	td->bw = ctx->bw;
	td->ssid = g_strdup(ssid);
	td->password = g_strdup(password);
	ctx->pending_td = td;

	g_thread_new("wifi-connect", connect_thread_func, td);
}

static void on_password_visibility_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	PasswordToggleCtx *ctx = (PasswordToggleCtx *)data;
	gboolean visible = gtk_entry_get_visibility(GTK_ENTRY(ctx->entry));
	gtk_entry_set_visibility(GTK_ENTRY(ctx->entry), !visible);
	gtk_button_set_label(GTK_BUTTON(ctx->button), visible ? ICON_EYE : ICON_EYE_OFF);
}

static void show_wifi_password_entry(BarWindow *bw, AppState *state, const char *ssid) {
	chromeos_menu_clear(bw);
	g_object_set_data(G_OBJECT(bw->cb_menu_main_box), "current-view", "wifi-password");

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(on_cancel_wifi_clicked), ctx, (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title = gtk_label_new("Connect to WiFi");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(content), "cb-menu-content");
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), content, TRUE, TRUE, 0);

	GtkWidget *ssid_lbl = gtk_label_new(ssid);
	gtk_style_context_add_class(gtk_widget_get_style_context(ssid_lbl), "cb-menu-wifi-ssid");
	gtk_widget_set_halign(ssid_lbl, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(content), ssid_lbl, FALSE, FALSE, 0);

	GtkWidget *password_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(password_row), "cb-menu-password-row");

	GtkWidget *entry = gtk_entry_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(entry), "cb-menu-entry");
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Password");
	gtk_box_pack_start(GTK_BOX(password_row), entry, TRUE, TRUE, 0);

	GtkWidget *visibility_btn = gtk_button_new_with_label(ICON_EYE);
	gtk_style_context_add_class(gtk_widget_get_style_context(visibility_btn), "cb-menu-icon-btn");
	gtk_style_context_add_class(gtk_widget_get_style_context(visibility_btn), "cb-menu-eye-btn");
	gtk_widget_set_halign(visibility_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(visibility_btn, GTK_ALIGN_CENTER);
	PasswordToggleCtx *toggle_ctx = g_new0(PasswordToggleCtx, 1);
	toggle_ctx->entry = entry;
	toggle_ctx->button = visibility_btn;
	g_signal_connect_data(visibility_btn, "clicked", G_CALLBACK(on_password_visibility_clicked), toggle_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(password_row), visibility_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(content), password_row, FALSE, FALSE, 0);

	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(actions, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand(actions, TRUE);

	GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
	gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "cb-menu-dialog-btn");
	gtk_widget_set_hexpand(cancel_btn, TRUE);
	MenuCtx *ctx2 = g_new0(MenuCtx, 1);
	ctx2->bw = bw;
	ctx2->state = state;
	g_signal_connect_data(cancel_btn, "clicked", G_CALLBACK(on_cancel_wifi_clicked), ctx2, (GClosureNotify)chromeos_menu_free_generic_ctx,
						  0);
	gtk_box_pack_start(GTK_BOX(actions), cancel_btn, TRUE, TRUE, 0);

	GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
	gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "cb-menu-dialog-btn");
	gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "cb-menu-dialog-btn-primary");
	gtk_widget_set_hexpand(connect_btn, TRUE);
	WifiConnectCtx *conn_ctx = g_new0(WifiConnectCtx, 1);
	conn_ctx->bw = bw;
	conn_ctx->state = state;
	conn_ctx->ssid = g_strdup(ssid);
	conn_ctx->password_entry = entry;
	g_signal_connect_data(connect_btn, "clicked", G_CALLBACK(on_connect_clicked), conn_ctx, (GClosureNotify)free_wifi_connect_ctx, 0);
	gtk_box_pack_start(GTK_BOX(actions), connect_btn, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(content), actions, FALSE, FALSE, 0);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

static void show_wifi_hidden_network_dialog(BarWindow *bw, AppState *state, const char *preset_ssid) {
	chromeos_menu_clear(bw);
	g_object_set_data(G_OBJECT(bw->cb_menu_main_box), "current-view", "wifi-hidden");

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(on_cancel_wifi_clicked), ctx, (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title = gtk_label_new("Hidden Network");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(content), "cb-menu-content");
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), content, TRUE, TRUE, 0);

	GtkWidget *ssid_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(ssid_row), "cb-menu-password-row");

	GtkWidget *ssid_entry = gtk_entry_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(ssid_entry), "cb-menu-entry");
	gtk_entry_set_placeholder_text(GTK_ENTRY(ssid_entry), "Network name (SSID)");
	if (preset_ssid && preset_ssid[0] != '\0') {
		gtk_entry_set_text(GTK_ENTRY(ssid_entry), preset_ssid);
		gtk_editable_set_editable(GTK_EDITABLE(ssid_entry), FALSE);
	}
	gtk_box_pack_start(GTK_BOX(ssid_row), ssid_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(content), ssid_row, FALSE, FALSE, 0);

	GtkWidget *password_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(password_row), "cb-menu-password-row");

	GtkWidget *password_entry = gtk_entry_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(password_entry), "cb-menu-entry");
	gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
	gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
	if (preset_ssid && preset_ssid[0] != '\0')
		gtk_widget_grab_focus(password_entry);
	gtk_box_pack_start(GTK_BOX(password_row), password_entry, TRUE, TRUE, 0);

	GtkWidget *visibility_btn = gtk_button_new_with_label(ICON_EYE);
	gtk_style_context_add_class(gtk_widget_get_style_context(visibility_btn), "cb-menu-icon-btn");
	gtk_style_context_add_class(gtk_widget_get_style_context(visibility_btn), "cb-menu-eye-btn");
	gtk_widget_set_halign(visibility_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(visibility_btn, GTK_ALIGN_CENTER);
	PasswordToggleCtx *toggle_ctx = g_new0(PasswordToggleCtx, 1);
	toggle_ctx->entry = password_entry;
	toggle_ctx->button = visibility_btn;
	g_signal_connect_data(visibility_btn, "clicked", G_CALLBACK(on_password_visibility_clicked), toggle_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(password_row), visibility_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(content), password_row, FALSE, FALSE, 0);

	GtkWidget *error_label = gtk_label_new(NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(error_label), "cb-menu-error");
	gtk_label_set_line_wrap(GTK_LABEL(error_label), TRUE);
	gtk_widget_set_no_show_all(error_label, TRUE);
	gtk_box_pack_start(GTK_BOX(content), error_label, FALSE, FALSE, 0);

	GtkWidget *spinner = gtk_spinner_new();
	gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_no_show_all(spinner, TRUE);
	gtk_box_pack_start(GTK_BOX(content), spinner, FALSE, FALSE, 8);

	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(actions, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand(actions, TRUE);

	GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
	gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "cb-menu-dialog-btn");
	gtk_widget_set_hexpand(cancel_btn, TRUE);
	MenuCtx *ctx2 = g_new0(MenuCtx, 1);
	ctx2->bw = bw;
	ctx2->state = state;
	g_signal_connect_data(cancel_btn, "clicked", G_CALLBACK(on_cancel_wifi_clicked), ctx2, (GClosureNotify)chromeos_menu_free_generic_ctx,
						  0);
	gtk_box_pack_start(GTK_BOX(actions), cancel_btn, TRUE, TRUE, 0);

	GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
	gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "cb-menu-dialog-btn");
	gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "cb-menu-dialog-btn-primary");
	gtk_widget_set_hexpand(connect_btn, TRUE);
	WifiHiddenConnectCtx *conn_ctx = g_new0(WifiHiddenConnectCtx, 1);
	conn_ctx->bw = bw;
	conn_ctx->state = state;
	conn_ctx->ssid_entry = ssid_entry;
	conn_ctx->password_entry = password_entry;
	conn_ctx->error_label = error_label;
	conn_ctx->spinner = spinner;
	conn_ctx->connect_btn = connect_btn;
	conn_ctx->cancel_btn = cancel_btn;
	g_signal_connect_data(connect_btn, "clicked", G_CALLBACK(on_hidden_connect_clicked), conn_ctx,
						  (GClosureNotify)free_wifi_hidden_connect_ctx, 0);
	gtk_box_pack_start(GTK_BOX(actions), connect_btn, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(content), actions, FALSE, FALSE, 0);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

static void on_hidden_network_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	show_wifi_hidden_network_dialog(ctx->bw, ctx->state, NULL);
}

static void on_wifi_net_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	WifiNetCtx *ctx = (WifiNetCtx *)data;
	if (ctx->active) {
		wifi_disconnect();
		chromeos_menu_show_wifi_networks(ctx->bw, ctx->state);
	} else if (ctx->hidden) {
		show_wifi_hidden_network_dialog(ctx->bw, ctx->state, ctx->ssid);
	} else if (ctx->security && strlen(ctx->security) > 0 && strcmp(ctx->security, "--") != 0) {
		if (wifi_has_saved_connection(ctx->ssid))
			wifi_connect(ctx->ssid, NULL, NULL, NULL);
		else
			show_wifi_password_entry(ctx->bw, ctx->state, ctx->ssid);
	} else {
		wifi_connect(ctx->ssid, NULL, NULL, NULL);
	}
}

static gboolean wifi_refresh_idle(gpointer user_data) {
	wifi_refresh_pending = 0;
	BarWindow *bw = (BarWindow *)user_data;

	/* Only refresh if still on the wifi-networks view */
	if (!bw->cb_menu_main_box)
		return G_SOURCE_REMOVE;
	const char *view = g_object_get_data(G_OBJECT(bw->cb_menu_main_box), "current-view");
	if (!view || strcmp(view, "wifi-networks") != 0)
		return G_SOURCE_REMOVE;

	chromeos_menu_show_wifi_networks(bw, bw->state);
	return G_SOURCE_REMOVE;
}

void chromeos_menu_refresh_wifi_list_if_open(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->cb_menu_main_box) {
			const char *view = g_object_get_data(G_OBJECT(bw->cb_menu_main_box), "current-view");
			if (view && strcmp(view, "wifi-networks") == 0) {
				if (wifi_refresh_pending)
					g_source_remove(wifi_refresh_pending);
				wifi_refresh_pending = g_timeout_add(500, wifi_refresh_idle, bw);
			}
		}
	}
	pthread_mutex_unlock(&state->mutex);
}

void chromeos_menu_show_wifi_networks(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);
	g_object_set_data(G_OBJECT(bw->cb_menu_main_box), "current-view", "wifi-networks");

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(chromeos_menu_on_back_to_main_clicked), ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title_lbl = gtk_label_new("WiFi");
	gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title_lbl, FALSE, FALSE, 0);

	pthread_mutex_lock(&state->mutex);
	gboolean wifi_on = state->sys_data.wifi_enabled;
	pthread_mutex_unlock(&state->mutex);

	GtkWidget *toggle = gtk_switch_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(toggle), "cb-menu-led-toggle");
	gtk_switch_set_active(GTK_SWITCH(toggle), wifi_on);
	gtk_widget_set_halign(toggle, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
	MenuCtx *toggle_ctx = g_new0(MenuCtx, 1);
	toggle_ctx->bw = bw;
	toggle_ctx->state = state;
	g_signal_connect_data(toggle, "notify::active", G_CALLBACK(chromeos_menu_on_wifi_toggled), toggle_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_end(GTK_BOX(header), toggle, FALSE, FALSE, 0);

	GtkWidget *hidden_btn = gtk_button_new_with_label("+");
	gtk_style_context_add_class(gtk_widget_get_style_context(hidden_btn), "cb-menu-icon-btn");
	gtk_widget_set_halign(hidden_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(hidden_btn, GTK_ALIGN_CENTER);
	MenuCtx *hidden_ctx = g_new0(MenuCtx, 1);
	hidden_ctx->bw = bw;
	hidden_ctx->state = state;
	g_signal_connect_data(hidden_btn, "clicked", G_CALLBACK(on_hidden_network_clicked), hidden_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_end(GTK_BOX(header), hidden_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), sep, FALSE, FALSE, 8);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scroll, -1, 250);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), scroll, TRUE, TRUE, 0);

	GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_set_spacing(GTK_BOX(list_box), 2);
	gtk_container_add(GTK_CONTAINER(scroll), list_box);

	GPtrArray *networks = NULL;
	if (wifi_on)
		networks = wifi_list_networks();
	else
		networks = g_ptr_array_new();
	for (guint i = 0; i < networks->len; i++) {
		WifiNetwork *network = g_ptr_array_index(networks, i);
		GtkWidget *btn = gtk_button_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-wifi-list-btn");
		GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

		GtkWidget *icon_lbl = gtk_label_new(wifi_signal_icon(network->strength, network->secured));
		gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "icon");
		gtk_box_pack_start(GTK_BOX(btn_box), icon_lbl, FALSE, FALSE, 0);

		GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *name_lbl = gtk_label_new(network->ssid);
		chromeos_menu_ellipsize_label(name_lbl, 64);
		gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text_box), name_lbl, FALSE, FALSE, 0);

		const char *security_text = network->hidden ? "Hidden" : (network->secured ? "Protected" : "Open");
		char subtitle[96];
		if (network->active)
			snprintf(subtitle, sizeof(subtitle), "<span foreground=\"#7fd88f\">%s</span> - Connected", security_text);
		else if (network->secured || network->hidden)
			snprintf(subtitle, sizeof(subtitle), "<span foreground=\"#7fd88f\">%s</span>", security_text);
		else
			snprintf(subtitle, sizeof(subtitle), "%s", security_text);

		GtkWidget *security_lbl = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(security_lbl), subtitle);
		gtk_style_context_add_class(gtk_widget_get_style_context(security_lbl), "subtitle");
		gtk_widget_set_halign(security_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text_box), security_lbl, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(btn_box), text_box, TRUE, TRUE, 0);

		if (network->active) {
			GtkWidget *check = gtk_label_new(ICON_CHECK);
			gtk_style_context_add_class(gtk_widget_get_style_context(check), "icon");
			gtk_box_pack_end(GTK_BOX(btn_box), check, FALSE, FALSE, 0);
		}

		gtk_container_add(GTK_CONTAINER(btn), btn_box);

		WifiNetCtx *net_ctx = g_new0(WifiNetCtx, 1);
		net_ctx->bw = bw;
		net_ctx->state = state;
		net_ctx->ssid = g_strdup(network->ssid);
		net_ctx->security = g_strdup(network->security);
		net_ctx->active = network->active;
		net_ctx->hidden = network->hidden;

		g_signal_connect_data(btn, "clicked", G_CALLBACK(on_wifi_net_clicked), net_ctx, (GClosureNotify)free_wifi_net_ctx, 0);
		gtk_box_pack_start(GTK_BOX(list_box), btn, FALSE, FALSE, 0);
	}
	g_ptr_array_unref(networks);

	if (!wifi_on) {
		GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
		gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
		gtk_widget_set_vexpand(empty_box, TRUE);

		GtkWidget *empty_icon = gtk_label_new(ICON_WIFI_OFF);
		gtk_style_context_add_class(gtk_widget_get_style_context(empty_icon), "icon");
		gtk_widget_set_margin_bottom(empty_icon, 8);
		gtk_box_pack_start(GTK_BOX(empty_box), empty_icon, FALSE, FALSE, 0);

		GtkWidget *empty_lbl = gtk_label_new("WiFi is disabled");
		gtk_style_context_add_class(gtk_widget_get_style_context(empty_lbl), "subtitle");
		gtk_box_pack_start(GTK_BOX(empty_box), empty_lbl, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(list_box), empty_box, FALSE, FALSE, 0);
	}

	gtk_widget_show_all(bw->cb_menu_main_box);
}
