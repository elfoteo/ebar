#include "chromeos_menu_internal.h"
#include "widgets.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void nightlight_save_state(int level) {
	if (level <= 0)
		return;
	FILE *f = fopen("/tmp/ebar_nightlight", "w");
	if (f) {
		fprintf(f, "%d", level);
		fclose(f);
	}
}

static int nightlight_load_state(void) {
	FILE *f = fopen("/tmp/ebar_nightlight", "r");
	if (!f)
		return 0;

	int level = 0;
	if (fscanf(f, "%d", &level) != 1)
		level = 0;
	fclose(f);
	return level;
}

static int nightlight_ipc(const char *cmd) {
	const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (!sig)
		return -1;
	if (!run)
		run = "/run/user/1000";

	char path[256];
	snprintf(path, sizeof(path), "%s/hypr/%s/.hyprsunset.sock", run, sig);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}

	char msg[128];
	int len = snprintf(msg, sizeof(msg), "%s\n", cmd);
	write(fd, msg, len);
	char buf[64];
	read(fd, buf, sizeof(buf));
	close(fd);
	return 0;
}

static void nightlight_apply(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	int level = state->sys_data.nightlight_level;
	double t_max = state->config.nightlight.temp_max;
	double t_min = state->config.nightlight.temp_min;
	double g_max = state->config.nightlight.gamma_max;
	double g_min = state->config.nightlight.gamma_min;
	char curve[16];
	strncpy(curve, state->config.nightlight.curve, sizeof(curve) - 1);
	curve[sizeof(curve) - 1] = '\0';
	pthread_mutex_unlock(&state->mutex);

	double t = level / 100.0;
	if (strcmp(curve, "linear") != 0)
		t = t * t * (3.0 - 2.0 * t);

	int temp = (int)(t_max - (t_max - t_min) * t);
	double gamma = g_max - (g_max - g_min) * t;

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "temperature %d", temp);
	int ok1 = nightlight_ipc(cmd);
	snprintf(cmd, sizeof(cmd), "gamma %.0f", gamma);
	int ok2 = nightlight_ipc(cmd);

	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_error = (ok1 < 0 || ok2 < 0) ? 1 : 0;
	pthread_mutex_unlock(&state->mutex);
}

static void nightlight_reset(AppState *state) {
	int ok1 = nightlight_ipc("temperature 6500");
	int ok2 = nightlight_ipc("gamma 100");

	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_error = (ok1 < 0 || ok2 < 0) ? 1 : 0;
	pthread_mutex_unlock(&state->mutex);
}

static void nightlight_set_level(AppState *state, int level) {
	level = CLAMP(level, 0, 100);

	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_level = level;
	state->sys_data.nightlight_on = level > 0;
	if (level > 0) {
		state->sys_data.nightlight_last_level = level;
		nightlight_save_state(level);
	}
	pthread_mutex_unlock(&state->mutex);

	if (level > 0)
		nightlight_apply(state);
	else
		nightlight_reset(state);
	update_widgets_idle(state);
}

static void update_slider_minimum_state(GtkRange *range) {
	GtkStyleContext *scale_ctx = gtk_widget_get_style_context(GTK_WIDGET(range));
	GtkWidget *icon = g_object_get_data(G_OBJECT(range), "slider-icon");
	GtkStyleContext *icon_ctx = icon ? gtk_widget_get_style_context(icon) : NULL;
	gboolean minimum = slider_get_actual_value(range) <= 0.5;

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

static void on_slider_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer data) {
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
	update_slider_minimum_state(range);
}

GtkWidget *create_menu_slider(const char *icon, const char *right_icon, double initial_val, GCallback on_changed,
									 GCallback on_right_clicked, GCallback on_arrow_clicked, gpointer user_data,
									 gboolean right_active, GtkWidget **scale_out) {
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
	g_signal_connect(scale, "value-changed", G_CALLBACK(on_slider_value_changed), NULL);
	g_signal_connect(scale, "size-allocate", G_CALLBACK(on_slider_size_allocate), NULL);
	if (on_changed)
		g_signal_connect(scale, "value-changed", on_changed, user_data);
	update_slider_minimum_state(GTK_RANGE(scale));

	gtk_container_add(GTK_CONTAINER(overlay), scale);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), icon_lbl);

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
		GtkWidget *arrow_btn = gtk_button_new_with_label("");
		gtk_widget_set_valign(arrow_btn, GTK_ALIGN_CENTER);
		gtk_style_context_add_class(gtk_widget_get_style_context(arrow_btn), "cb-menu-slider-arrow");
		g_signal_connect(arrow_btn, "clicked", on_arrow_clicked, user_data);
		gtk_box_pack_start(GTK_BOX(box), arrow_btn, FALSE, FALSE, 0);
	}

	return box;
}

