#include "util.h"
#include "chromeos_menu_internal.h"
#include "constants.h"
#include "ipc.h"
#include "pulse.h"
#include "widgets.h"
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int json_str(const char *haystack, const char *key, char *out, size_t outsz) {
	const char *p = strstr(haystack, key);
	if (!p)
		return 0;
	p += strlen(key);
	const char *q1 = strchr(p, '"');
	if (!q1)
		return 0;
	const char *q2 = strchr(q1 + 1, '"');
	if (!q2)
		return 0;
	size_t len = (size_t)(q2 - q1 - 1);
	if (len >= outsz)
		len = outsz - 1;
	memcpy(out, q1 + 1, len);
	out[len] = '\0';
	return 1;
}

long long get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void setup_transparent_window(GtkWidget *win) {
	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual && gdk_screen_is_composited(screen))
		gtk_widget_set_visual(win, visual);
	gtk_widget_set_app_paintable(win, TRUE);
}

const char *get_volume_icon(float vol, int muted) {
	if (muted || vol == 0)
		return "󰝟";
	if (vol <= 33)
		return "󰕿";
	if (vol <= 66)
		return "󰖀";
	return "󰕾";
}

int find_backlight_path(const char *filename, char *out, size_t outsz) {
	glob_t g;
	int res = -1;
	if (glob("/sys/class/backlight/*", 0, NULL, &g) == 0) {
		if (g.gl_pathc > 0) {
			snprintf(out, outsz, "%s/%s", g.gl_pathv[0], filename);
			res = 0;
		}
		globfree(&g);
	}
	return res;
}

void apply_css_from_string(const char *css, guint priority) {
	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(provider), priority);
	g_object_unref(provider);
}

void apply_forcergbx_bypass(const char *window_title, const char *layer_name) {
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "keyword windowrule forcergbx,title:^(%s)$", window_title);
	char *res = hyprctl_request(cmd);
	if (res) free(res);
	snprintf(cmd, sizeof(cmd), "keyword layerrule forcergbx,%s", layer_name);
	res = hyprctl_request(cmd);
	if (res) free(res);
}

float get_current_brightness(void) {
	char path[256];
	long actual = 0, max = 1;

	if (find_backlight_path("actual_brightness", path, sizeof(path)) == 0) {
		FILE *f = fopen(path, "r");
		if (f) {
			if (fscanf(f, "%ld", &actual) != 1) actual = 0;
			fclose(f);
		}
	}
	if (find_backlight_path("max_brightness", path, sizeof(path)) == 0) {
		FILE *f = fopen(path, "r");
		if (f) {
			if (fscanf(f, "%ld", &max) != 1) max = 1;
			fclose(f);
		}
	}

	if (actual == 0) return 0.0f;
	float pct = (float)actual * 100.0f / (float)max;
	if (pct < 1.0f) return 1.0f;
	return pct;
}

void set_brightness(float target_pct, int transition_ms) {
	char b_path[256], m_path[256];
	if (find_backlight_path("brightness", b_path, sizeof(b_path)) != 0 || find_backlight_path("max_brightness", m_path, sizeof(m_path)) != 0) {
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

	pid_t my_pid = getpid();
	const char *coord_file = COORD_FILE_PATH;
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

		cf = fopen(coord_file, "r");
		if (cf) {
			int boss_pid = 0;
			if (fscanf(cf, "%d", &boss_pid) == 1 && boss_pid != (int)my_pid) {
				fclose(cf);
				fclose(f);
				return;
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

		struct timespec sleep_ts = {0, BRIGHTNESS_TRANSITION_STEP_NS};
		nanosleep(&sleep_ts, NULL);
	}

	rewind(f);
	fprintf(f, "%ld", target_raw);
	fclose(f);
}

void handle_volume_slider_changed(GtkRange *range, AppState *state) {
	if (!state)
		return;
	double val = slider_get_actual_value(range);
	int was_muted = 0;
	pthread_mutex_lock(&state->mutex);
	state->last_manual_vol_update = time(NULL);
	state->sys_data.vol = val;
	if (val > 0.5 && state->sys_data.vol_muted) {
		state->sys_data.vol_muted = 0;
		was_muted = 1;
	}
	pthread_mutex_unlock(&state->mutex);
	/* Keep PulseAudio consistent: unmuting only locally would desync UI and
	 * server until the next sink event "re-mutes" the bar. */
	if (was_muted)
		pulse_set_mute(state, 0);
	pulse_set_volume(state, val);
	update_widgets_idle(state);
}

void handle_brightness_slider_changed(GtkRange *range, AppState *state) {
	if (!state)
		return;
	double val = slider_get_actual_value(range);
	pthread_mutex_lock(&state->mutex);
	state->sys_data.brightness = (float)val;
	state->last_manual_bright_update = time(NULL);
	pthread_mutex_unlock(&state->mutex);
	char cmd[64];
	snprintf(cmd, sizeof(cmd), BRIGHTNESSCTL_SET_FMT, val);
	g_spawn_command_line_async(cmd, NULL);
	update_widgets_idle(state);
}
