#include "ipc.h"
#include "bar.h"
#include "chromeos_launcher.h"
#include "chromeos_menu.h"
#include "chromeos_menu_internal.h"
#include "constants.h"
#include "chromeos_popup.h"
#include "metrics.h"
#include "util.h"
#include "widgets.h"
#include <ctype.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>

char *hyprctl_request(const char *cmd);
int check_fullscreen_on_monitor_with_data(int x, int y, const char *monitors, const char *clients);

static gboolean fullscreen_css_idle(gpointer data) {
    AppState *state = (AppState *)data;
    char *monitors = hyprctl_request("j/monitors");
    char *clients = hyprctl_request("j/clients");

    pthread_mutex_lock(&state->mutex);
    for (GList *l = state->bar_windows; l != NULL; l = l->next) {
        BarWindow *bw = (BarWindow *)l->data;
        if (bw->monitor) {
            GdkRectangle geom;
            gdk_monitor_get_geometry(bw->monitor, &geom);
            bw->has_fullscreen = check_fullscreen_on_monitor_with_data(geom.x, geom.y, monitors, clients);
        }
    }
    pthread_mutex_unlock(&state->mutex);

    free(monitors);
    free(clients);

    apply_global_css(state);
    return G_SOURCE_REMOVE;
}

static gboolean update_ws_idle(gpointer data) {
    update_workspace_display((AppState *)data);
    fullscreen_css_idle(data);
    return G_SOURCE_REMOVE;
}

/* ── socket1 helper ────────────────────────────────────────────────────────── */
/* Send a message to the extra events socket (for CLI commands). */
void send_to_ebar(const char *msg) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return;
    }
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, EXTRA_EVENTS_SOCK_PATH, sizeof(sa.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) >= 0) {
        write(fd, msg, strlen(msg));
    }
    close(fd);
}

/* Send a single request to Hyprland socket1 and return a malloc'd response.  */
char *hyprctl_request(const char *cmd) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!runtime || !his) return NULL;

    char path[256];
    snprintf(path, sizeof(path), "%s/hypr/%s/.socket.sock", runtime, his);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return NULL; }
    if (write(fd, cmd, strlen(cmd)) < 0)                     { close(fd); return NULL; }

    char *buf = malloc(HYPR_SOCKET_BUFFER_SIZE);
    if (!buf) { close(fd); return NULL; }

    size_t total = 0;
    const size_t capacity = HYPR_SOCKET_BUFFER_SIZE;
    ssize_t n;
    while (total < capacity - 1 && (n = read(fd, buf + total, capacity - total - 1)) > 0) total += (size_t)n;
    buf[total] = '\0';
    close(fd);
    return buf;
}

static int get_json_int_from(const char *obj, const char *key, const char *end_obj) {
    const char *p = strstr(obj, key);
    if (!p || (end_obj && p >= end_obj)) return -1;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '{' || *p == '"') p++;
    return atoi(p);
}

static int get_json_bool_from(const char *obj, const char *key, const char *end_obj) {
    const char *p = strstr(obj, key);
    if (!p || (end_obj && p >= end_obj)) return -1;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '{' || *p == '"') p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return -1;
}

int check_fullscreen_on_monitor_with_data(int x, int y, const char *monitors, const char *clients) {
    if (!monitors || !clients) return 0;

    int active_ws = -1;
    const char *p = monitors;
    while ((p = strstr(p, "\"name\":"))) {
        const char *next_mon = strstr(p + 7, "\"name\":");
        int mx = get_json_int_from(p, "\"x\"", next_mon);
        int my = get_json_int_from(p, "\"y\"", next_mon);
        if (mx == x && my == y) {
            const char *aw = strstr(p, "\"activeWorkspace\"");
            if (aw && (!next_mon || aw < next_mon)) {
                active_ws = get_json_int_from(aw, "\"id\"", next_mon);
            }
            break;
        }
        p += 7;
    }

    if (active_ws == -1) return 0;

    int has_fs = 0;
    int tiled_count = 0;
    p = clients;
    while ((p = strstr(p, "\"address\":"))) {
        const char *next_client = strstr(p + 10, "\"address\":");
        const char *ws = strstr(p, "\"workspace\"");
        if (ws && (!next_client || ws < next_client)) {
            int ws_id = get_json_int_from(ws, "\"id\"", next_client);
            if (ws_id == active_ws) {
                int fs = get_json_int_from(p, "\"fullscreen\"", next_client);
                int fl = get_json_bool_from(p, "\"floating\"", next_client);
                if (fs > 0) has_fs = 1;
                if (fl != 1) tiled_count++;
            }
        }
        p += 10;
    }

    return (has_fs || tiled_count == 1);
}

