#include "chromeos_menu.h"
#include "chromeos_launcher.h"
#include "chromeos_menu_internal.h"
#include "gtk-layer-shell.h"
#include "icons.h"
#include "ipc.h"
#include "util.h"
#include <gdk/gdkkeysyms.h>

static long long last_destroy_time = 0;
static long long last_menu_opened_time = 0;
static GtkWidget *last_sys_btn = NULL;

void chromeos_menu_free_generic_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	g_free(data);
}

void chromeos_menu_clear(BarWindow *bw) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(bw->cb_menu_main_box));
	for (GList *l = children; l != NULL; l = l->next)
		gtk_widget_destroy(GTK_WIDGET(l->data));
	g_list_free(children);
	bw->cb_menu_volume_slider = NULL;
	bw->cb_menu_brightness_slider = NULL;
}

GtkWidget *chromeos_menu_create_header_back_button(void) {
	GtkWidget *back_btn = gtk_button_new_with_label(ICON_ARROW_LEFT);
	gtk_style_context_add_class(gtk_widget_get_style_context(back_btn), "cb-menu-header-btn");
	gtk_widget_set_size_request(back_btn, 36, 36);
	gtk_widget_set_halign(back_btn, GTK_ALIGN_START);
	gtk_widget_set_valign(back_btn, GTK_ALIGN_CENTER);
	return back_btn;
}

GtkWidget *chromeos_menu_create_subpage_header(BarWindow *bw, AppState *state, const char *title) {
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(chromeos_menu_on_back_to_main_clicked), ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title_lbl = gtk_label_new(title);
	gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	return header;
}

void chromeos_menu_ellipsize_label(GtkWidget *label, int max_width_chars) {
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
	gtk_label_set_width_chars(GTK_LABEL(label), 1);
	gtk_label_set_max_width_chars(GTK_LABEL(label), max_width_chars);
}

/* GtkBox doesn't get GTK :hover prelight, so track it manually off the
 * inner buttons and reflect it as a class on the whole pill. */
static gboolean pill_hover_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
	(void)widget;
	(void)event;
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data)), "cb-hover");
	return FALSE;
}

static gboolean pill_hover_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
	(void)widget;
	(void)event;
	gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(data)), "cb-hover");
	return FALSE;
}

static void pill_track_hover(GtkWidget *button, GtkWidget *pill) {
	g_signal_connect(button, "enter-notify-event", G_CALLBACK(pill_hover_enter), pill);
	g_signal_connect(button, "leave-notify-event", G_CALLBACK(pill_hover_leave), pill);
}

GtkWidget *chromeos_menu_create_pill(const char *icon, const char *title, const char *subtitle, gboolean active, GtkWidget **subtitle_out,
									 GtkWidget **icon_out, GtkWidget **arrow_out, GCallback on_click, GCallback on_arrow_click,
									 gpointer user_data) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-pill");
	if (active)
		gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-pill-active");

	GtkWidget *main_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(main_btn), "cb-menu-pill-main");
	if (on_click)
		g_signal_connect(main_btn, "clicked", on_click, user_data);
	pill_track_hover(main_btn, box);

	GtkWidget *inner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *icon_lbl = gtk_label_new(icon);
	if (icon_out)
		*icon_out = icon_lbl;
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "icon");
	gtk_box_pack_start(GTK_BOX(inner_box), icon_lbl, FALSE, FALSE, 0);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

	GtkWidget *title_lbl = gtk_label_new(title);
	chromeos_menu_ellipsize_label(title_lbl, 11);
	gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

	if (subtitle || subtitle_out) {
		GtkWidget *sub_lbl = gtk_label_new(subtitle ? subtitle : "");
		gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl), "subtitle");
		chromeos_menu_ellipsize_label(sub_lbl, 12);
		gtk_widget_set_halign(sub_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(vbox), sub_lbl, FALSE, FALSE, 0);
		if (subtitle_out)
			*subtitle_out = sub_lbl;
	}

	gtk_box_pack_start(GTK_BOX(inner_box), vbox, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(main_btn), inner_box);
	gtk_box_pack_start(GTK_BOX(box), main_btn, TRUE, TRUE, 0);

	if (on_arrow_click) {
		GtkWidget *arrow_btn = gtk_button_new_with_label(ICON_CHEVRON_RIGHT);
		if (arrow_out)
			*arrow_out = arrow_btn;
		gtk_style_context_add_class(gtk_widget_get_style_context(arrow_btn), "cb-menu-pill-arrow");
		g_signal_connect(arrow_btn, "clicked", on_arrow_click, user_data);
		pill_track_hover(arrow_btn, box);
		gtk_box_pack_end(GTK_BOX(box), arrow_btn, FALSE, FALSE, 0);
	} else {
		if (arrow_out)
			*arrow_out = NULL;
		GtkWidget *arrow = gtk_label_new(ICON_CHEVRON_RIGHT);
		gtk_style_context_add_class(gtk_widget_get_style_context(arrow), "subtitle");
		gtk_widget_set_margin_end(arrow, 12);
		gtk_box_pack_end(GTK_BOX(box), arrow, FALSE, FALSE, 0);
	}

	return box;
}

