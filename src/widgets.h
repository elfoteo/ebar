#ifndef WIDGETS_H
#define WIDGETS_H

#include "types.h"

GtkWidget *widget_workspaces(BarWindow *bw, AppState *state);
GtkWidget *widget_clock(BarWindow *bw, AppState *state);
GtkWidget *widget_media(BarWindow *bw, AppState *state);
GtkWidget *widget_volume(BarWindow *bw, AppState *state);
GtkWidget *widget_metrics(BarWindow *bw, AppState *state);
GtkWidget *widget_nightlight(BarWindow *bw, AppState *state);
GtkWidget *widget_brightness(BarWindow *bw, AppState *state);
GtkWidget *widget_launcher(BarWindow *bw, AppState *state);

gboolean update_widgets_idle(gpointer data);
void update_widgets_idle_reset(void);
void update_workspace_display(AppState *state);
gboolean timer_update_widgets(gpointer data); /* use this for g_timeout_add */
const char *get_wifi_icon(int q);
const char *get_battery_icon(int cap, int charging);
#endif

