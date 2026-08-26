#ifndef UTIL_H
#define UTIL_H

#include "types.h"
#include <gtk/gtk.h>
#include <stddef.h>

int json_str(const char *haystack, const char *key, char *out, size_t outsz);

long long get_time_ms(void);
void setup_transparent_window(GtkWidget *win);
const char *get_volume_icon(float vol, int muted);
int find_backlight_path(const char *filename, char *out, size_t outsz);

void lighten_hex_color(const char *hex, float factor, char *out, size_t outsz);
void apply_css_from_string(const char *css, guint priority);
void apply_forcergbx_bypass(const char *window_title, const char *layer_name);

float get_current_brightness(void);
void set_brightness(float target_pct, int transition_ms);

void handle_volume_slider_changed(GtkRange *range, AppState *state);
void handle_brightness_slider_changed(GtkRange *range, AppState *state);

#endif
