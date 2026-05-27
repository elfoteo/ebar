#include "chromeos_bar.h"
#include "chromeos_menu.h"
#include "gtk-layer-shell.h"
#include "ipc.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>

/* ── ChromeOS CSS ─────────────────────────────────────────────────────────────
 * Standalone CSS for chromeos mode. Called on startup and whenever the
 * fullscreen state changes so the top corners can be flattened/restored.
 * ─────────────────────────────────────────────────────────────────────────── */

static void on_desk_prev(GtkWidget *widget, gpointer data) {
	(void)widget;
	(void)data;
	char *res = hyprctl_request("dispatch workspace e-1");
	if (res)
		free(res);
}

static void on_desk_next(GtkWidget *widget, gpointer data) {
	(void)widget;
	(void)data;
	char *res = hyprctl_request("dispatch workspace e+1");
	if (res)
		free(res);
}

void apply_chromeos_css(AppState *state) {
	char css[16384];
	int n = 0;

#define A(...) n += snprintf(css + n, (int)sizeof(css) - n, __VA_ARGS__)

	A("* { font-family: \"%s\"; font-weight: 600; background: none; box-shadow: none; border: none; } ", state->config.font.family);
	A("window, .background { background-color: transparent; } ");

	A(".cb-pill { background-color: #505153; color: #E8EAED; border-radius: 18px; "
	  "  padding: 0 8px; font-size: 14px; font-weight: 600; min-height: 36px; } ");
	A(".cb-pill:hover { background-color: #616264; } ");
	/* Semi-touch: date right corners and sys left corners flatten toward each other */
	A("#cb-date { border-radius: 18px 6px 6px 18px; } ");
	A("#cb-sys  { border-radius: 6px 18px 18px 6px; } ");

	A(".cb-circle { background-color: #505153; color: #E8EAED; border-radius: 18px; "
	  "  min-width: 36px; min-height: 36px; padding: 0; font-size: 14px; font-weight: 600; } ");
	A(".cb-circle:hover { background-color: #616264; } ");
	A(".cb-circle-icon { font-size: 16px; } ");
	A(".cb-launcher-btn { font-weight: 900; font-size: 24px; } ");

	A(".cb-app-btn { background: transparent; border-radius: 18px; "
	  "  min-width: 36px; min-height: 36px; padding: 0; margin: 0; "
	  "  border: none; outline: none; box-shadow: none; } ");
	A(".cb-app-btn image { padding: 0; margin: 0; -gtk-icon-style: requested; } ");
	A(".cb-app-btn:hover { background-color: #616264; } ");

	A(".cb-desk-pill { background-color: #505153; color: #E8EAED; border-radius: 18px; "
	  "  padding: 0 4px; min-height: 36px; } ");
	A(".cb-desk-name { background-color: #616264; color: #E8EAED; border-radius: 14px; "
	  "  font-size: 13px; font-weight: 600; padding: 0 10px; min-height: 28px; } ");
	A(".cb-desk-arrow { background: transparent; color: #E8EAED; border-radius: 8px; "
	  "  min-width: 15px; min-height: 20px; margin: 0 0px; padding: 0; font-size: 11px; "
	  "  border: none; box-shadow: none; font-family: \"JetBrainsMonoNerdFont\"; } ");
	A(".cb-desk-arrow:hover { background-color: rgba(255,255,255,0.12); } ");
	/* Add a specific background and corner radius rule for each monitor */
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->monitor) {
			GdkRectangle mgeom;
			gdk_monitor_get_geometry(bw->monitor, &mgeom);
			A("#chromebook-bar-%d-%d { background-color: #424348; border-radius: 24px 24px 0 0; padding: 6px; transition: border-radius "
			  "200ms ease; } ",
			  mgeom.x, mgeom.y);
			A("#chromebook-bar-%d-%d.fullscreen-mode { border-radius: 0; } ", mgeom.x, mgeom.y);
		}
	}

#undef A

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
											  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);

	/* Apply or remove the fullscreen-mode class based on state */
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->cb_box) {
			GtkStyleContext *ctx = gtk_widget_get_style_context(bw->cb_box);
			if (bw->has_fullscreen) {
				gtk_style_context_add_class(ctx, "fullscreen-mode");
			} else {
				gtk_style_context_remove_class(ctx, "fullscreen-mode");
			}
		}
	}
}

static void on_bar_window_destroy(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct {
		BarWindow *bw;
		AppState *state;
	} *ctx = data;
	pthread_mutex_lock(&ctx->state->mutex);
	ctx->state->bar_windows = g_list_remove(ctx->state->bar_windows, ctx->bw);
	pthread_mutex_unlock(&ctx->state->mutex);
	if (ctx->bw->menu_window) {
		gtk_widget_destroy(ctx->bw->menu_window);
	}
	if (ctx->bw->popup_window) {
		gtk_widget_destroy(ctx->bw->popup_window);
	}
	g_free(ctx->bw);
	g_free(ctx);
}

