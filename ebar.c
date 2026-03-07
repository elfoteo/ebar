#include "gtk-layer-shell.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/statvfs.h>

#define MAX_WORKSPACES 10

typedef struct {
  GtkWidget *window;
  GtkWidget *ws_labels[MAX_WORKSPACES];
  GtkWidget *clock_time_label;
  GtkWidget *clock_date_label;
  GtkWidget *ram_scale;
  GtkWidget *cpu_scale;
  GtkWidget *disk_scale;
  GtkWidget *temp_scale;
} BarWindow;

typedef struct {
  int active_workspace;
  int ws_win_count[MAX_WORKSPACES + 1];
  pthread_mutex_t mutex;
  GHashTable *window_map; // address -> workspace_id
  long long prev_total, prev_idle;
  GList *bar_windows;
} AppWidgets;

static void apply_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();
  const char *css = 
    "* { font-family: JetBrainsMonoNerdFont; background: none; box-shadow: none; border: none; } "
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
    ".workspace-active { background-color: rgba(255, 255, 255, 0.2); color: #ffffff; } "
    "#clock-time { font-size: 14px; font-weight: normal; color: #ffffff; } "
    "#clock-date { font-size: 14px; font-weight: normal; color: #ffffff; } "
    ".metric-icon { font-size: 14px; color: rgba(255,255,255,0.7); margin: 0px 0px 0px 6px; padding: 0; } "
    ".metric-scale { padding: 0; margin: 0; } "
    ".metric-scale slider { all: unset; min-width: 0; min-height: 0; opacity: 0; } "
    ".metric-scale trough highlight { background-color: #D35D6E; border-radius: 10px; } "
    ".metric-scale trough { "
    "  background-color: #4e4e4e; "
    "  border-radius: 50px; "
    "  min-height: 3px; "
    "  min-width: 55px; "
    "  margin: 2px 12px; "
    "  padding: 0; "
    "} ";

  gtk_css_provider_load_from_data(provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                            GTK_STYLE_PROVIDER(provider),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void sync_initial_state(AppWidgets *w) {
    FILE *fp = popen("hyprctl monitors -j 2>/dev/null", "r");
    if (fp) {
        char buffer[8192];
        size_t n = fread(buffer, 1, sizeof(buffer)-1, fp);
        buffer[n] = '\0';
        char *active = strstr(buffer, "\"activeWorkspace\":");
        if (active) {
            char *id_p = strstr(active, "\"id\":");
            if (id_p) w->active_workspace = atoi(id_p + 5);
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
                // Strip 0x if present to match IPC addresses (e.g., 0x55c7... -> 55c7...)
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
            if (!next) break;
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

static void update_system_metrics(AppWidgets *w) {
    double ram_val = 0, cpu_val = 0, disk_val = 0, temp_val = 0;
    
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        long total = 0, avail = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            if (strncmp(buf, "MemTotal:", 9) == 0) total = atol(buf + 10);
            else if (strncmp(buf, "MemAvailable:", 13) == 0) avail = atol(buf + 13);
        }
        fclose(fp);
        if (total > 0) ram_val = 100.0 * (total - avail) / total;
    }
    
    fp = fopen("/proc/stat", "r");
    if (fp) {
        long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
        if (fscanf(fp, "cpu  %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice) == 10) {
            long long current_idle = idle + iowait;
            long long current_total = user + nice + system + idle + iowait + irq + softirq + steal;
            if (w->prev_total > 0) {
                long long total_diff = current_total - w->prev_total;
                long long idle_diff = current_idle - w->prev_idle;
                if (total_diff > 0) cpu_val = 100.0 * (total_diff - idle_diff) / total_diff;
            }
            w->prev_total = current_total; w->prev_idle = current_idle;
        }
        fclose(fp);
    }
    
    struct statvfs st;
    if (statvfs("/", &st) == 0) {
        disk_val = 100.0 * (1.0 - (double)st.f_bavail / (double)st.f_blocks);
    }
    
    fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) fp = fopen("/sys/class/hwmon/hwmon0/temp1_input", "r");
    if (fp) {
        int temp;
        if (fscanf(fp, "%d", &temp) == 1) temp_val = temp / 1000.0;
        fclose(fp);
    }

    for (GList *l = w->bar_windows; l != NULL; l = l->next) {
        BarWindow *bw = (BarWindow *)l->data;
        gtk_range_set_value(GTK_RANGE(bw->ram_scale), ram_val);
        gtk_range_set_value(GTK_RANGE(bw->cpu_scale), cpu_val);
        gtk_range_set_value(GTK_RANGE(bw->disk_scale), disk_val);
        gtk_range_set_value(GTK_RANGE(bw->temp_scale), temp_val);
    }
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
  update_system_metrics(w);
}

static gboolean update_ws_idle(gpointer data) { update_workspace_display((AppWidgets*)data); return G_SOURCE_REMOVE; }
static gboolean update_timer(gpointer data) { update_clock((AppWidgets*)data); return G_SOURCE_CONTINUE; }

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
        if (!comma) return;
        char *addr = g_strndup(p, comma - p);
        char *ws_p = comma + 1;
        int ws_id = atoi(ws_p);
        pthread_mutex_lock(&w->mutex);
        g_hash_table_insert(w->window_map, addr, GINT_TO_POINTER(ws_id));
        if (ws_id >= 1 && ws_id <= MAX_WORKSPACES) w->ws_win_count[ws_id]++;
        pthread_mutex_unlock(&w->mutex);
        g_idle_add(update_ws_idle, w);
    } else if (strncmp(line, "closewindow>>", 13) == 0) {
        char *addr = line + 13;
        pthread_mutex_lock(&w->mutex);
        gpointer ws_val = g_hash_table_lookup(w->window_map, addr);
        if (ws_val) {
            int ws_id = GPOINTER_TO_INT(ws_val);
            if (ws_id >= 1 && ws_id <= MAX_WORKSPACES) w->ws_win_count[ws_id]--;
            g_hash_table_remove(w->window_map, addr);
        }
        pthread_mutex_unlock(&w->mutex);
        g_idle_add(update_ws_idle, w);
    } else if (strncmp(line, "movewindow>>", 12) == 0) {
        char *p = line + 12;
        char *comma = strchr(p, ',');
        if (!comma) return;
        char *addr = g_strndup(p, comma - p);
        int new_ws = atoi(comma + 1);
        pthread_mutex_lock(&w->mutex);
        gpointer old_ws_val = g_hash_table_lookup(w->window_map, addr);
        if (old_ws_val) {
            int old_ws = GPOINTER_TO_INT(old_ws_val);
            if (old_ws >= 1 && old_ws <= MAX_WORKSPACES) w->ws_win_count[old_ws]--;
        }
        g_hash_table_insert(w->window_map, g_strdup(addr), GINT_TO_POINTER(new_ws));
        if (new_ws >= 1 && new_ws <= MAX_WORKSPACES) w->ws_win_count[new_ws]++;
        g_free(addr);
        pthread_mutex_unlock(&w->mutex);
        g_idle_add(update_ws_idle, w);
    }
}

