#include "gtk-layer-shell.h"
#include <glob.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_WORKSPACES 10
#define METRIC_USE_BARS 1
#define CPU_TEMP_PATH "/sys/class/thermal/thermal_zone1/temp"
#define VOLUME_APP "pavucontrol"
#define VOLUME_SHOW_PERCENT 0
#define WORKSPACE_CENTER 0
#define SHOW_MEDIA_ARTIST 1

typedef enum { M_RAM = 0, M_CPU, M_GPU, M_DISK, M_TEMP, M_GPU_TEMP, M_NONE } MetricType;

#define BAR_CONFIG_ROW_COUNT 2
#define BAR_CONFIG_COLUMN_COUNT 3

typedef struct {
	MetricType layout[BAR_CONFIG_ROW_COUNT][BAR_CONFIG_COLUMN_COUNT];
} BarConfig;

// Change your layout here! Use M_NONE for empty slots.
static const BarConfig bar_config = {.layout = {{M_RAM, M_CPU, M_NONE}, {M_DISK, M_TEMP, M_NONE}}};

// Automatically detect whether any GPU metric is present in the layout so
// nvidia-smi is never spawned when it is not needed.
static int layout_has_gpu(void) {
	for (int r = 0; r < BAR_CONFIG_ROW_COUNT; r++)
		for (int c = 0; c < BAR_CONFIG_COLUMN_COUNT; c++) {
			MetricType t = bar_config.layout[r][c];
			if (t == M_GPU || t == M_GPU_TEMP)
				return 1;
		}
	return 0;
}

typedef struct {
	GtkWidget *window;
	GtkWidget *ws_labels[MAX_WORKSPACES];
	GtkWidget *clock_time_label;
	GtkWidget *clock_date_label;
	GtkWidget *metrics[6]; // Indexed by MetricType
	GtkWidget *volume_btn;
	GtkWidget *media_play_btn;
	GtkWidget *media_title_label;
	GtkWidget *media_artist_label;
	GtkWidget *media_sep;
} BarWindow;

typedef struct {
	double ram_val, cpu_val, disk_val, temp_val;
	double gpu_val, gpu_temp_val;
	float ram_total, ram_avail;
	float vol;
	int vol_muted;
	int is_playing; // 0: paused/stopped, 1: playing
	char media_title[256];
	char media_artist[256];
} SystemData;

typedef struct {
	int active_workspace;
	int ws_win_count[MAX_WORKSPACES + 1];
	pthread_mutex_t mutex;
	GHashTable *window_map; // address -> workspace_id
	long long prev_total, prev_idle;
	GList *bar_windows;
	SystemData sys_data;
	time_t last_manual_vol_update;
} AppWidgets;

