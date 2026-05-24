#ifndef IPC_H
#define IPC_H

#include "types.h"

void *ipc_thread_func(void *data);
void *extra_events_thread_func(void *data);
void sync_initial_state(AppState *w);
int check_fullscreen_on_monitor(int x, int y);

#endif
