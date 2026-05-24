#include "chromeos_menu.h"
#include "gtk-layer-shell.h"
#include <stdio.h>
#include <time.h>
#include <gdk/gdkkeysyms.h>

/* Global timestamp to handle focus-out vs click race conditions */
static long long last_destroy_time = 0;

static long long get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static gboolean on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
	(void)event;
	(void)data;
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

static gboolean on_window_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	(void)widget;
	(void)data;
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	return FALSE; /* let children draw normally */
}

void apply_menu_css(void) {
	/* NUCLEAR: No more 'loaded' guard. Load and apply freshly every time. */
	const char *css = "window#menu-window { "
					  "  background-color: transparent; "
					  "} "
					  "#menu-bg { "
					  "  background-color: #2b2b2b; "
					  "  border-radius: 24px; "
					  "  border: 1px solid rgba(255,255,255,0.1); "
					  "  box-shadow: 0 4px 12px rgba(0,0,0,0.5); "
					  "} "
					  ".cb-menu { "
					  "  padding: 16px; "
					  "} "
					  ".cb-menu-grid { "
					  "  margin-bottom: 12px; "
					  "} "
					  ".cb-menu-pill { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 16px; "
					  "  padding: 12px; "
					  "  margin: 0; "
					  "  border: none; "
					  "  box-shadow: none; "
					  "  min-height: 52px; "
					  "} "
					  ".cb-menu-pill:hover { "
					  "  background-color: #4c4c4c; "
					  "} "
					  ".cb-menu-pill-active { "
					  "  background-color: #cbbef9; "
					  "  color: #202124; "
					  "} "
					  ".cb-menu-pill-active:hover { "
					  "  background-color: #d8cefa; "
					  "} "
					  ".cb-menu-pill label { "
					  "  font-weight: 600; "
					  "  margin: 0; "
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
					  "  margin-left: 14px; "
					  "} "
					  ".cb-menu-slider trough { "
					  "  background-color: #3c3c3c; "
					  "  border-radius: 18px; "
					  "  min-height: 36px; "
					  "} "
					  ".cb-menu-slider trough highlight { "
					  "  background-color: #cbbef9; "
					  "  border-radius: 18px; "
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
					  "  border: none; "
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
					  "  border: none; "
					  "  padding: 0; "
					  "} "
					  ".cb-menu-settings label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 22px; "
					  "  margin: 0; "
					  "  padding: 0; "
					  "} "
					  ".cb-menu-battery { "
					  "  color: #e8eaed; "
					  "  font-size: 13px; "
					  "  margin: 0; "
					  "} "
					  ".cb-menu-slider-btn { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 12px; "
					  "  min-width: 36px; "
					  "  min-height: 36px; "
					  "  font-size: 16px; "
					  "  border: none; "
					  "  padding: 0; "
					  "  margin-left: 8px; "
					  "} "
					  ".cb-menu-slider-btn label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} ";

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
											  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
}

static GtkWidget *create_menu_pill(const char *icon, const char *title, const char *subtitle, gboolean active) {
	GtkWidget *btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-pill");

	if (active)
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-pill-active");

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	GtkWidget *icon_lbl = gtk_label_new(icon);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "icon");
	gtk_box_pack_start(GTK_BOX(box), icon_lbl, FALSE, FALSE, 0);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

	GtkWidget *title_lbl = gtk_label_new(title);
	gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

	if (subtitle) {
		GtkWidget *sub_lbl = gtk_label_new(subtitle);
		gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl), "subtitle");
		gtk_widget_set_halign(sub_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(vbox), sub_lbl, FALSE, FALSE, 0);
	}

	gtk_box_pack_start(GTK_BOX(box), vbox, TRUE, TRUE, 0);

	GtkWidget *arrow = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(arrow), "subtitle");
	gtk_box_pack_end(GTK_BOX(box), arrow, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(btn), box);

	return btn;
}

static GtkWidget *create_menu_slider(const char *icon, const char *right_icon) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-slider-box");

	GtkWidget *overlay = gtk_overlay_new();
	
	GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_range_set_value(GTK_RANGE(scale), 80);
	gtk_style_context_add_class(gtk_widget_get_style_context(scale), "cb-menu-slider");
	
	GtkWidget *icon_lbl = gtk_label_new(icon);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "cb-menu-slider-icon");
	gtk_widget_set_halign(icon_lbl, GTK_ALIGN_START);
	gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);
	
	gtk_container_add(GTK_CONTAINER(overlay), scale);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), icon_lbl);
	
	gtk_box_pack_start(GTK_BOX(box), overlay, TRUE, TRUE, 0);

	if (right_icon) {
		GtkWidget *right_btn = gtk_button_new_with_label(right_icon);
		gtk_style_context_add_class(gtk_widget_get_style_context(right_btn), "cb-menu-slider-btn");
		gtk_box_pack_start(GTK_BOX(box), right_btn, FALSE, FALSE, 0);
	}

	GtkWidget *arrow_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(arrow_btn), "cb-menu-slider-btn");
	gtk_container_add(GTK_CONTAINER(arrow_btn), gtk_label_new(""));
	gtk_box_pack_start(GTK_BOX(box), arrow_btn, FALSE, FALSE, 0);

	return box;
}

