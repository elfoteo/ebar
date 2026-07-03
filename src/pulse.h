#ifndef PULSE_H
#define PULSE_H

#include "types.h"

typedef struct PulseState {
	void *glib_mainloop;   /* pa_glib_mainloop* */
	void *context;         /* pa_context* */
	int ready;
	unsigned int default_sink_index;
	char default_sink_name[256];
	unsigned int channels;
} PulseState;

void pulse_init(AppState *state);
void pulse_set_volume(AppState *state, float vol);
void pulse_set_mute(AppState *state, int mute);
void pulse_cleanup(AppState *state);

#endif
