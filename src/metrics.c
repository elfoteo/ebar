#include "metrics.h"
#include "bar.h"
#include "bluetooth.h"
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <glob.h>
#include <libupower-glib/upower.h>
extern gboolean update_widgets_idle(gpointer data);
extern gboolean trigger_volume_popup_idle(gpointer data);

static int layout_has_gpu(Config *cfg) {
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) {
            MetricType t = cfg->metrics.layout[r][c];
            if (t == M_GPU || t == M_GPU_TEMP)
                return 1;
        }
    return 0;
}

static void fetch_system_metrics(AppState *w) {
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

    if (strcmp(w->config.metrics.temp_path, "auto") != 0) {
        fp = fopen(w->config.metrics.temp_path, "r");
        if (fp) {
            int t;
            if (fscanf(fp, "%d", &t) == 1)
                temp_val = t / 1000.0;
            fclose(fp);
        }
    } else {
        // Fallback auto-detection from original code
        fp = fopen("/sys/class/thermal/thermal_zone1/temp", "r");
        if (fp) {
            int t;
            if (fscanf(fp, "%d", &t) == 1) temp_val = t / 1000.0;
            fclose(fp);
        }
        if (temp_val == 0) {
            glob_t g;
            if (glob("/sys/class/hwmon/hwmon*/temp*_label", 0, NULL, &g) == 0) {
                double sum = 0; int count = 0;
                for (size_t i = 0; i < g.gl_pathc; i++) {
                    FILE *f = fopen(g.gl_pathv[i], "r");
                    if (f) {
                        char lbl[64];
                        if (fgets(lbl, sizeof(lbl), f)) {
                            if (strstr(lbl, "Core") || strstr(lbl, "Package")) {
                                char ipath[256]; snprintf(ipath, sizeof(ipath), "%s", g.gl_pathv[i]);
                                char *p = strstr(ipath, "_label");
                                if (p) {
                                    memcpy(p, "_input", 6);
                                    FILE *fi = fopen(ipath, "r");
                                    if (fi) {
                                        int v; if (fscanf(fi, "%d", &v) == 1) { sum += v; count++; }
                                        fclose(fi);
                                    }
                                }
                            }
                        }
                        fclose(f);
                    }
                }
                if (count > 0) temp_val = sum / (count * 1000.0);
                globfree(&g);
            }
        }
        if (temp_val == 0) {
            fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
            if (fp) {
                int t; if (fscanf(fp, "%d", &t) == 1) temp_val = t / 1000.0;
                fclose(fp);
            }
        }
    }

    double gpu_val = 0, gpu_temp_val = 0;
    if (layout_has_gpu(&w->config)) {
        fp = popen("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
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

static void fetch_volume(AppState *w) {
    if (time(NULL) - w->last_manual_vol_update < 2) return;

    float vol = 0;
    int muted = 0;
    FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@; pactl get-sink-mute @DEFAULT_SINK@", "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "Volume:")) {
                char *p = strstr(buf, "/ ");
                if (p) vol = atof(p + 2);
            } else if (strstr(buf, "Mute:")) {
                if (strstr(buf, "yes")) muted = 1;
            }
        }
        pclose(fp);
    }

    pthread_mutex_lock(&w->mutex);
    w->sys_data.vol = vol;
    w->sys_data.vol_muted = muted;
    pthread_mutex_unlock(&w->mutex);
}

void fetch_brightness(AppState *w) {
    static char actual_path[256] = "";
    static char max_path[256] = "";

    if (actual_path[0] == '\0') {
        glob_t g;
        if (glob("/sys/class/backlight/*/actual_brightness", 0, NULL, &g) == 0) {
            if (g.gl_pathc > 0) {
                strncpy(actual_path, g.gl_pathv[0], sizeof(actual_path) - 1);
                char *p = strstr(actual_path, "actual_brightness");
                if (p) {
                    size_t prefix_len = p - actual_path;
                    strncpy(max_path, actual_path, prefix_len);
                    strcpy(max_path + prefix_len, "max_brightness");
                }
            }
            globfree(&g);
        }
    }

    if (actual_path[0] == '\0') return;

    long actual = 0, max = 1;
    FILE *f = fopen(actual_path, "r");
    if (f) {
        if (fscanf(f, "%ld", &actual) != 1) actual = 0;
        fclose(f);
    }
    f = fopen(max_path, "r");
    if (f) {
        if (fscanf(f, "%ld", &max) != 1) max = 1;
        fclose(f);
    }

    float b = (float)actual * 100.0f / (float)max;
    pthread_mutex_lock(&w->mutex);
    w->sys_data.brightness = b;
    pthread_mutex_unlock(&w->mutex);
}

