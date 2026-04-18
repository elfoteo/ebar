#ifndef TYPES_H
#define TYPES_H

#include <gtk/gtk.h>
#include <pthread.h>
#include <time.h>

#define MAX_WORKSPACES 10

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
    MODE_ISLAND
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
} Config;

typedef struct {
    double ram_val, cpu_val, disk_val, temp_val;
    double gpu_val, gpu_temp_val;
    float ram_total, ram_avail;
    float vol;
    int vol_muted;
    int is_playing;
    char media_title[256];
    char media_artist[256];
} SystemData;

typedef struct {
    GtkWidget *window;
    GtkWidget *main_container;
    GtkWidget *ws_labels[MAX_WORKSPACES];
    GtkWidget *clock_time_label;
    GtkWidget *clock_date_label;
    GtkWidget *metrics_widgets[6];
    GtkWidget *volume_btn;
    GtkWidget *media_play_btn;
    GtkWidget *media_title_label;
    GtkWidget *media_artist_label;
    GtkWidget *media_sep;
    
    // Island mode specific groups
    GtkWidget *left_box;
    GtkWidget *center_box;
    GtkWidget *right_box;

} BarWindow;

typedef struct {
    int active_workspace;
    int ws_win_count[MAX_WORKSPACES + 1];
    pthread_mutex_t mutex;
    GHashTable *window_map;
    long long prev_total, prev_idle;
    GList *bar_windows;
    SystemData sys_data;
    time_t last_manual_vol_update;
    Config config;
} AppState;

#endif
