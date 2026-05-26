#include "chromeos_menu.h"
#include "chromeos_menu_internal.h"
#include "gtk-layer-shell.h"
#include <gdk/gdkkeysyms.h>
#include <time.h>

static long long last_destroy_time = 0;
static long long menu_opened_time = 0;

static long long get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void chromeos_menu_free_generic_ctx(gpointer data, GClosure *closure) {
	(void)closure;
	g_free(data);
}

void chromeos_menu_clear(BarWindow *bw) {
	GList *children = gtk_container_get_children(GTK_CONTAINER(bw->cb_menu_main_box));
	for (GList *l = children; l != NULL; l = l->next)
		gtk_widget_destroy(GTK_WIDGET(l->data));
	g_list_free(children);
}

GtkWidget *chromeos_menu_create_header_back_button(void) {
	GtkWidget *back_btn = gtk_button_new_with_label("󰁍");
	gtk_style_context_add_class(gtk_widget_get_style_context(back_btn), "cb-menu-header-btn");
	gtk_widget_set_size_request(back_btn, 36, 36);
	gtk_widget_set_halign(back_btn, GTK_ALIGN_START);
	gtk_widget_set_valign(back_btn, GTK_ALIGN_CENTER);
	return back_btn;
}

void chromeos_menu_ellipsize_label(GtkWidget *label, int max_width_chars) {
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
	gtk_label_set_width_chars(GTK_LABEL(label), 1);
	gtk_label_set_max_width_chars(GTK_LABEL(label), max_width_chars);
}

