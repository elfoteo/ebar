#ifndef BAR_H
#define BAR_H

#include "types.h"

typedef struct { BarWindow *bw; AppState *state; } BarDestroyCtx;

void create_bar_window(GdkMonitor *monitor, AppState *state);
void apply_global_css(AppState *state);
void ensure_anim_timer(AppState *state);
void on_bar_window_destroy(GtkWidget *widget, gpointer data);

#endif
