#ifndef NIGHTLIGHT_H
#define NIGHTLIGHT_H

#include "types.h"

void nightlight_save_state(int active, int level);
int  nightlight_load_state(int *active);
void nightlight_apply(AppState *state);
void nightlight_reset(AppState *state);
gboolean nightlight_retry_cb(gpointer data);
void nightlight_init(AppState *state);
void nightlight_set_level(AppState *state, int level);

#endif
