#include "widgets.h"
#include "bluetooth.h"
#include "chromeos_bar.h"
#include "chromeos_popup.h"
#include "constants.h"
#include "gtk-layer-shell.h"
#include "icons.h"
#include "nightlight.h"
#include "pulse.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUME_SCROLL_STEP 2.0
#define BRIGHTNESS_SCROLL_STEP 5.0
#define RING_SIZE 48
#define RING_THICKNESS 3.0
#define RING_PADDING 2.0

static double compute_scroll_delta(GdkEventScroll *event, double step) {
	if (event->direction == GDK_SCROLL_UP)
		return step;
	else if (event->direction == GDK_SCROLL_DOWN)
		return -step;
	else if (event->direction == GDK_SCROLL_SMOOTH)
		return -event->delta_y * step;
	return 0;
}

static void draw_ring(cairo_t *cr, GtkAllocation *alloc, double pct, const GdkRGBA *ring);

static gboolean on_btn_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
	(void)data;
	GdkWindow *win = event->window;
	if (win) {
		GdkDisplay *display = gtk_widget_get_display(widget);
		GdkCursor *cursor = gdk_cursor_new_from_name(display, "pointer");
		gdk_window_set_cursor(win, cursor);
		g_object_unref(cursor);
	}
	return FALSE;
}

static gboolean on_btn_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
	(void)data;
	GdkWindow *win = event->window;
	if (win) {
		GdkDisplay *display = gtk_widget_get_display(widget);
		GdkCursor *cursor = gdk_cursor_new_from_name(display, "default");
		gdk_window_set_cursor(win, cursor);
		g_object_unref(cursor);
	}
	return FALSE;
}

static void on_media_prev(GtkWidget *widget, gpointer data) {
	(void)widget;
	(void)data;
	g_spawn_command_line_async("playerctl previous || playerctl -p %any previous", NULL);
}
static void on_media_play(GtkWidget *widget, gpointer data) {
	(void)widget;
	(void)data;
	g_spawn_command_line_async("playerctl play-pause", NULL);
}
static void on_media_next(GtkWidget *widget, gpointer data) {
	(void)widget;
	(void)data;
	g_spawn_command_line_async("playerctl next || playerctl -p %any next", NULL);
}

static gboolean on_volume_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
	(void)widget;
	AppState *state = (AppState *)data;
	double delta = compute_scroll_delta(event, VOLUME_SCROLL_STEP);
	if (delta == 0)
		return TRUE;
	pthread_mutex_lock(&state->mutex);
	state->last_manual_vol_update = time(NULL);
	state->sys_data.vol = CLAMP(state->sys_data.vol + delta, 0.0, 100.0);
	double vol = state->sys_data.vol;
	pthread_mutex_unlock(&state->mutex);
	pulse_set_volume(state, vol);
	/* Trigger volume popup on each bar window */
	for (GList *l = state->bar_windows; l != NULL; l = l->next)
		g_idle_add(trigger_volume_popup_idle, l->data);
	update_widgets_idle(state);
	return TRUE;
}

static gboolean on_volume_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	(void)widget;
	AppState *state = (AppState *)data;
	if (event->button == 1)
		g_spawn_command_line_async(state->config.volume.app, NULL);
	else if (event->button == 3) {
		pthread_mutex_lock(&state->mutex);
		state->last_manual_vol_update = time(NULL);
		state->sys_data.vol_muted = !state->sys_data.vol_muted;
		int muted = state->sys_data.vol_muted;
		pthread_mutex_unlock(&state->mutex);
		pulse_set_mute(state, muted);
		update_widgets_idle(state);
	}
	return TRUE;
}

static void draw_ring(cairo_t *cr, GtkAllocation *alloc, double pct, const GdkRGBA *ring) {
	double cx = alloc->width / 2.0;
	double cy = alloc->height / 2.0;
	double radius = (MIN(alloc->width, alloc->height) / 2.0) - RING_THICKNESS / 2.0 - RING_PADDING;
	double start = -M_PI / 2.0;

	cairo_set_line_width(cr, RING_THICKNESS);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	/* Background track */
	cairo_set_source_rgba(cr, ring->red, ring->green, ring->blue, 0.15);
	cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
	cairo_stroke(cr);

	/* Foreground arc */
	if (pct > 0.5) {
		double end = start + (pct / 100.0) * 2.0 * M_PI;
		gdk_cairo_set_source_rgba(cr, ring);
		cairo_arc(cr, cx, cy, radius, start, end);
		cairo_stroke(cr);
	}
}

