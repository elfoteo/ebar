#include "chromeos_launcher.h"
#include "chromeos_menu.h"
#include "gtk-layer-shell.h"
#include "ipc.h"
#include <gio/gio.h>
#include <string.h>

#define LAUNCHER_WIDTH 540
#define LAUNCHER_HEIGHT 580

typedef struct {
	GAppInfo *app_info;
	GtkWidget *button;
} AppWidget;

#include <gdk/gdkkeysyms.h>
#include <time.h>

static long long get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long launcher_opened_time = 0;
static long long last_launcher_destroy_time = 0;

static gboolean on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
	(void)event;
	(void)data;
	if (get_time_ms() - launcher_opened_time < 300)
		return TRUE;
	gtk_widget_destroy(widget);
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	(void)data;
	if (event->keyval == GDK_KEY_Escape) {
		gtk_widget_destroy(widget);
		return TRUE;
	}
	return FALSE;
}

static void on_app_clicked(GtkButton *btn, gpointer data) {
	(void)btn;
	GAppInfo *info = G_APP_INFO(data);
	g_app_info_launch(info, NULL, NULL, NULL);

	GtkWidget *win = gtk_widget_get_toplevel(GTK_WIDGET(btn));
	if (GTK_IS_WINDOW(win))
		gtk_widget_destroy(win);
}

static GtkWidget *create_app_item(GAppInfo *info) {
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_size_request(vbox, 100, 120);

	GtkWidget *btn = gtk_button_new();
	gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "launcher-app-btn");

	GtkWidget *btn_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	GIcon *icon = g_app_info_get_icon(info);
	GtkWidget *img;
	if (icon) {
		img = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_DIALOG);
	} else {
		img = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
	}
	gtk_image_set_pixel_size(GTK_IMAGE(img), 40);

	GtkWidget *icon_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_size_request(icon_wrapper, 64, 64);
	gtk_widget_set_halign(icon_wrapper, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon_wrapper, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_wrapper), "launcher-app-icon");
	gtk_box_pack_start(GTK_BOX(icon_wrapper), img, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(btn_vbox), icon_wrapper, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(btn), btn_vbox);
	g_signal_connect(btn, "clicked", G_CALLBACK(on_app_clicked), info);

	const char *name = g_app_info_get_name(info);
	GtkWidget *lbl = gtk_label_new(name);
	gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(lbl), 11);
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "launcher-app-label");
	gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_top(lbl, 8);

	gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);

	g_object_set_data_full(G_OBJECT(btn), "app-name", g_strdup(name), g_free);

	return vbox;
}

static gboolean filter_func(GtkFlowBoxChild *child, gpointer user_data) {
	const char *search_text = (const char *)user_data;
	if (!search_text || !*search_text)
		return TRUE;

	GtkWidget *vbox = gtk_bin_get_child(GTK_BIN(child));
	GtkWidget *btn = gtk_container_get_children(GTK_CONTAINER(vbox))->data;
	const char *app_name = g_object_get_data(G_OBJECT(btn), "app-name");

	if (!app_name)
		return TRUE;

	return g_str_match_string(search_text, app_name, TRUE);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
	GtkFlowBox *flowbox = GTK_FLOW_BOX(data);
	const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
	gtk_flow_box_set_filter_func(flowbox, filter_func, (gpointer)text, NULL);
}

static void apply_launcher_css(AppState *state) {
	char css[4096];
	snprintf(css, sizeof(css),
			 ".launcher-window { background: none; } "
			 "#launcher-bg { "
			 "  background-color: #2b2b2b; "
			 "  border-radius: 24px; "
			 "  border: 1px solid rgba(255,255,255,0.1); "
			 "  padding: 16px; "
			 "} "
			 ".launcher-search { "
			 "  background-color: #3c3c3c; "
			 "  border-radius: 24px; "
			 "  padding: 8px 16px; "
			 "  margin-bottom: 24px; "
			 "  color: white; "
			 "  font-size: 16px; "
			 "  border: 1px solid transparent; "
			 "} "
			 ".launcher-search:focus-within { "
			 "  border: 1px solid %s; "
			 "} "
			 ".launcher-search entry { "
			 "  background: none; "
			 "  border: none; "
			 "  box-shadow: none; "
			 "  color: white; "
			 "} "
			 ".launcher-app-btn { "
			 "  background: none; "
			 "  border: none; "
			 "  outline: none; "
			 "  box-shadow: none; "
			 "  padding: 0; "
			 "  margin: 0; "
			 "} "
			 ".launcher-app-icon { "
			 "  background-color: white; "
			 "  border-radius: 32px; "
			 "  padding: 12px; "
			 "  transition: transform 200ms ease; "
			 "} "
			 ".launcher-app-btn:hover .launcher-app-icon { "
			 "  transform: scale(1.1); "
			 "} "
			 ".launcher-app-label { "
			 "  color: #bfbac6; "
			 "  font-size: 12px; "
			 "  font-weight: 500; "
			 "} "
			 ".launcher-scroll { "
			 "  background: none; "
			 "  border: none; "
			 "} ",
			 state->config.chromeos.accent_color);

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
											  GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(provider);
}

