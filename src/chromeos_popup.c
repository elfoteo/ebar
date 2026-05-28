#include "chromeos_popup.h"
#include "chromeos_menu.h"
#include "chromeos_menu_internal.h"
#include "gtk-layer-shell.h"
#include "ipc.h"
#include <gtk/gtk.h>
#include <time.h>

extern gboolean update_widgets_idle(gpointer data);

/* ── Slider helpers (forwarded from chromeos_menu_internal.h) ────────────── */

static void update_popup_slider_minimum_state(GtkRange *range) {
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

static void on_popup_scale_value_changed(GtkRange *range, gpointer data) {
	(void)data;
	double visual_min = slider_get_visual_min(range);
	if (!slider_is_updating(range) && gtk_range_get_value(range) < visual_min) {
		slider_set_updating(range, TRUE);
		gtk_range_set_value(range, visual_min);
		slider_set_updating(range, FALSE);
	}
	update_popup_slider_minimum_state(range);
}

static void on_popup_scale_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer data) {
	(void)data;
	GtkRange *range = GTK_RANGE(widget);
	double old_min = slider_get_visual_min(range);
	if (allocation->width <= 0)
		return;
	double visual_min = (SLIDER_VISUAL_MIN_PX * 100.0) / allocation->width;
	if (visual_min > 99.0)
		visual_min = 99.0;
	if ((int)(old_min * 100.0) == (int)(visual_min * 100.0))
		return;

	double actual_val = slider_get_actual_value(range);
	slider_set_visual_min(range, visual_min);
	slider_set_updating(range, TRUE);
	gtk_range_set_value(range, slider_get_display_value(range, actual_val));
	slider_set_updating(range, FALSE);
}

static GtkWidget *create_popup_slider(const char *icon, double initial_val, GCallback on_changed, gpointer user_data,
									  GtkWidget **scale_out) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-slider-box");

	GtkWidget *overlay = gtk_overlay_new();
	GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	if (scale_out)
		*scale_out = scale;
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	slider_set_visual_min(GTK_RANGE(scale), SLIDER_VISUAL_MIN_FALLBACK);
	gtk_range_set_value(GTK_RANGE(scale), slider_get_display_value(GTK_RANGE(scale), initial_val));
	gtk_style_context_add_class(gtk_widget_get_style_context(scale), "cb-menu-slider");

	GtkWidget *icon_lbl = gtk_label_new(icon);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "cb-menu-slider-icon");
	gtk_widget_set_halign(icon_lbl, GTK_ALIGN_START);
	gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);
	g_object_set_data(G_OBJECT(scale), "slider-icon", icon_lbl);
	g_signal_connect(scale, "value-changed", G_CALLBACK(on_popup_scale_value_changed), NULL);
	g_signal_connect(scale, "size-allocate", G_CALLBACK(on_popup_scale_size_allocate), NULL);
	if (on_changed)
		g_signal_connect(scale, "value-changed", on_changed, user_data);
	update_popup_slider_minimum_state(GTK_RANGE(scale));

	gtk_container_add(GTK_CONTAINER(overlay), scale);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), icon_lbl);
	gtk_box_pack_start(GTK_BOX(box), overlay, TRUE, TRUE, 0);
	return box;
}

/* ── Per-popup callbacks ─────────────────────────────────────────────────── */

static void on_popup_volume_changed(GtkRange *range, gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	AppState *state = bw->state;
	if (slider_is_updating(range))
		return;
	double val = slider_get_actual_value(range);
	pthread_mutex_lock(&state->mutex);
	state->last_manual_vol_update = time(NULL);
	state->sys_data.vol = val;
	if (val > 0.5)
		state->sys_data.vol_muted = 0;
	pthread_mutex_unlock(&state->mutex);

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %.0f%%", val);
	g_spawn_command_line_async(cmd, NULL);
	update_widgets_idle(state);
}

static void on_popup_brightness_changed(GtkRange *range, gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	AppState *state = bw->state;
	if (slider_is_updating(range))
		return;
	double val = slider_get_actual_value(range);
	pthread_mutex_lock(&state->mutex);
	state->sys_data.brightness = (float)val;
	pthread_mutex_unlock(&state->mutex);

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "brightnessctl set %.0f%%", val);
	g_spawn_command_line_async(cmd, NULL);
	update_widgets_idle(state);
}

/* ── Animation and visibility ────────────────────────────────────────────── */

static void update_popup_visibility(BarWindow *bw);

