#include "chromeos_menu_internal.h"
#include "nightlight.h"
#include "pulse.h"
#include "util.h"
#include "widgets.h"
#include <math.h>

/* nightlight_save_state, nightlight_load_state, nightlight_ipc,
 * nightlight_apply, nightlight_reset, nightlight_set_level
 * moved to nightlight.c */

static void on_vol_scale_changed(GtkRange *range, gpointer data) {
	MenuCtx *ctx = (MenuCtx *)data;
	if (slider_is_updating(range))
		return;
	handle_volume_slider_changed(range, ctx ? ctx->state : NULL);
}

static void on_mute_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	if (!ctx || !ctx->state)
		return;

	AppState *state = ctx->state;
	pthread_mutex_lock(&state->mutex);
	state->last_manual_vol_update = time(NULL);
	int muted = state->sys_data.vol_muted;
	state->sys_data.vol_muted = !muted;
	pthread_mutex_unlock(&state->mutex);

	pulse_set_mute(state, !muted);
	GtkStyleContext *style = gtk_widget_get_style_context(widget);
	if (muted)
		gtk_style_context_remove_class(style, "cb-menu-slider-btn-active");
	else
		gtk_style_context_add_class(style, "cb-menu-slider-btn-active");
	update_widgets_idle(state);
}

static void on_bright_scale_changed(GtkRange *range, gpointer data) {
	MenuCtx *ctx = (MenuCtx *)data;
	if (slider_is_updating(range))
		return;
	handle_brightness_slider_changed(range, ctx ? ctx->state : NULL);
}

static void on_nightlight_scale_changed(GtkRange *range, gpointer data) {
	MenuCtx *ctx = (MenuCtx *)data;
	AppState *state = ctx ? ctx->state : NULL;
	if (!state || slider_is_updating(range))
		return;

	int level = (int)(slider_get_actual_value(range) + 0.5);
	nightlight_set_level(state, level);
}

void chromeos_menu_on_nightlight_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	AppState *state = ctx->state;

	pthread_mutex_lock(&state->mutex);
	int level = state->sys_data.nightlight_level;
	int last = state->sys_data.nightlight_last_level;
	pthread_mutex_unlock(&state->mutex);

	if (level > 0) {
		nightlight_set_level(state, 0);
	} else {
		if (last <= 0) {
			int dummy_active = 0;
			last = nightlight_load_state(&dummy_active);
		}
		if (last <= 0)
			last = 15;
		nightlight_set_level(state, last);
	}

	chromeos_menu_show_main(ctx->bw, state);
}

void chromeos_menu_on_nightlight_arrow_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	chromeos_menu_show_nightlight(ctx->bw, ctx->state);
}

static void on_volume_arrow_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = (MenuCtx *)data;
	chromeos_menu_show_volume(ctx->bw, ctx->state);
}

GtkWidget *chromeos_menu_create_volume_slider(MenuCtx *ctx) {
	pthread_mutex_lock(&ctx->state->mutex);
	double vol = ctx->state->sys_data.vol;
	gboolean muted = ctx->state->sys_data.vol_muted;
	pthread_mutex_unlock(&ctx->state->mutex);

	GtkWidget *scale = NULL;
	GtkWidget *box = create_menu_slider("󰕾", "󰝟", vol, G_CALLBACK(on_vol_scale_changed), G_CALLBACK(on_mute_clicked),
										G_CALLBACK(on_volume_arrow_clicked), ctx, muted, &scale);
	if (ctx->bw)
		ctx->bw->cb_menu_volume_slider = scale;
	return box;
}

GtkWidget *chromeos_menu_create_brightness_nightlight_slider(MenuCtx *ctx) {
	pthread_mutex_lock(&ctx->state->mutex);
	gboolean nightlight_active = ctx->state->sys_data.nightlight_level > 0;
	float bright = ctx->state->sys_data.brightness;
	pthread_mutex_unlock(&ctx->state->mutex);

	GtkWidget *scale = NULL;
	GtkWidget *box =
		create_menu_slider("󰃟", "", bright, G_CALLBACK(on_bright_scale_changed), G_CALLBACK(chromeos_menu_on_nightlight_clicked),
						   G_CALLBACK(chromeos_menu_on_nightlight_arrow_clicked), ctx, nightlight_active, &scale);
	if (ctx->bw)
		ctx->bw->cb_menu_brightness_slider = scale;
	return box;
}

void chromeos_menu_show_nightlight(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);

	chromeos_menu_create_subpage_header(bw, state, "Night Light");

	pthread_mutex_lock(&state->mutex);
	int level = state->sys_data.nightlight_level;
	float bright = state->sys_data.brightness;
	pthread_mutex_unlock(&state->mutex);

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(content), "cb-menu-content");
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), content, TRUE, TRUE, 0);

	MenuCtx *slider_ctx = g_new0(MenuCtx, 1);
	slider_ctx->bw = bw;
	slider_ctx->state = state;
	g_object_set_data_full(G_OBJECT(content), "nightlight-slider-ctx", slider_ctx, g_free);

	GtkWidget *bright_scale = NULL;
	gtk_box_pack_start(
		GTK_BOX(content),
		create_menu_slider("󰃟", NULL, bright, G_CALLBACK(on_bright_scale_changed), NULL, NULL, slider_ctx, FALSE, &bright_scale), FALSE,
		FALSE, 0);
	bw->cb_menu_brightness_slider = bright_scale;

	gtk_box_pack_start(
		GTK_BOX(content),
		create_menu_slider("󰖔", NULL, level, G_CALLBACK(on_nightlight_scale_changed), NULL, NULL, slider_ctx, FALSE, NULL), FALSE, FALSE,
		0);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

void chromeos_menu_show_volume(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);

	chromeos_menu_create_subpage_header(bw, state, "Volume");

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(content), "cb-menu-content");
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), content, TRUE, TRUE, 0);

	MenuCtx *slider_ctx = g_new0(MenuCtx, 1);
	slider_ctx->bw = bw;
	slider_ctx->state = state;
	g_object_set_data_full(G_OBJECT(content), "volume-slider-ctx", slider_ctx, g_free);

	pthread_mutex_lock(&state->mutex);
	double vol = state->sys_data.vol;
	gboolean muted = state->sys_data.vol_muted;
	pthread_mutex_unlock(&state->mutex);

	GtkWidget *vol_scale = NULL;
	gtk_box_pack_start(GTK_BOX(content),
					   create_menu_slider("󰕾", "󰝟", vol, G_CALLBACK(on_vol_scale_changed), G_CALLBACK(on_mute_clicked), NULL,
										  slider_ctx, muted, &vol_scale),
					   FALSE, FALSE, 0);
	bw->cb_menu_volume_slider = vol_scale;

	gtk_widget_show_all(bw->cb_menu_main_box);
}