static void apply_css(void) {
	GtkCssProvider *provider = gtk_css_provider_new();
	const char *css = "* { font-family: JetBrainsMonoNerdFont; background: none; box-shadow: "
					  "none; border: none; } "
					  "window, .background { "
					  "  background-color: transparent;"
					  "} "
					  "#main-container { background-color: rgba(0, 0, 0, 0.2); } "
					  ".workspace-label { "
					  "  font-size: 8px; "
					  "  padding: 8px; "
					  "  min-width: 24px; "
					  "  min-height: 24px; "
					  "  margin-left: 0px; "
					  "  margin-right: 0px; "
					  "  color: #ffffff; "
					  "  border-radius: 999px; "
					  "} "
					  ".workspace-label:first-child { margin-left: 10px; } "
					  ".workspace-occupied { color: #ffffff; } "
					  ".workspace-active { background-color: rgba(255, 255, 255, 0.2); "
					  "color: #ffffff; } "
					  "#clock-time { font-size: 14px; font-weight: normal; color: #ffffff; } "
					  "#clock-date { font-size: 14px; font-weight: normal; color: #ffffff; } "
					  ".metric-icon { font-size: 14px; color: rgba(255,255,255,0.7); margin: "
					  "0px 0px 0px 6px; padding: 0; } "
					  ".metric-scale { padding: 0; margin: 0; } "
					  ".metric-scale slider { all: unset; min-width: 0; min-height: 0; "
					  "opacity: 0; } "
					  ".metric-scale trough highlight { background-color: #D35D6E; "
					  "border-radius: 10px; } "
					  ".metric-scale trough { "
					  "  background-color: #4e4e4e; "
					  "  border-radius: 50px; "
					  "  min-height: 3px; "
					  "  min-width: 55px; "
					  "  margin: 2px 12px; "
					  "  padding: 0; "
					  "} "
					  ".metric-label { font-size: 12px; color: #ffffff; margin: 0 8px; } "
					  ".volume-btn { "
					  "  margin-right: 15px; "
					  "  background-color: rgba(0, 0, 0, 0.2); "
					  "  padding-left: 10px; "
					  "  padding-right: 15px; "
					  "  border-radius: 20px; "
					  "  color: #ffffff; "
					  "} "
					  ".volume-btn label { "
#if !VOLUME_SHOW_PERCENT
					  "  font-size: 22px; "
#else
					  "  font-size: 14px; "
#endif
					  "} "
					  ".media-box { "
					  "  margin-left: 10px; "
					  "  background-color: rgba(0, 0, 0, 0.2); "
					  "  padding: 0 5px; "
					  "  border-radius: 20px; "
					  "} "
					  ".media-box button { "
					  "  color: #ffffff; "
					  "  background: none; "
					  "  font-size: 16px; "
					  "  padding: 0 8px; "
					  "  min-height: 24px; "
					  "} "
					  ".media-box button:hover { color: rgba(255,255,255,0.7); } "
					  ".media-title-label { "
					  "  font-size: 12px; "
					  "  color: #ffffff; "
					  "  margin-right: 12px; "
					  "  margin-left: 4px; "
					  "  max-width: 400px; "
					  "} "
					  ".media-artist-label { "
					  "  font-size: 10px; "
					  "  color: rgba(255,255,255,0.6); "
					  "  margin-right: 12px; "
					  "  margin-left: 4px; "
					  "  max-width: 400px; "
					  "} "
					  ".media-sep { "
					  "  background-color: rgba(255,255,255,0.2); "
					  "  min-width: 1px; "
					  "  margin: 4px 8px; "
					  "} ";

	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
											  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
}

static void sync_initial_state(AppWidgets *w) {
	FILE *fp = popen("hyprctl monitors -j 2>/dev/null", "r");
	if (fp) {
		char buffer[8192];
		size_t n = fread(buffer, 1, sizeof(buffer) - 1, fp);
		buffer[n] = '\0';
		char *active = strstr(buffer, "\"activeWorkspace\":");
		if (active) {
			char *id_p = strstr(active, "\"id\":");
			if (id_p)
				w->active_workspace = atoi(id_p + 5);
		}
		pclose(fp);
	}

	fp = popen("hyprctl clients -j 2>/dev/null", "r");
	if (fp) {
		char *buf = malloc(65536);
		size_t n = fread(buf, 1, 65535, fp);
		buf[n] = '\0';

		char *p = buf;
		while ((p = strstr(p, "{"))) {
			char *addr_p = strstr(p, "\"address\":");
			char *ws_p = strstr(p, "\"workspace\":");
			char *next = strstr(p + 1, "{");

			if (addr_p && ws_p && (!next || (addr_p < next && ws_p < next))) {
				char *s = strchr(addr_p + 10, '"') + 1;
				char *e = strchr(s, '"');
				char *raw_addr = g_strndup(s, e - s);
				// Strip 0x if present to match IPC addresses (e.g., 0x55c7...
				// -> 55c7...)
				char *addr_to_map = (strncmp(raw_addr, "0x", 2) == 0) ? g_strdup(raw_addr + 2) : g_strdup(raw_addr);

				char *id_p = strstr(ws_p, "\"id\":");
				if (id_p) {
					int id = atoi(id_p + 5);
					if (id >= 1 && id <= MAX_WORKSPACES) {
						w->ws_win_count[id]++;
						g_hash_table_insert(w->window_map, addr_to_map, GINT_TO_POINTER(id));
					} else {
						g_free(addr_to_map);
					}
				} else {
					g_free(addr_to_map);
				}
				g_free(raw_addr);
			}
			if (!next)
				break;
			p = next;
		}
		free(buf);
		pclose(fp);
	}
}