int check_fullscreen_on_monitor(int x, int y) {
    char *monitors = hyprctl_request("j/monitors");
    char *clients = hyprctl_request("j/clients");
    int res = check_fullscreen_on_monitor_with_data(x, y, monitors, clients);
    free(monitors);
    free(clients);
    return res;
}

/* ── keyboard layout helpers ───────────────────────────────────────────────── */

/* Uppercase in-place */
static void str_upper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* Query Hyprland for the active keyboard layout and store uppercase code.
   Uses j/getoption for the layout list and j/devices for the active keymap. */
static void fetch_layout_into(AppState *state) {
    /* 1. Layout list: "us,es,fr,..." */
    char *opt = hyprctl_request("j/getoption input:kb_layout");
    if (!opt) return;

    char layouts_str[256] = "";
    json_str(opt, "\"str\":", layouts_str, sizeof(layouts_str));
    free(opt);
    if (!layouts_str[0]) return;

    /* 2. Main keyboard active_keymap */
    char *devs = hyprctl_request("j/devices");
    if (!devs) return;

    char active_keymap[128] = "";
    /* Walk each keyboard object looking for "main": true */
    const char *p = devs;
    while ((p = strstr(p, "\"main\":"))) {
        /* Check if value is true */
        const char *val = p + 7;
        while (*val == ' ') val++;
        if (strncmp(val, "true", 4) == 0) {
            /* Find active_keymap in the enclosing object — scan backward */
            const char *obj = p;
            while (obj > devs && *obj != '{') obj--;
            json_str(obj, "\"active_keymap\":", active_keymap, sizeof(active_keymap));
            break;
        }
        p++;
    }
    free(devs);
    if (!active_keymap[0]) { strncpy(active_keymap, "English (US)", sizeof(active_keymap)-1); }

    /* 3. Map known keymap display names to layout-list index */
    int idx = 0;
    if      (strstr(active_keymap, "Spanish"))         idx = 1;
    else if (strstr(active_keymap, "French"))          idx = 2;
    else if (strstr(active_keymap, "German"))          idx = 3;
    else if (strstr(active_keymap, "Italian"))         idx = 4;
    else if (strstr(active_keymap, "Portuguese"))      idx = 5;
    /* Add more mappings as needed */

    /* 4. Extract idx-th comma-delimited token */
    char tmp[256];
    strncpy(tmp, layouts_str, sizeof(tmp) - 1); tmp[sizeof(tmp)-1] = '\0';
    char *tok = strtok(tmp, ",");
    for (int i = 0; i < idx && tok; i++) tok = strtok(NULL, ",");
    if (!tok) { tok = tmp; strncpy(tmp, layouts_str, sizeof(tmp)-1); tok = strtok(tmp, ","); }

    /* Trim and uppercase */
    while (tok && *tok == ' ') tok++;
    char out[32] = "??";
    if (tok) { strncpy(out, tok, sizeof(out) - 1); out[sizeof(out)-1] = '\0'; }
    str_upper(out);

    pthread_mutex_lock(&state->mutex);
    strncpy(state->sys_data.kb_layout, out, sizeof(state->sys_data.kb_layout) - 1);
    pthread_mutex_unlock(&state->mutex);
}

/* ── activelayout event ────────────────────────────────────────────────────── */
/* activelayout>>keyboard_name,Layout Display Name
   We just re-query to keep the index logic in one place. */
