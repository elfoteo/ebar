#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gboolean on_btn_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
    (void)event; (void)data;
    GdkWindow *win = gtk_widget_get_window(widget);
    if (win) {
        GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "pointer");
        gdk_window_set_cursor(win, cursor);
        g_object_unref(cursor);
    }
    return FALSE;
}

static gboolean on_btn_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
    (void)event; (void)data;
    GdkWindow *win = gtk_widget_get_window(widget);
    if (win) {
        GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "default");
        gdk_window_set_cursor(win, cursor);
        g_object_unref(cursor);
    }
    return FALSE;
}

static void on_media_prev(GtkWidget *widget, gpointer data) { (void)widget; (void)data; g_spawn_command_line_async("playerctl previous || playerctl -p %any previous", NULL); }
static void on_media_play(GtkWidget *widget, gpointer data) { (void)widget; (void)data; g_spawn_command_line_async("playerctl play-pause", NULL); }
static void on_media_next(GtkWidget *widget, gpointer data) { (void)widget; (void)data; g_spawn_command_line_async("playerctl next || playerctl -p %any next", NULL); }

static gboolean on_volume_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    (void)widget;
    AppState *w = (AppState *)data;
    pthread_mutex_lock(&w->mutex);
    w->last_manual_vol_update = time(NULL);
    if (event->direction == GDK_SCROLL_UP) {
        if (w->sys_data.vol < 100.0) {
            w->sys_data.vol += 2.0;
            if (w->sys_data.vol > 100.0) w->sys_data.vol = 100.0;
            char cmd[64]; snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %.0f%%", w->sys_data.vol);
            g_spawn_command_line_async(cmd, NULL);
        }
    } else if (event->direction == GDK_SCROLL_DOWN) {
        w->sys_data.vol -= 2.0;
        if (w->sys_data.vol < 0.0) w->sys_data.vol = 0.0;
        char cmd[64]; snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %.0f%%", w->sys_data.vol);
        g_spawn_command_line_async(cmd, NULL);
    }
    pthread_mutex_unlock(&w->mutex);
    update_widgets_idle(w);
    return TRUE;
}

static gboolean on_volume_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    AppState *w = (AppState *)data;
    if (event->button == 1) g_spawn_command_line_async(w->config.volume.app, NULL);
    else if (event->button == 3) {
        pthread_mutex_lock(&w->mutex);
        w->last_manual_vol_update = time(NULL);
        w->sys_data.vol_muted = !w->sys_data.vol_muted;
        pthread_mutex_unlock(&w->mutex);
        g_spawn_command_line_async("pactl set-sink-mute @DEFAULT_SINK@ toggle", NULL);
        update_widgets_idle(w);
    }
    return TRUE;
}

GtkWidget *widget_workspaces(BarWindow *bw, AppState *state) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    for (int i = 0; i < state->config.workspaces.count; i++) {
        bw->ws_labels[i] = gtk_label_new(NULL);
        GtkStyleContext *context = gtk_widget_get_style_context(bw->ws_labels[i]);
        gtk_style_context_add_class(context, "workspace-label");
        gtk_box_pack_start(GTK_BOX(box), bw->ws_labels[i], FALSE, FALSE, 0);
    }
    return box;
}

GtkWidget *widget_clock(BarWindow *bw, AppState *state) {
    (void)state;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    bw->clock_time_label = gtk_label_new("");
    gtk_widget_set_name(bw->clock_time_label, "clock-time");
    bw->clock_date_label = gtk_label_new("");
    gtk_widget_set_name(bw->clock_date_label, "clock-date");
    gtk_box_pack_start(GTK_BOX(box), bw->clock_time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bw->clock_date_label, FALSE, FALSE, 0);
    return box;
}

