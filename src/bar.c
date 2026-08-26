#include "bar.h"
#include "chromeos_bar.h"
#include "widgets.h"
#include "gtk-layer-shell.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Island Cairo draw ──────────────────────────────────────────────────────
 * POS_BOTTOM (anchored): top corners convex, bottom corners concave.
 *   The concave bottom arc is drawn with center at the bounding-box corner,
 *   sweeping through the interior of the shape – this creates the outward
 *   "tangent to the screen edge" effect the user described.
 * POS_TOP (anchored): mirror of POS_BOTTOM via Y-flip.
 * Floating island: fully rounded pill on all four corners.
 * ──────────────────────────────────────────────────────────────────────── */
static gboolean on_island_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    AppState *state = (AppState *)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double w = alloc.width;
    double h = alloc.height;
    double r = state->config.border_radius;

    /* 1. Clear to transparent */
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* 2. Parse background colour */
    GdkRGBA color;
    if (!gdk_rgba_parse(&color, state->config.colors.background)) {
        color.red = 0; color.green = 0; color.blue = 0; color.alpha = 0.2;
    }
    gdk_cairo_set_source_rgba(cr, &color);

    /* 3. Build the path */
    cairo_new_path(cr);

    if (state->config.mode == MODE_FLOATING) {
        /* Full rounded pill */
        cairo_arc(cr, r,     r,     r, M_PI,      3*M_PI/2);
        cairo_arc(cr, w-r,   r,     r, 3*M_PI/2,  2*M_PI);
        cairo_arc(cr, w-r,   h-r,   r, 0,          M_PI/2);
        cairo_arc(cr, r,     h-r,   r, M_PI/2,     M_PI);
    } else {
        /* Anchored Island mode: fillets */
        int pos = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "island_pos"));

        if (state->config.position == POS_TOP) {
            cairo_save(cr);
            cairo_translate(cr, 0, h);
            cairo_scale(cr, 1, -1);
        }

        if (pos == 1) cairo_move_to(cr, 0, 0);
        else          cairo_move_to(cr, 2*r, 0);

        if (pos == 3) {
            cairo_line_to(cr, w, 0);
            cairo_line_to(cr, w, h);
        } else {
            cairo_line_to(cr, w - 2*r, 0);
            cairo_arc(cr, w - 2*r, r, r, -M_PI/2, 0);
            cairo_line_to(cr, w - r, h - r);
            cairo_arc_negative(cr, w, h - r, r, M_PI, M_PI/2);
        }

        if (pos == 1) {
            cairo_line_to(cr, 0, h);
            cairo_line_to(cr, 0, 0);
        } else {
            cairo_line_to(cr, 0, h);
            cairo_arc_negative(cr, 0, h - r, r, M_PI/2, 0);
            cairo_line_to(cr, r, r);
            cairo_arc(cr, 2*r, r, r, M_PI, 3*M_PI/2);
        }

        if (state->config.position == POS_TOP) cairo_restore(cr);
    }

    cairo_close_path(cr);
    cairo_fill(cr);
    return FALSE;
}




/* ── CSS generation ─────────────────────────────────────────────────────────
 * Closely mirrors the original hardcoded CSS, substituting only the values
 * that are now configurable (font, bg colour, accent colour for metric bars).
 * Everything else keeps the same values as the original ebar.c.
 * ──────────────────────────────────────────────────────────────────────── */
