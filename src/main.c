#include "types.h"
#include "config.h"
#include "bar.h"
#include "widgets.h"
#include "ipc.h"
#include "metrics.h"
#include "media.h"

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);


    AppState *state = g_new0(AppState, 1);
    pthread_mutex_init(&state->mutex, NULL);
    state->window_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    config_load(&state->config);
    sync_initial_state(state);
    apply_global_css(state);

    GdkDisplay *display = gdk_display_get_default();
    int n_monitors = gdk_display_get_n_monitors(display);
    for (int i = 0; i < n_monitors; i++) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        create_bar_window(monitor, state);
    }

    update_widgets_idle(state);
    g_timeout_add(1000, timer_update_widgets, state);

    pthread_t ipc_thread, metrics_thread, media_thread;
    pthread_create(&ipc_thread, NULL, ipc_thread_func, state);
    pthread_create(&metrics_thread, NULL, metrics_thread_func, state);
    pthread_create(&media_thread, NULL, media_thread_func, state);

    gtk_main();

    return 0;
}