GtkWidget *widget_media(BarWindow *bw, AppState *state) {
    (void)state;
    GtkWidget *media_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(media_box), "media-box");

    GtkWidget *pbtn = gtk_button_new_with_label("󰒮");
    bw->media_play_btn = gtk_button_new_with_label("󰐊");
    GtkWidget *nbtn = gtk_button_new_with_label("󰒭");

    gtk_widget_set_can_focus(pbtn, FALSE);
    gtk_widget_set_can_focus(bw->media_play_btn, FALSE);
    gtk_widget_set_can_focus(nbtn, FALSE);

    g_signal_connect(pbtn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
    g_signal_connect(pbtn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);
    g_signal_connect(bw->media_play_btn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
    g_signal_connect(bw->media_play_btn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);
    g_signal_connect(nbtn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
    g_signal_connect(nbtn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);

    g_signal_connect(pbtn, "clicked", G_CALLBACK(on_media_prev), NULL);
    g_signal_connect(bw->media_play_btn, "clicked", G_CALLBACK(on_media_play), NULL);
    g_signal_connect(nbtn, "clicked", G_CALLBACK(on_media_next), NULL);

    gtk_box_pack_start(GTK_BOX(media_box), pbtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media_box), bw->media_play_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(media_box), nbtn, FALSE, FALSE, 0);

    bw->media_sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_sep), "media-sep");
    gtk_box_pack_start(GTK_BOX(media_box), bw->media_sep, FALSE, FALSE, 0);

    GtkWidget *text_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(text_vbox, GTK_ALIGN_CENTER);

    bw->media_title_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_title_label), "media-title-label");
    gtk_label_set_ellipsize(GTK_LABEL(bw->media_title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(bw->media_title_label), 0);
    gtk_widget_set_size_request(bw->media_title_label, 1, -1); // Allow shrinking
    gtk_box_pack_start(GTK_BOX(text_vbox), bw->media_title_label, FALSE, FALSE, 0);

    bw->media_artist_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(bw->media_artist_label), "media-artist-label");
    gtk_label_set_ellipsize(GTK_LABEL(bw->media_artist_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(bw->media_artist_label), 0);
    gtk_widget_set_size_request(bw->media_artist_label, 1, -1);
    gtk_box_pack_start(GTK_BOX(text_vbox), bw->media_artist_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(media_box), text_vbox, FALSE, FALSE, 0);
    return media_box;
}

GtkWidget *widget_volume(BarWindow *bw, AppState *state) {
    bw->volume_btn = gtk_button_new_with_label("");
    gtk_widget_set_can_focus(bw->volume_btn, FALSE);
    g_signal_connect(bw->volume_btn, "enter-notify-event", G_CALLBACK(on_btn_enter), NULL);
    g_signal_connect(bw->volume_btn, "leave-notify-event", G_CALLBACK(on_btn_leave), NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(bw->volume_btn), "volume-btn");
    gtk_widget_add_events(bw->volume_btn, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
    g_signal_connect(bw->volume_btn, "scroll-event", G_CALLBACK(on_volume_scroll), state);
    g_signal_connect(bw->volume_btn, "button-press-event", G_CALLBACK(on_volume_click), state);
    return bw->volume_btn;
}

static GtkWidget *create_metric_box(const char *icon_str, GtkWidget **out_widget, int use_bars) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    GtkWidget *icon = gtk_label_new(icon_str);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "metric-icon");
    if (use_bars) {
        *out_widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
        gtk_scale_set_draw_value(GTK_SCALE(*out_widget), FALSE);
        /* (has_origin defaults to TRUE, so GTK draws the 'highlight' fill correctly from 0) */
        gtk_widget_set_can_focus(*out_widget, FALSE);
        gtk_widget_set_size_request(*out_widget, 55, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(*out_widget), "metric-scale");
    } else {
        *out_widget = gtk_label_new("0%");
        gtk_style_context_add_class(gtk_widget_get_style_context(*out_widget), "metric-label");
    }
    gtk_widget_set_valign(*out_widget, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *out_widget, FALSE, FALSE, 0);
    return box;
}

GtkWidget *widget_metrics(BarWindow *bw, AppState *state) {
    GtkWidget *grid = gtk_grid_new();
    const char *icons[] = {"", "", "󰢮", "󰋊", "󰔏", "󰔏"};
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            MetricType type = state->config.metrics.layout[r][c];
            if (type != M_NONE && type < 6) {
                gtk_grid_attach(GTK_GRID(grid), create_metric_box(icons[type], &bw->metrics_widgets[type], state->config.metrics.use_bars), c, r, 1, 1);
            }
        }
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(grid), "metrics");
    return grid;
}

void update_workspace_display(AppState *w) {
    pthread_mutex_lock(&w->mutex);
    for (GList *l = w->bar_windows; l != NULL; l = l->next) {
        BarWindow *bw = (BarWindow *)l->data;
        for (int i = 0; i < w->config.workspaces.count; i++) {
            if (!bw->ws_labels[i]) continue;
            int ws_id = i + 1;
            int occupied = (w->ws_win_count[ws_id] > 0);
            const char *symbol = occupied ? w->config.workspaces.icon_occupied : w->config.workspaces.icon_empty;
            gtk_label_set_text(GTK_LABEL(bw->ws_labels[i]), symbol);
            GtkStyleContext *context = gtk_widget_get_style_context(bw->ws_labels[i]);
            gtk_style_context_remove_class(context, "workspace-active");
            gtk_style_context_remove_class(context, "workspace-occupied");
            if (ws_id == w->active_workspace) gtk_style_context_add_class(context, "workspace-active");
            else if (occupied) gtk_style_context_add_class(context, "workspace-occupied");
            
            if (!occupied && !w->config.workspaces.show_empty && ws_id != w->active_workspace) gtk_widget_hide(bw->ws_labels[i]);
            else gtk_widget_show(bw->ws_labels[i]);
        }
    }
    pthread_mutex_unlock(&w->mutex);
}

