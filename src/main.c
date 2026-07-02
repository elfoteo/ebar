#include "bar.h"
#include "chromeos_bar.h"
#include "chromeos_menu.h"
#include "config.h"
#include "extra_events.h"
#include "ipc.h"
#include "media.h"
#include "metrics.h"
#include "types.h"
#include "widgets.h"
#include "wifi.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <glob.h>
#include <time.h>
#include <errno.h>

static void send_to_ebar(const char *msg) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return;
	}
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, "/tmp/hypr-events-extras.sock", sizeof(sa.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) >= 0) {
		write(fd, msg, strlen(msg));
	}
	close(fd);
}

static int get_backlight_path(char *out_path, const char *filename) {
	glob_t g;
	int res = -1;
	if (glob("/sys/class/backlight/*", 0, NULL, &g) == 0) {
		if (g.gl_pathc > 0) {
			snprintf(out_path, 256, "%s/%s", g.gl_pathv[0], filename);
			res = 0;
		}
		globfree(&g);
	}
	return res;
}

static float get_current_brightness() {
	char path[256];
	long actual = 0, max = 1;
	
	if (get_backlight_path(path, "actual_brightness") == 0) {
		FILE *f = fopen(path, "r");
		if (f) {
			if (fscanf(f, "%ld", &actual) != 1) actual = 0;
			fclose(f);
		}
	}
	if (get_backlight_path(path, "max_brightness") == 0) {
		FILE *f = fopen(path, "r");
		if (f) {
			if (fscanf(f, "%ld", &max) != 1) max = 1;
			fclose(f);
		}
	}

	if (actual == 0) return 0.0f;
	float pct = (float)actual * 100.0f / (float)max;
	if (pct < 1.0f) return 1.0f; // DIM sentinel
	return pct;
}

static void set_brightness(float target_pct, int transition_ms) {
	char b_path[256], m_path[256];
	if (get_backlight_path(b_path, "brightness") != 0 || get_backlight_path(m_path, "max_brightness") != 0) {
		fprintf(stderr, "CRITICAL: Could not find backlight sysfs paths\n");
		return;
	}

	long max = 1;
	FILE *f = fopen(m_path, "r");
	if (f) {
		if (fscanf(f, "%ld", &max) != 1) max = 1;
		fclose(f);
	}

	long start_raw = 0;
	f = fopen(b_path, "r");
	if (f) {
		if (fscanf(f, "%ld", &start_raw) != 1) start_raw = 0;
		fclose(f);
	}

	long target_raw = (long)(target_pct * (float)max / 100.0f);
	if (target_pct > 0.001f && target_raw == 0) target_raw = 1;

	// Check permissions
	f = fopen(b_path, "w");
	if (!f) {
		fprintf(stderr, "FATAL: No write permission to %s. Error: %s\n", b_path, strerror(errno));
		fprintf(stderr, "HINT: Ensure your user is in the 'video' or 'backlight' group or udev rules are set.\n");
		return;
	}

	if (transition_ms <= 0) {
		fprintf(f, "%ld", target_raw);
		fclose(f);
		return;
	}

	// Handover coordination
	pid_t my_pid = getpid();
	const char *coord_file = "/tmp/ebar-brightness.coord";
	FILE *cf = fopen(coord_file, "w");
	if (cf) {
		fprintf(cf, "%d", (int)my_pid);
		fclose(cf);
	}

	struct timespec start_time, now;
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	double duration = (double)transition_ms / 1000.0;
	long last_val = start_raw;

	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - start_time.tv_sec) + (now.tv_nsec - start_time.tv_nsec) * 1e-9;
		
		if (elapsed >= duration) break;

		// Check if a newer process has taken over
		cf = fopen(coord_file, "r");
		if (cf) {
			int boss_pid = 0;
			if (fscanf(cf, "%d", &boss_pid) == 1 && boss_pid != (int)my_pid) {
				fclose(cf);
				fclose(f);
				return; // Handover complete
			}
			fclose(cf);
		}

		double t = elapsed / duration;
		long current_val = start_raw + (long)((double)(target_raw - start_raw) * t);
		
		if (current_val != last_val) {
			rewind(f);
			fprintf(f, "%ld", current_val);
			fflush(f);
			last_val = current_val;
		}
		
		struct timespec sleep_ts = {0, 8000000}; // 8ms (approx 125Hz)
		nanosleep(&sleep_ts, NULL);
	}

	// Final value
	rewind(f);
	fprintf(f, "%ld", target_raw);
	fclose(f);
}

static gboolean anim_timer_func(gpointer data) {
	AppState *state = (AppState *)data;
	pthread_mutex_lock(&state->mutex);
	int changed = 0;

	/* Brightness animation */
	float b_target = state->sys_data.brightness;
	float b_current = state->sys_data.visual_brightness;
	float b_diff = b_target - b_current;
	if (fabsf(b_diff) > 0.05f) {
		state->sys_data.visual_brightness += b_diff * 0.25f;
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
			const char *last_file = "/tmp/ebar-brightness.last";
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

	if (state->config.mode == MODE_CHROMEOS) {
		chromeos_menu_apply_css(state);
		apply_chromeos_css(state);
	}

	wifi_init(state);
	update_widgets_idle(state);
	ensure_anim_timer(state);

	pthread_t ipc_thread, metrics_thread, media_thread, volume_thread, extra_events_thread;
	pthread_create(&ipc_thread, NULL, ipc_thread_func, state);
	pthread_create(&metrics_thread, NULL, metrics_thread_func, state);
	pthread_create(&media_thread, NULL, media_thread_func, state);
	pthread_create(&volume_thread, NULL, volume_thread_func, state);
	pthread_create(&extra_events_thread, NULL, extra_events_thread_func, state);

	gtk_main();

	wifi_cleanup(state);

	return 0;
}
