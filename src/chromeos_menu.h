#ifndef CHROMEOS_MENU_H
#define CHROMEOS_MENU_H

#include "types.h"

void toggle_chromeos_menu(BarWindow *bw, AppState *state);
void setup_chromeos_menu_toggle(BarWindow *bw, AppState *state);
void close_all_chromeos_menus(AppState *state);
gboolean chromeos_any_menu_open(AppState *state);
void chromeos_menu_apply_css(void);

#endif
