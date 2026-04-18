#include "bar.h"
#include "widgets.h"
#include "gtk-layer-shell.h"
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
    A(".metric-scale trough highlight { background-color: %s; border-radius: 10px; } ",
      cfg->colors.accent);
    A(".metric-scale trough { background-color: #4e4e4e; border-radius: 50px; "
      "  min-height: 3px; min-width: 55px; margin: 2px 12px; padding: 0; } ");
    A(".metric-label { font-size: 12px; color: %s; margin: 0 8px; } ", cfg->colors.foreground);

    /* volume – original values */
    A(".volume-btn { margin-right: 15px; background-color: rgba(0,0,0,0.2); "
      "  padding-left: 10px; padding-right: 15px; border-radius: 20px; color: %s; } ",
      cfg->colors.foreground);
    A(".volume-btn label { font-size: %s; } ", vol_label_size);

    /* media */
    A(".media-box { margin-left: 10px; background-color: %s; "
      "  padding: 0 %dpx; border-radius: 20px; } ",
      cfg->media.background ? "rgba(0,0,0,0.2)" : "transparent",
      cfg->media.background ? 5 : 0);
    A(".media-box button { color: %s; background: none; font-size: 16px; "
      "  padding: 0 8px; min-height: 24px; } ", cfg->colors.foreground);
    A(".media-box button:hover, .media-box button:hover label { color: %s; } ", cfg->colors.accent);
    A(".media-title-label { font-size: 12px; color: %s; "
      "  margin-right: 12px; margin-left: 4px; } ",
      cfg->colors.foreground);
    A(".media-artist-label { font-size: 10px; color: %s; "
      "  margin-right: 12px; margin-left: 4px; } ",
      cfg->colors.dim_foreground);
    A(".media-sep { background-color: rgba(255,255,255,0.2); min-width: 1px; margin: 4px 8px; } ");


#undef A

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
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

        if (w) gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
        tok = strtok_r(NULL, ",", &sp);
    }
    free(dup);
}

/* ── Bar window creation ─────────────────────────────────────────────────── */
void create_bar_window(GdkMonitor *monitor, AppState *state) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GdkScreen *screen = gdk_screen_get_default();
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen))
        gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);

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
    bw->left_box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    bw->center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    bw->right_box  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    add_widgets_to_box(bw->left_box,   state->config.left.widgets,   bw, state);
    add_widgets_to_box(bw->center_box, state->config.center.widgets, bw, state);
    add_widgets_to_box(bw->right_box,  state->config.right.widgets,  bw, state);

    if (state->config.mode == MODE_ISLAND) {
        /* Island mode: wrap each section in an EventBox so we can Cairo-paint
         * the pill background with the correct shape (melting or floating). */
        GtkWidget *l_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *c_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *r_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        gtk_widget_set_app_paintable(l_wrap, TRUE);
        gtk_widget_set_app_paintable(c_wrap, TRUE);
        gtk_widget_set_app_paintable(r_wrap, TRUE);

        /* Inner padding for each pill. Add border_radius to horizontal padding
         * where the pill walls are inset to allow the bottom fillet to flare out.
         * The far edges of l_wrap and r_wrap are flush with screen. */
        int pad_h = state->config.padding_h + state->config.border_radius;
        gtk_widget_set_margin_top(bw->left_box,      state->config.padding_v);
        gtk_widget_set_margin_bottom(bw->left_box,   state->config.padding_v);
        gtk_widget_set_margin_start(bw->left_box,    state->config.padding_h);
        gtk_widget_set_margin_end(bw->left_box,      pad_h);
        gtk_widget_set_margin_top(bw->center_box,    state->config.padding_v);
        gtk_widget_set_margin_bottom(bw->center_box, state->config.padding_v);
        gtk_widget_set_margin_start(bw->center_box,  pad_h);
        gtk_widget_set_margin_end(bw->center_box,    pad_h);
        gtk_widget_set_margin_top(bw->right_box,     state->config.padding_v);
        gtk_widget_set_margin_bottom(bw->right_box,  state->config.padding_v);
        gtk_widget_set_margin_start(bw->right_box,   pad_h);
        gtk_widget_set_margin_end(bw->right_box,     state->config.padding_h);

        g_object_set_data(G_OBJECT(l_wrap), "island_pos", GINT_TO_POINTER(1));
        g_object_set_data(G_OBJECT(c_wrap), "island_pos", GINT_TO_POINTER(2));
        g_object_set_data(G_OBJECT(r_wrap), "island_pos", GINT_TO_POINTER(3));

        gtk_container_add(GTK_CONTAINER(l_wrap), bw->left_box);
        gtk_container_add(GTK_CONTAINER(c_wrap), bw->center_box);
        gtk_container_add(GTK_CONTAINER(r_wrap), bw->right_box);

        gtk_container_add(GTK_CONTAINER(r_wrap), bw->right_box);

        g_signal_connect(l_wrap, "draw", G_CALLBACK(on_island_draw), state);
        g_signal_connect(c_wrap, "draw", G_CALLBACK(on_island_draw), state);
        g_signal_connect(r_wrap, "draw", G_CALLBACK(on_island_draw), state);

        gtk_box_pack_start(GTK_BOX(hbox), l_wrap, FALSE, FALSE, 0);
        gtk_box_set_center_widget(GTK_BOX(hbox), c_wrap);
        gtk_box_pack_end(GTK_BOX(hbox), r_wrap, FALSE, FALSE, 0);

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
    gtk_widget_show_all(win);
}
