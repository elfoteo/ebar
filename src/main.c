#include "bar.h"
#include "chromeos_bar.h"
#include "chromeos_menu.h"
#include "chromeos_menu_internal.h"
#include "config.h"
#include "constants.h"
#include "extra_events.h"
#include "ipc.h"
#include "media.h"
#include "metrics.h"
#include "nightlight.h"
#include "pulse.h"
#include "types.h"
#include "util.h"
#include "widgets.h"
#include "wifi.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

static gboolean anim_timer_func(gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	int changed = 0;

	/* Brightness animation */
	float b_target = state->sys_data.brightness;
	float b_current = state->sys_data.visual_brightness;
	float b_diff = b_target - b_current;
	if (fabsf(b_diff) > ANIM_DEADZONE) {
		state->sys_data.visual_brightness += b_diff * ANIM_BRIGHTNESS_LERP;
		changed = 1;
	} else if (b_current != b_target) {
		state->sys_data.visual_brightness = b_target;
		changed = 1;
	}

	/* Volume animation */
	float v_target = state->sys_data.vol;
	float v_current = state->sys_data.visual_volume;
	float v_diff = v_target - v_current;
	if (fabsf(v_diff) > ANIM_DEADZONE) {
		state->sys_data.visual_volume += v_diff * ANIM_VOLUME_LERP;
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
		state->anim_timer_id = g_timeout_add(ANIM_TIMER_INTERVAL_MS, anim_timer_func, state);
	}
	pthread_mutex_unlock(&state->mutex);
}

static void on_monitor_added(GdkDisplay *display, GdkMonitor *monitor, gpointer data) {
	(void)display;
	AppState *state = (AppState *)data;
	create_bar_window(monitor, state);
	update_widgets_idle_reset();
	g_idle_add(update_widgets_idle, state);
}

static void on_monitor_removed(GdkDisplay *display, GdkMonitor *monitor, gpointer data) {
	(void)display;
	AppState *state = (AppState *)data;
	BarWindow *target = NULL;
	pthread_mutex_lock(&state->mutex);
	for (GList *l = state->bar_windows; l; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->monitor == monitor) {
			target = bw;
			break;
		}
	}
	pthread_mutex_unlock(&state->mutex);
	if (target)
		gtk_widget_destroy(target->window);
}

int main(int argc, char **argv) {
	if (argc > 1) {
		if (strcmp(argv[1], "--togglefloat") == 0) {
			system("hyprctl dispatch togglefloating > /dev/null 2>&1");
			send_to_ebar("togglefloating\n");
			return 0;
		} else if (strcmp(argv[1], "--brightness") == 0 && argc > 2) {
			Config cfg;
			config_load(&cfg);
			float cur = get_current_brightness();
			
			// Check for logical target handover
			const char *last_file = BRIGHTNESS_LAST_FILE_PATH;
			FILE *lf = fopen(last_file, "r");
			if (lf) {
				float last_target;
				struct stat st;
				if (fstat(fileno(lf), &st) == 0) {
					time_t now = time(NULL);
					if (now - st.st_mtime < 2 && fscanf(lf, "%f", &last_target) == 1) {
						cur = last_target;
					}
				}
				fclose(lf);
			}

			float target = cur;
			if (strcmp(argv[2], "raise") == 0) {
				target = cfg.brightness.levels[0];
				for (int i = 0; i < cfg.brightness.count; i++) {
					if (cur >= cfg.brightness.levels[i] - 0.1f) {
						target = cfg.brightness.levels[i > 0 ? i - 1 : 0];
						break;
					}
				}
			} else if (strcmp(argv[2], "lower") == 0) {
				target = cfg.brightness.levels[cfg.brightness.count - 1];
				for (int i = 0; i < cfg.brightness.count; i++) {
					if (cur > cfg.brightness.levels[i] + 0.5f) {
						target = cfg.brightness.levels[i];
						break;
					}
				}
			}

			// Save new logical target
			lf = fopen(last_file, "w");
			if (lf) {
				fprintf(lf, "%.2f", target);
				fclose(lf);
			}

			char msg[64];
			snprintf(msg, sizeof(msg), "brightness>>%.2f\n", target);
			send_to_ebar(msg);
			set_brightness(target, cfg.brightness.transition_ms);
			return 0;
		}
	}

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
	g_signal_connect(display, "monitor-added",   G_CALLBACK(on_monitor_added),   state);
	g_signal_connect(display, "monitor-removed", G_CALLBACK(on_monitor_removed), state);

	if (state->config.mode == MODE_CHROMEOS) {
		chromeos_menu_apply_css(state);
		apply_chromeos_css(state);
	}

	wifi_init(state);
	if (state->config.mode == MODE_CHROMEOS)
		wifi_set_changed_callback(chromeos_menu_refresh_wifi_list_if_open);
	pulse_init(state);
	update_widgets_idle(state);
	ensure_anim_timer(state);

	pthread_t ipc_thread, metrics_thread, media_thread, extra_events_thread;
	if (pthread_create(&ipc_thread, NULL, ipc_thread_func, state) != 0)
		fprintf(stderr, "Failed to create IPC thread\n");
	if (pthread_create(&metrics_thread, NULL, metrics_thread_func, state) != 0)
		fprintf(stderr, "Failed to create metrics thread\n");
	if (pthread_create(&media_thread, NULL, media_thread_func, state) != 0)
		fprintf(stderr, "Failed to create media thread\n");
	if (pthread_create(&extra_events_thread, NULL, extra_events_thread_func, state) != 0)
		fprintf(stderr, "Failed to create extra_events thread\n");

	gtk_main();

	pthread_cancel(ipc_thread);
	pthread_cancel(metrics_thread);
	pthread_cancel(media_thread);
	pthread_cancel(extra_events_thread);
	pthread_join(ipc_thread, NULL);
	pthread_join(metrics_thread, NULL);
	pthread_join(media_thread, NULL);
	pthread_join(extra_events_thread, NULL);

	pulse_cleanup(state);
	wifi_cleanup(state);
	pthread_mutex_destroy(&state->mutex);
	g_hash_table_destroy(state->window_map);
	g_list_free(state->bar_windows);
	g_free(state);

	unlink(COORD_FILE_PATH);

	return 0;
}