void update_metric_widget(GtkWidget *widget, MetricType type, SystemData *d, int use_bars) {
    if (!widget) return;
    char buf[64];
    if (use_bars) {
        double val = 0;
        switch (type) {
            case M_RAM: val = d->ram_val; break;
            case M_CPU: val = d->cpu_val; break;
            case M_GPU: val = d->gpu_val; break;
            case M_DISK: val = d->disk_val; break;
            case M_TEMP: val = d->temp_val; break;
            case M_GPU_TEMP: val = d->gpu_temp_val; break;
            default: return;
        }
        gtk_range_set_value(GTK_RANGE(widget), val);
    } else {
        switch (type) {
            case M_RAM: snprintf(buf, sizeof(buf), "%.1f/%.1f", d->ram_total - d->ram_avail, d->ram_total); break;
            case M_CPU: snprintf(buf, sizeof(buf), "%.0f%%", d->cpu_val); break;
            case M_GPU: snprintf(buf, sizeof(buf), "%.0f%%", d->gpu_val); break;
            case M_DISK: snprintf(buf, sizeof(buf), "%.0f%%", d->disk_val); break;
            case M_TEMP: snprintf(buf, sizeof(buf), "%.0f°C", d->temp_val); break;
            case M_GPU_TEMP: snprintf(buf, sizeof(buf), "%.0f°C", d->gpu_temp_val); break;
            default: return;
        }
        gtk_label_set_text(GTK_LABEL(widget), buf);
    }
}

gboolean update_widgets_idle(gpointer data) {
    AppState *w = (AppState *)data;
    pthread_mutex_lock(&w->mutex);
    SystemData d = w->sys_data;
    pthread_mutex_unlock(&w->mutex);

    time_t now = time(NULL);
    struct tm tmv = *localtime(&now);
    char tstr[64], dstr[64];
    strftime(tstr, sizeof(tstr), w->config.clock.time_format, &tmv);
    strftime(dstr, sizeof(dstr), w->config.clock.date_format, &tmv);

    for (GList *l = w->bar_windows; l != NULL; l = l->next) {
        BarWindow *bw = (BarWindow *)l->data;
        if (bw->clock_time_label) gtk_label_set_text(GTK_LABEL(bw->clock_time_label), tstr);
        if (bw->clock_date_label) gtk_label_set_text(GTK_LABEL(bw->clock_date_label), dstr);

        for (int i = 0; i < 6; i++) update_metric_widget(bw->metrics_widgets[i], (MetricType)i, &d, w->config.metrics.use_bars);

        const char *vicon = "󰕾";
        if (d.vol_muted || d.vol == 0) vicon = "󰝟";
        else if (d.vol <= 33) vicon = "󰕿";
        else if (d.vol <= 66) vicon = "󰖀";
        char vstr[32];
        if (d.vol_muted) snprintf(vstr, sizeof(vstr), "%s%s", vicon, w->config.volume.show_percent ? " Muted" : "");
        else if (w->config.volume.show_percent) snprintf(vstr, sizeof(vstr), "%s %.0f%%", vicon, d.vol);
        else snprintf(vstr, sizeof(vstr), "%s", vicon);
        if (bw->volume_btn) gtk_button_set_label(GTK_BUTTON(bw->volume_btn), vstr);

        if (bw->media_play_btn) gtk_button_set_label(GTK_BUTTON(bw->media_play_btn), d.is_playing ? "󰏤" : "󰐊");
        int has_media   = (d.media_title[0] != '\0');
        int show_title  = has_media && w->config.media.show_title;
        int show_artist = has_media && w->config.media.show_artist && d.media_artist[0] != '\0';
        /* Separator is only visible when media is playing AND at least one text line shows */
        if (bw->media_sep)         gtk_widget_set_visible(bw->media_sep,         show_title || show_artist);
        if (bw->media_title_label) {
            gtk_label_set_text(GTK_LABEL(bw->media_title_label), d.media_title);
            gtk_widget_set_visible(bw->media_title_label, show_title);
            gtk_label_set_max_width_chars(GTK_LABEL(bw->media_title_label), w->config.media.max_title_width / 8);
        }
        if (bw->media_artist_label) {
            gtk_label_set_text(GTK_LABEL(bw->media_artist_label), d.media_artist);
            gtk_widget_set_visible(bw->media_artist_label, show_artist);
        }
    }
    update_workspace_display(w);
    return G_SOURCE_REMOVE;
}

/* Called by GLib periodic timer – must return G_SOURCE_CONTINUE to keep firing */
gboolean timer_update_widgets(gpointer data) {
    update_widgets_idle(data);
    return G_SOURCE_CONTINUE;
}