static gboolean on_ring_draw_generic(GtkWidget *widget, cairo_t *cr, double pct, AppState *state) {
	GtkAllocation alloc;
	gtk_widget_get_allocation(widget, &alloc);
	GdkRGBA ring;
	if (!gdk_rgba_parse(&ring, state->config.colors.ring_color))
		ring = (GdkRGBA){1.0, 1.0, 1.0, 0.9};
	draw_ring(cr, &alloc, pct, &ring);
	return FALSE;
}

static gboolean on_volume_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	double vol = state->sys_data.visual_volume;
	int muted = state->sys_data.vol_muted;
	pthread_mutex_unlock(&state->mutex);
	return on_ring_draw_generic(widget, cr, muted ? 0 : vol, state);
}

GtkWidget *widget_workspaces(BarWindow *bw, AppState *state) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	for (int i = 0; i < state->config.workspaces.count; i++) {
		bw->ws_labels[i] = gtk_label_new(NULL);
		GtkStyleContext *context = gtk_widget_get_style_context(bw->ws_labels[i]);
		gtk_style_context_add_class(context, "workspace-label");
		gtk_box_pack_start(GTK_BOX(box), bw->ws_labels[i], FALSE, FALSE, 0);
	}
	return box;
}

GtkWidget *widget_clock(BarWindow *bw, AppState *state) {
	(void)state;
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	bw->clock_time_label = gtk_label_new("");
	gtk_widget_set_name(bw->clock_time_label, "clock-time");
	bw->clock_date_label = gtk_label_new("");
	gtk_widget_set_name(bw->clock_date_label, "clock-date");
	gtk_box_pack_start(GTK_BOX(box), bw->clock_time_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), bw->clock_date_label, FALSE, FALSE, 0);
	return box;
}

GtkWidget *widget_media(BarWindow *bw, AppState *state) {
	(void)state;
	GtkWidget *media_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(media_box), "media-box");

	gtk_widget_set_valign(media_box, GTK_ALIGN_CENTER);

	GtkWidget *pbtn = gtk_button_new_with_label(ICON_MEDIA_PREV);
	bw->media_play_btn = gtk_button_new_with_label(ICON_MEDIA_PLAY);
	GtkWidget *nbtn = gtk_button_new_with_label(ICON_MEDIA_NEXT);

	gtk_widget_set_can_focus(pbtn, FALSE);
	gtk_widget_set_can_focus(bw->media_play_btn, FALSE);
	gtk_widget_set_can_focus(nbtn, FALSE);

	gtk_widget_set_valign(pbtn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->media_play_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(nbtn, GTK_ALIGN_CENTER);

	g_signal_connect(pbtn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
	g_signal_connect(pbtn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);
	g_signal_connect(bw->media_play_btn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
	g_signal_connect(bw->media_play_btn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);
	g_signal_connect(nbtn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
	g_signal_connect(nbtn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);

	g_signal_connect(pbtn, "clicked", G_CALLBACK(on_media_prev), NULL);
	g_signal_connect(bw->media_play_btn, "clicked", G_CALLBACK(on_media_play), NULL);
	g_signal_connect(nbtn, "clicked", G_CALLBACK(on_media_next), NULL);

	gtk_box_pack_start(GTK_BOX(media_box), pbtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(media_box), bw->media_play_btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(media_box), nbtn, FALSE, FALSE, 0);

	bw->media_sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_sep), "media-sep");
	gtk_box_pack_start(GTK_BOX(media_box), bw->media_sep, FALSE, FALSE, 0);

	GtkWidget *text_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(text_vbox, GTK_ALIGN_CENTER);

	bw->media_title_label = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_title_label), "media-title-label");
	gtk_label_set_ellipsize(GTK_LABEL(bw->media_title_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(bw->media_title_label), 0);
	gtk_widget_set_size_request(bw->media_title_label, 1, -1); // Allow shrinking
	gtk_box_pack_start(GTK_BOX(text_vbox), bw->media_title_label, FALSE, FALSE, 0);

	bw->media_artist_label = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_artist_label), "media-artist-label");
	gtk_label_set_ellipsize(GTK_LABEL(bw->media_artist_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(bw->media_artist_label), 0);
	gtk_widget_set_size_request(bw->media_artist_label, 1, -1);
	gtk_box_pack_start(GTK_BOX(text_vbox), bw->media_artist_label, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(media_box), text_vbox, FALSE, FALSE, 0);
	return media_box;
}