static gboolean on_popup_fade_tick(gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	gboolean showing = (bw->popup_timer[POPUP_TYPE_BRIGHTNESS] != 0 || bw->popup_timer[POPUP_TYPE_VOLUME] != 0 || bw->popup_hovered);

	if (showing) {
		bw->popup_opacity += 0.12;
		if (bw->popup_opacity >= 1.0) {
			bw->popup_opacity = 1.0;
			gtk_widget_set_opacity(bw->popup_window, 1.0);
			bw->popup_fade_timer = 0;
			return G_SOURCE_REMOVE;
		}
	} else {
		bw->popup_opacity -= 0.12;
		if (bw->popup_opacity <= 0.0) {
			bw->popup_opacity = 0.0;
			gtk_widget_set_opacity(bw->popup_window, 0.0);
			gtk_widget_hide(bw->popup_window);
			bw->popup_fade_timer = 0;
			return G_SOURCE_REMOVE;
		}
	}
	gtk_widget_set_opacity(bw->popup_window, bw->popup_opacity);
	return G_SOURCE_CONTINUE;
}

static void update_popup_visibility(BarWindow *bw) {
	if (!bw->popup_window)
		return;

	gboolean any_visible = FALSE;
	for (int i = 0; i < POPUP_COUNT; i++) {
		if (bw->popup_timer[i] != 0 || (bw->popup_hovered && gtk_widget_get_visible(bw->popup_slider_box[i]))) {
			gtk_widget_show(bw->popup_slider_box[i]);
			any_visible = TRUE;
		} else {
			gtk_widget_hide(bw->popup_slider_box[i]);
		}
	}

	if (any_visible || bw->popup_hovered) {
		gtk_widget_show(bw->popup_window);
		if (!bw->popup_fade_timer)
			bw->popup_fade_timer = g_timeout_add(16, on_popup_fade_tick, bw);
	} else {
		if (!bw->popup_fade_timer)
			bw->popup_fade_timer = g_timeout_add(16, on_popup_fade_tick, bw);
	}
}

/* ── Timeout callbacks ───────────────────────────────────────────────────── */

static gboolean on_brightness_popup_timeout(gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	bw->popup_timer[POPUP_TYPE_BRIGHTNESS] = 0;
	update_popup_visibility(bw);
	return G_SOURCE_REMOVE;
}

static gboolean on_volume_popup_timeout(gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	bw->popup_timer[POPUP_TYPE_VOLUME] = 0;
	update_popup_visibility(bw);
	return G_SOURCE_REMOVE;
}

/* ── Enter/leave hover handlers ──────────────────────────────────────────── */

static gboolean on_popup_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
	(void)widget;
	(void)event;
	BarWindow *bw = (BarWindow *)data;
	bw->popup_hovered = 1;
	for (int i = 0; i < POPUP_COUNT; i++) {
		if (bw->popup_timer[i]) {
			g_source_remove(bw->popup_timer[i]);
			bw->popup_timer[i] = 0;
		}
	}
	update_popup_visibility(bw);
	return FALSE;
}

static gboolean on_popup_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
	(void)widget;
	(void)event;
	BarWindow *bw = (BarWindow *)data;
	bw->popup_hovered = 0;
	if (gtk_widget_get_visible(bw->popup_slider_box[POPUP_TYPE_BRIGHTNESS]))
		bw->popup_timer[POPUP_TYPE_BRIGHTNESS] = g_timeout_add(3000, on_brightness_popup_timeout, bw);
	if (gtk_widget_get_visible(bw->popup_slider_box[POPUP_TYPE_VOLUME]))
		bw->popup_timer[POPUP_TYPE_VOLUME] = g_timeout_add(3000, on_volume_popup_timeout, bw);
	update_popup_visibility(bw);
	return FALSE;
}

/* ── Generic popup builder ────────────────────────────────────────────────── */