GtkWidget *chromeos_menu_create_pill(const char *icon, const char *title, const char *subtitle, gboolean active,
									 GtkWidget **subtitle_out, GtkWidget **icon_out, GtkWidget **arrow_out, GCallback on_click, GCallback on_arrow_click,
									 gpointer user_data) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-pill");
	if (active)
		gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-pill-active");

	GtkWidget *main_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(main_btn), "cb-menu-pill-main");
	if (on_click)
		g_signal_connect(main_btn, "clicked", on_click, user_data);

	GtkWidget *inner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *icon_lbl = gtk_label_new(icon);
	if (icon_out)
		*icon_out = icon_lbl;
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "icon");
	gtk_box_pack_start(GTK_BOX(inner_box), icon_lbl, FALSE, FALSE, 0);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

	GtkWidget *title_lbl = gtk_label_new(title);
	chromeos_menu_ellipsize_label(title_lbl, 16);
	gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

	if (subtitle || subtitle_out) {
		GtkWidget *sub_lbl = gtk_label_new(subtitle ? subtitle : "");
		gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl), "subtitle");
		chromeos_menu_ellipsize_label(sub_lbl, 18);
		gtk_widget_set_halign(sub_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(vbox), sub_lbl, FALSE, FALSE, 0);
		if (subtitle_out)
			*subtitle_out = sub_lbl;
	}

	gtk_box_pack_start(GTK_BOX(inner_box), vbox, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(main_btn), inner_box);
	gtk_box_pack_start(GTK_BOX(box), main_btn, TRUE, TRUE, 0);

	if (on_arrow_click) {
		GtkWidget *arrow_btn = gtk_button_new_with_label("");
		if (arrow_out)
			*arrow_out = arrow_btn;
		gtk_style_context_add_class(gtk_widget_get_style_context(arrow_btn), "cb-menu-pill-arrow");
		g_signal_connect(arrow_btn, "clicked", on_arrow_click, user_data);
		gtk_box_pack_end(GTK_BOX(box), arrow_btn, FALSE, FALSE, 0);
	} else {
		if (arrow_out)
			*arrow_out = NULL;
		GtkWidget *arrow = gtk_label_new("");
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
	if (get_time_ms() - menu_opened_time < 300)
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

void chromeos_menu_apply_css(void) {
	const char *css = "* { text-shadow: none; box-shadow: none; } "
					  ".ebar-menu-window { "
					  "  background-color: transparent; "
					  "  background: none; "
					  "  box-shadow: none; "
					  "  border: none; "
					  "  outline: none; "
					  "} "
					  "#menu-bg { "
					  "  background-color: #2b2b2b; "
					  "  border-radius: 24px; "
					  "  border: 1px solid rgba(255,255,255,0.1); "
					  "} "
					  ".cb-menu { "
					  "  padding: 16px; "
					  "} "
					  ".cb-menu-view { "
					  "  min-width: 388px; "
					  "  min-height: 348px; "
					  "} "
					  ".cb-menu-pill { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 16px; "
					  "  min-height: 52px; "
					  "} "
					  ".cb-menu-pill label, .cb-menu-pill .icon { "
					  "  color: #e8eaed; "
					  "  text-shadow: none; "
					  "} "
					  ".cb-menu-pill:hover { "
					  "  background-color: #4c4c4c; "
					  "} "
					  ".cb-menu-pill-active { "
					  "  background-color: #cbbef9; "
					  "} "
					  ".cb-menu-pill-active label, .cb-menu-pill-active .icon { "
					  "  color: #202124; "
					  "  text-shadow: none; "
					  "} "
					  ".cb-menu-pill-active:hover { "
					  "  background-color: #d8cefa; "
					  "} "
					  ".cb-menu-pill-main { "
					  "  padding: 12px; "
					  "  border-radius: 16px 0 0 16px; "
					  "  background: none; "
					  "} "
					  ".cb-menu-pill-arrow { "
					  "  padding: 12px; "
					  "  border-radius: 0 16px 16px 0; "
					  "  background: none; "
					  "  border-left: 1px solid rgba(255,255,255,0.1); "
					  "} "
					  ".cb-menu-pill label { "
					  "  font-weight: 600; "
					  "} "
					  ".cb-menu-pill .icon { "
					  "  font-size: 20px; "
					  "  margin-right: 12px; "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} "
					  ".cb-menu-pill .subtitle { "
					  "  font-size: 11px; "
					  "  opacity: 0.8; "
					  "} "
					  ".cb-menu-slider-box { "
					  "  margin: 6px 0; "
					  "} "
					  ".cb-menu-slider-icon { "
					  "  font-size: 18px; "
					  "  color: #202124; "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  margin-left: 22px; "
					  "} "
					  ".cb-menu-slider trough { "
					  "  background-color: transparent; "
					  "  background-image: linear-gradient(#3c3c3c, #3c3c3c); "
					  "  background-size: 100% 4px; "
					  "  background-repeat: no-repeat; "
					  "  background-position: center; "
					  "  border-radius: 18px; "
					  "  min-height: 36px; "
					  "} "
					  ".cb-menu-slider highlight { "
					  "  background-color: #cbbef9; "
					  "  border-radius: 18px; "
					  "  min-height: 36px; "
					  "  min-width: 36px; "
					  "} "
					  ".cb-menu-slider-minimum highlight { "
					  "  background-color: #5f6368; "
					  "} "
					  ".cb-menu-slider-icon-minimum { "
					  "  color: #ffffff; "
					  "} "
					  ".cb-menu-slider slider { "
					  "  all: unset; "
					  "} "
					  ".cb-menu-bottom { "
					  "  margin-top: 16px; "
					  "} "
					  ".cb-menu-power { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 20px; "
					  "  padding: 8px 16px; "
					  "} "
					  ".cb-menu-power label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 24px; "
					  "} "
					  ".cb-menu-settings { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 22px; "
					  "  min-width: 44px; "
					  "  min-height: 44px; "
					  "  padding: 0; "
					  "} "
					  ".cb-menu-settings label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 22px; "
					  "} "
					  ".cb-menu-battery { "
					  "  color: #e8eaed; "
					  "  font-size: 13px; "
					  "} "
					  ".cb-menu-slider-arrow { "
					  "  background: none; "
					  "  color: #e8eaed; "
					  "  font-size: 16px; "
					  "  margin-left: 12px; "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} "
					  ".cb-menu-slider-arrow label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 16px; "
					  "} "
					  ".cb-menu-slider-btn { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 18px; "
					  "  min-width: 36px; "
					  "  min-height: 36px; "
					  "  font-size: 18px; "
					  "  margin-left: 8px; "
					  "} "
					  ".cb-menu-slider-btn-active { "
					  "  background-color: #cbbef9; "
					  "  color: #202124; "
					  "} "
					  ".cb-menu-slider-btn label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} "
					  ".cb-menu-wifi-list-btn { "
					  "  background: none; "
					  "  color: #e8eaed; "
					  "  padding: 6px 10px; "
					  "  border-radius: 8px; "
					  "  font-size: 14px; "
					  "  min-height: 40px; "
					  "} "
					  ".cb-menu-wifi-list-btn:hover { "
					  "  background-color: rgba(255,255,255,0.05); "
					  "} "
					  ".cb-menu-wifi-list-btn label { "
					  "  color: #e8eaed; "
					  "} "
					  ".cb-menu-wifi-list-btn .icon { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 18px; "
					  "} "
					  ".cb-menu-wifi-list-btn .subtitle { "
					  "  font-size: 11px; "
					  "  opacity: 0.72; "
					  "} "
					  ".cb-menu-header { "
					  "  margin-bottom: 12px; "
					  "  min-height: 36px; "
					  "} "
					  ".cb-menu-header-btn { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 18px; "
					  "  min-width: 36px; "
					  "  min-height: 36px; "
					  "} "
					  ".cb-menu-header-btn label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 18px; "
					  "} "
					  ".cb-menu-header-title { "
					  "  color: #e8eaed; "
					  "  font-weight: 600; "
					  "  font-size: 16px; "
					  "} "
					  ".cb-menu-entry { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 18px; "
					  "  padding: 8px 12px; "
					  "  border: 1px solid rgba(255,255,255,0.1); "
					  "  min-height: 36px; "
					  "} "
					  ".cb-menu-password-row { "
					  "  margin: 8px 0 14px 0; "
					  "} "
					  ".cb-menu-icon-btn { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 18px; "
					  "  min-width: 36px; "
					  "  min-height: 36px; "
					  "  margin-left: 8px; "
					  "} "
					  ".cb-menu-icon-btn label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 18px; "
					  "} "
					  ".cb-menu-dialog-btn { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 18px; "
					  "  padding: 0 18px; "
					  "  min-width: 96px; "
					  "  min-height: 36px; "
					  "  font-weight: 600; "
					  "} "
					  ".cb-menu-dialog-btn-primary { "
					  "  background-color: #cbbef9; "
					  "  color: #202124; "
					  "} "
					  ".cb-menu-dialog-btn-primary:hover { "
					  "  background-color: #d8cefa; "
					  "} "
					  ".cb-menu-wifi-ssid { "
					  "  color: #e8eaed; "
					  "  font-size: 15px; "
					  "  font-weight: 600; "
					  "  margin-bottom: 8px; "
					  "} ";

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
											  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
}

