#ifndef CHROMEOS_BAR_H
#define CHROMEOS_BAR_H

#include "types.h"
#include "chromeos_popup.h"

void create_chromeos_bar_window(GdkMonitor *monitor, AppState *state);
void apply_chromeos_css(AppState *state);
void chromeos_update_tray(AppState *w, BarWindow *bw, SystemData *d,
						  int time_changed, int wifi_changed, int bat_changed,
						  int kb_changed, int vol_changed, int brightness_changed,
						  time_t now, struct tm *tmv);

#endif
