#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern void update_workspace_display(AppState *w);

static gboolean update_ws_idle(gpointer data) {
    update_workspace_display((AppState *)data);
    return G_SOURCE_REMOVE;
}

void sync_initial_state(AppState *w) {
    FILE *fp = popen("hyprctl monitors -j 2>/dev/null", "r");
    if (fp) {
        char buffer[8192];
        size_t n = fread(buffer, 1, sizeof(buffer) - 1, fp);
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
                char *addr_to_map = (strncmp(raw_addr, "0x", 2) == 0) ? g_strdup(raw_addr + 2) : g_strdup(raw_addr);
                char *id_p = strstr(ws_p, "\"id\":");
                if (id_p) {
                    int id = atoi(id_p + 5);
                    if (id >= 1 && id <= MAX_WORKSPACES) {
                        w->ws_win_count[id]++;
                        g_hash_table_insert(w->window_map, addr_to_map, GINT_TO_POINTER(id));
                    } else g_free(addr_to_map);
                } else g_free(addr_to_map);
                g_free(raw_addr);
            }
            if (!next) break;
            p = next;
        }
        free(buf);
        pclose(fp);
    }
}

static void handle_ipc_line(AppState *w, char *line) {
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
        if (ws_val) w->active_workspace = GPOINTER_TO_INT(ws_val);
        pthread_mutex_unlock(&w->mutex);
        g_free(addr);
        g_idle_add(update_ws_idle, w);
    } else if (strncmp(line, "openwindow>>", 12) == 0) {
        char *p = line + 12;
        char *comma = strchr(p, ',');
        if (!comma) return;
        char *addr = g_strndup(p, comma - p);
        int ws_id = atoi(comma + 1);
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

void *ipc_thread_func(void *data) {
    AppState *w = (AppState *)data;
    const char *runtime = getenv("XDG_RUNTIME_DIR"), *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!runtime || !his) return NULL;
    char path[256];
    snprintf(path, sizeof(path), "%s/hypr/%s/.socket2.sock", runtime, his);

    while (1) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) { sleep(1); continue; }
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); sleep(1); continue; }

        char buffer[8192];
        ssize_t n;
        while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            char *saveptr, *line = strtok_r(buffer, "\n", &saveptr);
            while (line) { handle_ipc_line(w, line); line = strtok_r(NULL, "\n", &saveptr); }
        }
        close(fd);
        sleep(1);
    }
}