static gboolean layout_idle(gpointer data) {
    fetch_layout_into((AppState *)data);
    update_widgets_idle(data);
    return G_SOURCE_REMOVE;
}

/* ── initial state ─────────────────────────────────────────────────────────── */
void sync_initial_state(AppState *state) {
    char *monitors_json = hyprctl_request("j/monitors");
    if (monitors_json) {
        char *active = strstr(monitors_json, "\"activeWorkspace\":");
        if (active) {
            char *id_p = strstr(active, "\"id\":");
            if (id_p) state->active_workspace = atoi(id_p + 5);
        }
        free(monitors_json);
    }

    char *clients_json = hyprctl_request("j/clients");
    if (clients_json) {
        char *p = clients_json;
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
                        state->ws_win_count[id]++;
                        g_hash_table_insert(state->window_map, addr_to_map, GINT_TO_POINTER(id));
                    } else g_free(addr_to_map);
                } else g_free(addr_to_map);
                g_free(raw_addr);
            }
            if (!next) break;
            p = next;
        }
        free(clients_json);
    }

    /* Initial keyboard layout (no popen needed beyond here) */
    fetch_layout_into(state);

    state->app_list = g_app_info_get_all();

    fetch_brightness(state);
    pthread_mutex_lock(&state->mutex);
    state->sys_data.brightness_initialized = 1;
    pthread_mutex_unlock(&state->mutex);
}

/* ── IPC event dispatcher ──────────────────────────────────────────────────── */
static gboolean close_menus_idle(gpointer data) {
    close_all_chromeos_menus((AppState *)data);
    close_all_chromeos_launchers((AppState *)data);
    return G_SOURCE_REMOVE;
}