static gboolean pointer_is_over_widget(GtkWidget *widget) {
	if (!widget || !gtk_widget_get_realized(widget))
		return FALSE;

	GdkWindow *window = gtk_widget_get_window(widget);
	if (!window)
		return FALSE;

	GdkDisplay *display = gdk_display_get_default();
	GdkSeat *seat = display ? gdk_display_get_default_seat(display) : NULL;
	GdkDevice *pointer = seat ? gdk_seat_get_pointer(seat) : NULL;
	if (!pointer)
		return FALSE;

	gint pointer_x = 0;
	gint pointer_y = 0;
	gdk_device_get_position(pointer, NULL, &pointer_x, &pointer_y);

	gint widget_x = 0;
	gint widget_y = 0;
	gdk_window_get_origin(window, &widget_x, &widget_y);

	gint width = gtk_widget_get_allocated_width(widget);
	gint height = gtk_widget_get_allocated_height(widget);
	return pointer_x >= widget_x && pointer_x < widget_x + width && pointer_y >= widget_y && pointer_y < widget_y + height;
}

static gboolean on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
	(void)event;
	BarWindow *bw = (BarWindow *)data;
	if (get_time_ms() - last_menu_opened_time < 500)
		return TRUE;

	if (pointer_is_over_widget(widget) || (bw && pointer_is_over_widget(bw->window)))
		return TRUE;

	last_destroy_time = get_time_ms();
	gtk_widget_destroy(widget);
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	(void)data;
	if (event->keyval == GDK_KEY_Escape) {
		last_destroy_time = get_time_ms();
		gtk_widget_destroy(widget);
		return TRUE;
	}
	return FALSE;
}