GtkWidget *widget_volume(BarWindow *bw, AppState *state) {
	/* EventBox with above_child=TRUE captures ALL pointer events for the full
	 * bounding box (ring + label) before any child GDK window sees them.
	 * The overlay inside is purely visual. */
	GtkWidget *evbox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(evbox), TRUE);
	gtk_widget_set_valign(evbox, GTK_ALIGN_CENTER);
	gtk_widget_add_events(evbox, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect(evbox, "scroll-event", G_CALLBACK(on_volume_scroll), state);
	g_signal_connect(evbox, "button-press-event", G_CALLBACK(on_volume_click), state);
	g_signal_connect(evbox, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
	g_signal_connect(evbox, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);

	GtkWidget *overlay = gtk_overlay_new();

	/* Drawing area – ring visual only, no event connections needed */
	bw->volume_ring = gtk_drawing_area_new();
	gtk_widget_set_size_request(bw->volume_ring, RING_SIZE, RING_SIZE);
	gtk_widget_set_halign(bw->volume_ring, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->volume_ring, GTK_ALIGN_CENTER);
	g_signal_connect(bw->volume_ring, "draw", G_CALLBACK(on_volume_ring_draw), state);
	gtk_container_add(GTK_CONTAINER(overlay), bw->volume_ring);

	/* Label – windowless, so EventBox above_child captures all clicks cleanly */
	bw->volume_btn = gtk_label_new("");
	gtk_widget_set_halign(bw->volume_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->volume_btn, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->volume_btn), "volume-btn");
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bw->volume_btn);

	gtk_container_add(GTK_CONTAINER(evbox), overlay);
	return evbox;
}

/* ── Nightlight ─────────────────────────────────────────────────────────── */
/* Functions moved to nightlight.c */

static gboolean on_brightness_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
	(void)widget;
	BarWindow *bw = (BarWindow *)data;
	AppState *state = bw->state;
	double delta = compute_scroll_delta(event, BRIGHTNESS_SCROLL_STEP);
	if (delta == 0)
		return TRUE;
	pthread_mutex_lock(&state->mutex);
	state->sys_data.brightness = CLAMP(state->sys_data.brightness + delta, 0.0, 100.0);
	double bright = state->sys_data.brightness;
	pthread_mutex_unlock(&state->mutex);
	char cmd[64];
	snprintf(cmd, sizeof(cmd), BRIGHTNESSCTL_SET_FMT, bright);
	g_spawn_command_line_async(cmd, NULL);
	g_idle_add(trigger_brightness_popup_idle, bw);
	update_widgets_idle(state);
	return TRUE;
}

static gboolean on_brightness_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	(void)widget;
	(void)data;
	if (event->button == 1) {
		/* Show menu on left click? No, usually it's just scroll or right click for settings */
	}
	return TRUE;
}

static gboolean on_brightness_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	double bright = state->sys_data.visual_brightness;
	pthread_mutex_unlock(&state->mutex);
	return on_ring_draw_generic(widget, cr, bright, state);
}

GtkWidget *widget_brightness(BarWindow *bw, AppState *state) {
	GtkWidget *overlay = gtk_overlay_new();
	GtkWidget *btn = gtk_label_new(ICON_BRIGHTNESS);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "nightlight-btn"); // Reuse nightlight-btn style
	bw->brightness_btn = btn;

	GtkWidget *ring = gtk_drawing_area_new();
	gtk_widget_set_size_request(ring, RING_SIZE, RING_SIZE);
	g_signal_connect(ring, "draw", G_CALLBACK(on_brightness_ring_draw), state);
	bw->brightness_ring = ring;

	GtkWidget *ev = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ev), FALSE);
	gtk_container_add(GTK_CONTAINER(ev), overlay);
	gtk_widget_add_events(ev, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
	g_signal_connect(ev, "scroll-event", G_CALLBACK(on_brightness_scroll), bw);
	g_signal_connect(ev, "button-press-event", G_CALLBACK(on_brightness_click), state);

	gtk_container_add(GTK_CONTAINER(overlay), btn);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ring);

	return ev;
}

static gboolean on_nightlight_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	int nl_level = state->sys_data.nightlight_level;
	pthread_mutex_unlock(&state->mutex);
	return on_ring_draw_generic(widget, cr, nl_level, state);
}

static gboolean on_nightlight_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
	(void)widget;
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	double delta = compute_scroll_delta(event, state->config.nightlight.step);

	int old_level = state->sys_data.nightlight_level;
	state->sys_data.nightlight_level = (int)CLAMP(old_level + delta, 0, 100);
	int new_level = state->sys_data.nightlight_level;
	state->sys_data.nightlight_on = (new_level > 0);
	if (new_level > 0) {
		state->sys_data.nightlight_last_level = new_level;
		nightlight_save_state(1, new_level);
	} else {
		nightlight_save_state(0, state->sys_data.nightlight_last_level);
	}
	pthread_mutex_unlock(&state->mutex);

	if (new_level > 0)
		nightlight_apply(state);
	else if (old_level > 0)
		nightlight_reset(state);

	update_widgets_idle(state);
	return TRUE;
}