void handle_ipc_line(AppState *state, char *line) {
    if (strstr(line, "togglefloating")) {
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, (GSourceFunc)update_widgets_idle, state, NULL);
        g_idle_add(fullscreen_css_idle, state);
        return;
    }
    if (strncmp(line, "workspace>>", 11) == 0) {
        pthread_mutex_lock(&state->mutex);
        state->active_workspace = atoi(line + 11);
        pthread_mutex_unlock(&state->mutex);
        g_idle_add(update_ws_idle, state);
        g_idle_add(close_menus_idle, state);
    } else if (strncmp(line, "activewindowv2>>", 16) == 0) {
        char *p = line + 16;
        char *comma = strchr(p, ',');
        char *addr = comma ? g_strndup(p, comma - p) : g_strdup(p);
        pthread_mutex_lock(&state->mutex);
        gpointer ws_val = g_hash_table_lookup(state->window_map, addr);
        if (ws_val) state->active_workspace = GPOINTER_TO_INT(ws_val);
        pthread_mutex_unlock(&state->mutex);
        g_free(addr);
        g_idle_add(update_ws_idle, state);
        g_idle_add(close_menus_idle, state);
    } else if (strncmp(line, "openwindow>>", 12) == 0) {
        char *p = line + 12;
        char *comma = strchr(p, ',');
        if (!comma) return;
        char *addr = g_strndup(p, comma - p);
        int ws_id = atoi(comma + 1);
        pthread_mutex_lock(&state->mutex);
        g_hash_table_insert(state->window_map, addr, GINT_TO_POINTER(ws_id));
        if (ws_id >= 1 && ws_id <= MAX_WORKSPACES) state->ws_win_count[ws_id]++;
        pthread_mutex_unlock(&state->mutex);
        g_idle_add(update_ws_idle, state);
        g_idle_add(close_menus_idle, state);
    } else if (strncmp(line, "closewindow>>", 13) == 0) {
        char *addr = line + 13;
        pthread_mutex_lock(&state->mutex);
        gpointer ws_val = g_hash_table_lookup(state->window_map, addr);
        if (ws_val) {
            int ws_id = GPOINTER_TO_INT(ws_val);
            if (ws_id >= 1 && ws_id <= MAX_WORKSPACES) state->ws_win_count[ws_id]--;
            g_hash_table_remove(state->window_map, addr);
        }
        pthread_mutex_unlock(&state->mutex);
        g_idle_add(update_ws_idle, state);
        g_idle_add(close_menus_idle, state);
    } else if (strncmp(line, "movewindow>>", 12) == 0) {
        char *p = line + 12;
        char *comma = strchr(p, ',');
        if (!comma) return;
        char *addr = g_strndup(p, comma - p);
        int new_ws = atoi(comma + 1);
        pthread_mutex_lock(&state->mutex);
        gpointer old_ws_val = g_hash_table_lookup(state->window_map, addr);
        if (old_ws_val) {
            int old_ws = GPOINTER_TO_INT(old_ws_val);
            if (old_ws >= 1 && old_ws <= MAX_WORKSPACES) state->ws_win_count[old_ws]--;
        }
        g_hash_table_insert(state->window_map, g_strdup(addr), GINT_TO_POINTER(new_ws));
        if (new_ws >= 1 && new_ws <= MAX_WORKSPACES) state->ws_win_count[new_ws]++;
        g_free(addr);
        pthread_mutex_unlock(&state->mutex);
        g_idle_add(update_ws_idle, state);
        g_idle_add(close_menus_idle, state);
    } else if (strncmp(line, "activelayout>>", 14) == 0) {
        /* activelayout>>keyboard_name,Layout Display Name */
        g_idle_add(layout_idle, state);
    } else if (strncmp(line, "fullscreen>>", 12) == 0) {
        /* fullscreen>>1 = entered fullscreen, fullscreen>>0 = left fullscreen */
        int fs = atoi(line + 12);
        pthread_mutex_lock(&state->mutex);
        state->has_fullscreen = fs;
        pthread_mutex_unlock(&state->mutex);
        g_idle_add(fullscreen_css_idle, state);
        g_idle_add(close_menus_idle, state);
    } else if (strncmp(line, "brightness>>", 12) == 0) {
        float val = atof(line + 12);
        pthread_mutex_lock(&state->mutex);
        float old_bright = state->sys_data.brightness;
        int old_initialized = state->sys_data.brightness_initialized;
        int changed = (!old_initialized || old_bright != val);
        
        state->sys_data.brightness = val;
        state->sys_data.brightness_initialized = 1;

        if (changed && old_initialized) {
            for (GList *l = state->bar_windows; l != NULL; l = l->next) {
                BarWindow *bw = (BarWindow *)l->data;
                g_idle_add_full(G_PRIORITY_HIGH_IDLE, trigger_brightness_popup_idle, bw, NULL);
            }
        }
        pthread_mutex_unlock(&state->mutex);
        ensure_anim_timer(state);
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, (GSourceFunc)update_widgets_idle, state, NULL);
    }
}

void *ipc_thread_func(void *data) {
    AppState *state = (AppState *)data;
    const char *runtime = getenv("XDG_RUNTIME_DIR"), *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!runtime || !his) return NULL;
    char sock_path[256];
    snprintf(sock_path, sizeof(sock_path), "%s/hypr/%s/.socket2.sock", runtime, his);

    while (1) {
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un sock_addr;
        memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sun_family = AF_UNIX;
        strncpy(sock_addr.sun_path, sock_path, sizeof(sock_addr.sun_path)-1);
        
        if (connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
            close(sock_fd);
            usleep(IPC_RECONNECT_DELAY_US);
            continue;
        }

        struct pollfd fds[1];
        fds[0].fd = sock_fd;
        fds[0].events = POLLIN;

        while (1) {
            int ret = poll(fds, 1, -1);
            if (ret <= 0) break;

            if (fds[0].revents & POLLIN) {
                char buffer[8192];
                ssize_t n = read(sock_fd, buffer, sizeof(buffer)-1);
                if (n <= 0) break;
                buffer[n] = '\0';
                char *saveptr, *line = strtok_r(buffer, "\n", &saveptr);
                while (line) {
                    handle_ipc_line(state, line);
                    line = strtok_r(NULL, "\n", &saveptr);
                }
            } else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                break;
            }
        }
        close(sock_fd);
        usleep(IPC_RECONNECT_DELAY_US); // 100ms
    }
    return NULL;
}

