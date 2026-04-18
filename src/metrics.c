#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <glob.h>

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
    FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@", "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            char *p = strstr(buf, "/ ");
            if (p) vol = atof(p + 2);
        }
        pclose(fp);
    }

    fp = popen("pactl get-sink-mute @DEFAULT_SINK@", "r");
    if (fp) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "yes")) muted = 1;
        }
        pclose(fp);
    }

    pthread_mutex_lock(&w->mutex);
    w->sys_data.vol = vol;
    w->sys_data.vol_muted = muted;
    pthread_mutex_unlock(&w->mutex);
}

extern gboolean update_widgets_idle(gpointer data);

void *metrics_thread_func(void *data) {
    AppState *w = (AppState *)data;
    while (1) {
        fetch_system_metrics(w);
        fetch_volume(w);
        g_idle_add(update_widgets_idle, w);
        sleep(1);
    }
    return NULL;
}