void chromeos_menu_apply_css(AppState *state) {
	static char *cached_css = NULL;
	static char cached_accent[32] = "";

	if (!cached_css || strcmp(cached_accent, state->config.chromeos.accent_color) != 0) {
		g_free(cached_css);
		g_strlcpy(cached_accent, state->config.chromeos.accent_color, sizeof(cached_accent));

		char css[16384];
		char accent_light[32];
		int n = 0;

		lighten_hex_color(state->config.chromeos.accent_color, 0.15f, accent_light, sizeof(accent_light));

#define A(...) n += snprintf(css + n, (int)sizeof(css) - n, __VA_ARGS__)

		A("* { text-shadow: none; box-shadow: none; } ");
		A(".ebar-menu-window { "
		  "  background-color: transparent; "
		  "  background: none; "
		  "  box-shadow: none; "
		  "  border: none; "
		  "  outline: none; "
		  "} ");
		A("#menu-bg { "
		  "  background-color: #2b2b2b; "
		  "  border-radius: 24px; "
		  "  border: 1px solid rgba(255,255,255,0.1); "
		  "} ");
		A(".cb-menu { "
		  "  padding: 16px; "
		  "} ");
		A(".cb-menu-view { "
		  "  min-width: 388px; "
		  "  min-height: 348px; "
		  "} ");
		A(".cb-menu-pill { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 16px; "
		  "  min-height: 52px; "
		  "} ");
		A(".cb-menu-pill label, .cb-menu-pill .icon { "
		  "  color: #e8eaed; "
		  "  text-shadow: none; "
		  "} ");
		A(".cb-menu-pill.cb-hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-pill-active { "
		  "  background-color: %s; "
		  "} ",
		  state->config.chromeos.accent_color);
		A(".cb-menu-pill-active label, .cb-menu-pill-active .icon { "
		  "  color: #202124; "
		  "  text-shadow: none; "
		  "} ");
		A(".cb-menu-pill-active.cb-hover { "
		  "  background-color: %s; "
		  "} ",
		  accent_light);
		A(".cb-menu-pill-main { "
		  "  padding: 12px; "
		  "  border-radius: 16px 0 0 16px; "
		  "  background: none; "
		  "} ");
		A(".cb-menu-pill-arrow { "
		  "  padding: 12px; "
		  "  border-radius: 0 16px 16px 0; "
		  "  background: none; "
		  "  border-left: 1px solid rgba(255,255,255,0.1); "
		  "} ");
		A(".cb-menu-pill-arrow:hover { "
		  "  background-color: rgba(255,255,255,0.08); "
		  "} ");
		A(".cb-menu-pill-active .cb-menu-pill-arrow:hover { "
		  "  background-color: rgba(0,0,0,0.15); "
		  "} ");
		A(".cb-menu-pill label { "
		  "  font-weight: 600; "
		  "} ");
		A(".cb-menu-pill .icon { "
		  "  font-size: 20px; "
		  "  margin-right: 12px; "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "} ");
		A(".cb-menu-pill .subtitle { "
		  "  font-size: 11px; "
		  "  opacity: 0.8; "
		  "} ");
		A(".cb-menu-grid { "
		  "  margin-bottom: 12px; "
		  "} ");
		A(".cb-menu-slider-box { "
		  "  margin: 3px 0; "
		  "} ");
		A(".cb-menu-slider-icon { "
		  "  font-size: 18px; "
		  "  color: #202124; "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  margin-left: 22px; "
		  "} ");
		A(".cb-menu-slider trough { "
		  "  background-color: transparent; "
		  "  background-image: linear-gradient(#3c3c3c, #3c3c3c); "
		  "  background-size: 100%% 4px; "
		  "  background-repeat: no-repeat; "
		  "  background-position: center; "
		  "  border-radius: 18px; "
		  "  min-height: 36px; "
		  "} ");
		A(".cb-menu-slider:hover trough { "
		  "  background-image: linear-gradient(#4c4c4c, #4c4c4c); "
		  "} ");
		A(".cb-menu-slider highlight { "
		  "  background-color: %s; "
		  "  border-radius: 18px; "
		  "  min-height: 36px; "
		  "} ",
		  state->config.chromeos.accent_color);
		A(".cb-menu-slider-minimum highlight { "
		  "  background-color: #5f6368; "
		  "} ");
		A(".cb-menu-slider-muted highlight { "
		  "  background-color: #5f6368; "
		  "} ");
		A(".cb-menu-slider-icon-minimum { "
		  "  color: #ffffff; "
		  "} ");
		A(".cb-menu-slider slider { "
		  "  all: unset; "
		  "} ");
		A(".cb-menu-led-toggle { "
		  "  min-width: 48px; "
		  "  min-height: 26px; "
		  "} ");
		A("switch.cb-menu-led-toggle { "
		  "  background-color: #3c3c3c; "
		  "  border-radius: 13px; "
		  "  min-width: 48px; "
		  "  min-height: 26px; "
		  "  padding: 2px; "
		  "} ");
		A("switch.cb-menu-led-toggle:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A("switch.cb-menu-led-toggle:checked { "
		  "  background-color: %s; "
		  "} ",
		  state->config.chromeos.accent_color);
		A("switch.cb-menu-led-toggle slider { "
		  "  background-color: #e8eaed; "
		  "  border-radius: 11px; "
		  "  min-width: 22px; "
		  "  min-height: 22px; "
		  "} ");
		A(".cb-menu-led-channels { "
		  "  font-size: 11px; "
		  "  opacity: 0.8; "
		  "} ");
		A("scrollbar, scrollbar slider { "
		  "  background-color: transparent; "
		  "  border: none; "
		  "} ");
		A("scrollbar slider { "
		  "  background-color: rgba(255,255,255,0.3); "
		  "  border-radius: 9999px; "
		  "  min-width: 6px; "
		  "  min-height: 24px; "
		  "} ");
		A("scrollbar slider:hover { "
		  "  background-color: rgba(255,255,255,0.5); "
		  "} ");
		A(".cb-menu-bottom { "
		  "  margin-top: 16px; "
		  "} ");
		A(".cb-menu-power { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 20px; "
		  "  padding: 8px 16px; "
		  "} ");
		A(".cb-menu-power:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-power label { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  font-size: 24px; "
		  "} ");
		A("popover.background { "
		  "  background-color: #3c3c3c; "
		  "  border-radius: 16px; "
		  "} ");
		A("popover.background separator { "
		  "  background-color: rgba(255,255,255,0.12); "
		  "} ");
		A(".cb-menu-settings { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 9999px; "
		  "  min-width: 44px; "
		  "  min-height: 44px; "
		  "  padding: 0; "
		  "} ");
		A(".cb-menu-settings:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-settings label { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  font-size: 22px; "
		  "  margin-left: -4px; "
		  "} ");
		A(".cb-menu-battery { "
		  "  color: #e8eaed; "
		  "  font-size: 13px; "
		  "} ");
		A(".cb-menu-slider-arrow { "
		  "  background: none; "
		  "  color: #e8eaed; "
		  "  border-radius: 9999px; "
		  "  font-size: 16px; "
		  "  margin-left: 6px; "
		  "  min-width: 36px; "
		  "  min-height: 36px; "
		  "  padding: 0; "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "} ");
		A(".cb-menu-slider-arrow:hover { "
		  "  background-color: rgba(255,255,255,0.08); "
		  "} ");
		A(".cb-menu-slider-arrow label { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  font-size: 16px; "
		  "  margin-right: 6px; "
		  "} ");
		A(".cb-menu-slider-btn { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 18px; "
		  "  min-width: 36px; "
		  "  min-height: 36px; "
		  "  padding: 0; "
		  "  font-size: 18px; "
		  "  margin-left: 8px; "
		  "} ");
		A(".cb-menu-slider-btn:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-slider-btn-active { "
		  "  background-color: %s; "
		  "  color: #202124; "
		  "} ",
		  state->config.chromeos.accent_color);
		A(".cb-menu-slider-btn-active:hover { "
		  "  background-color: %s; "
		  "} ",
		  accent_light);
		A(".cb-menu-slider-btn label { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "} ");
		A(".cb-menu-wifi-list-btn { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  padding: 6px 18px 6px 10px; "
		  "  border-radius: 9999px; "
		  "  margin: 4px 0; "
		  "  font-size: 14px; "
		  "  min-height: 40px; "
		  "} ");
		A(".cb-menu-wifi-list-btn:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-wifi-list-btn label { "
		  "  color: #e8eaed; "
		  "} ");
		A(".cb-menu-wifi-list-btn .icon { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  font-size: 18px; "
		  "} ");
		A(".cb-menu-wifi-list-btn .subtitle { "
		  "  font-size: 11px; "
		  "  opacity: 0.72; "
		  "} ");
		A(".cb-menu-header { "
		  "  margin-bottom: 12px; "
		  "  min-height: 36px; "
		  "} ");
		A(".cb-menu-header-btn { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 18px; "
		  "  min-width: 36px; "
		  "  min-height: 36px; "
		  "} ");
		A(".cb-menu-header-btn:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-header-btn label { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  font-size: 18px; "
		  "} ");
		A(".cb-menu-header-title { "
		  "  color: #e8eaed; "
		  "  font-weight: 600; "
		  "  font-size: 16px; "
		  "} ");
		A(".cb-menu-section-label { "
		  "  color: #e8eaed; "
		  "  font-size: 13px; "
		  "  font-weight: 600; "
		  "  margin-top: 4px; "
		  "  margin-bottom: 2px; "
		  "} ");
		A(".cb-menu-entry { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 18px; "
		  "  padding: 8px 12px; "
		  "  border: 1px solid rgba(255,255,255,0.1); "
		  "  min-height: 36px; "
		  "} ");
		A(".cb-menu-password-row { "
		  "  margin: 8px 0 14px 0; "
		  "} ");
		A(".cb-menu-icon-btn { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 18px; "
		  "  min-width: 36px; "
		  "  min-height: 36px; "
		  "  margin-left: 8px; "
		  "} ");
		A(".cb-menu-icon-btn:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-icon-btn label { "
		  "  font-family: \"JetBrainsMonoNerdFont\"; "
		  "  font-size: 18px; "
		  "} ");
		A(".cb-menu-dialog-btn { "
		  "  background-color: #3c3c3c; "
		  "  color: #e8eaed; "
		  "  border-radius: 18px; "
		  "  padding: 0 18px; "
		  "  min-width: 96px; "
		  "  min-height: 36px; "
		  "  font-weight: 600; "
		  "} ");
		A(".cb-menu-dialog-btn:hover { "
		  "  background-color: #4c4c4c; "
		  "} ");
		A(".cb-menu-dialog-btn-primary { "
		  "  background-color: %s; "
		  "  color: #202124; "
		  "} ",
		  state->config.chromeos.accent_color);
		A(".cb-menu-dialog-btn-primary:hover { "
		  "  background-color: %s; "
		  "} ",
		  accent_light);
		A(".cb-menu-wifi-ssid { "
		  "  color: #e8eaed; "
		  "  font-size: 15px; "
		  "  font-weight: 600; "
		  "  margin-bottom: 8px; "
		  "} ");

#undef A
		cached_css = g_strdup(css);
	}

	apply_css_from_string(cached_css, GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static GtkWidget *create_chromeos_menu(BarWindow *bw, AppState *state) {
	chromeos_menu_apply_css(state);
	chromeos_menu_refresh_bluetooth_state(state);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "ebar-menu");
	gtk_widget_set_name(win, "ebar-menu-window");
	gtk_style_context_add_class(gtk_widget_get_style_context(win), "ebar-menu-window");

	setup_transparent_window(win);
	g_signal_connect(win, "focus-out-event", G_CALLBACK(on_focus_out), bw);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);

	gtk_layer_init_for_window(GTK_WINDOW(win));
	gtk_layer_set_monitor(GTK_WINDOW(win), bw->monitor);
	gtk_layer_set_namespace(GTK_WINDOW(win), "ebar-menu");

	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

	const int SPACING_FROM_BAR = 6;
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, 48 + SPACING_FROM_BAR);
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, SPACING_FROM_BAR);

	gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
	gtk_layer_set_exclusive_zone(GTK_WINDOW(win), -1);

	/* Hyprland bug bypass: force RGBX to prevent unintended transparency/blur issues */
	apply_forcergbx_bypass("ebar-menu", "ebar-menu");

	gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(win), MENU_WIDTH, MENU_HEIGHT);
	gtk_widget_set_size_request(win, MENU_WIDTH, MENU_HEIGHT);
	GdkGeometry geometry = {
		.min_width = MENU_WIDTH,
		.min_height = MENU_HEIGHT,
		.max_width = MENU_WIDTH,
		.max_height = MENU_HEIGHT,
	};
	gtk_window_set_geometry_hints(GTK_WINDOW(win), win, &geometry, GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);

	GtkWidget *bg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(bg, "menu-bg");
	gtk_widget_set_size_request(bg, MENU_WIDTH, MENU_HEIGHT);
	gtk_widget_set_halign(bg, GTK_ALIGN_END);
	gtk_widget_set_valign(bg, GTK_ALIGN_END);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "cb-menu");
	gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "cb-menu-view");
	gtk_widget_set_size_request(main_box, MENU_CONTENT_WIDTH, MENU_CONTENT_HEIGHT);
	gtk_widget_set_halign(main_box, GTK_ALIGN_END);
	gtk_widget_set_valign(main_box, GTK_ALIGN_END);
	bw->cb_menu_main_box = main_box;

	chromeos_menu_show_main(bw, state);

	gtk_container_add(GTK_CONTAINER(bg), main_box);
	gtk_container_add(GTK_CONTAINER(win), bg);

	return win;
}