static void *ipc_thread_func(void *data) {
  AppWidgets *w = (AppWidgets *)data;
  const char *runtime = getenv("XDG_RUNTIME_DIR"), *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!runtime || !his) return NULL;
  char path[256]; snprintf(path, sizeof(path), "%s/hypr/%s/.socket2.sock", runtime, his);

  while (1) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { sleep(1); continue; }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); sleep(1); continue; }

    char buffer[8192]; ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
      buffer[n] = '\0';
      char *saveptr, *line = strtok_r(buffer, "\n", &saveptr);
      while (line) { handle_ipc_line(w, line); line = strtok_r(NULL, "\n", &saveptr); }
    }
    close(fd); sleep(1);
  }
}

static GtkWidget* create_metric_box(const char *icon_str, GtkWidget **out_scale) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget *icon = gtk_label_new(icon_str);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "metric-icon");

    *out_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(*out_scale), FALSE);
    gtk_widget_set_can_focus(*out_scale, FALSE);
    gtk_widget_set_size_request(*out_scale, 55, 10);
    gtk_widget_set_valign(*out_scale, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(*out_scale), "metric-scale");

    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *out_scale, FALSE, FALSE, 0);
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

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(hbox), ws_box, FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(hbox), spacer);

    GtkWidget *metrics_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    gtk_box_pack_start(GTK_BOX(left_col), create_metric_box("", &bw->ram_scale), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_col), create_metric_box("", &bw->cpu_scale), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_col), create_metric_box("󰋊", &bw->disk_scale), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_col), create_metric_box("󰔏", &bw->temp_scale), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(metrics_box), left_col, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(metrics_box), right_col, FALSE, FALSE, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(metrics_box), "metrics");

    GtkWidget *cvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    bw->clock_time_label = gtk_label_new("");
    gtk_widget_set_name(bw->clock_time_label, "clock-time");
    bw->clock_date_label = gtk_label_new("");
    gtk_widget_set_name(bw->clock_date_label, "clock-date");
    gtk_box_pack_start(GTK_BOX(cvbox), bw->clock_time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cvbox), bw->clock_date_label, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(hbox), cvbox, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), metrics_box, FALSE, FALSE, 0);

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

    pthread_t ipc_thread;
    pthread_create(&ipc_thread, NULL, ipc_thread_func, widgets);

    gtk_main();

    return 0;
}