static gboolean on_nightlight_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	(void)widget;
	if (event->button != 1)
		return TRUE;
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	if (state->sys_data.nightlight_level > 0) {
		state->sys_data.nightlight_level = 0;
		state->sys_data.nightlight_on = 0;
		int last = state->sys_data.nightlight_last_level;
		pthread_mutex_unlock(&state->mutex);
		nightlight_save_state(0, last);
		nightlight_reset(state);
	} else {
		int last = state->sys_data.nightlight_last_level;
		if (last <= 0) {
			int dummy_active = 0;
			last = nightlight_load_state(&dummy_active);
		}
		if (last <= 0)
			last = NIGHTLIGHT_DEFAULT_LEVEL;
		state->sys_data.nightlight_level = last;
		state->sys_data.nightlight_on = 1;
		pthread_mutex_unlock(&state->mutex);
		nightlight_save_state(1, last);
		nightlight_apply(state);
	}
	update_widgets_idle(state);
	return TRUE;
}

GtkWidget *widget_nightlight(BarWindow *bw, AppState *state) {
	pthread_mutex_lock(&state->mutex);
	if (state->sys_data.nightlight_last_level <= 0) {
		int dummy_active = 0;
		state->sys_data.nightlight_last_level = nightlight_load_state(&dummy_active);
	}
	pthread_mutex_unlock(&state->mutex);

	GtkWidget *evbox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(evbox), TRUE);
	gtk_widget_set_valign(evbox, GTK_ALIGN_CENTER);
	gtk_widget_add_events(evbox, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect(evbox, "scroll-event", G_CALLBACK(on_nightlight_scroll), state);
	g_signal_connect(evbox, "button-press-event", G_CALLBACK(on_nightlight_click), state);
	g_signal_connect(evbox, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
	g_signal_connect(evbox, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);

	GtkWidget *overlay = gtk_overlay_new();

	bw->nightlight_ring = gtk_drawing_area_new();
	gtk_widget_set_size_request(bw->nightlight_ring, RING_SIZE, RING_SIZE);
	gtk_widget_set_halign(bw->nightlight_ring, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->nightlight_ring, GTK_ALIGN_CENTER);
	g_signal_connect(bw->nightlight_ring, "draw", G_CALLBACK(on_nightlight_ring_draw), state);
	gtk_container_add(GTK_CONTAINER(overlay), bw->nightlight_ring);

	bw->nightlight_btn = gtk_label_new("\xf0\xb0\x85\x99"); /* 󰖙 sun – off by default, windowless */
	gtk_widget_set_halign(bw->nightlight_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->nightlight_btn, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->nightlight_btn), "nightlight-btn");
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bw->nightlight_btn);

	gtk_container_add(GTK_CONTAINER(evbox), overlay);
	return evbox;
}

/* nightlight_init moved to nightlight.c */

/* ── Bluetooth ─────────────────────────────────────────────────────────── */

static gboolean on_bluetooth_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	int powered = state->sys_data.bluetooth_powered;
	pthread_mutex_unlock(&state->mutex);
	return on_ring_draw_generic(widget, cr, powered ? 100.0 : 0.0, state);
}

static gboolean on_bluetooth_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	(void)widget;
	if (event->button != 1)
		return TRUE;
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	int powered = state->sys_data.bluetooth_powered;
	pthread_mutex_unlock(&state->mutex);
	bluetooth_set_powered(!powered);
	fetch_bluetooth(state);
	update_widgets_idle(state);
	return TRUE;
}

GtkWidget *widget_bluetooth(BarWindow *bw, AppState *state) {
	(void)state;
	GtkWidget *evbox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(evbox), TRUE);
	gtk_widget_set_valign(evbox, GTK_ALIGN_CENTER);
	gtk_widget_add_events(evbox, GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect(evbox, "button-press-event", G_CALLBACK(on_bluetooth_click), state);
	g_signal_connect(evbox, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
	g_signal_connect(evbox, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);

	GtkWidget *overlay = gtk_overlay_new();

	bw->bluetooth_ring = gtk_drawing_area_new();
	gtk_widget_set_size_request(bw->bluetooth_ring, RING_SIZE, RING_SIZE);
	gtk_widget_set_halign(bw->bluetooth_ring, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->bluetooth_ring, GTK_ALIGN_CENTER);
	g_signal_connect(bw->bluetooth_ring, "draw", G_CALLBACK(on_bluetooth_ring_draw), state);
	gtk_container_add(GTK_CONTAINER(overlay), bw->bluetooth_ring);

	bw->bluetooth_btn = gtk_label_new(ICON_BLUETOOTH_OFF);
	gtk_widget_set_halign(bw->bluetooth_btn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(bw->bluetooth_btn, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->bluetooth_btn), "bluetooth-btn");
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bw->bluetooth_btn);

	gtk_container_add(GTK_CONTAINER(evbox), overlay);
	return evbox;
}