static void on_menu_destroy(GtkWidget *widget, gpointer data) {
	(void)widget;
	BarWindow *bw = (BarWindow *)data;
	bw->menu_window = NULL;

	last_destroy_time = get_time_ms();

	if (bw->cb_sys_label) {
		GtkWidget *btn = gtk_widget_get_parent(bw->cb_sys_label);
		if (GTK_IS_BUTTON(btn)) {
			gtk_style_context_remove_class(gtk_widget_get_style_context(btn), "active");
		}
	}

	bw->cb_menu_kb_label = NULL;
	bw->cb_menu_bat_label = NULL;
	bw->cb_menu_wifi_pill = NULL;
	bw->cb_menu_wifi_subtitle = NULL;
	bw->cb_menu_main_box = NULL;
	bw->cb_menu_brightness_slider = NULL;
	bw->cb_menu_volume_slider = NULL;
}

void toggle_chromeos_menu(BarWindow *bw, AppState *state) {
	long long now = get_time_ms();
	GtkWidget *btn = bw->cb_sys_label ? gtk_widget_get_parent(bw->cb_sys_label) : NULL;

	if (bw->menu_window) {
		last_destroy_time = get_time_ms();
		gtk_widget_destroy(bw->menu_window);
		return;
	}

	if (now - last_destroy_time < 500 && last_sys_btn == btn)
		return;

	close_all_chromeos_launchers(state);
	if (bw->popup_window) {
		gtk_widget_hide(bw->popup_window);
		for (int i = 0; i < POPUP_COUNT; i++) {
			if (bw->popup_timer[i]) {
				g_source_remove(bw->popup_timer[i]);
				bw->popup_timer[i] = 0;
			}
		}
	}
	bw->menu_window = create_chromeos_menu(bw, state);
	g_signal_connect(bw->menu_window, "destroy", G_CALLBACK(on_menu_destroy), bw);

	if (btn && GTK_IS_BUTTON(btn)) {
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "active");
	}

	last_menu_opened_time = get_time_ms();
	last_sys_btn = btn;
	gtk_widget_show_all(bw->menu_window);
	gtk_widget_grab_focus(bw->menu_window);
}

