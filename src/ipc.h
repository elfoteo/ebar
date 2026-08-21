#ifndef IPC_H
#define IPC_H

#include "types.h"

void *ipc_thread_func(void *data);
void sync_initial_state(AppState *state);
int check_fullscreen_on_monitor(int x, int y);
char *hyprctl_request(const char *cmd);
void handle_ipc_line(AppState *state, char *line);
void send_to_ebar(const char *msg);

#endif