static GtkWidget *create_chromeos_menu(BarWindow *bw, AppState *state) {
	(void)state;
	apply_menu_css();

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(win, "menu-window");

	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual && gdk_screen_is_composited(screen))
		gtk_widget_set_visual(win, visual);

	gtk_widget_set_app_paintable(win, TRUE);
	g_signal_connect(win, "draw", G_CALLBACK(on_window_draw), NULL);
	g_signal_connect(win, "focus-out-event", G_CALLBACK(on_focus_out), NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);

	gtk_layer_init_for_window(GTK_WINDOW(win));
	gtk_layer_set_monitor(GTK_WINDOW(win), bw->monitor);
	gtk_layer_set_namespace(GTK_WINDOW(win), "ebar-menu");
	
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, 58);
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, 10);
	
	gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_keyboard_interactivity(GTK_WINDOW(win), TRUE);

	GtkWidget *bg = gtk_event_box_new();
	gtk_widget_set_name(bg, "menu-bg");

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "cb-menu");
	gtk_widget_set_size_request(main_box, 420, -1);

	GtkWidget *grid = gtk_grid_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(grid), "cb-menu-grid");
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

	GtkWidget *wifi = create_menu_pill("󰤨", "No conectado", "No hay redes", TRUE);
	gtk_grid_attach(GTK_GRID(grid), wifi, 0, 0, 2, 1);

	GtkWidget *screenshot = create_menu_pill("󰄀", "Captura de pantalla", NULL, FALSE);
	gtk_grid_attach(GTK_GRID(grid), screenshot, 2, 0, 2, 1);

	GtkWidget *bluetooth = create_menu_pill("󰂯", "Bluetooth", "Activado", TRUE);
	gtk_grid_attach(GTK_GRID(grid), bluetooth, 0, 1, 2, 1);

	GtkWidget *keyboard = create_menu_pill("󰌌", "Teclado", "ES", FALSE);
	gtk_grid_attach(GTK_GRID(grid), keyboard, 2, 1, 2, 1);

	gtk_box_pack_start(GTK_BOX(main_box), grid, FALSE, FALSE, 0);
	
	gtk_box_pack_start(GTK_BOX(main_box), create_menu_slider("󰕾", "󰝟"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_box), create_menu_slider("󰖔", "󰽢"), FALSE, FALSE, 0);

	GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(bottom_box), "cb-menu-bottom");

	GtkWidget *power_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(power_btn), "cb-menu-power");
	gtk_container_add(GTK_CONTAINER(power_btn), gtk_label_new("󰐥 "));
	gtk_box_pack_start(GTK_BOX(bottom_box), power_btn, FALSE, FALSE, 0);

	GtkWidget *battery_lbl = gtk_label_new("22 % - Queda: 1:32");
	gtk_style_context_add_class(gtk_widget_get_style_context(battery_lbl), "cb-menu-battery");
	gtk_widget_set_halign(battery_lbl, GTK_ALIGN_END);
	gtk_box_pack_start(GTK_BOX(bottom_box), battery_lbl, TRUE, TRUE, 12);

	GtkWidget *settings_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(settings_btn), "cb-menu-settings");
	GtkWidget *settings_icon = gtk_label_new("󰒓");
	gtk_widget_set_halign(settings_icon, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(settings_icon, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(settings_btn), settings_icon);
	gtk_box_pack_end(GTK_BOX(bottom_box), settings_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(main_box), bottom_box, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(bg), main_box);
	gtk_container_add(GTK_CONTAINER(win), bg);

	return win;
}

void toggle_chromeos_menu(BarWindow *bw, AppState *state) {
	long long now = get_time_ms();
	
	if (bw->menu_window) {
		last_destroy_time = get_time_ms();
		gtk_widget_destroy(bw->menu_window);
		bw->menu_window = NULL;
		return;
	}

	if (now - last_destroy_time < 500) {
		return;
	}

	bw->menu_window = create_chromeos_menu(bw, state);
	g_signal_connect(bw->menu_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &bw->menu_window);
	
	gtk_widget_show_all(bw->menu_window);
	gtk_widget_grab_focus(bw->menu_window);
}

static void on_sys_btn_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct { BarWindow *bw; AppState *state; } *ctx = data;
	toggle_chromeos_menu(ctx->bw, ctx->state);
}

void setup_chromeos_menu_toggle(BarWindow *bw, AppState *state) {
	if (!bw->cb_sys_label) return;
	
	GtkWidget *btn = gtk_widget_get_parent(bw->cb_sys_label);
	if (GTK_IS_BUTTON(btn)) {
		typedef struct { BarWindow *bw; AppState *state; } CallbackCtx;
		CallbackCtx *ctx = g_new0(CallbackCtx, 1);
		ctx->bw = bw;
		ctx->state = state;
		
		g_signal_connect(btn, "clicked", G_CALLBACK(on_sys_btn_clicked), ctx);
	}
}