void close_all_chromeos_menus(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->menu_window) {
			last_destroy_time = get_time_ms();
			gtk_widget_destroy(bw->menu_window);
			bw->menu_window = NULL;
		}
	}
	pthread_mutex_unlock(&state->mutex);
}

gboolean chromeos_any_menu_open(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->menu_window) {
			pthread_mutex_unlock(&state->mutex);
			return TRUE;
		}
	}
	pthread_mutex_unlock(&state->mutex);
	return FALSE;
}

static void on_sys_btn_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct {
		BarWindow *bw;
		AppState *state;
	} *ctx = data;
	toggle_chromeos_menu(ctx->bw, ctx->state);
}

void setup_chromeos_menu_toggle(BarWindow *bw, AppState *state) {
	if (!bw->cb_sys_label)
		return;

	GtkWidget *btn = gtk_widget_get_parent(bw->cb_sys_label);
	if (GTK_IS_BUTTON(btn)) {
		typedef struct {
			BarWindow *bw;
			AppState *state;
		} CallbackCtx;
		CallbackCtx *ctx = g_new0(CallbackCtx, 1);
		ctx->bw = bw;
		ctx->state = state;

		g_signal_connect_data(btn, "clicked", G_CALLBACK(on_sys_btn_clicked), ctx, chromeos_menu_free_generic_ctx, 0);
	}
}

