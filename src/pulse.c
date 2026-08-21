#include "pulse.h"
#include "chromeos_popup.h"
#include "widgets.h"
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <stdio.h>
#include <string.h>

#define PA_EVENT_DEBOUNCE_MS 30

/* userdata for one query chain (get_server_info -> get_sink_info_by_name) */
typedef struct {
	AppState *state;
	unsigned int seq;
} PulseQuery;

static void pulse_sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
	PulseQuery *q = (PulseQuery *)userdata;
	AppState *state = q->state;
	PulseState *ps = (PulseState *)state->pulse;
	(void)c;

	if (eol > 0) {
		g_free(q);
		return;
	}
	if (!i)
		return;

	/* A newer query chain has been issued meanwhile: drop this stale response
	 * instead of overwriting state with outdated volume/mute values. */
	if (q->seq != ps->query_seq)
		return;

	pthread_mutex_lock(&state->mutex);
	int was_init = state->sys_data.vol_initialized;
	float old_vol = state->sys_data.vol;
	int old_muted = state->sys_data.vol_muted;

	float new_vol = (float)pa_cvolume_avg(&i->volume) * 100.0f / (float)PA_VOLUME_NORM;
	int new_muted = i->mute ? 1 : 0;

	state->sys_data.vol = new_vol;
	state->sys_data.vol_muted = new_muted;
	state->sys_data.vol_initialized = 1;
	((PulseState *)(state->pulse))->default_sink_index = i->index;
	((PulseState *)(state->pulse))->channels = i->volume.channels;
	pthread_mutex_unlock(&state->mutex);

	int changed = (!was_init || old_vol != new_vol || old_muted != new_muted);
	g_idle_add(update_widgets_idle, state);

	if (changed && was_init) {
		pthread_mutex_lock(&state->mutex);
		for (GList *l = state->bar_windows; l != NULL; l = l->next)
			g_idle_add(trigger_volume_popup_idle, l->data);
		pthread_mutex_unlock(&state->mutex);
	}
}

static void pulse_server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
	PulseQuery *q = (PulseQuery *)userdata;
	AppState *state = q->state;

	if (!i) {
		g_free(q);
		return;
	}

	PulseState *ps = (PulseState *)state->pulse;
	strncpy(ps->default_sink_name, i->default_sink_name, sizeof(ps->default_sink_name) - 1);
	ps->default_sink_name[sizeof(ps->default_sink_name) - 1] = '\0';

	pa_context_get_sink_info_by_name(c, i->default_sink_name, pulse_sink_info_callback, q);
}

/* Issue a fresh query chain; bumps the sequence so any in-flight response
 * from a previous chain is dropped as stale. */
static void pulse_issue_query(AppState *state) {
	PulseState *ps = (PulseState *)state->pulse;
	if (!ps || !ps->context)
		return;
	if (pa_context_get_state((pa_context *)ps->context) != PA_CONTEXT_READY)
		return;

	PulseQuery *q = g_new(PulseQuery, 1);
	q->state = state;
	q->seq = ++ps->query_seq;
	pa_context_get_server_info((pa_context *)ps->context, pulse_server_info_callback, q);
}

static gboolean pulse_debounced_query(gpointer data) {
	AppState *state = (AppState *)data;
	PulseState *ps = (PulseState *)state->pulse;
	ps->debounce_id = 0;
	pulse_issue_query(state);
	return G_SOURCE_REMOVE;
}

/* Coalesce subscription event storms (a single wpctl/pactl command emits
 * several sink events) into one query after a short quiet window. */
static void pulse_schedule_query(AppState *state) {
	PulseState *ps = (PulseState *)state->pulse;
	if (!ps || ps->debounce_id)
		return;
	ps->debounce_id = g_timeout_add(PA_EVENT_DEBOUNCE_MS, pulse_debounced_query, state);
}