/* ── ChromeOS bar window ──────────────────────────────────────────────────── */
void create_chromeos_bar_window(GdkMonitor *monitor, AppState *state) {
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual && gdk_screen_is_composited(screen))
		gtk_widget_set_visual(win, visual);
	gtk_widget_set_app_paintable(win, TRUE);

	gtk_layer_init_for_window(GTK_WINDOW(win));
	gtk_layer_set_monitor(GTK_WINDOW(win), monitor);
	gtk_layer_set_namespace(GTK_WINDOW(win), "ebar");

	/* ChromeOS shelf is always anchored to the bottom */
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(win));

	BarWindow *bw = g_new0(BarWindow, 1);
	bw->window = win;
	bw->monitor = monitor;
	bw->state = state;
	GdkRectangle geom;
	gdk_monitor_get_geometry(monitor, &geom);
	bw->has_fullscreen = check_fullscreen_on_monitor(geom.x, geom.y);

	GtkWidget *cb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	bw->cb_box = cb_box;
	char bar_id[64];
	snprintf(bar_id, sizeof(bar_id), "chromebook-bar-%d-%d", geom.x, geom.y);
	gtk_widget_set_name(cb_box, bar_id);
	gtk_widget_set_halign(cb_box, GTK_ALIGN_FILL);

	/* ── Far-left: launcher button ── */
	GtkWidget *btn_o = gtk_button_new_with_label("");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_o), "cb-circle");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_o), "cb-circle-icon");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_o), "cb-launcher-btn");

	/* ── Left area: desk switcher pill ── */
	GtkWidget *desk_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(desk_pill), "cb-desk-pill");

	GtkWidget *desk_name_lbl = gtk_label_new("Desk 1");
	gtk_style_context_add_class(gtk_widget_get_style_context(desk_name_lbl), "cb-desk-name");
	gtk_widget_set_valign(desk_name_lbl, GTK_ALIGN_CENTER);
	bw->cb_desk_label = desk_name_lbl;

	GtkWidget *btn_prev = gtk_button_new_with_label("");
	gtk_button_set_relief(GTK_BUTTON(btn_prev), GTK_RELIEF_NONE);
	gtk_widget_set_valign(btn_prev, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_prev), "cb-desk-arrow");
	g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_desk_prev), NULL);

	GtkWidget *btn_next = gtk_button_new_with_label("");
	gtk_button_set_relief(GTK_BUTTON(btn_next), GTK_RELIEF_NONE);
	gtk_widget_set_valign(btn_next, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_next), "cb-desk-arrow");
	g_signal_connect(btn_next, "clicked", G_CALLBACK(on_desk_next), NULL);

	gtk_box_pack_start(GTK_BOX(desk_pill), desk_name_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(desk_pill), btn_prev, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(desk_pill), btn_next, FALSE, FALSE, 0);

	/* ── Center: app launcher ── */
	GtkWidget *c_box = widget_launcher(bw, state);

	/* ── Right: system tray ── */
	GtkWidget *r_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *btn_clip = gtk_button_new_with_label("");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_clip), "cb-circle");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_clip), "cb-circle-icon");

	GtkWidget *btn_us = gtk_button_new_with_label("US");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_us), "cb-circle");
	bw->cb_layout_label = gtk_bin_get_child(GTK_BIN(btn_us));

	GtkWidget *btn_pen = gtk_button_new_with_label("󰏫");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_pen), "cb-circle");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_pen), "cb-circle-icon");

	GtkWidget *btn_date = gtk_button_new_with_label("May 14");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_date), "cb-pill");
	gtk_widget_set_name(btn_date, "cb-date");
	bw->cb_date_label = gtk_bin_get_child(GTK_BIN(btn_date));

	GtkWidget *btn_sys = gtk_button_new_with_label("9:29 󰤨  󰁹");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_sys), "cb-pill");
	gtk_widget_set_name(btn_sys, "cb-sys");
	bw->cb_sys_label = gtk_bin_get_child(GTK_BIN(btn_sys));

	/* Wrap date + sys in a tight inner box for the semi-touch look */
	GtkWidget *date_sys_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(date_sys_box), btn_date, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(date_sys_box), btn_sys, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(r_box), btn_clip, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r_box), btn_us, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r_box), btn_pen, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r_box), date_sys_box, FALSE, FALSE, 0);

	/* ── Assemble ── */
	GtkWidget *l_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(cb_box), btn_o, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(cb_box), l_spacer, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(cb_box), desk_pill, FALSE, FALSE, 0);
	gtk_box_set_center_widget(GTK_BOX(cb_box), c_box);
	gtk_box_pack_end(GTK_BOX(cb_box), r_box, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(win), cb_box);

	state->bar_windows = g_list_append(state->bar_windows, bw);

	typedef struct {
		BarWindow *bw;
		AppState *state;
	} BarDestroyCtx;
	BarDestroyCtx *dctx = g_new0(BarDestroyCtx, 1);
	dctx->bw = bw;
	dctx->state = state;
	g_signal_connect(win, "destroy", G_CALLBACK(on_bar_window_destroy), dctx);

	setup_chromeos_menu_toggle(bw, state);
	gtk_widget_show_all(win);
}