static void update_workspace_display(AppWidgets *w) {
	pthread_mutex_lock(&w->mutex);
	for (GList *l = w->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		for (int i = 0; i < MAX_WORKSPACES; i++) {
			int ws_id = i + 1;
			gboolean occupied = (w->ws_win_count[ws_id] > 0);
			const char *symbol = occupied ? "" : "";
			gtk_label_set_text(GTK_LABEL(bw->ws_labels[i]), symbol);

			GtkStyleContext *context = gtk_widget_get_style_context(bw->ws_labels[i]);
			gtk_style_context_remove_class(context, "workspace-active");
			gtk_style_context_remove_class(context, "workspace-occupied");

			if (ws_id == w->active_workspace) {
				gtk_style_context_add_class(context, "workspace-active");
			} else if (occupied) {
				gtk_style_context_add_class(context, "workspace-occupied");
			}
		}
	}
	pthread_mutex_unlock(&w->mutex);
}

static void fetch_system_metrics(AppWidgets *w) {
	double ram_val = 0, cpu_val = 0, disk_val = 0, temp_val = 0;
	float ram_total = 0, ram_avail = 0;

	FILE *fp = fopen("/proc/meminfo", "r");
	if (fp) {
		long total = 0, avail = 0;
		char buf[256];
		while (fgets(buf, sizeof(buf), fp)) {
			if (strncmp(buf, "MemTotal:", 9) == 0)
				total = atol(buf + 10);
			else if (strncmp(buf, "MemAvailable:", 13) == 0)
				avail = atol(buf + 13);
		}
		fclose(fp);
		if (total > 0)
			ram_val = 100.0 * (total - avail) / total;
		ram_total = (float)total / 1000 / 1000;
		ram_avail = (float)avail / 1000 / 1000;
	}

	fp = fopen("/proc/stat", "r");
	if (fp) {
		long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
		if (fscanf(fp, "cpu  %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld", &user, &nice, &system, &idle, &iowait, &irq, &softirq,
				   &steal, &guest, &guest_nice) == 10) {
			long long current_idle = idle + iowait;
			long long current_total = user + nice + system + idle + iowait + irq + softirq + steal;

			pthread_mutex_lock(&w->mutex);
			if (w->prev_total > 0) {
				long long total_diff = current_total - w->prev_total;
				long long idle_diff = current_idle - w->prev_idle;
				if (total_diff > 0)
					cpu_val = 100.0 * (total_diff - idle_diff) / total_diff;
			}
			w->prev_total = current_total;
			w->prev_idle = current_idle;
			pthread_mutex_unlock(&w->mutex);
		}
		fclose(fp);
	}

	struct statvfs st;
	if (statvfs("/", &st) == 0) {
		disk_val = 100.0 * (1.0 - (double)st.f_bavail / (double)st.f_blocks);
	}

	fp = fopen(CPU_TEMP_PATH, "r");
	if (fp) {
		int t;
		if (fscanf(fp, "%d", &t) == 1)
			temp_val = t / 1000.0;
		fclose(fp);
	}

	if (temp_val == 0) {
		glob_t g;
		if (glob("/sys/class/hwmon/hwmon*/temp*_label", 0, NULL, &g) == 0) {
			double sum = 0;
			int count = 0;
			for (size_t i = 0; i < g.gl_pathc; i++) {
				FILE *f = fopen(g.gl_pathv[i], "r");
				if (f) {
					char lbl[64];
					if (fgets(lbl, sizeof(lbl), f)) {
						if (strstr(lbl, "Core") || strstr(lbl, "Package")) {
							char ipath[256];
							snprintf(ipath, sizeof(ipath), "%s", g.gl_pathv[i]);
							char *p = strstr(ipath, "_label");
							if (p) {
								memcpy(p, "_input", 6);
								FILE *fi = fopen(ipath, "r");
								if (fi) {
									int v;
									if (fscanf(fi, "%d", &v) == 1) {
										sum += v;
										count++;
									}
									fclose(fi);
								}
							}
						}
					}
					fclose(f);
				}
			}
			if (count > 0)
				temp_val = sum / (count * 1000.0);
			globfree(&g);
		}
	}

	if (temp_val == 0) {
		fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
		if (fp) {
			int t;
			if (fscanf(fp, "%d", &t) == 1)
				temp_val = t / 1000.0;
			fclose(fp);
		}
	}

	double gpu_val = 0, gpu_temp_val = 0;
	if (layout_has_gpu()) {
		fp = popen("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu "
				   "--format=csv,noheader,nounits 2>/dev/null",
				   "r");
		if (fp) {
			int g_usage, g_temp;
			if (fscanf(fp, "%d, %d", &g_usage, &g_temp) == 2) {
				gpu_val = g_usage;
				gpu_temp_val = g_temp;
			}
			pclose(fp);
		}
	}

	pthread_mutex_lock(&w->mutex);
	w->sys_data.ram_val = ram_val;
	w->sys_data.cpu_val = cpu_val;
	w->sys_data.disk_val = disk_val;
	w->sys_data.temp_val = temp_val;
	w->sys_data.gpu_val = gpu_val;
	w->sys_data.gpu_temp_val = gpu_temp_val;
	w->sys_data.ram_total = ram_total;
	w->sys_data.ram_avail = ram_avail;
	pthread_mutex_unlock(&w->mutex);
}