static void pulse_subscription_callback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
	AppState *state = (AppState *)userdata;
	(void)c;
	(void)idx;

	/* On any sink event (NEW or CHANGE), re-query the DEFAULT sink volume,
	 * mirroring the pactl subscribe + pactl get-sink-volume @DEFAULT_SINK@
	 * behaviour: we always show the default sink, not the event sink. */
	switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
	case PA_SUBSCRIPTION_EVENT_SINK:
		if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_REMOVE)
			pulse_schedule_query(state);
		break;
	case PA_SUBSCRIPTION_EVENT_SERVER:
		pulse_schedule_query(state);
		break;
	default:
		break;
	}
}

static void pulse_context_state_callback(pa_context *c, void *userdata) {
	AppState *state = (AppState *)userdata;

	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
		pa_context_set_subscribe_callback(c, pulse_subscription_callback, state);
		pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
		pulse_issue_query(state);
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		fprintf(stderr, "PulseAudio: connection failed/terminated\n");
		break;
	default:
		break;
	}
}

void pulse_init(AppState *state) {
	PulseState *ps = g_new0(PulseState, 1);
	state->pulse = (PulseState *)ps;

	ps->glib_mainloop = pa_glib_mainloop_new(NULL);
	if (!ps->glib_mainloop) {
		fprintf(stderr, "PulseAudio: failed to create glib mainloop\n");
		g_free(ps);
		state->pulse = NULL;
		return;
	}

	pa_mainloop_api *api = pa_glib_mainloop_get_api((pa_glib_mainloop *)ps->glib_mainloop);

	ps->context = pa_context_new(api, "ebar");
	if (!ps->context) {
		fprintf(stderr, "PulseAudio: failed to create context\n");
		pa_glib_mainloop_free((pa_glib_mainloop *)ps->glib_mainloop);
		g_free(ps);
		state->pulse = NULL;
		return;
	}

	pa_context_set_state_callback((pa_context *)ps->context, pulse_context_state_callback, state);

	if (pa_context_connect((pa_context *)ps->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		fprintf(stderr, "PulseAudio: failed to connect: %s\n",
			pa_strerror(pa_context_errno((pa_context *)ps->context)));
		pa_context_unref((pa_context *)ps->context);
		pa_glib_mainloop_free((pa_glib_mainloop *)ps->glib_mainloop);
		g_free(ps);
		state->pulse = NULL;
	}
}

void pulse_set_volume(AppState *state, float vol) {
	if (!state->pulse)
		return;
	PulseState *ps = (PulseState *)state->pulse;
	if (!ps->context || !ps->channels)
		return;
	if (pa_context_get_state((pa_context *)ps->context) != PA_CONTEXT_READY)
		return;

	pa_cvolume cvol;
	pa_cvolume_set(&cvol, ps->channels, (pa_volume_t)(vol / 100.0 * PA_VOLUME_NORM));
	pa_context_set_sink_volume_by_name((pa_context *)ps->context, ps->default_sink_name, &cvol, NULL, NULL);
}

void pulse_set_mute(AppState *state, int mute) {
	if (!state->pulse)
		return;
	PulseState *ps = (PulseState *)state->pulse;
	if (!ps->context || !ps->default_sink_name[0])
		return;
	if (pa_context_get_state((pa_context *)ps->context) != PA_CONTEXT_READY)
		return;

	pa_context_set_sink_mute_by_name((pa_context *)ps->context, ps->default_sink_name, mute, NULL, NULL);
}

void pulse_cleanup(AppState *state) {
	if (!state->pulse)
		return;
	PulseState *ps = (PulseState *)state->pulse;
	if (ps->debounce_id) {
		g_source_remove(ps->debounce_id);
		ps->debounce_id = 0;
	}
	if (ps->context) {
		pa_context_disconnect((pa_context *)ps->context);
		pa_context_unref((pa_context *)ps->context);
	}
	if (ps->glib_mainloop)
		pa_glib_mainloop_free((pa_glib_mainloop *)ps->glib_mainloop);
	g_free(ps);
	state->pulse = NULL;
}