void apply_global_css(AppState *state) {
    Config *cfg = &state->config;

    /* ChromeOS mode has its own standalone CSS — delegate and bail out */
    if (cfg->mode == MODE_CHROMEOS) {
        apply_chromeos_css(state);
        return;
    }

    int   bar_radius = (cfg->mode == MODE_FLOATING) ? cfg->border_radius : 0;
    const char *bar_bg = (cfg->mode == MODE_ISLAND) ? "transparent" : cfg->colors.background;
    const char *vol_label_size = cfg->volume.show_percent ? "14px" : "22px";

    char css[8192];
    int  n = 0;

#define A(...) n += snprintf(css+n, (int)sizeof(css)-n, __VA_ARGS__)

    A("* { font-family: \"%s\"; background: none; box-shadow: none; border: none; } ",
      cfg->font.family);
    A("window, .background { background-color: transparent; } ");
    A("#main-container { background-color: %s; border-radius: %dpx; } ", bar_bg, bar_radius);

    /* workspaces – identical to original, uses config foreground/accent */
    A(".workspace-label { font-size: 8px; padding: 8px; min-width: 24px; min-height: 24px; "
      "  margin-left: 0px; margin-right: 0px; color: %s; border-radius: 999px; } ",
      cfg->colors.foreground);
    A(".workspace-label:first-child { margin-left: 10px; } ");
    A(".workspace-occupied { color: %s; } ", cfg->colors.foreground);
    A(".workspace-active { background-color: rgba(255,255,255,0.2); color: %s; } ",
      cfg->colors.foreground);

    /* clock */
    A("#clock-time { font-size: 14px; font-weight: normal; color: %s; } ", cfg->colors.foreground);
    A("#clock-date { font-size: 14px; font-weight: normal; color: %s; } ", cfg->colors.foreground);

    /* metrics – accent colour drives trough highlight, rest is original */
    A(".metric-icon { font-size: 14px; color: rgba(255,255,255,0.7); "
      "  margin: 0px 0px 0px 6px; padding: 0; } ");
    A(".metric-scale { padding: 0; margin: 0; } ");
    A(".metric-scale slider { all: unset; min-width: 0; min-height: 0; opacity: 0; } ");
    A(".metric-scale trough highlight { background-color: %s; border-radius: 10px; "
      "  min-height: 3px; margin: 0; padding: 0; } ",
      cfg->colors.accent);
    A(".metric-scale trough { background-color: #4e4e4e; border-radius: 50px; "
      "  min-height: 3px; min-width: 55px; margin: 2px 12px; padding: 0; } ");
    A(".metric-label { font-size: 12px; color: %s; margin: 0 8px; } ", cfg->colors.foreground);

    /* volume – transparent pill behind ring; icon centred inside 48×48 overlay */
    A(".volume-btn { background-color: transparent; border-radius: 999px; "
      "  padding: 0; margin: 0 10px; font-size: %s; color: %s; } ",
      vol_label_size, cfg->colors.foreground);
    A(".volume-btn.vol-high { margin-left: 6px; } "); /* nerdfont icon isn't centered */

    /* nightlight */
    A(".nightlight-btn { background-color: transparent; border-radius: 999px; "
      "  padding: 0; margin: 0 6px; font-size: 20px; color: %s; } ",
      cfg->colors.foreground);
    
    /* nudged moon, I hate nerdfont for not centering it's icons */
    A(".nightlight-btn.nightlight-on { margin-left: 4px; margin-top: -1px; } ");
    A(".nightlight-btn.nightlight-off { margin-left: 3px; margin-top: -1px; } ");
    A(".nightlight-btn.nightlight-error { color: #e05555; margin-left: 4px; margin-top: -1px; } ");
    A(".nightlight-btn.nightlight-retrying { color: #e08855; margin-left: 4px; margin-top: -1px; } ");

    /* bluetooth */
    A(".bluetooth-btn { background-color: transparent; border-radius: 999px; "
      "  padding: 0; margin: 0 6px; font-size: 20px; color: %s; } ",
      cfg->colors.foreground);
    A(".bluetooth-btn.bluetooth-on { } ");
    A(".bluetooth-btn.bluetooth-off { } ");

    /* media */
    A(".media-box { background-color: %s; "
      "  padding: 0 %dpx; border-radius: 20px; } ",
      cfg->media.background ? "rgba(0,0,0,0.2)" : "transparent",
      cfg->media.background ? 5 : 0);
    A(".media-box button { color: %s; background: none; font-size: 22px; "
      "  padding: 0 4px; margin: 0 8px; min-height: 24px; } ", cfg->colors.foreground);
    A(".media-box button:hover, .media-box button:hover label { color: %s; } ", cfg->colors.accent);
    A(".media-title-label { font-size: 12px; color: %s; "
      "  margin-right: 12px; margin-left: 4px; } ",
      cfg->colors.foreground);
    A(".media-artist-label { font-size: 10px; color: %s; "
      "  margin-right: 12px; margin-left: 4px; } ",
      cfg->colors.dim_foreground);
    A(".media-sep { background-color: rgba(255,255,255,0.2); min-width: 1px; margin: 4px 8px; } ");


    /* ChromeOS CSS is handled entirely by apply_chromeos_css() in chromeos_bar.c */

#undef A
    apply_css_from_string(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* ── Widget placement helper ─────────────────────────────────────────────── */
static void add_widgets_to_box(GtkWidget *box, const char *csv,
                                BarWindow *bw, AppState *state) {
    if (!csv || !*csv) return;
    char *dup = strdup(csv);
    char *sp, *tok = strtok_r(dup, ",", &sp);
    while (tok) {
        while (*tok == ' ') tok++;
        char *e = tok + strlen(tok) - 1;
        while (e > tok && (*e == ' ' || *e == '\n' || *e == '\r')) *e-- = '\0';

        GtkWidget *w = NULL;
        if      (!strcmp(tok, "workspaces")) w = widget_workspaces(bw, state);
        else if (!strcmp(tok, "clock"))      w = widget_clock(bw, state);
        else if (!strcmp(tok, "media"))      w = widget_media(bw, state);
        else if (!strcmp(tok, "volume"))     w = widget_volume(bw, state);
        else if (!strcmp(tok, "metrics"))    w = widget_metrics(bw, state);
        else if (!strcmp(tok, "nightlight")) w = widget_nightlight(bw, state);
        else if (!strcmp(tok, "bluetooth"))  w = widget_bluetooth(bw, state);
        else if (!strcmp(tok, "brightness")) w = widget_brightness(bw, state);
        else if (!strcmp(tok, "launcher"))   w = widget_launcher(bw, state);

        if (w) gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
        tok = strtok_r(NULL, ",", &sp);
    }
    free(dup);
}

void on_bar_window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    BarDestroyCtx *ctx = data;
    pthread_mutex_lock(&ctx->state->mutex);
    ctx->state->bar_windows = g_list_remove(ctx->state->bar_windows, ctx->bw);
    pthread_mutex_unlock(&ctx->state->mutex);
    if (ctx->bw->menu_window)
        gtk_widget_destroy(ctx->bw->menu_window);
    if (ctx->bw->launcher_window)
        gtk_widget_destroy(ctx->bw->launcher_window);
    if (ctx->bw->popup_window)
        gtk_widget_destroy(ctx->bw->popup_window);
    g_free(ctx->bw);
    g_free(ctx);
}

/* ── Bar window creation ─────────────────────────────────────────────────── */
void create_bar_window(GdkMonitor *monitor, AppState *state) {
    if (state->config.mode == MODE_CHROMEOS) {
        create_chromeos_bar_window(monitor, state);
        return;
    }

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    setup_transparent_window(win);

    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_monitor(GTK_WINDOW(win), monitor);
    gtk_layer_set_namespace(GTK_WINDOW(win), "ebar");

    BarPosition pos = state->config.position;
    GtkLayerShellEdge v_edge = (pos == POS_TOP) ? GTK_LAYER_SHELL_EDGE_TOP
                                                  : GTK_LAYER_SHELL_EDGE_BOTTOM;
    gtk_layer_set_anchor(GTK_WINDOW(win), v_edge,                      TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT,   TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT,  TRUE);

    if (state->config.mode == MODE_FLOATING) {
        gtk_layer_set_margin(GTK_WINDOW(win), v_edge,                     state->config.margin);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT,  state->config.margin);
        gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, state->config.margin);
    }
    gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(win));

    BarWindow *bw = g_new0(BarWindow, 1);
    bw->window = win;
    bw->monitor = monitor;
    bw->state = state;

    /* Outer hbox – same spacing (12) */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, state->config.spacing);
    if (state->config.mode != MODE_ISLAND) {
        /* In floating/normal, the outer container takes the padding.
         * In island mode, the internal pills take the padding, so the hbox
         * remains flush with the screen edges to allow 'melting' drawing. */
        gtk_widget_set_margin_top(hbox,    state->config.padding_v);
        gtk_widget_set_margin_bottom(hbox, state->config.padding_v);
        gtk_widget_set_margin_start(hbox,  state->config.padding_h);
        gtk_widget_set_margin_end(hbox,    state->config.padding_h);
    }
    /* NOTE: no size_request – bar height is driven by content, not forced */

    /* Three section boxes */
    bw->left_box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, state->config.spacing);
    bw->center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, state->config.spacing);
    bw->right_box  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, state->config.spacing);

    add_widgets_to_box(bw->left_box,   state->config.left.widgets,   bw, state);
    add_widgets_to_box(bw->center_box, state->config.center.widgets, bw, state);
    add_widgets_to_box(bw->right_box,  state->config.right.widgets,  bw, state);

    if (state->config.mode == MODE_ISLAND) {
        /* Island mode: wrap each non-empty section so we can Cairo-paint
         * the pill background with the correct shape (melting or floating).
         * Empty sections get no pill at all. */
        struct { GtkWidget *box, *wrap; int pos, start_pad, end_pad; } secs[3] = {
            { bw->left_box,   NULL, 1, state->config.padding_h, state->config.padding_h + state->config.border_radius },
            { bw->center_box, NULL, 2, state->config.padding_h + state->config.border_radius, state->config.padding_h + state->config.border_radius },
            { bw->right_box,  NULL, 3, state->config.padding_h + state->config.border_radius, state->config.padding_h },
        };

        for (int i = 0; i < 3; i++) {
            if (!gtk_container_get_children(GTK_CONTAINER(secs[i].box)))
                continue;
            secs[i].wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_app_paintable(secs[i].wrap, TRUE);

            /* Inner padding for each pill. Add border_radius to horizontal padding
             * where the pill walls are inset to allow the bottom fillet to flare out.
             * The far edges of l_wrap and r_wrap are flush with screen. */
            gtk_widget_set_margin_top(secs[i].box,    state->config.padding_v);
            gtk_widget_set_margin_bottom(secs[i].box, state->config.padding_v);
            gtk_widget_set_margin_start(secs[i].box,  secs[i].start_pad);
            gtk_widget_set_margin_end(secs[i].box,    secs[i].end_pad);

            g_object_set_data(G_OBJECT(secs[i].wrap), "island_pos", GINT_TO_POINTER(secs[i].pos));
            gtk_container_add(GTK_CONTAINER(secs[i].wrap), secs[i].box);
            g_signal_connect(secs[i].wrap, "draw", G_CALLBACK(on_island_draw), state);
        }

        if (secs[0].wrap) gtk_box_pack_start(GTK_BOX(hbox), secs[0].wrap, FALSE, FALSE, 0);
        if (secs[1].wrap) gtk_box_set_center_widget(GTK_BOX(hbox), secs[1].wrap);
        if (secs[2].wrap) gtk_box_pack_end(GTK_BOX(hbox), secs[2].wrap, FALSE, FALSE, 0);

        /* For island the transparency is in the pill border, not main-container */
        gtk_container_add(GTK_CONTAINER(win), hbox);
    } else {
        /* Normal / Floating: use a named main-container for CSS background */
        GtkWidget *main_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_name(main_container, "main-container");

        gtk_box_pack_start(GTK_BOX(hbox), bw->left_box,   FALSE, FALSE, 0);
        gtk_box_set_center_widget(GTK_BOX(hbox), bw->center_box);
        gtk_box_pack_end(GTK_BOX(hbox), bw->right_box,    FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(main_container), hbox, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(win), main_container);
    }

    state->bar_windows = g_list_append(state->bar_windows, bw);

    BarDestroyCtx *dctx = g_new0(BarDestroyCtx, 1);
    dctx->bw = bw;
    dctx->state = state;
    g_signal_connect(win, "destroy", G_CALLBACK(on_bar_window_destroy), dctx);

    gtk_widget_show_all(win);
}