static void fetch_volume(AppWidgets *w) {
	// Skip polling if there was a manual update recently (within 2 seconds)
	if (time(NULL) - w->last_manual_vol_update < 2)
		return;

	float vol = 0;
	int muted = 0;
	FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@", "r");
	if (fp) {
		char buf[256];
		if (fgets(buf, sizeof(buf), fp)) {
			char *p = strstr(buf, "/ ");
			if (p)
				vol = atof(p + 2);
		}
		pclose(fp);
	}

	fp = popen("pactl get-sink-mute @DEFAULT_SINK@", "r");
	if (fp) {
		char buf[64];
		if (fgets(buf, sizeof(buf), fp)) {
			if (strstr(buf, "yes"))
				muted = 1;
		}
		pclose(fp);
	}

	pthread_mutex_lock(&w->mutex);
	w->sys_data.vol = vol;
	w->sys_data.vol_muted = muted;
	pthread_mutex_unlock(&w->mutex);
}

static void fetch_media_status(AppWidgets *w) {
	// This is now handled by the persistent media_thread_func
	return;
}

static void update_metric_widget(GtkWidget *widget, MetricType type, SystemData *d) {
	if (!widget)
		return;
	char buf[64];

	if (METRIC_USE_BARS) {
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
}

static gboolean update_metrics_idle(gpointer data) {
	AppWidgets *w = (AppWidgets *)data;
	pthread_mutex_lock(&w->mutex);
	SystemData d = w->sys_data;
	pthread_mutex_unlock(&w->mutex);

	for (GList *l = w->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;

		for (int i = 0; i < 6; i++) {
			update_metric_widget(bw->metrics[i], (MetricType)i, &d);
		}

		const char *icon = "󰕾";
		if (d.vol_muted || d.vol == 0)
			icon = "󰝟";
		else if (d.vol <= 33)
			icon = "󰕿";
		else if (d.vol <= 66)
			icon = "󰖀";

		char vstr[32];
#if VOLUME_SHOW_PERCENT
		if (d.vol_muted)
			snprintf(vstr, sizeof(vstr), "%s Muted", icon);
#else
		if (d.vol_muted)
			snprintf(vstr, sizeof(vstr), "%s", icon);
#endif
		else if (VOLUME_SHOW_PERCENT)
			snprintf(vstr, sizeof(vstr), "%s %.0f%%", icon, d.vol);
		else
			snprintf(vstr, sizeof(vstr), "%s", icon);
		gtk_button_set_label(GTK_BUTTON(bw->volume_btn), vstr);

#if WORKSPACE_CENTER
		if (bw->media_play_btn) {
			gtk_button_set_label(GTK_BUTTON(bw->media_play_btn), d.is_playing ? "󰏤" : "󰐊");
		}
		int has_media = (d.media_title[0] != '\0');
		if (bw->media_sep)
			gtk_widget_set_visible(bw->media_sep, has_media);
		if (bw->media_title_label) {
			gtk_label_set_text(GTK_LABEL(bw->media_title_label), d.media_title);
			gtk_widget_set_visible(bw->media_title_label, has_media);
		}
		if (bw->media_artist_label) {
			gtk_label_set_text(GTK_LABEL(bw->media_artist_label), d.media_artist);
#if SHOW_MEDIA_ARTIST
			gtk_widget_set_visible(bw->media_artist_label, has_media && d.media_artist[0] != '\0');
#else
			gtk_widget_set_visible(bw->media_artist_label, FALSE);
#endif
		}
#endif
	}
	return G_SOURCE_REMOVE;
}

static void *media_thread_func(void *data) {
	AppWidgets *w = (AppWidgets *)data;
	while (1) {
		FILE *fp = popen("playerctl -F metadata --format "
						 "'{{status}}::{{title}}::{{artist}}' 2>/dev/null",
						 "r");
		if (!fp) {
			sleep(5);
			continue;
		}

		char buf[1024];
		while (fgets(buf, sizeof(buf), fp)) {
			char *sep1 = strstr(buf, "::");
			if (!sep1)
				continue;
			*sep1 = '\0';
			char *status = buf;

			char *sep2 = strstr(sep1 + 2, "::");
			if (!sep2)
				continue;
			*sep2 = '\0';
			char *title = sep1 + 2;
			char *artist = sep2 + 2;

			// Remove newline from artist
			size_t len = strlen(artist);
			if (len > 0 && artist[len - 1] == '\n')
				artist[len - 1] = '\0';

			int playing = 0;
			if (strstr(status, "Playing"))
				playing = 1;

			pthread_mutex_lock(&w->mutex);
			w->sys_data.is_playing = playing;
			strncpy(w->sys_data.media_title, title, sizeof(w->sys_data.media_title) - 1);
			strncpy(w->sys_data.media_artist, artist, sizeof(w->sys_data.media_artist) - 1);
			pthread_mutex_unlock(&w->mutex);

			g_idle_add(update_metrics_idle, w);
		}
		pclose(fp);

		pthread_mutex_lock(&w->mutex);
		w->sys_data.is_playing = 0;
		w->sys_data.media_title[0] = '\0';
		w->sys_data.media_artist[0] = '\0';
		pthread_mutex_unlock(&w->mutex);
		g_idle_add(update_metrics_idle, w);

		sleep(2);
	}
	return NULL;
}

static void *metrics_thread_func(void *data) {
	AppWidgets *w = (AppWidgets *)data;
	while (1) {
		fetch_system_metrics(w);
		fetch_volume(w);
		fetch_media_status(w);
		g_idle_add(update_metrics_idle, w);
		sleep(1);
	}
	return NULL;
}

static void update_clock(AppWidgets *w) {
	time_t now = time(NULL);
	struct tm tmv = *localtime(&now);
	char time_str[16], date_str[32];
	strftime(time_str, sizeof(time_str), "%H:%M", &tmv);
	strftime(date_str, sizeof(date_str), "%d/%m/%Y", &tmv);

	for (GList *l = w->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		gtk_label_set_text(GTK_LABEL(bw->clock_time_label), time_str);
		gtk_label_set_text(GTK_LABEL(bw->clock_date_label), date_str);
	}
}

static void on_media_prev(GtkWidget *widget, gpointer data) {
	g_spawn_command_line_async("playerctl previous || playerctl -p %any previous", NULL);
}

static void on_media_play(GtkWidget *widget, gpointer data) { g_spawn_command_line_async("playerctl play-pause", NULL); }

static void on_media_next(GtkWidget *widget, gpointer data) {
	g_spawn_command_line_async("playerctl next || playerctl -p %any next", NULL);
}

static gboolean on_volume_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
	AppWidgets *w = (AppWidgets *)data;
	pthread_mutex_lock(&w->mutex);
	w->last_manual_vol_update = time(NULL);

	if (event->direction == GDK_SCROLL_UP) {
		if (w->sys_data.vol < 100.0) {
			w->sys_data.vol += 2.0;
			if (w->sys_data.vol > 100.0)
				w->sys_data.vol = 100.0;
			char cmd[64];
			snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %.0f%%", w->sys_data.vol);
			g_spawn_command_line_async(cmd, NULL);
		}
	} else if (event->direction == GDK_SCROLL_DOWN) {
		w->sys_data.vol -= 2.0;
		if (w->sys_data.vol < 0.0)
			w->sys_data.vol = 0.0;
		char cmd[64];
		snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %.0f%%", w->sys_data.vol);
		g_spawn_command_line_async(cmd, NULL);
	}

	pthread_mutex_unlock(&w->mutex);
	update_metrics_idle(w);
	return TRUE;
}

