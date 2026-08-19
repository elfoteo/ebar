#include "nightlight.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern gboolean update_widgets_idle(gpointer data);

void nightlight_save_state(int active, int level) {
	FILE *f = fopen("/tmp/ebar_nightlight", "w");
	if (f) {
		fprintf(f, "%d %d", active, level);
		fclose(f);
	}
}

int nightlight_load_state(int *active) {
	FILE *f = fopen("/tmp/ebar_nightlight", "r");
	if (!f) {
		if (active)
			*active = 0;
		return 0;
	}
	int act = 0;
	int level = 0;
	if (fscanf(f, "%d %d", &act, &level) != 2) {
		fseek(f, 0, SEEK_SET);
		if (fscanf(f, "%d", &level) != 1) {
			level = 0;
		}
		act = 0;
	}
	if (active)
		*active = act;
	fclose(f);
	return level;
}

static int nightlight_ipc(const char *cmd) {
	const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	if (!sig)
		return -1;
	const char *run = getenv("XDG_RUNTIME_DIR");
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

void nightlight_apply(AppState *state) {
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

void nightlight_reset(AppState *state) {
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

gboolean nightlight_retry_cb(gpointer data) {
	AppState *state = (AppState *)data;

	pthread_mutex_lock(&state->mutex);
	int active = state->sys_data.nightlight_on;
	pthread_mutex_unlock(&state->mutex);

	if (active)
		nightlight_apply(state);
	else
		nightlight_reset(state);

	pthread_mutex_lock(&state->mutex);
	int err = state->sys_data.nightlight_error;
	if (!err) {
		state->sys_data.nightlight_retrying = 0;
		state->nightlight_retry_tag = 0;
		state->nightlight_retries = 0;
		pthread_mutex_unlock(&state->mutex);
		update_widgets_idle(state);
		return G_SOURCE_REMOVE;
	}
	state->nightlight_retries++;
	if (state->nightlight_retries >= 5) {
		state->sys_data.nightlight_retrying = 0;
		state->nightlight_retry_tag = 0;
		state->nightlight_retries = 0;
		pthread_mutex_unlock(&state->mutex);
		update_widgets_idle(state);
		return G_SOURCE_REMOVE;
	}
	pthread_mutex_unlock(&state->mutex);
	return G_SOURCE_CONTINUE;
}

void nightlight_init(AppState *state) {
	int active = 0;
	int level = nightlight_load_state(&active);
	if (level <= 0)
		level = 15;

	pthread_mutex_lock(&state->mutex);
	state->sys_data.nightlight_last_level = level;
	state->sys_data.nightlight_on = active;
	state->sys_data.nightlight_level = active ? level : 0;
	pthread_mutex_unlock(&state->mutex);

	if (active) {
		nightlight_apply(state);
	} else {
		nightlight_reset(state);
	}
}

void nightlight_set_level(AppState *state, int level) {
	if (level < 0) level = 0;
	if (level > 100) level = 100;

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
