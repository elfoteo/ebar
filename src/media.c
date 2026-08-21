#include "media.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *media_thread_func(void *data) {
    AppState *state = (AppState *)data;
    while (1) {
        FILE *fp = popen("playerctl -F metadata --format '{{status}}::{{title}}::{{artist}}' 2>/dev/null", "r");
        if (!fp) {
            sleep(5);
            continue;
        }

        char buf[1024];
        while (fgets(buf, sizeof(buf), fp)) {
            char *sep1 = strstr(buf, "::");
            if (!sep1) continue;
            *sep1 = '\0';
            char *status = buf;

            char *sep2 = strstr(sep1 + 2, "::");
            if (!sep2) continue;
            *sep2 = '\0';
            char *title = sep1 + 2;
            char *artist = sep2 + 2;

            size_t len = strlen(artist);
            if (len > 0 && artist[len - 1] == '\n') artist[len - 1] = '\0';

            int playing = (strstr(status, "Playing") != NULL);

            pthread_mutex_lock(&state->mutex);
            state->sys_data.is_playing = playing;
            strncpy(state->sys_data.media_title, title, sizeof(state->sys_data.media_title) - 1);
            strncpy(state->sys_data.media_artist, artist, sizeof(state->sys_data.media_artist) - 1);
            pthread_mutex_unlock(&state->mutex);

            g_idle_add(update_widgets_idle, state);
        }
        pclose(fp);

        pthread_mutex_lock(&state->mutex);
        state->sys_data.is_playing = 0;
        state->sys_data.media_title[0] = '\0';
        state->sys_data.media_artist[0] = '\0';
        pthread_mutex_unlock(&state->mutex);
        g_idle_add(update_widgets_idle, state);

        sleep(2);
    }
    return NULL;
}
