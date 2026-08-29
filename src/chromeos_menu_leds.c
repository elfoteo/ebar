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
#define MAX_CHANNELS 8
#define MAX_PATH_LEN 512

typedef struct {
	char name[128];
	char sysfs_path[MAX_PATH_LEN];
	int max_brightness;
	int num_channels;
	char channels[MAX_CHANNELS][64];
	int channel_max[MAX_CHANNELS];
	int channel_val[MAX_CHANNELS];
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
		char mb_path[MAX_PATH_LEN + 32];
		snprintf(mb_path, sizeof(mb_path), "%s/max_brightness", led_path);
		FILE *f = fopen(mb_path, "r");
		if (f) {
			if (fscanf(f, "%d", &led->max_brightness) != 1)
				led->max_brightness = 100;
			fclose(f);
		}

		led->num_channels = 0;
		char idx_path[MAX_PATH_LEN + 32];
		snprintf(idx_path, sizeof(idx_path), "%s/multi_index", led_path);
		f = fopen(idx_path, "r");
		if (f) {
			char buf[512];
			if (fgets(buf, sizeof(buf), f)) {
				size_t len = strlen(buf);
				while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
					buf[--len] = '\0';

				char *tok = strtok(buf, " ");
				while (tok && led->num_channels < MAX_CHANNELS) {
					g_strlcpy(led->channels[led->num_channels], tok, sizeof(led->channels[0]));

						led->channel_max[led->num_channels] = led->max_brightness > 1 ? led->max_brightness : 100;
					char ch_mbPath[MAX_PATH_LEN + 64];
					snprintf(ch_mbPath, sizeof(ch_mbPath), "%s/max_brightness_%s", led_path, tok);
					FILE *cmf = fopen(ch_mbPath, "r");
					if (cmf) {
						if (fscanf(cmf, "%d", &led->channel_max[led->num_channels]) != 1)
				led->channel_max[led->num_channels] = led->max_brightness > 1 ? led->max_brightness : 100;
						fclose(cmf);
					}

					led->num_channels++;
					tok = strtok(NULL, " ");
				}
			}
			fclose(f);
		}

		count++;
	}
	closedir(dir);
	return count;
}

static void format_led_name(const char *raw, char *out, size_t out_size) {
	const char *name = raw;

	if (strncmp(name, "chromeos::", 10) == 0)
		name += 10;
	else if (strncmp(name, "chromeos:", 9) == 0)
		name += 9;

	size_t j = 0;
	int capitalize = 1;
	for (size_t i = 0; name[i] && j < out_size - 1; i++) {
		char c = name[i];
		if (c == ':' || c == '_') {
			c = ' ';
			capitalize = 1;
		} else if (capitalize && c >= 'a' && c <= 'z') {
			c = c - 'a' + 'A';
			capitalize = 0;
		} else if (c == ' ') {
			capitalize = 1;
		} else {
			capitalize = 0;
		}
		out[j++] = c;
	}
	out[j] = '\0';
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

static void read_multi_intensity(const char *sysfs_path, LedInfo *led) {
	char path[MAX_PATH_LEN];
	snprintf(path, sizeof(path), "%s/multi_intensity", sysfs_path);
	char buf[512] = {0};
	FILE *f = fopen(path, "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f)) {
			size_t len = strlen(buf);
			while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
				buf[--len] = '\0';
		}
		fclose(f);
	}

	int idx = 0;
	char *tok = strtok(buf, " ");
	while (tok && idx < led->num_channels) {
		led->channel_val[idx] = atoi(tok);
		idx++;
		tok = strtok(NULL, " ");
	}
}

static void write_multi_intensity(const char *sysfs_path, LedInfo *led) {
	char path[MAX_PATH_LEN];
	snprintf(path, sizeof(path), "%s/multi_intensity", sysfs_path);
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return;
	char buf[256] = {0};
	int off = 0;
	for (int i = 0; i < led->num_channels; i++) {
		off += snprintf(buf + off, sizeof(buf) - off, "%s%d", i > 0 ? " " : "", led->channel_val[i]);
	}
	write(fd, buf, off);
	close(fd);
}

/* ── detail page callbacks ──────────────────────────────────────────────────── */

typedef struct {
	LedInfo led;
	gint updating;
} LedDetailCtx;

