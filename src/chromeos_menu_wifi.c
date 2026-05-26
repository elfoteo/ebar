#include "chromeos_menu_internal.h"
#include "wifi.h"

typedef struct {
	BarWindow *bw;
	AppState *state;
	char *ssid;
	char *security;
} WifiNetCtx;

typedef struct {
	BarWindow *bw;
	AppState *state;
	char *ssid;
	GtkWidget *password_entry;
} WifiConnectCtx;

typedef struct {
	GtkWidget *entry;
	GtkWidget *button;
} PasswordToggleCtx;

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
		static const char *secured_icons[] = {"󰤬", "󰤡", "󰤤", "󰤧", "󰤪"};
		return secured_icons[level];
	}

	static const char *open_icons[] = {"󰤯", "󰤟", "󰤢", "󰤥", "󰤨"};
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
	wifi_connect(ctx->ssid, password);
	chromeos_menu_show_wifi_networks(ctx->bw, ctx->state);
}

static void on_password_visibility_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	PasswordToggleCtx *ctx = (PasswordToggleCtx *)data;
	gboolean visible = gtk_entry_get_visibility(GTK_ENTRY(ctx->entry));
	gtk_entry_set_visibility(GTK_ENTRY(ctx->entry), !visible);
	gtk_button_set_label(GTK_BUTTON(ctx->button), visible ? "󰈈" : "󰈉");
}

static void show_wifi_password_entry(BarWindow *bw, AppState *state, const char *ssid) {
	chromeos_menu_clear(bw);

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

	GtkWidget *visibility_btn = gtk_button_new_with_label("󰈈");
	gtk_style_context_add_class(gtk_widget_get_style_context(visibility_btn), "cb-menu-icon-btn");
	gtk_widget_set_size_request(visibility_btn, 36, 36);
	PasswordToggleCtx *toggle_ctx = g_new0(PasswordToggleCtx, 1);
	toggle_ctx->entry = entry;
	toggle_ctx->button = visibility_btn;
	g_signal_connect_data(visibility_btn, "clicked", G_CALLBACK(on_password_visibility_clicked), toggle_ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(password_row), visibility_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(content), password_row, FALSE, FALSE, 0);

	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_halign(actions, GTK_ALIGN_END);

	GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
	gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "cb-menu-dialog-btn");
	MenuCtx *ctx2 = g_new0(MenuCtx, 1);
	ctx2->bw = bw;
	ctx2->state = state;
	g_signal_connect_data(cancel_btn, "clicked", G_CALLBACK(on_cancel_wifi_clicked), ctx2, (GClosureNotify)chromeos_menu_free_generic_ctx,
						  0);
	gtk_box_pack_start(GTK_BOX(actions), cancel_btn, FALSE, FALSE, 0);

	GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
	gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "cb-menu-dialog-btn");
	gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "cb-menu-dialog-btn-primary");
	WifiConnectCtx *conn_ctx = g_new0(WifiConnectCtx, 1);
	conn_ctx->bw = bw;
	conn_ctx->state = state;
	conn_ctx->ssid = g_strdup(ssid);
	conn_ctx->password_entry = entry;
	g_signal_connect_data(connect_btn, "clicked", G_CALLBACK(on_connect_clicked), conn_ctx, (GClosureNotify)free_wifi_connect_ctx, 0);
	gtk_box_pack_start(GTK_BOX(actions), connect_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(content), actions, FALSE, FALSE, 0);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

static void on_wifi_net_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	WifiNetCtx *ctx = (WifiNetCtx *)data;
	if (ctx->security && strlen(ctx->security) > 0 && strcmp(ctx->security, "--") != 0) {
		show_wifi_password_entry(ctx->bw, ctx->state, ctx->ssid);
	} else {
		wifi_connect(ctx->ssid, NULL);
	}
}

static gboolean chromeos_menu_show_wifi_networks_idle(BarWindow *bw) {
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
				g_idle_add((GSourceFunc)chromeos_menu_show_wifi_networks_idle, bw);
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

	GtkWidget *title = gtk_label_new("WiFi");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), sep, FALSE, FALSE, 8);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scroll, -1, 278);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), scroll, TRUE, TRUE, 0);

	GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_set_spacing(GTK_BOX(list_box), 2);
	gtk_container_add(GTK_CONTAINER(scroll), list_box);

	GPtrArray *networks = wifi_list_networks();
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
		chromeos_menu_ellipsize_label(name_lbl, 32);
		gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text_box), name_lbl, FALSE, FALSE, 0);

		const char *security_text = network->secured ? "Protected" : "Open";
		char subtitle[64];
		if (network->active)
			snprintf(subtitle, sizeof(subtitle), "Connected - %s", security_text);
		else
			snprintf(subtitle, sizeof(subtitle), "%s", security_text);

		GtkWidget *security_lbl = gtk_label_new(subtitle);
		gtk_style_context_add_class(gtk_widget_get_style_context(security_lbl), "subtitle");
		gtk_widget_set_halign(security_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text_box), security_lbl, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(btn_box), text_box, TRUE, TRUE, 0);

		if (network->active) {
			GtkWidget *check = gtk_label_new("󰄬");
			gtk_style_context_add_class(gtk_widget_get_style_context(check), "icon");
			gtk_box_pack_end(GTK_BOX(btn_box), check, FALSE, FALSE, 0);
		}

		gtk_container_add(GTK_CONTAINER(btn), btn_box);

		WifiNetCtx *net_ctx = g_new0(WifiNetCtx, 1);
		net_ctx->bw = bw;
		net_ctx->state = state;
		net_ctx->ssid = g_strdup(network->ssid);
		net_ctx->security = g_strdup(network->security);

		g_signal_connect_data(btn, "clicked", G_CALLBACK(on_wifi_net_clicked), net_ctx, (GClosureNotify)free_wifi_net_ctx, 0);
		gtk_box_pack_start(GTK_BOX(list_box), btn, FALSE, FALSE, 0);
	}
	g_ptr_array_unref(networks);

	gtk_widget_show_all(bw->cb_menu_main_box);
}
