#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>
#include <pthread.h>
#include <time.h>

#define MAX_WORKSPACES 10
#define MAX_LAUNCHER_APPS 15

typedef struct {
    char action[128];
    char icon_path[256];
} LauncherApp;


typedef enum {
    M_RAM = 0,
    M_CPU,
    M_GPU,
    M_DISK,
    M_TEMP,
    M_GPU_TEMP,
    M_NONE
} MetricType;

typedef enum {
    MODE_NORMAL,
    MODE_FLOATING,
    MODE_ISLAND,
    MODE_CHROMEOS
} BarMode;

typedef enum {
    POS_TOP,
    POS_BOTTOM
} BarPosition;

typedef struct {
    BarPosition position;
    BarMode mode;
    int margin;
    int height;
    int border_radius;
    int padding_h;
    int padding_v;
    int spacing;
    
    struct {
        char background[32];
        char accent[32];
        char foreground[32];
        char dim_foreground[32];
        char border[32];
        char ring_color[32];   /* circular progress ring colour (volume + nightlight) */
    } colors;

    struct {
        char family[64];
        int size;
    } font;

    struct {
        int count;
        char icon_empty[16];
        char icon_occupied[16];
        int show_empty;
    } workspaces;

    struct {
        char widgets[128];
    } left, center, right;

    struct {
        char time_format[32];
        char date_format[32];
    } clock;

    struct {
        int show_title;
        int show_artist;
        int max_title_width;
        int background;
    } media;

    struct {
        char app[64];
        int show_percent;
    } volume;

    struct {
        MetricType layout[2][3];
        int use_bars;
        char temp_path[256];
    } metrics;

    struct {
        int    temp_max;    /* identity temp (K), default 6500   */
        int    temp_min;    /* warm temp (K),     default 5400   */
        double gamma_max;   /* identity gamma,    default 100.0  */
        double gamma_min;   /* warm gamma,        default 75.0   */
        int    step;        /* level change per scroll tick, 5   */
        char   curve[16];  /* "linear" | "ease"                 */
    } nightlight;

    struct {
        LauncherApp apps[MAX_LAUNCHER_APPS];
        int count;
    } launcher;
} Config;

typedef struct {
    double ram_val, cpu_val, disk_val, temp_val;
    double gpu_val, gpu_temp_val;
    float ram_total, ram_avail;
    float vol;
    float visual_volume;
    int vol_muted;
    int vol_initialized;
    float brightness;
    float visual_brightness;
    int brightness_initialized;
    int is_playing;
    char media_title[256];
    char media_artist[256];
    int  nightlight_on;     /* 0 = off, 1 = on           */
    int  nightlight_level;  /* 0-100 curve position      */
    int  nightlight_last_level; /* remembered level for toggle */
    int  nightlight_error;  /* 1 = last IPC call failed  */
    char kb_layout[32];     /* active keyboard layout code, e.g. "US" */
    int  bat_percent;
    int  bat_charging;
    char bat_time_remaining[64];
    int  wifi_enabled;      /* 0 = off, 1 = on */
    int  wifi_connected;    /* 0 = disconnected, 1 = connected */
    char wifi_ssid[64];     /* Current SSID if connected */
    int  wifi_adapter_exists; /* 0 = no, 1 = yes */
    int  wifi_strength;     /* 0-100, -1 if unavailable */
    int  bluetooth_adapter_exists;
    int  bluetooth_powered;
    int  bluetooth_connected;
    char bluetooth_device[64];
} SystemData;

typedef struct {
    GtkWidget *window;
    GdkMonitor *monitor;
    int has_fullscreen;
    GtkWidget *main_container;
    GtkWidget *ws_labels[MAX_WORKSPACES];
    GtkWidget *clock_time_label;
    GtkWidget *clock_date_label;
    GtkWidget *metrics_widgets[6];
    GtkWidget *volume_btn;
    GtkWidget *volume_ring;
    GtkWidget *brightness_btn;
    GtkWidget *brightness_ring;
    GtkWidget *nightlight_btn;
    GtkWidget *nightlight_ring;
    GtkWidget *media_play_btn;
    GtkWidget *media_title_label;
    GtkWidget *media_artist_label;
    GtkWidget *media_sep;
    
    // Island mode specific groups
    GtkWidget *left_box;
    GtkWidget *center_box;
    GtkWidget *right_box;

    GtkWidget *cb_desk_label;
    GtkWidget *cb_date_label;
    GtkWidget *cb_sys_label;
    GtkWidget *cb_layout_label;
    GtkWidget *cb_launcher_btn;
    GtkWidget *cb_box;
    GtkWidget *menu_window;
    GtkWidget *launcher_window;
    GtkWidget *cb_menu_kb_label;
    GtkWidget *cb_menu_bat_label;
    GtkWidget *cb_menu_wifi_pill;
    GtkWidget *cb_menu_wifi_icon;
    GtkWidget *cb_menu_wifi_subtitle;
    GtkWidget *cb_menu_wifi_arrow;
    GtkWidget *cb_menu_main_box;
    GtkWidget *cb_menu_brightness_slider;
    GtkWidget *cb_menu_volume_slider;

#define POPUP_TYPE_BRIGHTNESS 0
#define POPUP_TYPE_VOLUME     1
#define POPUP_COUNT           2

    /* Passive popups */
    GtkWidget *popup_window;
    GtkWidget *popup_slider_box[POPUP_COUNT];
    GtkWidget *popup_slider_range[POPUP_COUNT];
    guint popup_timer[POPUP_COUNT];
    double popup_opacity;
    guint popup_fade_timer;
    int popup_hovered;
    struct AppState *state; /* Backpointer for convenience */
    } BarWindow;

typedef struct AppState {
    int active_workspace;
    int ws_win_count[MAX_WORKSPACES + 1];
    pthread_mutex_t mutex;
    GHashTable *window_map;
    long long prev_total, prev_idle;
    GList *bar_windows;
    SystemData sys_data;
    time_t last_manual_vol_update;
    time_t last_manual_bright_update;
    Config config;
    int has_fullscreen;  /* 1 when a fullscreen window is active */
    GList *app_list;
    guint anim_timer_id;
    gpointer nm_client; /* NMClient*, kept as gpointer to avoid header dependency */
} AppState;

#endif