static gboolean on_volume_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	AppWidgets *w = (AppWidgets *)data;
	if (event->button == 1) { // Left click
		g_spawn_command_line_async(VOLUME_APP, NULL);
	} else if (event->button == 3) { // Right click
		pthread_mutex_lock(&w->mutex);
		w->last_manual_vol_update = time(NULL);
		w->sys_data.vol_muted = !w->sys_data.vol_muted;
		pthread_mutex_unlock(&w->mutex);
		g_spawn_command_line_async("pactl set-sink-mute @DEFAULT_SINK@ toggle", NULL);
		update_metrics_idle(w);
	}
	return TRUE;
}

static gboolean update_ws_idle(gpointer data) {
	update_workspace_display((AppWidgets *)data);
	return G_SOURCE_REMOVE;
}
static gboolean update_timer(gpointer data) {
	update_clock((AppWidgets *)data);
	return G_SOURCE_CONTINUE;
}

static void handle_ipc_line(AppWidgets *w, char *line) {
	if (strncmp(line, "workspace>>", 11) == 0) {
		pthread_mutex_lock(&w->mutex);
		w->active_workspace = atoi(line + 11);
		pthread_mutex_unlock(&w->mutex);
		g_idle_add(update_ws_idle, w);
	} else if (strncmp(line, "activewindowv2>>", 16) == 0) {
		char *p = line + 16;
		char *comma = strchr(p, ',');
		char *addr = comma ? g_strndup(p, comma - p) : g_strdup(p);
		pthread_mutex_lock(&w->mutex);
		gpointer ws_val = g_hash_table_lookup(w->window_map, addr);
		if (ws_val) {
			w->active_workspace = GPOINTER_TO_INT(ws_val);
		}
		pthread_mutex_unlock(&w->mutex);
		g_free(addr);
		g_idle_add(update_ws_idle, w);
	} else if (strncmp(line, "openwindow>>", 12) == 0) {
		char *p = line + 12;
		char *comma = strchr(p, ',');
		if (!comma)
			return;
		char *addr = g_strndup(p, comma - p);
		char *ws_p = comma + 1;
		int ws_id = atoi(ws_p);
		pthread_mutex_lock(&w->mutex);
		g_hash_table_insert(w->window_map, addr, GINT_TO_POINTER(ws_id));
		if (ws_id >= 1 && ws_id <= MAX_WORKSPACES)
			w->ws_win_count[ws_id]++;
		pthread_mutex_unlock(&w->mutex);
		g_idle_add(update_ws_idle, w);
	} else if (strncmp(line, "closewindow>>", 13) == 0) {
		char *addr = line + 13;
		pthread_mutex_lock(&w->mutex);
		gpointer ws_val = g_hash_table_lookup(w->window_map, addr);
		if (ws_val) {
			int ws_id = GPOINTER_TO_INT(ws_val);
			if (ws_id >= 1 && ws_id <= MAX_WORKSPACES)
				w->ws_win_count[ws_id]--;
			g_hash_table_remove(w->window_map, addr);
		}
		pthread_mutex_unlock(&w->mutex);
		g_idle_add(update_ws_idle, w);
	} else if (strncmp(line, "movewindow>>", 12) == 0) {
		char *p = line + 12;
		char *comma = strchr(p, ',');
		if (!comma)
			return;
		char *addr = g_strndup(p, comma - p);
		int new_ws = atoi(comma + 1);
		pthread_mutex_lock(&w->mutex);
		gpointer old_ws_val = g_hash_table_lookup(w->window_map, addr);
		if (old_ws_val) {
			int old_ws = GPOINTER_TO_INT(old_ws_val);
			if (old_ws >= 1 && old_ws <= MAX_WORKSPACES)
				w->ws_win_count[old_ws]--;
		}
		g_hash_table_insert(w->window_map, g_strdup(addr), GINT_TO_POINTER(new_ws));
		if (new_ws >= 1 && new_ws <= MAX_WORKSPACES)
			w->ws_win_count[new_ws]++;
		g_free(addr);
		pthread_mutex_unlock(&w->mutex);
		g_idle_add(update_ws_idle, w);
	}
}