static void on_led_toggle(GObject *object, GParamSpec *pspec, gpointer data) {
	(void)pspec;
	LedDetailCtx *ctx = (LedDetailCtx *)data;
	int active = gtk_switch_get_active(GTK_SWITCH(object));
	write_led_brightness(ctx->led.sysfs_path, active ? ctx->led.max_brightness : 0);
	if (ctx->led.num_channels > 1)
		write_multi_intensity(ctx->led.sysfs_path, &ctx->led);
}

static void on_led_slider_changed(GtkRange *range, gpointer data) {
	LedDetailCtx *ctx = (LedDetailCtx *)data;
	if (ctx->updating || slider_is_updating(range))
		return;

	int actual = (int)(slider_get_actual_value(range) + 0.5);
	int target = (int)(actual / 100.0 * ctx->led.max_brightness + 0.5);

	write_led_brightness(ctx->led.sysfs_path, target);

	if (ctx->led.num_channels > 1) {
		for (int i = 0; i < ctx->led.num_channels; i++)
			ctx->led.channel_val[i] = (int)(actual / 100.0 * ctx->led.channel_max[i] + 0.5);
		write_multi_intensity(ctx->led.sysfs_path, &ctx->led);
	}

	GtkWidget *icon_lbl = g_object_get_data(G_OBJECT(range), "slider-icon");
	if (icon_lbl) {
		const char *cur_icon = gtk_label_get_text(GTK_LABEL(icon_lbl));
		if (actual > 0 && strcmp(cur_icon, ICON_LIGHTBULB_OFF) == 0)
			gtk_label_set_text(GTK_LABEL(icon_lbl), ICON_LIGHTBULB);
		else if (actual == 0 && strcmp(cur_icon, ICON_LIGHTBULB) == 0)
			gtk_label_set_text(GTK_LABEL(icon_lbl), ICON_LIGHTBULB_OFF);
	}
}

typedef struct {
	LedDetailCtx *parent;
	int channel;
} LedChannelCtx;

static void on_channel_slider_changed(GtkRange *range, gpointer data) {
	LedChannelCtx *ctx = (LedChannelCtx *)data;
	if (ctx->parent->updating || slider_is_updating(range))
		return;

	int actual = (int)(slider_get_actual_value(range) + 0.5);
	int target = (int)(actual / 100.0 * ctx->parent->led.channel_max[ctx->channel] + 0.5);
	ctx->parent->led.channel_val[ctx->channel] = target;
	write_led_brightness(ctx->parent->led.sysfs_path, ctx->parent->led.max_brightness);
	write_multi_intensity(ctx->parent->led.sysfs_path, &ctx->parent->led);

	GtkWidget *icon_lbl = g_object_get_data(G_OBJECT(range), "slider-icon");
	if (icon_lbl) {
		const char *cur_icon = gtk_label_get_text(GTK_LABEL(icon_lbl));
		if (actual > 0 && strcmp(cur_icon, ICON_LIGHTBULB_OFF) == 0)
			gtk_label_set_text(GTK_LABEL(icon_lbl), ICON_LIGHTBULB);
		else if (actual == 0 && strcmp(cur_icon, ICON_LIGHTBULB) == 0)
			gtk_label_set_text(GTK_LABEL(icon_lbl), ICON_LIGHTBULB_OFF);
	}
}

/* ── detail page ───────────────────────────────────────────────────────────── */

static void on_led_detail_back(GtkWidget *widget, gpointer data) {
	(void)widget;
	MenuCtx *ctx = data;
	chromeos_menu_show_leds(ctx->bw, ctx->state);
}

