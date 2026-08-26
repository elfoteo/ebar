#include "chromeos_menu_internal.h"
#include "icons.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define LEDS_SYSFS_DIR "/sys/class/leds"
#define MAX_LEDS 16
#define MAX_PATH_LEN 512

typedef struct {
	char name[128];
	char sysfs_path[MAX_PATH_LEN];
	int max_brightness;
} LedInfo;

static int scan_chromeos_leds(LedInfo *leds, int max_count) {
	DIR *dir = opendir(LEDS_SYSFS_DIR);
	if (!dir)
		return 0;

	int count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL && count < max_count) {
		if (entry->d_name[0] == '.')
			continue;
		if (!strstr(entry->d_name, "chromeos"))
			continue;

		char led_path[MAX_PATH_LEN];
		snprintf(led_path, sizeof(led_path), "%s/%s", LEDS_SYSFS_DIR, entry->d_name);

		LedInfo *led = &leds[count];
		g_strlcpy(led->name, entry->d_name, sizeof(led->name));
		g_strlcpy(led->sysfs_path, led_path, sizeof(led->sysfs_path));

		led->max_brightness = 100;
		char mb_path[MAX_PATH_LEN];
		snprintf(mb_path, sizeof(mb_path), "%s/max_brightness", led_path);
		FILE *f = fopen(mb_path, "r");
		if (f) {
			if (fscanf(f, "%d", &led->max_brightness) != 1)
				led->max_brightness = 100;
			fclose(f);
		}

		count++;
	}
	closedir(dir);
	return count;
}

static int read_led_brightness(const char *sysfs_path) {
	char path[MAX_PATH_LEN];
	snprintf(path, sizeof(path), "%s/brightness", sysfs_path);
	int val = 0;
	FILE *f = fopen(path, "r");
	if (f) {
		if (fscanf(f, "%d", &val) != 1)
			val = 0;
		fclose(f);
	}
	return val;
}

static void write_led_brightness(const char *sysfs_path, int value) {
	char path[MAX_PATH_LEN];
	snprintf(path, sizeof(path), "%s/brightness", sysfs_path);
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return;
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "%d", value);
	write(fd, buf, len);
	close(fd);
}

typedef struct {
	LedInfo led;
	gint updating;
} LedRowCtx;

static void on_led_toggle(GObject *object, GParamSpec *pspec, gpointer data) {
	(void)pspec;
	LedRowCtx *ctx = (LedRowCtx *)data;
	int active = gtk_switch_get_active(GTK_SWITCH(object));
	int target = active ? ctx->led.max_brightness : 0;
	write_led_brightness(ctx->led.sysfs_path, target);
}

static void on_led_slider_changed(GtkRange *range, gpointer data) {
	LedRowCtx *ctx = (LedRowCtx *)data;
	if (ctx->updating || slider_is_updating(range))
		return;
	/* Slider is normalized to 0..100; map back to sysfs units */
	int val = (int)(slider_get_actual_value(range) / 100.0 * ctx->led.max_brightness + 0.5);
	write_led_brightness(ctx->led.sysfs_path, val);
}

void chromeos_menu_show_leds(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);
	chromeos_menu_create_subpage_header(bw, state, "LEDs");

	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), scroller, TRUE, TRUE, 0);

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(scroller), content);

	LedInfo leds[MAX_LEDS];
	int count = scan_chromeos_leds(leds, MAX_LEDS);

	if (count == 0) {
		GtkWidget *empty_lbl = gtk_label_new("No ChromeOS LEDs found");
		gtk_widget_set_halign(empty_lbl, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_top(empty_lbl, 24);
		gtk_box_pack_start(GTK_BOX(content), empty_lbl, FALSE, FALSE, 0);
		gtk_widget_show_all(bw->cb_menu_main_box);
		return;
	}

	for (int i = 0; i < count; i++) {
		int cur = read_led_brightness(leds[i].sysfs_path);

		LedRowCtx *row_ctx = g_new0(LedRowCtx, 1);
		row_ctx->led = leds[i];

		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		g_object_set_data_full(G_OBJECT(row), "led_row_ctx", row_ctx, g_free);
		gtk_widget_set_margin_start(row, 8);
		gtk_widget_set_margin_end(row, 8);
		gtk_widget_set_margin_top(row, 6);
		gtk_widget_set_margin_bottom(row, 6);

		GtkWidget *name_lbl = gtk_label_new(leds[i].name);
		gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
		gtk_widget_set_hexpand(name_lbl, TRUE);
		gtk_box_pack_start(GTK_BOX(row), name_lbl, TRUE, TRUE, 0);

		if (leds[i].max_brightness <= 1) {
			GtkWidget *toggle = gtk_switch_new();
			gtk_style_context_add_class(gtk_widget_get_style_context(toggle), "cb-menu-led-toggle");
			gtk_switch_set_active(GTK_SWITCH(toggle), cur > 0);
			g_signal_connect(toggle, "notify::active", G_CALLBACK(on_led_toggle), row_ctx);
			gtk_box_pack_end(GTK_BOX(row), toggle, FALSE, FALSE, 0);
		} else {
			/* Same style as the volume/brightness sliders: icon overlay plus
			 * circle-shaped fill at minimum. Normalized to 0..100. */
			int pct = (int)(cur * 100.0 / leds[i].max_brightness + 0.5);
			row_ctx->updating = 1;
			GtkWidget *slider_overlay =
				create_menu_slider_overlay(ICON_LIGHTBULB, pct, G_CALLBACK(on_led_slider_changed), row_ctx, NULL);
			row_ctx->updating = 0;
			gtk_box_pack_end(GTK_BOX(row), slider_overlay, TRUE, TRUE, 0);
		}

		gtk_box_pack_start(GTK_BOX(content), row, FALSE, FALSE, 0);

		if (i < count - 1) {
			GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_box_pack_start(GTK_BOX(content), sep, FALSE, FALSE, 0);
		}
	}

	gtk_widget_show_all(bw->cb_menu_main_box);
}