static void on_vol_scale_changed(GtkRange *range, gpointer data) {
	MenuCtx *ctx = (MenuCtx *)data;
	AppState *state = ctx ? ctx->state : NULL;
	if (slider_is_updating(range))
		return;
	double val = slider_get_actual_value(range);
	if (state) {
		pthread_mutex_lock(&state->mutex);
		state->last_manual_vol_update = time(NULL);
		state->sys_data.vol = val;
		if (val > 0.5)
			state->sys_data.vol_muted = 0;
		pthread_mutex_unlock(&state->mutex);
	}
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %.0f%%", val);
	g_spawn_command_line_async(cmd, NULL);
	if (state)
		update_widgets_idle(state);
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

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "pactl set-sink-mute @DEFAULT_SINK@ %d", muted ? 0 : 1);
	g_spawn_command_line_async(cmd, NULL);
	GtkStyleContext *style = gtk_widget_get_style_context(widget);
	if (muted)
		gtk_style_context_remove_class(style, "cb-menu-slider-btn-active");
	else
		gtk_style_context_add_class(style, "cb-menu-slider-btn-active");
	update_widgets_idle(state);
}

static void on_bright_scale_changed(GtkRange *range, gpointer data) {
	MenuCtx *ctx = (MenuCtx *)data;
	AppState *state = ctx ? ctx->state : NULL;
	if (slider_is_updating(range))
		return;
	double val = slider_get_actual_value(range);
	if (state) {
		pthread_mutex_lock(&state->mutex);
		state->sys_data.brightness = (float)val;
		state->last_manual_bright_update = time(NULL);
		pthread_mutex_unlock(&state->mutex);
	}
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "brightnessctl set %.0f%%", val);
	g_spawn_command_line_async(cmd, NULL);
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
		if (last <= 0)
			last = nightlight_load_state();
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
	GtkWidget *box = create_menu_slider("󰃟", "", bright, G_CALLBACK(on_bright_scale_changed),
										G_CALLBACK(chromeos_menu_on_nightlight_clicked),
										G_CALLBACK(chromeos_menu_on_nightlight_arrow_clicked), ctx, nightlight_active, &scale);
	if (ctx->bw)
		ctx->bw->cb_menu_brightness_slider = scale;
	return box;
}

void chromeos_menu_show_nightlight(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(chromeos_menu_on_back_to_main_clicked), ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title = gtk_label_new("Night Light");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

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
	gtk_box_pack_start(GTK_BOX(content),
					   create_menu_slider("󰃟", NULL, bright, G_CALLBACK(on_bright_scale_changed), NULL, NULL,
										  slider_ctx, FALSE, &bright_scale),
					   FALSE, FALSE, 0);
	bw->cb_menu_brightness_slider = bright_scale;

	gtk_box_pack_start(GTK_BOX(content),
					   create_menu_slider("󰖔", NULL, level, G_CALLBACK(on_nightlight_scale_changed), NULL, NULL, slider_ctx, FALSE, NULL),
					   FALSE, FALSE, 0);

	gtk_widget_show_all(bw->cb_menu_main_box);
}

void chromeos_menu_show_volume(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *ctx = g_new0(MenuCtx, 1);
	ctx->bw = bw;
	ctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(chromeos_menu_on_back_to_main_clicked), ctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	GtkWidget *title = gtk_label_new("Volume");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

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
					   create_menu_slider("󰕾", "󰝟", vol, G_CALLBACK(on_vol_scale_changed), G_CALLBACK(on_mute_clicked),
										  NULL, slider_ctx, muted, &vol_scale),
					   FALSE, FALSE, 0);
	bw->cb_menu_volume_slider = vol_scale;

	gtk_widget_show_all(bw->cb_menu_main_box);
}