/* ── slider helpers ────────────────────────────────────────────────────────── */
void update_slider_minimum_state(GtkRange *range) {
	GtkStyleContext *scale_ctx = gtk_widget_get_style_context(GTK_WIDGET(range));
	GtkWidget *icon = g_object_get_data(G_OBJECT(range), "slider-icon");
	GtkStyleContext *icon_ctx = icon ? gtk_widget_get_style_context(icon) : NULL;
	gboolean muted = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "muted"));
	gboolean minimum = (slider_get_actual_value(range) <= 0.5) || muted;

	if (minimum) {
		gtk_style_context_add_class(scale_ctx, "cb-menu-slider-minimum");
		if (icon_ctx)
			gtk_style_context_add_class(icon_ctx, "cb-menu-slider-icon-minimum");
	} else {
		gtk_style_context_remove_class(scale_ctx, "cb-menu-slider-minimum");
		if (icon_ctx)
			gtk_style_context_remove_class(icon_ctx, "cb-menu-slider-icon-minimum");
	}
}

static void on_slider_value_changed(GtkRange *range, gpointer data) {
	(void)data;
	double visual_min = slider_get_visual_min(range);

	if (!slider_is_updating(range) && gtk_range_get_value(range) < visual_min) {
		slider_set_updating(range, TRUE);
		gtk_range_set_value(range, visual_min);
		slider_set_updating(range, FALSE);
	}

	update_slider_minimum_state(range);
}

