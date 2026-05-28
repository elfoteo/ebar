#ifndef CHROMEOS_MENU_INTERNAL_H
#define CHROMEOS_MENU_INTERNAL_H

#include "types.h"

enum {
	MENU_WIDTH = 420,
	MENU_HEIGHT = 380,
	MENU_CONTENT_WIDTH = 388,
	MENU_CONTENT_HEIGHT = 348,
	SLIDER_VISUAL_MIN_PX = 36,
};

#define SLIDER_VISUAL_MIN_FALLBACK 12.0

typedef struct {
	BarWindow *bw;
	AppState *state;
} MenuCtx;

void chromeos_menu_show_main(BarWindow *bw, AppState *state);
void chromeos_menu_show_wifi_networks(BarWindow *bw, AppState *state);
void chromeos_menu_show_nightlight(BarWindow *bw, AppState *state);
void chromeos_menu_show_volume(BarWindow *bw, AppState *state);
void chromeos_menu_refresh_bluetooth_state(AppState *state);
void chromeos_menu_refresh_wifi_list_if_open(AppState *state);

void chromeos_menu_clear(BarWindow *bw);
GtkWidget *chromeos_menu_create_header_back_button(void);
void chromeos_menu_ellipsize_label(GtkWidget *label, int max_width_chars);
void chromeos_menu_free_generic_ctx(gpointer data, GClosure *closure);

GtkWidget *chromeos_menu_create_pill(const char *icon, const char *title, const char *subtitle, gboolean active, GtkWidget **subtitle_out,
									 GtkWidget **icon_out, GtkWidget **arrow_out, GCallback on_click, GCallback on_arrow_click,
									 gpointer user_data);
GtkWidget *chromeos_menu_create_volume_slider(MenuCtx *ctx);
GtkWidget *chromeos_menu_create_brightness_nightlight_slider(MenuCtx *ctx);
GtkWidget *create_menu_slider(const char *icon, const char *right_icon, double initial_val, GCallback on_changed,
							  GCallback on_right_clicked, GCallback on_arrow_clicked, gpointer user_data, gboolean right_active,
							  GtkWidget **scale_out);

void chromeos_menu_on_wifi_clicked(GtkWidget *widget, gpointer data);
void chromeos_menu_on_wifi_arrow_clicked(GtkWidget *widget, gpointer data);
void chromeos_menu_on_nightlight_clicked(GtkWidget *widget, gpointer data);
void chromeos_menu_on_nightlight_arrow_clicked(GtkWidget *widget, gpointer data);
void chromeos_menu_on_back_to_main_clicked(GtkWidget *widget, gpointer data);

gboolean slider_is_updating(GtkRange *range);
void slider_set_updating(GtkRange *range, gboolean updating);
double slider_get_visual_min(GtkRange *range);
void slider_set_visual_min(GtkRange *range, double visual_min);
double slider_get_actual_value(GtkRange *range);
double slider_get_display_value(GtkRange *range, double actual_val);

#endif