static void *ipc_thread_func(void *data) {
	AppWidgets *w = (AppWidgets *)data;
	const char *runtime = getenv("XDG_RUNTIME_DIR"), *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	if (!runtime || !his)
		return NULL;
	char path[256];
	snprintf(path, sizeof(path), "%s/hypr/%s/.socket2.sock", runtime, his);

	while (1) {
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0) {
			sleep(1);
			continue;
		}
		struct sockaddr_un addr = {.sun_family = AF_UNIX};
		strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			close(fd);
			sleep(1);
			continue;
		}

		char buffer[8192];
		ssize_t n;
		while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
			buffer[n] = '\0';
			char *saveptr, *line = strtok_r(buffer, "\n", &saveptr);
			while (line) {
				handle_ipc_line(w, line);
				line = strtok_r(NULL, "\n", &saveptr);
			}
		}
		close(fd);
		sleep(1);
	}
}

static GtkWidget *create_metric_box(const char *icon_str, GtkWidget **out_widget) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

	GtkWidget *icon = gtk_label_new(icon_str);
	gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon), "metric-icon");

	if (METRIC_USE_BARS) {
		*out_widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
		gtk_scale_set_draw_value(GTK_SCALE(*out_widget), FALSE);
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

static void create_bar_window(GdkMonitor *monitor, AppWidgets *widgets) {
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual != NULL && gdk_screen_is_composited(screen)) {
		gtk_widget_set_visual(win, visual);
	}
	gtk_widget_set_app_paintable(win, TRUE);

	gtk_layer_init_for_window(GTK_WINDOW(win));
	gtk_layer_set_monitor(GTK_WINDOW(win), monitor);
	gtk_layer_set_namespace(GTK_WINDOW(win), "ebar");
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(win));
	gtk_window_set_decorated(GTK_WINDOW(win), FALSE);

	BarWindow *bw = g_new0(BarWindow, 1);
	bw->window = win;

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_margin_top(hbox, 5);
	gtk_widget_set_margin_bottom(hbox, 5);
	gtk_widget_set_margin_start(hbox, 12);
	gtk_widget_set_margin_end(hbox, 12);

	GtkWidget *ws_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	for (int i = 0; i < MAX_WORKSPACES; i++) {
		bw->ws_labels[i] = gtk_label_new(NULL);
		GtkStyleContext *context = gtk_widget_get_style_context(bw->ws_labels[i]);
		gtk_style_context_add_class(context, "workspace-label");
		gtk_box_pack_start(GTK_BOX(ws_box), bw->ws_labels[i], FALSE, FALSE, 0);
	}

