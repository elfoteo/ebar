#include "chromeos_bar.h"
#include "icons.h"
#include "bar.h"
#include "chromeos_launcher.h"
#include "chromeos_menu.h"
#include "chromeos_menu_internal.h"
#include "gtk-layer-shell.h"
#include "ipc.h"
#include "util.h"
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

static void on_launcher_btn_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct {
		BarWindow *bw;
		AppState *state;
	} *ctx = data;
	toggle_chromeos_launcher(ctx->bw, ctx->state);
}

void apply_chromeos_css(AppState *state) {
	static char *cached_css = NULL;
	static char cached_font[64] = "";
	static char cached_accent[32] = "";
	static guint cached_n_bars = 0;
	guint n_bars = g_list_length(state->bar_windows);

	if (!cached_css || strcmp(cached_font, state->config.font.family) != 0 ||
	    strcmp(cached_accent, state->config.chromeos.accent_color) != 0 ||
	    cached_n_bars != n_bars) {
		g_free(cached_css);
		g_strlcpy(cached_font, state->config.font.family, sizeof(cached_font));
		g_strlcpy(cached_accent, state->config.chromeos.accent_color, sizeof(cached_accent));
		cached_n_bars = n_bars;

		char css[16384];
		int n = 0;

#define A(...) n += snprintf(css + n, (int)sizeof(css) - n, __VA_ARGS__)

		A("* { font-family: \"%s\"; font-weight: 600; background: none; box-shadow: none; border: none; } ", state->config.font.family);
		A("window, .background { background-color: transparent; } ");

		A(".cb-pill { background-color: #505153; color: #E8EAED; border-radius: 18px; "
		  "  padding: 0 8px; font-size: 14px; font-weight: 600; min-height: 36px; } ");
		A(".cb-pill:hover { background-color: #616264; } ");
		A(".cb-pill.active { background-color: %s; color: #202124; } ", state->config.chromeos.accent_color);
		A(".cb-pill.active label { color: #202124; } ");
		A("#cb-date { border-radius: 18px 6px 6px 18px; } ");
		A("#cb-sys  { border-radius: 6px 18px 18px 6px; } ");

		A(".cb-circle { background-color: #505153; color: #E8EAED; border-radius: 18px; "
		  "  min-width: 36px; min-height: 36px; padding: 0; font-size: 14px; font-weight: 600; } ");
		A(".cb-circle:hover { background-color: #616264; } ");
		A(".cb-circle-icon { font-size: 16px; } ");
		A(".cb-launcher-btn { font-weight: 900; font-size: 24px; } ");
		A(".cb-launcher-btn.active { background-color: %s; } ", state->config.chromeos.accent_color);

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
		cached_css = g_strdup(css);
	}

	apply_css_from_string(cached_css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

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

/* ── ChromeOS tray update (called from update_widgets_idle) ──────────────── */

void chromeos_update_tray(AppState *state, BarWindow *bw, SystemData *d,
						  int time_changed, int wifi_changed, int bat_changed,
						  int kb_changed, int vol_changed, int brightness_changed,
						  time_t now, struct tm *tmv) {

	if (time_changed) {
		if (bw->cb_date_label) {
			char cb_dstr[64];
			strftime(cb_dstr, sizeof(cb_dstr), "%b %-d", tmv);
			gtk_label_set_text(GTK_LABEL(bw->cb_date_label), cb_dstr);
		}
	}

	if (time_changed || wifi_changed || bat_changed) {
		if (bw->cb_sys_label) {
			char sys_buf[128], t_buf[32];
			strftime(t_buf, sizeof(t_buf), "%-I:%M", tmv);
			const char *b_icon = get_battery_icon(d->bat_percent, d->bat_charging);

			const char *w_icon = ICON_WIFI_OFF;
			if (d->wifi_enabled && d->wifi_adapter_exists) {
				if (d->wifi_connected) w_icon = get_wifi_icon(d->wifi_strength);
				else w_icon = ICON_WIFI_0;
			}

			if (d->bat_percent >= 0 && d->bat_percent < 20 && !d->bat_charging)
				snprintf(sys_buf, sizeof(sys_buf), "%s %s  <span foreground=\"orange\">%s</span>", t_buf, w_icon, b_icon);
			else
				snprintf(sys_buf, sizeof(sys_buf), "%s %s  %s", t_buf, w_icon, b_icon);
			gtk_label_set_markup(GTK_LABEL(bw->cb_sys_label), sys_buf);
		}
	}

	if (kb_changed) {
		if (bw->cb_layout_label && d->kb_layout[0])
			gtk_label_set_text(GTK_LABEL(bw->cb_layout_label), d->kb_layout);
		if (bw->cb_menu_kb_label && GTK_IS_LABEL(bw->cb_menu_kb_label) && d->kb_layout[0])
			gtk_label_set_text(GTK_LABEL(bw->cb_menu_kb_label), d->kb_layout);
	}

	if (bat_changed) {
		if (bw->cb_menu_bat_label && GTK_IS_LABEL(bw->cb_menu_bat_label)) {
			char bat_buf[128];
			if (d->bat_percent >= 0) {
				snprintf(bat_buf, sizeof(bat_buf), "%d%% - %s", d->bat_percent,
						 d->bat_time_remaining[0] ? d->bat_time_remaining : (d->bat_charging ? "Charging" : "Discharging"));
			} else {
				snprintf(bat_buf, sizeof(bat_buf), "Battery: N/A");
			}
			gtk_label_set_text(GTK_LABEL(bw->cb_menu_bat_label), bat_buf);
		}
	}

	if (wifi_changed) {
		const char *w_icon = ICON_WIFI_OFF;
		const char *w_subtitle = "Off";
		int active = 0;
		int pill_sensitive = 1;
		int arrow_sensitive = 0;

		if (!d->wifi_adapter_exists) {
			w_subtitle = "No adapter";
			pill_sensitive = 0;
		} else {
			arrow_sensitive = 1;
			if (d->wifi_enabled) {
				active = 1;
				if (d->wifi_connected) {
					w_icon = get_wifi_icon(d->wifi_strength);
					w_subtitle = d->wifi_ssid[0] ? d->wifi_ssid : "Connected";
				} else {
					w_icon = ICON_WIFI_0;
					w_subtitle = "Disconnected";
				}
			}
		}

		if (bw->cb_menu_wifi_pill && GTK_IS_WIDGET(bw->cb_menu_wifi_pill)) {
			GtkStyleContext *ctx = gtk_widget_get_style_context(bw->cb_menu_wifi_pill);
			if (active) gtk_style_context_add_class(ctx, "cb-menu-pill-active");
			else gtk_style_context_remove_class(ctx, "cb-menu-pill-active");

			gtk_widget_set_sensitive(bw->cb_menu_wifi_pill, pill_sensitive);
			if (bw->cb_menu_wifi_arrow) gtk_widget_set_sensitive(bw->cb_menu_wifi_arrow, arrow_sensitive);
			if (bw->cb_menu_wifi_subtitle && GTK_IS_LABEL(bw->cb_menu_wifi_subtitle))
				gtk_label_set_text(GTK_LABEL(bw->cb_menu_wifi_subtitle), w_subtitle);
			if (bw->cb_menu_wifi_icon && GTK_IS_LABEL(bw->cb_menu_wifi_icon))
				gtk_label_set_text(GTK_LABEL(bw->cb_menu_wifi_icon), w_icon);
		}
	}

	if (vol_changed) {
		if (bw->popup_window && gtk_widget_get_visible(bw->popup_window)) {
			trigger_volume_popup_idle(bw);
		}
		if (bw->cb_menu_volume_slider && GTK_IS_RANGE(bw->cb_menu_volume_slider)) {
			GtkRange *range = GTK_RANGE(bw->cb_menu_volume_slider);
			GtkStyleContext *scale_ctx = gtk_widget_get_style_context(GTK_WIDGET(range));
			if (d->vol_muted) {
				gtk_style_context_add_class(scale_ctx, "cb-menu-slider-muted");
			} else {
				gtk_style_context_remove_class(scale_ctx, "cb-menu-slider-muted");
			}
			if (!slider_is_updating(range) && (now - state->last_manual_vol_update > 1)) {
				slider_set_updating(range, TRUE);
				gtk_range_set_value(range, slider_get_display_value(range, d->visual_volume));
				slider_set_updating(range, FALSE);
			}
		}
	}

	if (brightness_changed) {
		if (bw->popup_window && gtk_widget_get_visible(bw->popup_window)) {
			trigger_brightness_popup_idle(bw);
		}
		if (bw->cb_menu_brightness_slider && GTK_IS_RANGE(bw->cb_menu_brightness_slider)) {
			GtkRange *range = GTK_RANGE(bw->cb_menu_brightness_slider);
			if (!slider_is_updating(range) && (now - state->last_manual_bright_update > 1)) {
				slider_set_updating(range, TRUE);
				gtk_range_set_value(range, slider_get_display_value(range, d->visual_brightness));
				slider_set_updating(range, FALSE);
			}
		}
	}
}

/* ── ChromeOS bar window ──────────────────────────────────────────────────── */
void create_chromeos_bar_window(GdkMonitor *monitor, AppState *state) {
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	setup_transparent_window(win);

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
	GtkWidget *btn_o = gtk_button_new_with_label(ICON_LAUNCHER);
	bw->cb_launcher_btn = btn_o;
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_o), "cb-circle");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_o), "cb-circle-icon");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_o), "cb-launcher-btn");

	typedef struct {
		BarWindow *bw;
		AppState *state;
	} LauncherCtx;
	LauncherCtx *lctx = g_new0(LauncherCtx, 1);
	lctx->bw = bw;
	lctx->state = state;
	g_signal_connect_data(btn_o, "clicked", G_CALLBACK(on_launcher_btn_clicked), lctx, chromeos_menu_free_generic_ctx, 0);

	/* ── Left area: desk switcher pill ── */
	GtkWidget *desk_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(desk_pill), "cb-desk-pill");

	GtkWidget *desk_name_lbl = gtk_label_new("Desk 1");
	gtk_style_context_add_class(gtk_widget_get_style_context(desk_name_lbl), "cb-desk-name");
	gtk_widget_set_valign(desk_name_lbl, GTK_ALIGN_CENTER);
	bw->cb_desk_label = desk_name_lbl;

	GtkWidget *btn_prev = gtk_button_new_with_label(ICON_CHEVRON_LEFT);
	gtk_button_set_relief(GTK_BUTTON(btn_prev), GTK_RELIEF_NONE);
	gtk_widget_set_valign(btn_prev, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_prev), "cb-desk-arrow");
	g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_desk_prev), NULL);

	GtkWidget *btn_next = gtk_button_new_with_label(ICON_CHEVRON_RIGHT);
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
	GtkWidget *btn_clip = gtk_button_new_with_label(ICON_CLIPBOARD);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_clip), "cb-circle");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_clip), "cb-circle-icon");

	GtkWidget *btn_us = gtk_button_new_with_label("US");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_us), "cb-circle");
	bw->cb_layout_label = gtk_bin_get_child(GTK_BIN(btn_us));

	GtkWidget *btn_pen = gtk_button_new_with_label(ICON_PEN);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_pen), "cb-circle");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_pen), "cb-circle-icon");

	GtkWidget *btn_date = gtk_button_new_with_label("May 14");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_date), "cb-pill");
	gtk_widget_set_name(btn_date, "cb-date");
	bw->cb_date_label = gtk_bin_get_child(GTK_BIN(btn_date));

	GtkWidget *btn_sys = gtk_button_new_with_label("9:29 " ICON_WIFI_4 "  " ICON_BATTERY_100);
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

	BarDestroyCtx *dctx = g_new0(BarDestroyCtx, 1);
	dctx->bw = bw;
	dctx->state = state;
	g_signal_connect(win, "destroy", G_CALLBACK(on_bar_window_destroy), dctx);

	setup_chromeos_menu_toggle(bw, state);
	gtk_widget_show_all(win);
}