static void fetch_battery(AppState *w) {
    int percent = -1, charging = 0;
    char time_rem[64] = "";

    UpClient *client = up_client_new();
    if (client) {
        GPtrArray *devices = up_client_get_devices2(client);
        if (devices) {
            for (guint i = 0; i < devices->len; i++) {
                UpDevice *device = g_ptr_array_index(devices, i);
                UpDeviceKind kind;
                g_object_get(device, "kind", &kind, NULL);

                if (kind == UP_DEVICE_KIND_BATTERY) {
                    double percentage;
                    gint64 t_empty, t_full;
                    UpDeviceState state;

                    g_object_get(device, "percentage", &percentage, "state", &state, 
                                 "time-to-empty", &t_empty, "time-to-full", &t_full, NULL);

                    percent = (int)percentage;
                    if (state == UP_DEVICE_STATE_CHARGING) {
                        charging = 1;
                        int h = (int)(t_full / 3600);
                        int m = (int)((t_full % 3600) / 60);
                        if (h > 0 || m > 0) snprintf(time_rem, sizeof(time_rem), "Time to full: %d:%02d", h, m);
                        else strncpy(time_rem, "Charging", sizeof(time_rem) - 1);
                    } else if (state == UP_DEVICE_STATE_DISCHARGING) {
                        int h = (int)(t_empty / 3600);
                        int m = (int)((t_empty % 3600) / 60);
                        if (h > 0 || m > 0) snprintf(time_rem, sizeof(time_rem), "Remaining: %d:%02d", h, m);
                    } else if (state == UP_DEVICE_STATE_FULLY_CHARGED) {
                        charging = 1;
                        strncpy(time_rem, "Charged", sizeof(time_rem) - 1);
                    }
                    break;
                }
            }
            g_ptr_array_unref(devices);
        }
        g_object_unref(client);
    }

    pthread_mutex_lock(&w->mutex);
    w->sys_data.bat_percent = percent;
    w->sys_data.bat_charging = charging;
    strncpy(w->sys_data.bat_time_remaining, time_rem, sizeof(w->sys_data.bat_time_remaining) - 1);
    pthread_mutex_unlock(&w->mutex);
}

static void fetch_wifi(AppState *w) {
    int exists = 0;
    int enabled = 0;
    int connected = 0;
    int strength = -1;
    char ssid[64] = "";
    wifi_get_status(&exists, &enabled, &connected, ssid, sizeof(ssid), &strength);

    pthread_mutex_lock(&w->mutex);
    w->sys_data.wifi_adapter_exists = exists;
    w->sys_data.wifi_enabled = enabled;
    w->sys_data.wifi_connected = connected;
    w->sys_data.wifi_strength = strength;
    g_strlcpy(w->sys_data.wifi_ssid, ssid, sizeof(w->sys_data.wifi_ssid));
    pthread_mutex_unlock(&w->mutex);
}

static void fetch_bluetooth(AppState *w) {
    int exists = 0;
    int powered = 0;
    int connected = 0;
    char device[64] = "";

    bluetooth_get_status(&exists, &powered, &connected, device, sizeof(device));

    pthread_mutex_lock(&w->mutex);
    w->sys_data.bluetooth_adapter_exists = exists;
    w->sys_data.bluetooth_powered = powered;
    w->sys_data.bluetooth_connected = connected;
    g_strlcpy(w->sys_data.bluetooth_device, device, sizeof(w->sys_data.bluetooth_device));
    pthread_mutex_unlock(&w->mutex);
}

extern gboolean update_widgets_idle(gpointer data);
void *volume_thread_func(void *data) {
    AppState *w = (AppState *)data;
    fetch_volume(w);
    pthread_mutex_lock(&w->mutex);
    w->sys_data.vol_initialized = 1;
    pthread_mutex_unlock(&w->mutex);
    g_idle_add(update_widgets_idle, w);

    while (1) {
        FILE *fp = popen("stdbuf -oL pactl subscribe 2>/dev/null", "r");
        if (!fp) {
            sleep(5);
            continue;
        }

        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "change") && strstr(buf, "on sink #")) {
                pthread_mutex_lock(&w->mutex);
                float old_vol = w->sys_data.vol;
                int old_muted = w->sys_data.vol_muted;
                int old_initialized = w->sys_data.vol_initialized;
                pthread_mutex_unlock(&w->mutex);

                fetch_volume(w);
                g_idle_add(update_widgets_idle, w);

                pthread_mutex_lock(&w->mutex);
                float new_vol = w->sys_data.vol;
                int new_muted = w->sys_data.vol_muted;
                int changed = (!old_initialized || old_vol != new_vol || old_muted != new_muted);
                w->sys_data.vol_initialized = 1;
                pthread_mutex_unlock(&w->mutex);

                if (changed && old_initialized) {
                    /* Trigger volume popup on each bar window */
                    pthread_mutex_lock(&w->mutex);
                    for (GList *l = w->bar_windows; l != NULL; l = l->next)
                        g_idle_add(trigger_volume_popup_idle, l->data);
                    pthread_mutex_unlock(&w->mutex);
                }
            }
        }
        pclose(fp);
        sleep(2);
    }
    return NULL;
}

void *metrics_thread_func(void *data) {
    AppState *w = (AppState *)data;
    int count = 0;
    while (1) {
        fetch_system_metrics(w);
        if (count % 5 == 0) {
            fetch_battery(w);
            fetch_wifi(w);
            fetch_bluetooth(w);
        }
        g_idle_add(update_widgets_idle, w);
        sleep(1);
        count++;
    }
    return NULL;
}