static void on_launcher_destroy(GtkWidget *widget, gpointer data) {
	(void)widget;
	BarWindow *bw = (BarWindow *)data;
	bw->launcher_window = NULL;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	last_launcher_destroy_time = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

	if (bw->cb_launcher_btn) {
		gtk_style_context_remove_class(gtk_widget_get_style_context(bw->cb_launcher_btn), "active");
	}
}

static GtkWidget *create_launcher_window(BarWindow *bw, AppState *state) {
	apply_launcher_css(state);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "ebar-launcher");

	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual && gdk_screen_is_composited(screen))
		gtk_widget_set_visual(win, visual);

	gtk_widget_set_name(win, "launcher-window");
	gtk_style_context_add_class(gtk_widget_get_style_context(win), "launcher-window");
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

	gtk_layer_init_for_window(GTK_WINDOW(win));
	gtk_layer_set_monitor(GTK_WINDOW(win), bw->monitor);
	gtk_layer_set_namespace(GTK_WINDOW(win), "ebar-launcher");

	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);

	const int SPACING = 6;
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, SPACING);
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, SPACING);

	gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

	g_signal_connect(win, "focus-out-event", G_CALLBACK(on_focus_out), NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);
	g_signal_connect(win, "destroy", G_CALLBACK(on_launcher_destroy), bw);

	/* Hyprland bug bypass: force RGBX to prevent unintended transparency/blur issues */
	char *res = hyprctl_request("keyword windowrule forcergbx,title:^(ebar-launcher)$");
	if (res) free(res);
	res = hyprctl_request("keyword layerrule forcergbx,ebar-launcher");
	if (res) free(res);

	GtkWidget *bg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(bg, "launcher-bg");
	gtk_widget_set_size_request(bg, LAUNCHER_WIDTH, LAUNCHER_HEIGHT);
	gtk_container_add(GTK_CONTAINER(win), bg);

	GtkWidget *search = gtk_search_entry_new();
	gtk_widget_set_name(search, "launcher-search");
	gtk_style_context_add_class(gtk_widget_get_style_context(search), "launcher-search");
	gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Search your images, files, apps and more...");
	gtk_box_pack_start(GTK_BOX(bg), search, FALSE, FALSE, 0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "launcher-scroll");
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(bg), scroll, TRUE, TRUE, 0);

	GtkWidget *flowbox = gtk_flow_box_new();
	gtk_widget_set_valign(flowbox, GTK_ALIGN_START);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowbox), 5);
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flowbox), 5);
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowbox), GTK_SELECTION_NONE);
	gtk_container_add(GTK_CONTAINER(scroll), flowbox);

	g_signal_connect(search, "search-changed", G_CALLBACK(on_search_changed), flowbox);

	GList *apps = state->app_list;
	for (GList *l = apps; l != NULL; l = l->next) {
		GAppInfo *info = G_APP_INFO(l->data);
		if (g_app_info_should_show(info)) {
			GtkWidget *item = create_app_item(info);
			gtk_container_add(GTK_CONTAINER(flowbox), item);
		}
	}

	launcher_opened_time = get_time_ms();
	gtk_widget_show_all(win);
	gtk_widget_grab_focus(search);
	return win;
}

void toggle_chromeos_launcher(BarWindow *bw, AppState *state) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

	if (bw->launcher_window) {
		last_launcher_destroy_time = now;
		gtk_widget_destroy(bw->launcher_window);
		return;
	}

	if (now - last_launcher_destroy_time < 500)
		return;

	close_all_chromeos_menus(state);

	if (bw->cb_launcher_btn) {
		gtk_style_context_add_class(gtk_widget_get_style_context(bw->cb_launcher_btn), "active");
	}

	bw->launcher_window = create_launcher_window(bw, state);
}

void close_all_chromeos_launchers(AppState *state) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->launcher_window) {
			last_launcher_destroy_time = now;
			gtk_widget_destroy(bw->launcher_window);
		}
	}
}