static void on_launcher_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	const char *action = (const char *)data;
	if (!action || !*action)
		return;
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", action, NULL);
		exit(1);
	}
}

GtkWidget *widget_launcher(BarWindow *bw, AppState *state) {
	(void)bw;
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "launcher");

	for (int i = 0; i < state->config.launcher.count; i++) {
		GtkWidget *btn = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-app-btn");

		GError *err = NULL;
		GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(state->config.launcher.apps[i].icon_path, 36, 36, TRUE, &err);
		GtkWidget *img;
		if (pix) {
			img = gtk_image_new_from_pixbuf(pix);
			g_object_unref(pix);
		} else {
			img = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DND);
			if (err)
				g_error_free(err);
		}
		gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(img, GTK_ALIGN_CENTER);

		gtk_container_add(GTK_CONTAINER(btn), img);
		g_signal_connect(btn, "clicked", G_CALLBACK(on_launcher_clicked), state->config.launcher.apps[i].action);

		gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
	}
	return box;
}

static GtkWidget *create_metric_box(const char *icon_str, GtkWidget **out_widget, int use_bars) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	GtkWidget *icon = gtk_label_new(icon_str);
	gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon), "metric-icon");
	if (use_bars) {
		*out_widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
		gtk_scale_set_draw_value(GTK_SCALE(*out_widget), FALSE);
		/* (has_origin defaults to TRUE, so GTK draws the 'highlight' fill correctly from 0) */
		gtk_widget_set_can_focus(*out_widget, FALSE);
		gtk_widget_set_size_request(*out_widget, 55, 10);
		gtk_style_context_add_class(gtk_widget_get_style_context(*out_widget), "metric-scale");
	} else {
		*out_widget = gtk_label_new("0%");
		gtk_style_context_add_class(gtk_widget_get_style_context(*out_widget), "metric-label");
	}
	gtk_widget_set_valign(*out_widget, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), *out_widget, FALSE, FALSE, 0);
	return box;
}

GtkWidget *widget_metrics(BarWindow *bw, AppState *state) {
	GtkWidget *grid = gtk_grid_new();
	gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);
	const char *icons[] = {ICON_METRIC_RAM, ICON_METRIC_CPU, ICON_METRIC_GPU, ICON_METRIC_DISK, ICON_METRIC_TEMP, ICON_METRIC_TEMP};
	for (int r = 0; r < 2; r++) {
		for (int c = 0; c < 3; c++) {
			MetricType type = state->config.metrics.layout[r][c];
			if (type != M_NONE && type < 6) {
				gtk_grid_attach(GTK_GRID(grid), create_metric_box(icons[type], &bw->metrics_widgets[type], state->config.metrics.use_bars),
								c, r, 1, 1);
			}
		}
	}
	gtk_style_context_add_class(gtk_widget_get_style_context(grid), "metrics");
	return grid;
}

void update_workspace_display(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->cb_desk_label) {
			char buf[64];
			snprintf(buf, sizeof(buf), "Desk %d", state->active_workspace);
			gtk_label_set_text(GTK_LABEL(bw->cb_desk_label), buf);
			gtk_widget_queue_draw(bw->cb_desk_label);
		}
		for (int i = 0; i < state->config.workspaces.count; i++) {
			if (!bw->ws_labels[i])
				continue;
			int ws_id = i + 1;
			int occupied = (state->ws_win_count[ws_id] > 0);
			const char *symbol = occupied ? state->config.workspaces.icon_occupied : state->config.workspaces.icon_empty;
			gtk_label_set_text(GTK_LABEL(bw->ws_labels[i]), symbol);
			GtkStyleContext *context = gtk_widget_get_style_context(bw->ws_labels[i]);
			gtk_style_context_remove_class(context, "workspace-active");
			gtk_style_context_remove_class(context, "workspace-occupied");
			if (ws_id == state->active_workspace)
				gtk_style_context_add_class(context, "workspace-active");
			else if (occupied)
				gtk_style_context_add_class(context, "workspace-occupied");

			if (!occupied && !state->config.workspaces.show_empty && ws_id != state->active_workspace)
				gtk_widget_hide(bw->ws_labels[i]);
			else
				gtk_widget_show(bw->ws_labels[i]);
			gtk_widget_queue_draw(bw->ws_labels[i]);
		}
		gtk_widget_queue_draw(bw->window);
	}
	pthread_mutex_unlock(&state->mutex);
}

