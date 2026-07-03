#include "chromeos_menu_internal.h"
#include "pulse.h"
#include "widgets.h"
#include <math.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void nightlight_save_state(int active, int level) {
	FILE *f = fopen("/tmp/ebar_nightlight", "w");
	if (f) {
		fprintf(f, "%d %d", active, level);
		fclose(f);
	}
}

static int nightlight_load_state(int *active) {
	FILE *f = fopen("/tmp/ebar_nightlight", "r");
	if (!f) {
		if (active)
			*active = 0;
		return 0;
	}
	int act = 0, level = 0;
	if (fscanf(f, "%d %d", &act, &level) != 2) {
		fseek(f, 0, SEEK_SET);
		if (fscanf(f, "%d", &level) != 1)
			level = 0;
		act = 0;
	}
	if (active)
		*active = act;
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

	int err = (ok1 < 0 || ok2 < 0) ? 1 : 0;
	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_error = err;
	if (err) {
		state->sys_data.nightlight_retrying = 1;
		if (state->nightlight_retry_tag == 0) {
			state->nightlight_retries = 0;
			state->nightlight_retry_tag = g_timeout_add(2000, (GSourceFunc)nightlight_retry_cb, state);
		}
	} else {
		state->sys_data.nightlight_retrying = 0;
		state->nightlight_retries = 0;
	}
	pthread_mutex_unlock(&state->mutex);
}

static void nightlight_reset(AppState *state) {
	int ok1 = nightlight_ipc("temperature 6500");
	int ok2 = nightlight_ipc("gamma 100");

	int err = (ok1 < 0 || ok2 < 0) ? 1 : 0;
	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_error = err;
	if (err) {
		state->sys_data.nightlight_retrying = 1;
		if (state->nightlight_retry_tag == 0) {
			state->nightlight_retries = 0;
			state->nightlight_retry_tag = g_timeout_add(2000, (GSourceFunc)nightlight_retry_cb, state);
		}
	} else {
		state->sys_data.nightlight_retrying = 0;
		state->nightlight_retries = 0;
	}
	pthread_mutex_unlock(&state->mutex);
}

static void nightlight_set_level(AppState *state, int level) {
	level = CLAMP(level, 0, 100);

	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_level = level;
	state->sys_data.nightlight_on = level > 0;
	if (level > 0) {
		state->sys_data.nightlight_last_level = level;
		nightlight_save_state(1, level);
	} else {
		nightlight_save_state(0, state->sys_data.nightlight_last_level);
	}
	pthread_mutex_unlock(&state->mutex);

	if (level > 0)
		nightlight_apply(state);
	else
		nightlight_reset(state);
	update_widgets_idle(state);
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
	pulse_set_volume(state, val);
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
					   create_menu_slider("󰕾", "󰝟", vol, G_CALLBACK(on_vol_scale_changed), G_CALLBACK(on_mute_clicked), NULL,
										  slider_ctx, muted, &vol_scale),
					   FALSE, FALSE, 0);
	bw->cb_menu_volume_slider = vol_scale;

	gtk_widget_show_all(bw->cb_menu_main_box);
}