void chromeos_menu_show_led_detail(BarWindow *bw, AppState *state, const LedInfo *led_in) {
	chromeos_menu_clear(bw);

	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(header), "cb-menu-header");

	GtkWidget *back_btn = chromeos_menu_create_header_back_button();
	MenuCtx *mctx = g_new0(MenuCtx, 1);
	mctx->bw = bw;
	mctx->state = state;
	g_signal_connect_data(back_btn, "clicked", G_CALLBACK(on_led_detail_back), mctx,
						  (GClosureNotify)chromeos_menu_free_generic_ctx, 0);
	gtk_box_pack_start(GTK_BOX(header), back_btn, FALSE, FALSE, 0);

	char display_name[128];
	format_led_name(led_in->name, display_name, sizeof(display_name));
	GtkWidget *title = gtk_label_new(display_name);
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "cb-menu-header-title");
	gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), header, FALSE, FALSE, 0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll), FALSE);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), scroll, TRUE, TRUE, 0);

	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(scroll), content);

	LedDetailCtx *ctx = g_new0(LedDetailCtx, 1);
	ctx->led = *led_in;
	ctx->updating = 0;

	if (led_in->num_channels > 1)
		read_multi_intensity(led_in->sysfs_path, &ctx->led);

	/* brightness toggle + slider */
	int cur = read_led_brightness(led_in->sysfs_path);

	if (led_in->max_brightness <= 1) {
		GtkWidget *toggle_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_widget_set_margin_start(toggle_row, 8);
		gtk_widget_set_margin_end(toggle_row, 8);
		gtk_widget_set_margin_top(toggle_row, 6);
		gtk_widget_set_margin_bottom(toggle_row, 6);

		GtkWidget *lbl = gtk_label_new("Brightness");
		gtk_widget_set_halign(lbl, GTK_ALIGN_START);
		gtk_widget_set_hexpand(lbl, TRUE);
		gtk_box_pack_start(GTK_BOX(toggle_row), lbl, TRUE, TRUE, 0);

		GtkWidget *toggle = gtk_switch_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(toggle), "cb-menu-led-toggle");
		gtk_switch_set_active(GTK_SWITCH(toggle), cur > 0);
		g_signal_connect(toggle, "notify::active", G_CALLBACK(on_led_toggle), ctx);
		gtk_box_pack_end(GTK_BOX(toggle_row), toggle, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(content), toggle_row, FALSE, FALSE, 0);
	} else {
		int pct = (int)(cur * 100.0 / led_in->max_brightness + 0.5);
		const char *start_icon = pct > 0 ? ICON_LIGHTBULB : ICON_LIGHTBULB_OFF;
		ctx->updating = 1;
		GtkWidget *slider_overlay =
			create_menu_slider_overlay(start_icon, pct, G_CALLBACK(on_led_slider_changed), ctx, NULL);
		ctx->updating = 0;
		gtk_box_pack_start(GTK_BOX(content), slider_overlay, FALSE, FALSE, 0);
	}

	/* per-channel sliders */
	if (led_in->num_channels > 1) {
		GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gtk_box_pack_start(GTK_BOX(content), sep, FALSE, FALSE, 8);

		GtkWidget *ch_title = gtk_label_new("Channels");
		gtk_style_context_add_class(gtk_widget_get_style_context(ch_title), "cb-menu-section-label");
		gtk_widget_set_halign(ch_title, GTK_ALIGN_START);
		gtk_widget_set_margin_start(ch_title, 8);
		gtk_box_pack_start(GTK_BOX(content), ch_title, FALSE, FALSE, 0);

		for (int ch = 0; ch < led_in->num_channels; ch++) {
			LedChannelCtx *ch_ctx = g_new0(LedChannelCtx, 1);
			ch_ctx->parent = ctx;
			ch_ctx->channel = ch;

			char ch_name[64];
			const char *raw = led_in->channels[ch];
			ch_name[0] = raw[0] >= 'a' && raw[0] <= 'z' ? raw[0] - 'a' + 'A' : raw[0];
			size_t k = 1;
			for (size_t j = 1; raw[j] && k < sizeof(ch_name) - 1; j++)
				ch_name[k++] = raw[j];
			ch_name[k] = '\0';

			GtkWidget *ch_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
			gtk_widget_set_margin_start(ch_row, 8);
			gtk_widget_set_margin_end(ch_row, 8);

			GtkWidget *ch_name_lbl = gtk_label_new(ch_name);
			gtk_style_context_add_class(gtk_widget_get_style_context(ch_name_lbl), "cb-menu-led-channels");
			gtk_widget_set_halign(ch_name_lbl, GTK_ALIGN_START);
			gtk_widget_set_size_request(ch_name_lbl, 50, -1);
			gtk_box_pack_start(GTK_BOX(ch_row), ch_name_lbl, FALSE, FALSE, 0);

			int ch_pct = (int)(ctx->led.channel_val[ch] * 100.0 / ctx->led.channel_max[ch] + 0.5);
			const char *ch_icon = ch_pct > 0 ? ICON_LIGHTBULB : ICON_LIGHTBULB_OFF;
			ctx->updating = 1;
			GtkWidget *ch_slider = create_menu_slider_overlay(ch_icon, ch_pct,
				G_CALLBACK(on_channel_slider_changed), ch_ctx, NULL);
			g_object_set_data_full(G_OBJECT(ch_row), "led_ch_ctx", ch_ctx, g_free);
			ctx->updating = 0;
			gtk_box_pack_start(GTK_BOX(ch_row), ch_slider, TRUE, TRUE, 0);

			gtk_box_pack_start(GTK_BOX(content), ch_row, FALSE, FALSE, 0);
		}
	}

	g_object_set_data_full(G_OBJECT(content), "led_detail_ctx", ctx, g_free);
	gtk_widget_show_all(bw->cb_menu_main_box);
}