void update_metric_widget(GtkWidget *widget, MetricType type, SystemData *d, int use_bars) {
	if (!widget)
		return;
	char buf[64];
	if (use_bars) {
		double val = 0;
		switch (type) {
			case M_RAM:
				val = d->ram_val;
				break;
			case M_CPU:
				val = d->cpu_val;
				break;
			case M_GPU:
				val = d->gpu_val;
				break;
			case M_DISK:
				val = d->disk_val;
				break;
			case M_TEMP:
				val = d->temp_val;
				break;
			case M_GPU_TEMP:
				val = d->gpu_temp_val;
				break;
			default:
				return;
		}
		gtk_range_set_value(GTK_RANGE(widget), val);
	} else {
		switch (type) {
			case M_RAM:
				snprintf(buf, sizeof(buf), "%.1f/%.1f", d->ram_total - d->ram_avail, d->ram_total);
				break;
			case M_CPU:
				snprintf(buf, sizeof(buf), "%.0f%%", d->cpu_val);
				break;
			case M_GPU:
				snprintf(buf, sizeof(buf), "%.0f%%", d->gpu_val);
				break;
			case M_DISK:
				snprintf(buf, sizeof(buf), "%.0f%%", d->disk_val);
				break;
			case M_TEMP:
				snprintf(buf, sizeof(buf), "%.0f°C", d->temp_val);
				break;
			case M_GPU_TEMP:
				snprintf(buf, sizeof(buf), "%.0f°C", d->gpu_temp_val);
				break;
			default:
				return;
		}
		gtk_label_set_text(GTK_LABEL(widget), buf);
	}
	gtk_widget_queue_draw(widget);
}

const char *get_wifi_icon(int q) {
	if (q < 0)
		return ICON_WIFI_0;
	if (q == 0)
		return ICON_WIFI_0;
	if (q < 25)
		return ICON_WIFI_1;
	if (q < 50)
		return ICON_WIFI_2;
	if (q < 75)
		return ICON_WIFI_3;
	return ICON_WIFI_4;
}

const char *get_battery_icon(int cap, int charging) {
	/* Index 0 = ≤10%, index 9 = 91-100% */
	static const char *discharging[10] = {
		ICON_BATTERY_10, ICON_BATTERY_20, ICON_BATTERY_30, ICON_BATTERY_40, ICON_BATTERY_50,
		ICON_BATTERY_60, ICON_BATTERY_70, ICON_BATTERY_80, ICON_BATTERY_90, ICON_BATTERY_100,
	};
	static const char *charging_icons[10] = {
		ICON_BATTERY_CHG_10, ICON_BATTERY_CHG_20, ICON_BATTERY_CHG_30, ICON_BATTERY_CHG_40, ICON_BATTERY_CHG_50,
		ICON_BATTERY_CHG_60, ICON_BATTERY_CHG_70, ICON_BATTERY_CHG_80, ICON_BATTERY_CHG_90, ICON_BATTERY_CHG_100,
	};
	if (cap < 0)
		return ICON_BATTERY_UNKNOWN; /* no battery present */
	/* Use a balanced round-to-nearest mapping:
	 * >= 95 -> idx 9 (100%)
	 * 85-94 -> idx 8 (90%)
	 * 75-84 -> idx 7 (80%)
	 * 65-74 -> idx 6 (70%)
	 * 55-64 -> idx 5 (60%)
	 * 45-54 -> idx 4 (50%)
	 * 35-44 -> idx 3 (40%)
	 * 25-34 -> idx 2 (30%)
	 * 15-24 -> idx 1 (20%)
	 * < 15  -> idx 0 (10%)
	 */
	int idx = (cap + 5) / 10 - 1;
	if (idx >= 10)
		idx = 9;
	if (idx < 0)
		idx = 0;
	return charging ? charging_icons[idx] : discharging[idx];
}

static SystemData last_d;
static int last_d_init = 0;
static time_t last_time = 0;
static int last_active_ws = -1;
static int last_ws_count[MAX_WORKSPACES + 1];
static int last_ws_count_init = 0;

void update_widgets_idle_reset(void) {
	last_d_init = 0;
	last_ws_count_init = 0;
}

