#ifndef UTIL_H
#define UTIL_H

#include <gtk/gtk.h>
#include <stddef.h>

int json_str(const char *haystack, const char *key, char *out, size_t outsz);

long long get_time_ms(void);
void setup_transparent_window(GtkWidget *win);
const char *get_volume_icon(float vol, int muted);
int find_backlight_path(const char *filename, char *out, size_t outsz);

#endif