static void trigger_popup_generic(BarWindow *bw, int type, double val, const char *icon, gboolean muted) {
	if (!bw || !bw->state || bw->state->config.mode != MODE_CHROMEOS)
		return;

	/* Only show passive popup if the main menu is NOT open on this monitor */
	if (bw->menu_window) {
		if (bw->popup_window) {
			gtk_widget_hide(bw->popup_window);
		}
		return;
	}

	if (!bw->popup_window) {
		chromeos_menu_apply_css(bw->state);

		GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(win), "ebar-popup");
		gtk_widget_set_name(win, "ebar-passive-popup-window");
		gtk_style_context_add_class(gtk_widget_get_style_context(win), "ebar-menu-window");

		GdkScreen *screen = gdk_screen_get_default();
		GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
		if (visual && gdk_screen_is_composited(screen))
			gtk_widget_set_visual(win, visual);
		gtk_widget_set_app_paintable(win, TRUE);

		gtk_layer_init_for_window(GTK_WINDOW(win));
		gtk_layer_set_monitor(GTK_WINDOW(win), bw->monitor);
		gtk_layer_set_namespace(GTK_WINDOW(win), "ebar-popup");
		gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
		gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
		gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

		gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, 6);
		gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, 6);

		gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
		gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
		gtk_window_set_default_size(GTK_WINDOW(win), 284, -1);
		gtk_widget_set_opacity(win, 0.0);
		bw->popup_opacity = 0.0;

		/* Hyprland bug bypass: force RGBX to prevent unintended transparency/blur issues */
		char *res = hyprctl_request("keyword windowrule forcergbx,title:^(ebar-popup)$");
		if (res) free(res);
		res = hyprctl_request("keyword layerrule forcergbx,ebar-popup");
		if (res) free(res);

		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_name(box, "menu-bg");
		gtk_container_add(GTK_CONTAINER(win), box);

		GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		gtk_widget_set_margin_top(inner, 8);
		gtk_widget_set_margin_bottom(inner, 8);
		gtk_widget_set_margin_start(inner, 12);
		gtk_widget_set_margin_end(inner, 12);
		gtk_widget_set_size_request(inner, 260, 40);
		gtk_container_add(GTK_CONTAINER(box), inner);

		/* Create both sliders upfront but hidden */
		GtkWidget *b_scale = NULL;
		bw->popup_slider_box[POPUP_TYPE_BRIGHTNESS] = create_popup_slider("󰃟", 0, G_CALLBACK(on_popup_brightness_changed), bw, &b_scale);
		bw->popup_slider_range[POPUP_TYPE_BRIGHTNESS] = b_scale;
		gtk_box_pack_start(GTK_BOX(inner), bw->popup_slider_box[POPUP_TYPE_BRIGHTNESS], FALSE, FALSE, 0);

		GtkWidget *v_scale = NULL;
		bw->popup_slider_box[POPUP_TYPE_VOLUME] = create_popup_slider("󰕾", 0, G_CALLBACK(on_popup_volume_changed), bw, &v_scale);
		bw->popup_slider_range[POPUP_TYPE_VOLUME] = v_scale;
		gtk_box_pack_start(GTK_BOX(inner), bw->popup_slider_box[POPUP_TYPE_VOLUME], FALSE, FALSE, 0);

		gtk_widget_add_events(win, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
		g_signal_connect(win, "enter-notify-event", G_CALLBACK(on_popup_enter), bw);
		g_signal_connect(win, "leave-notify-event", G_CALLBACK(on_popup_leave), bw);

		bw->popup_window = win;
		gtk_widget_show_all(win);
		gtk_widget_hide(bw->popup_slider_box[POPUP_TYPE_BRIGHTNESS]);
		gtk_widget_hide(bw->popup_slider_box[POPUP_TYPE_VOLUME]);
	}

	/* Reset the auto-close timer */
	if (bw->popup_timer[type]) {
		g_source_remove(bw->popup_timer[type]);
	}
	bw->popup_timer[type] = g_timeout_add(3000, (type == POPUP_TYPE_VOLUME) ? on_volume_popup_timeout : on_brightness_popup_timeout, bw);

	/* Update icon and value */
	if (bw->popup_slider_range[type]) {
		GtkRange *range = GTK_RANGE(bw->popup_slider_range[type]);
		g_object_set_data(G_OBJECT(range), "muted", GINT_TO_POINTER(muted));
		slider_set_updating(range, TRUE);
		gtk_range_set_value(range, slider_get_display_value(range, val));
		slider_set_updating(range, FALSE);
		update_popup_slider_minimum_state(range);

		if (type == POPUP_TYPE_VOLUME) {
			GtkWidget *icon_lbl = g_object_get_data(G_OBJECT(range), "slider-icon");
			if (icon_lbl)
				gtk_label_set_text(GTK_LABEL(icon_lbl), icon);
		}
	}

	update_popup_visibility(bw);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

gboolean trigger_brightness_popup_idle(gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	AppState *state = bw->state;

	pthread_mutex_lock(&state->mutex);
	float b;
	/* If already visible, use visual_brightness to follow the animation.
	 * If just showing up, use the target brightness so it appears immediately at the right spot. */
	if (bw->popup_window && gtk_widget_get_visible(bw->popup_window) && bw->popup_opacity > 0.5) {
		b = state->sys_data.visual_brightness;
	} else {
		b = state->sys_data.brightness;
	}
	pthread_mutex_unlock(&state->mutex);

	trigger_popup_generic(bw, POPUP_TYPE_BRIGHTNESS, b, "󰃟", FALSE);

	if (bw->popup_window) {
		gtk_widget_show_now(bw->popup_window);
		gdk_display_flush(gdk_display_get_default());
	}

	return G_SOURCE_REMOVE;
}

gboolean trigger_volume_popup_idle(gpointer data) {
	BarWindow *bw = (BarWindow *)data;
	AppState *state = bw->state;

	pthread_mutex_lock(&state->mutex);
	float v = state->sys_data.visual_volume;
	gboolean muted = state->sys_data.vol_muted;
	pthread_mutex_unlock(&state->mutex);

	const char *icon = "󰕾";
	if (muted || v == 0)
		icon = "󰝟";
	else if (v <= 33)
		icon = "󰕿";
	else if (v <= 66)
		icon = "󰖀";

	trigger_popup_generic(bw, POPUP_TYPE_VOLUME, v, icon, muted);
	return G_SOURCE_REMOVE;
}
