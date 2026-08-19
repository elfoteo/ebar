#include "util.h"
#include <glob.h>
#include <string.h>
#include <time.h>

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