#if WORKSPACE_CENTER
	GtkWidget *media_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(media_box), "media-box");

	GtkWidget *pbtn = gtk_button_new_with_label("󰒮");
	bw->media_play_btn = gtk_button_new_with_label("󰐊");
	GtkWidget *nbtn = gtk_button_new_with_label("󰒭");

	gtk_widget_set_can_focus(pbtn, FALSE);
	gtk_widget_set_can_focus(bw->media_play_btn, FALSE);
	gtk_widget_set_can_focus(nbtn, FALSE);

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
	gtk_box_pack_start(GTK_BOX(text_vbox), bw->media_title_label, FALSE, FALSE, 0);

	bw->media_artist_label = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_artist_label), "media-artist-label");
	gtk_label_set_ellipsize(GTK_LABEL(bw->media_artist_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(bw->media_artist_label), 0);
	gtk_box_pack_start(GTK_BOX(text_vbox), bw->media_artist_label, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(media_box), text_vbox, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(hbox), media_box, FALSE, FALSE, 0);
#endif

	GtkWidget *spacer = gtk_label_new("");
#if WORKSPACE_CENTER
	gtk_box_pack_start(GTK_BOX(hbox), spacer, FALSE, FALSE, 0);
	gtk_box_set_center_widget(GTK_BOX(hbox), ws_box);
#else
	gtk_box_pack_start(GTK_BOX(hbox), ws_box, FALSE, FALSE, 0);
	gtk_box_set_center_widget(GTK_BOX(hbox), spacer);
#endif

	GtkWidget *metrics_grid = gtk_grid_new();
	const char *icons[] = {"", "", "󰢮", "󰋊", "󰔏", "󰔏"};

	for (int r = 0; r < BAR_CONFIG_ROW_COUNT; r++) {
		for (int c = 0; c < BAR_CONFIG_COLUMN_COUNT; c++) {
			MetricType type = bar_config.layout[r][c];
			if (type != M_NONE && type < 6) {
				gtk_grid_attach(GTK_GRID(metrics_grid), create_metric_box(icons[type], &bw->metrics[type]), c, r, 1, 1);
			}
		}
	}

	gtk_style_context_add_class(gtk_widget_get_style_context(metrics_grid), "metrics");

	GtkWidget *cvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	bw->clock_time_label = gtk_label_new("");
	gtk_widget_set_name(bw->clock_time_label, "clock-time");
	bw->clock_date_label = gtk_label_new("");
	gtk_widget_set_name(bw->clock_date_label, "clock-date");
	gtk_box_pack_start(GTK_BOX(cvbox), bw->clock_time_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(cvbox), bw->clock_date_label, FALSE, FALSE, 0);

	gtk_box_pack_end(GTK_BOX(hbox), cvbox, FALSE, FALSE, 0);

	bw->volume_btn = gtk_button_new_with_label("");
	gtk_widget_set_can_focus(bw->volume_btn, FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(bw->volume_btn), "volume-btn");
	gtk_widget_add_events(bw->volume_btn, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
	g_signal_connect(bw->volume_btn, "scroll-event", G_CALLBACK(on_volume_scroll), widgets);
	g_signal_connect(bw->volume_btn, "button-press-event", G_CALLBACK(on_volume_click), widgets);

	gtk_box_pack_end(GTK_BOX(hbox), bw->volume_btn, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), metrics_grid, FALSE, FALSE, 0);

	GtkWidget *main_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_name(main_container, "main-container");
	gtk_box_pack_start(GTK_BOX(main_container), hbox, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(win), main_container);

	widgets->bar_windows = g_list_append(widgets->bar_windows, bw);
	gtk_widget_show_all(win);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	apply_css();

	AppWidgets *widgets = g_new0(AppWidgets, 1);
	pthread_mutex_init(&widgets->mutex, NULL);
	widgets->window_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	sync_initial_state(widgets);

	GdkDisplay *display = gdk_display_get_default();
	int n_monitors = gdk_display_get_n_monitors(display);
	for (int i = 0; i < n_monitors; i++) {
		GdkMonitor *monitor = gdk_display_get_monitor(display, i);
		create_bar_window(monitor, widgets);
	}

	update_workspace_display(widgets);
	update_clock(widgets);
	g_timeout_add_seconds(1, update_timer, widgets);

	pthread_t ipc_thread, metrics_thread, media_thread;
	pthread_create(&ipc_thread, NULL, ipc_thread_func, widgets);
	pthread_create(&metrics_thread, NULL, metrics_thread_func, widgets);
	pthread_create(&media_thread, NULL, media_thread_func, widgets);

	gtk_main();

	return 0;
}