static GtkWidget *create_chromeos_menu(BarWindow *bw, AppState *state) {
	chromeos_menu_apply_css();
	chromeos_menu_refresh_bluetooth_state(state);

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(win, "ebar-menu-window");
	gtk_style_context_add_class(gtk_widget_get_style_context(win), "ebar-menu-window");

	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual && gdk_screen_is_composited(screen))
		gtk_widget_set_visual(win, visual);

	gtk_widget_set_app_paintable(win, TRUE);
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
	bw->cb_menu_kb_label = NULL;
	bw->cb_menu_bat_label = NULL;
	bw->cb_menu_wifi_pill = NULL;
	bw->cb_menu_wifi_subtitle = NULL;
	bw->cb_menu_main_box = NULL;
	bw->cb_menu_brightness_slider = NULL;
}

void toggle_chromeos_menu(BarWindow *bw, AppState *state) {
	long long now = get_time_ms();

	if (bw->menu_window) {
		last_destroy_time = get_time_ms();
		gtk_widget_destroy(bw->menu_window);
		return;
	}

	if (now - last_destroy_time < 500)
		return;

	bw->menu_window = create_chromeos_menu(bw, state);
	g_signal_connect(bw->menu_window, "destroy", G_CALLBACK(on_menu_destroy), bw);

	menu_opened_time = get_time_ms();
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

		g_signal_connect(btn, "clicked", G_CALLBACK(on_sys_btn_clicked), ctx);
	}
}

/* ── slider helpers ────────────────────────────────────────────────────────── */
double slider_get_visual_min(GtkRange *range) {
	int basis_points = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "slider-visual-min-bp"));
	if (basis_points <= 0)
		return SLIDER_VISUAL_MIN_FALLBACK;
	return basis_points / 100.0;
}

void slider_set_visual_min(GtkRange *range, double visual_min) {
	if (visual_min < 0.0)
		visual_min = 0.0;
	if (visual_min > 99.0)
		visual_min = 99.0;
	g_object_set_data(G_OBJECT(range), "slider-visual-min-bp", GINT_TO_POINTER((int)(visual_min * 100.0)));
}

gboolean slider_is_updating(GtkRange *range) {
	return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "slider-updating"));
}

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