/* Keep the zero-position fill exactly SLIDER_VISUAL_MIN_PX wide so the
 * highlight renders as a perfect circle, recomputed from the real width. */
static void on_menu_slider_recalc_cancel(GtkWidget *widget, gpointer data) {
	(void)data;
	guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "slider-recalc-src"));
	if (id) {
		g_source_remove(id);
		g_object_set_data(G_OBJECT(widget), "slider-recalc-src", NULL);
	}
}

/* Runs outside the size-allocate cycle: changing the value during allocation
 * leaves the trough/highlight unpainted until some other event forces a
 * redraw (e.g. hovering the slider). */
static gboolean on_menu_slider_recalc_idle(gpointer data) {
	GtkWidget *scale = GTK_WIDGET(data);
	GtkRange *range = GTK_RANGE(scale);
	g_object_set_data(G_OBJECT(scale), "slider-recalc-src", NULL);

	double width = gtk_widget_get_allocated_width(scale);
	if (width < SLIDER_VISUAL_MIN_PX)
		return G_SOURCE_REMOVE;
	double visual_min = (SLIDER_VISUAL_MIN_PX * 100.0) / width;
	if (visual_min > 99.0)
		visual_min = 99.0;
	double old_min = slider_get_visual_min(range);
	if ((int)(old_min * 100.0) == (int)(visual_min * 100.0))
		return G_SOURCE_REMOVE;

	double actual_val = slider_get_actual_value(range);
	slider_set_visual_min(range, visual_min);
	slider_set_updating(range, TRUE);
	gtk_range_set_value(range, slider_get_display_value(range, actual_val));
	slider_set_updating(range, FALSE);
	update_slider_minimum_state(range);
	return G_SOURCE_REMOVE;
}

static void on_menu_slider_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer data) {
	(void)allocation;
	(void)data;
	if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "slider-recalc-src")))
		return;
	guint id = g_idle_add(on_menu_slider_recalc_idle, widget);
	g_object_set_data(G_OBJECT(widget), "slider-recalc-src", GUINT_TO_POINTER(id));
}

/* Builds the icon-over-scale widget shared by every menu slider, wiring up
 * the visual-minimum (circle at zero) behaviour. Range must be 0..100. */
GtkWidget *create_menu_slider_overlay(const char *icon, double initial_val, GCallback on_changed, gpointer user_data,
									  GtkWidget **scale_out) {
	GtkWidget *overlay = gtk_overlay_new();

	GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	if (scale_out)
		*scale_out = scale;
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);

	/* Initial guess until the first size-allocate provides the real width */
	slider_set_visual_min(GTK_RANGE(scale), (SLIDER_VISUAL_MIN_PX * 100.0) / MENU_CONTENT_WIDTH);
	gtk_range_set_value(GTK_RANGE(scale), slider_get_display_value(GTK_RANGE(scale), initial_val));
	gtk_style_context_add_class(gtk_widget_get_style_context(scale), "cb-menu-slider");

	GtkWidget *icon_lbl = gtk_label_new(icon);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "cb-menu-slider-icon");
	gtk_widget_set_halign(icon_lbl, GTK_ALIGN_START);
	gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);
	g_object_set_data(G_OBJECT(scale), "slider-icon", icon_lbl);
	g_signal_connect(scale, "value-changed", G_CALLBACK(on_slider_value_changed), NULL);
	g_signal_connect(scale, "size-allocate", G_CALLBACK(on_menu_slider_size_allocate), NULL);
	g_signal_connect(scale, "destroy", G_CALLBACK(on_menu_slider_recalc_cancel), NULL);
	if (on_changed)
		g_signal_connect(scale, "value-changed", on_changed, user_data);
	update_slider_minimum_state(GTK_RANGE(scale));

	gtk_container_add(GTK_CONTAINER(overlay), scale);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), icon_lbl);

	return overlay;
}

