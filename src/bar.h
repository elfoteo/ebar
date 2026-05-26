#ifndef BAR_H
#define BAR_H

#include "types.h"

void create_bar_window(GdkMonitor *monitor, AppState *state);
void apply_global_css(AppState *state);
void ensure_anim_timer(AppState *state);

#endif