gboolean update_widgets_idle(gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	SystemData d = state->sys_data;
	int active_workspace = state->active_workspace;
	int ws_win_count[MAX_WORKSPACES + 1];
	memcpy(ws_win_count, state->ws_win_count, sizeof(ws_win_count));
	pthread_mutex_unlock(&state->mutex);

	time_t now = time(NULL);

	if (!last_d_init) {
		memset(&last_d, 0, sizeof(last_d));
		last_d.vol = -1;
		last_d.vol_muted = -1;
		last_d.visual_brightness = -1;
		last_d.nightlight_level = -1;
		last_d_init = 1;
	}
	if (!last_ws_count_init) {
		memset(last_ws_count, -1, sizeof(last_ws_count));
		last_ws_count_init = 1;
	}

	struct tm tmv = *localtime(&now);
	char tstr[64], dstr[64];

	// Check if time components changed (hour/minute/date)
	int time_changed = (now / 60 != last_time / 60);
	last_time = now;

	if (time_changed) {
		strftime(tstr, sizeof(tstr), state->config.clock.time_format, &tmv);
		strftime(dstr, sizeof(dstr), state->config.clock.date_format, &tmv);
	}

	int bat_changed = (d.bat_percent != last_d.bat_percent || d.bat_charging != last_d.bat_charging ||
					   strcmp(d.bat_time_remaining, last_d.bat_time_remaining) != 0);
	int wifi_changed =
		(d.wifi_enabled != last_d.wifi_enabled || d.wifi_connected != last_d.wifi_connected || d.wifi_strength != last_d.wifi_strength ||
		 strcmp(d.wifi_ssid, last_d.wifi_ssid) != 0 || d.wifi_adapter_exists != last_d.wifi_adapter_exists);
	int kb_changed = (strcmp(d.kb_layout, last_d.kb_layout) != 0);

	int metrics_changed = (d.ram_val != last_d.ram_val || d.cpu_val != last_d.cpu_val || d.disk_val != last_d.disk_val ||
						   d.temp_val != last_d.temp_val || d.gpu_val != last_d.gpu_val || d.gpu_temp_val != last_d.gpu_temp_val);

	int vol_changed = (d.vol != last_d.vol || d.vol_muted != last_d.vol_muted || d.visual_volume != last_d.visual_volume);
	int brightness_changed = (d.visual_brightness != last_d.visual_brightness);
	int nightlight_changed = (d.nightlight_level != last_d.nightlight_level || d.nightlight_error != last_d.nightlight_error ||
							  d.nightlight_retrying != last_d.nightlight_retrying);
	int bluetooth_changed = (d.bluetooth_powered != last_d.bluetooth_powered || d.bluetooth_connected != last_d.bluetooth_connected);
	int media_changed = (d.is_playing != last_d.is_playing || strcmp(d.media_title, last_d.media_title) != 0 ||
						 strcmp(d.media_artist, last_d.media_artist) != 0);

	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;

		if (time_changed) {
			if (bw->clock_time_label)
				gtk_label_set_text(GTK_LABEL(bw->clock_time_label), tstr);
			if (bw->clock_date_label)
				gtk_label_set_text(GTK_LABEL(bw->clock_date_label), dstr);
		}

		if (metrics_changed) {
			for (int i = 0; i < 6; i++)
				update_metric_widget(bw->metrics_widgets[i], (MetricType)i, &d, state->config.metrics.use_bars);
		}

		if (vol_changed) {
			const char *vicon = get_volume_icon(d.vol, d.vol_muted);
			char vstr[32];
			if (d.vol_muted)
				snprintf(vstr, sizeof(vstr), "%s%s", vicon, state->config.volume.show_percent ? " Muted" : "");
			else if (state->config.volume.show_percent)
				snprintf(vstr, sizeof(vstr), "%s %.0f%%", vicon, d.vol);
			else
				snprintf(vstr, sizeof(vstr), "%s", vicon);
			if (bw->volume_btn) {
				gtk_label_set_text(GTK_LABEL(bw->volume_btn), vstr);
				GtkStyleContext *vctx = gtk_widget_get_style_context(bw->volume_btn);
				if (d.vol > 66 && !d.vol_muted)
					gtk_style_context_add_class(vctx, "vol-high");
				else
					gtk_style_context_remove_class(vctx, "vol-high");
			}
			if (bw->volume_ring)
				gtk_widget_queue_draw(bw->volume_ring);
		}

		if (brightness_changed) {
			if (bw->brightness_ring)
				gtk_widget_queue_draw(bw->brightness_ring);
		}

		if (nightlight_changed) {
			/* Nightlight – sun (off) / moon (on) */
			int nl_error = d.nightlight_error;
			int nl_retrying = d.nightlight_retrying;
			int nl_active = (d.nightlight_level > 0 || d.nightlight_on);
			const char *nl_icon = nl_active ? ICON_NIGHTLIGHT_ON : ICON_NIGHTLIGHT_OFF;

			if (bw->nightlight_btn) {
				gtk_label_set_text(GTK_LABEL(bw->nightlight_btn), nl_icon);
				GtkStyleContext *nlctx = gtk_widget_get_style_context(bw->nightlight_btn);
				if (nl_active && !nl_error) {
					gtk_style_context_add_class(nlctx, "nightlight-on");
					gtk_style_context_remove_class(nlctx, "nightlight-off");
				} else {
					gtk_style_context_remove_class(nlctx, "nightlight-on");
					gtk_style_context_add_class(nlctx, "nightlight-off");
				}
				if (nl_retrying) {
					gtk_style_context_add_class(nlctx, "nightlight-retrying");
					gtk_style_context_remove_class(nlctx, "nightlight-error");
				} else if (nl_error) {
					gtk_style_context_add_class(nlctx, "nightlight-error");
					gtk_style_context_remove_class(nlctx, "nightlight-retrying");
				} else {
					gtk_style_context_remove_class(nlctx, "nightlight-error");
					gtk_style_context_remove_class(nlctx, "nightlight-retrying");
				}
			}
			if (bw->nightlight_ring)
				gtk_widget_queue_draw(bw->nightlight_ring);
		}

		if (bluetooth_changed) {
			int bt_on = d.bluetooth_powered;
			const char *bt_icon = bt_on ? ICON_BLUETOOTH_ON : ICON_BLUETOOTH_OFF;

			if (bw->bluetooth_btn) {
				gtk_label_set_text(GTK_LABEL(bw->bluetooth_btn), bt_icon);
				GtkStyleContext *btctx = gtk_widget_get_style_context(bw->bluetooth_btn);
				if (bt_on) {
					gtk_style_context_add_class(btctx, "bluetooth-on");
					gtk_style_context_remove_class(btctx, "bluetooth-off");
				} else {
					gtk_style_context_remove_class(btctx, "bluetooth-on");
					gtk_style_context_add_class(btctx, "bluetooth-off");
				}
			}
			if (bw->bluetooth_ring)
				gtk_widget_queue_draw(bw->bluetooth_ring);
		}

		if (media_changed) {
			if (bw->media_play_btn)
				gtk_button_set_label(GTK_BUTTON(bw->media_play_btn), d.is_playing ? ICON_MEDIA_PAUSE : ICON_MEDIA_PLAY);
			int has_media = (d.media_title[0] != '\0');
			int show_title = has_media && state->config.media.show_title;
			int show_artist = has_media && state->config.media.show_artist && d.media_artist[0] != '\0';
			/* Separator is only visible when media is playing AND at least one text line shows */
			if (bw->media_sep)
				gtk_widget_set_visible(bw->media_sep, show_title || show_artist);

			if (bw->media_title_label) {
				gtk_label_set_text(GTK_LABEL(bw->media_title_label), d.media_title);
				gtk_widget_set_visible(bw->media_title_label, show_title);
				gtk_label_set_max_width_chars(GTK_LABEL(bw->media_title_label), state->config.media.max_title_width / 8);
			}
			if (bw->media_artist_label) {
				gtk_label_set_text(GTK_LABEL(bw->media_artist_label), d.media_artist);
				gtk_widget_set_visible(bw->media_artist_label, show_artist);
			}
		}

		/* ChromeOS tray labels, popups and menu sliders */
		chromeos_update_tray(state, bw, &d, time_changed, wifi_changed, bat_changed, kb_changed, vol_changed, brightness_changed, now,
							 &tmv);
	}

	int ws_changed = (active_workspace != last_active_ws || memcmp(ws_win_count, last_ws_count, sizeof(ws_win_count)) != 0);
	if (ws_changed) {
		update_workspace_display(state);
		last_active_ws = active_workspace;
		memcpy(last_ws_count, ws_win_count, sizeof(ws_win_count));
	}

	last_d = d;

	extern void ensure_anim_timer(AppState * state);
	ensure_anim_timer(state);

	return G_SOURCE_REMOVE;
}

/* Called by GLib periodic timer – must return G_SOURCE_CONTINUE to keep firing */
gboolean timer_update_widgets(gpointer data) {
	update_widgets_idle(data);
	return G_SOURCE_CONTINUE;
}