/* ── list page ─────────────────────────────────────────────────────────────── */

typedef struct {
	BarWindow *bw;
	AppState *state;
	LedInfo led;
} LedListCtx;

static void on_led_list_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	LedListCtx *ctx = data;
	chromeos_menu_show_led_detail(ctx->bw, ctx->state, &ctx->led);
}

void chromeos_menu_show_leds(BarWindow *bw, AppState *state) {
	chromeos_menu_clear(bw);
	chromeos_menu_create_subpage_header(bw, state, "LEDs");

	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), sep, FALSE, FALSE, 8);

	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller), FALSE);
	gtk_widget_set_size_request(scroller, -1, 250);
	gtk_box_pack_start(GTK_BOX(bw->cb_menu_main_box), scroller, TRUE, TRUE, 0);

	GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_set_spacing(GTK_BOX(list_box), 2);
	gtk_container_add(GTK_CONTAINER(scroller), list_box);

	LedInfo leds[MAX_LEDS];
	int count = scan_chromeos_leds(leds, MAX_LEDS);

	if (count == 0) {
		GtkWidget *empty_lbl = gtk_label_new("No ChromeOS LEDs found");
		gtk_widget_set_halign(empty_lbl, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_top(empty_lbl, 24);
		gtk_box_pack_start(GTK_BOX(list_box), empty_lbl, FALSE, FALSE, 0);
		gtk_widget_show_all(bw->cb_menu_main_box);
		return;
	}

	for (int i = 0; i < count; i++) {
		GtkWidget *btn = gtk_button_new();
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-wifi-list-btn");
		GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

		GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		char display_name[128];
		format_led_name(leds[i].name, display_name, sizeof(display_name));
		GtkWidget *name_lbl = gtk_label_new(display_name);
		chromeos_menu_ellipsize_label(name_lbl, 48);
		gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text_box), name_lbl, FALSE, FALSE, 0);

		char subtitle_buf[256] = {0};
		if (leds[i].num_channels > 1) {
			int off = 0;
			for (int ch = 0; ch < leds[i].num_channels && off < (int)sizeof(subtitle_buf) - 1; ch++) {
				if (ch > 0)
					off += snprintf(subtitle_buf + off, sizeof(subtitle_buf) - off, " \xC2\xB7 ");
				off += snprintf(subtitle_buf + off, sizeof(subtitle_buf) - off, "%s", leds[i].channels[ch]);
			}
		} else if (leds[i].max_brightness <= 1) {
			g_strlcpy(subtitle_buf, "On / Off", sizeof(subtitle_buf));
		} else {
			snprintf(subtitle_buf, sizeof(subtitle_buf), "0 \xE2\x80\x93 %d", leds[i].max_brightness);
		}
		GtkWidget *sub_lbl = gtk_label_new(subtitle_buf);
		gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl), "subtitle");
		gtk_widget_set_halign(sub_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(text_box), sub_lbl, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(btn_box), text_box, TRUE, TRUE, 0);

		GtkWidget *arrow = gtk_label_new(ICON_CHEVRON_RIGHT);
		gtk_style_context_add_class(gtk_widget_get_style_context(arrow), "icon");
		gtk_box_pack_end(GTK_BOX(btn_box), arrow, FALSE, FALSE, 0);

		gtk_container_add(GTK_CONTAINER(btn), btn_box);

		LedListCtx *lctx = g_new0(LedListCtx, 1);
		lctx->bw = bw;
		lctx->state = state;
		lctx->led = leds[i];

		g_signal_connect_data(btn, "clicked", G_CALLBACK(on_led_list_clicked), lctx, chromeos_menu_free_generic_ctx, 0);
		gtk_box_pack_start(GTK_BOX(list_box), btn, FALSE, FALSE, 0);
	}

	gtk_widget_show_all(bw->cb_menu_main_box);
}
