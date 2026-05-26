#include "bar.h"
#include "chromeos_bar.h"
#include "chromeos_menu.h"
#include "config.h"
#include "ipc.h"
#include "media.h"
#include "metrics.h"
#include "types.h"
#include "widgets.h"
#include "wifi.h"
#include <math.h>

static gboolean anim_timer_func(gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	int changed = 0;

	/* Brightness animation */
	float b_target = state->sys_data.brightness;
	float b_current = state->sys_data.visual_brightness;
	float b_diff = b_target - b_current;
	if (fabsf(b_diff) > 0.05f) {
		state->sys_data.visual_brightness += b_diff * 0.15f;
		changed = 1;
	} else if (b_current != b_target) {
		state->sys_data.visual_brightness = b_target;
		changed = 1;
	}

	/* Volume animation */
	float v_target = state->sys_data.vol;
	float v_current = state->sys_data.visual_volume;
	float v_diff = v_target - v_current;
	if (fabsf(v_diff) > 0.05f) {
		state->sys_data.visual_volume += v_diff * 0.15f;
		changed = 1;
	} else if (v_current != v_target) {
		state->sys_data.visual_volume = v_target;
		changed = 1;
	}

	if (!changed) {
		state->anim_timer_id = 0;
		pthread_mutex_unlock(&state->mutex);
		return G_SOURCE_REMOVE;
	}
	pthread_mutex_unlock(&state->mutex);

	update_widgets_idle(state);
	return G_SOURCE_CONTINUE;
}

void ensure_anim_timer(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	if (state->anim_timer_id == 0 &&
	    (state->sys_data.visual_brightness != state->sys_data.brightness ||
	     state->sys_data.visual_volume != state->sys_data.vol)) {
		state->anim_timer_id = g_timeout_add(16, anim_timer_func, state);
	}
	pthread_mutex_unlock(&state->mutex);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);

	AppState *state = g_new0(AppState, 1);
	pthread_mutex_init(&state->mutex, NULL);
	state->window_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	config_load(&state->config);
	if (state->config.mode == MODE_CHROMEOS) {
		strcpy(state->config.font.family, "Noto Sans");
	}
	sync_initial_state(state);
	nightlight_init(state);
	apply_global_css(state);
	state->sys_data.visual_brightness = state->sys_data.brightness;
	state->sys_data.visual_volume = state->sys_data.vol;

	GdkDisplay *display = gdk_display_get_default();
	int n_monitors = gdk_display_get_n_monitors(display);
	for (int i = 0; i < n_monitors; i++) {
		GdkMonitor *monitor = gdk_display_get_monitor(display, i);
		create_bar_window(monitor, state);
	}

	if (state->config.mode == MODE_CHROMEOS) {
		chromeos_menu_apply_css();
		apply_chromeos_css(state);
	}

	wifi_init(state);
	update_widgets_idle(state);
	ensure_anim_timer(state);

	pthread_t ipc_thread, metrics_thread, media_thread, volume_thread;
	pthread_create(&ipc_thread, NULL, ipc_thread_func, state);
	pthread_create(&metrics_thread, NULL, metrics_thread_func, state);
	pthread_create(&media_thread, NULL, media_thread_func, state);
	pthread_create(&volume_thread, NULL, volume_thread_func, state);

	gtk_main();

	wifi_cleanup(state);

	return 0;
}