GtkWidget *create_menu_slider(const char *icon, const char *right_icon, double initial_val, GCallback on_changed,
							  GCallback on_right_clicked, GCallback on_arrow_clicked, gpointer user_data, gboolean right_active,
							  GtkWidget **scale_out) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-slider-box");

	GtkWidget *overlay = create_menu_slider_overlay(icon, initial_val, on_changed, user_data, scale_out);

	gtk_box_pack_start(GTK_BOX(box), overlay, TRUE, TRUE, 0);

	if (right_icon) {
		GtkWidget *right_btn = gtk_button_new_with_label(right_icon);
		gtk_widget_set_valign(right_btn, GTK_ALIGN_CENTER);
		gtk_widget_set_halign(right_btn, GTK_ALIGN_CENTER);
		gtk_widget_set_size_request(right_btn, 36, 36);
		gtk_style_context_add_class(gtk_widget_get_style_context(right_btn), "cb-menu-slider-btn");
		if (right_active)
			gtk_style_context_add_class(gtk_widget_get_style_context(right_btn), "cb-menu-slider-btn-active");
		if (on_right_clicked)
			g_signal_connect(right_btn, "clicked", on_right_clicked, user_data);
		gtk_box_pack_start(GTK_BOX(box), right_btn, FALSE, FALSE, 0);
	}

	if (on_arrow_clicked) {
		GtkWidget *arrow_btn = gtk_button_new_with_label(ICON_CHEVRON_RIGHT);
		gtk_widget_set_valign(arrow_btn, GTK_ALIGN_CENTER);
		gtk_widget_set_size_request(arrow_btn, 36, 36);
		gtk_style_context_add_class(gtk_widget_get_style_context(arrow_btn), "cb-menu-slider-arrow");
		g_signal_connect(arrow_btn, "clicked", on_arrow_clicked, user_data);
		gtk_box_pack_start(GTK_BOX(box), arrow_btn, FALSE, FALSE, 0);
	}

	return box;
}

double slider_get_visual_min(GtkRange *range) {
	int basis_points = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "slider-visual-min-bp"));
	if (basis_points <= 0)
		return 0.0;
	return basis_points / 100.0;
}

void slider_set_visual_min(GtkRange *range, double visual_min) {
	if (visual_min < 0.0)
		visual_min = 0.0;
	if (visual_min > 99.0)
		visual_min = 99.0;
	g_object_set_data(G_OBJECT(range), "slider-visual-min-bp", GINT_TO_POINTER((int)(visual_min * 100.0)));
}

gboolean slider_is_updating(GtkRange *range) { return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "slider-updating")); }

void slider_set_updating(GtkRange *range, gboolean updating) {
	g_object_set_data(G_OBJECT(range), "slider-updating", GINT_TO_POINTER(updating));
}

double slider_get_actual_value(GtkRange *range) {
	double display_val = gtk_range_get_value(range);
	double visual_min = slider_get_visual_min(range);

	if (display_val <= visual_min)
		return 0.0;

	double actual_val = ((display_val - visual_min) * 100.0) / (100.0 - visual_min);
	if (actual_val > 100.0)
		return 100.0;
	return actual_val;
}

double slider_get_display_value(GtkRange *range, double actual_val) {
	double visual_min = slider_get_visual_min(range);

	if (actual_val <= 0.0)
		return visual_min;
	if (actual_val > 100.0)
		actual_val = 100.0;
	return visual_min + ((actual_val / 100.0) * (100.0 - visual_min));
}
