#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void trim(char *s) {
    int l = strlen(s);
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\n' || s[l-1] == '\r'))
        s[--l] = '\0';
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    memmove(s, p, strlen(p) + 1);
}

void config_save_default(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "[bar]\n");
    fprintf(f, "position        = bottom          # top | bottom\n");
    fprintf(f, "mode            = normal          # normal | floating | island\n");
    fprintf(f, "margin          = 8               # outer gap in px (used when floating)\n");
    fprintf(f, "border_radius   = 12              # corner radius px (floating / island)\n");
    fprintf(f, "padding_h       = 12              # horizontal inner padding px\n");
    fprintf(f, "padding_v       = 5               # vertical inner padding px\n");
    fprintf(f, "spacing         = 12              # spacing between widgets px\n\n");

    fprintf(f, "[colors]\n");
    fprintf(f, "# Use any valid CSS colour: #RRGGBB, rgba(r,g,b,a), etc.\n");
    fprintf(f, "background      = rgba(0,0,0,0.2)\n");
    fprintf(f, "accent          = #D35D6E\n");
    fprintf(f, "foreground      = #ffffff\n");
    fprintf(f, "dim_foreground  = rgba(255,255,255,0.6)\n\n");

    fprintf(f, "[font]\n");
    fprintf(f, "family          = JetBrainsMonoNerdFont\n");
    fprintf(f, "size            = 13\n\n");

    fprintf(f, "[workspaces]\n");
    fprintf(f, "count           = 10\n");
    fprintf(f, "icon_empty      = \n");
    fprintf(f, "icon_occupied   = \n");
    fprintf(f, "show_empty      = true\n\n");

    fprintf(f, "[left]\n");
    fprintf(f, "# Options: workspaces, clock, media, volume, metrics\n");
    fprintf(f, "widgets         = workspaces\n\n");

    fprintf(f, "[center]\n");
    fprintf(f, "widgets         =\n\n");

    fprintf(f, "[right]\n");
    fprintf(f, "widgets         = metrics, volume, clock\n\n");

    fprintf(f, "[clock]\n");
    fprintf(f, "time_format     = %%H:%%M\n");
    fprintf(f, "date_format     = %%d/%%m/%%Y\n\n");

    fprintf(f, "[media]\n");
    fprintf(f, "show_title      = true\n");
    fprintf(f, "show_artist     = true\n");
    fprintf(f, "background      = true\n");
    fprintf(f, "max_title_width = 400\n\n");

    fprintf(f, "[volume]\n");
    fprintf(f, "app             = pavucontrol\n");
    fprintf(f, "show_percent    = false\n\n");

    fprintf(f, "[metrics]\n");
    fprintf(f, "# Rows separated by ; columns by spaces. Options: ram cpu gpu disk temp gputemp\n");
    fprintf(f, "layout          = ram cpu ; disk temp\n");
    fprintf(f, "use_bars        = true\n");
    fprintf(f, "temp_path       = /sys/class/thermal/thermal_zone1/temp\n");

    fclose(f);
}

static MetricType parse_metric_type(const char *s) {
    if (strcmp(s, "ram")    == 0) return M_RAM;
    if (strcmp(s, "cpu")    == 0) return M_CPU;
    if (strcmp(s, "gpu")    == 0) return M_GPU;
    if (strcmp(s, "disk")   == 0) return M_DISK;
    if (strcmp(s, "temp")   == 0) return M_TEMP;
    if (strcmp(s, "gputemp")== 0) return M_GPU_TEMP;
    return M_NONE;
}

void config_load(Config *cfg) {
    /* ── Defaults matching original ebar.c behaviour ── */
    cfg->position      = POS_BOTTOM;
    cfg->mode          = MODE_NORMAL;
    cfg->margin        = 8;
    cfg->height        = 36;
    cfg->border_radius = 12;
    cfg->padding_h     = 12;
    cfg->padding_v     = 5;
    cfg->spacing       = 12; /* original hbox had spacing=12 */

    /* CSS-valid colour strings – inserted verbatim into the CSS stylesheet */
    strcpy(cfg->colors.background,    "rgba(0,0,0,0.2)");
    strcpy(cfg->colors.accent,         "#D35D6E");
    strcpy(cfg->colors.foreground,     "#ffffff");
    strcpy(cfg->colors.dim_foreground, "rgba(255,255,255,0.6)");
    strcpy(cfg->colors.border,         "rgba(255,255,255,0.2)");

    strcpy(cfg->font.family, "JetBrainsMonoNerdFont");
    cfg->font.size = 13;

    cfg->workspaces.count = 10;
    strcpy(cfg->workspaces.icon_empty,    "");
    strcpy(cfg->workspaces.icon_occupied, "");
    cfg->workspaces.show_empty = 1;

    /* Default layout: media in center, metrics+volume+clock on right */
    strcpy(cfg->left.widgets,   "workspaces");
    strcpy(cfg->center.widgets, "media");
    strcpy(cfg->right.widgets,  "metrics, volume, clock");

    strcpy(cfg->clock.time_format, "%H:%M");
    strcpy(cfg->clock.date_format, "%d/%m/%Y");

    cfg->media.show_title  = 1;
    cfg->media.show_artist = 1;
    cfg->media.background  = 1;
    cfg->media.max_title_width = 400;
    strcpy(cfg->volume.app, "pavucontrol");
    cfg->volume.show_percent = 0;

    cfg->metrics.layout[0][0] = M_RAM;  cfg->metrics.layout[0][1] = M_CPU;  cfg->metrics.layout[0][2] = M_NONE;
    cfg->metrics.layout[1][0] = M_DISK; cfg->metrics.layout[1][1] = M_TEMP; cfg->metrics.layout[1][2] = M_NONE;
    cfg->metrics.use_bars = 1;
    strcpy(cfg->metrics.temp_path, "/sys/class/thermal/thermal_zone1/temp");

    /* ── Load from file ── */
    char path[512];
    const char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/.config/ebar", home);
    mkdir(path, 0755);
    strcat(path, "/ebar.conf");

    if (access(path, F_OK) == -1)
        config_save_default(path);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    char section[64] = "";
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        if (line[0] == '[' && strchr(line, ']')) {
            char *end = strchr(line, ']');
            *end = '\0';
            strcpy(section, line + 1);
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        trim(key);
        trim(val);

        /* Strip inline comments: only treat ' #' or '\t#' as comment markers.
         * This preserves bare colour values like #D35D6E in the value. */
        char *comment = strstr(val, " #");
        if (!comment) comment = strstr(val, "\t#");
        if (comment) *comment = '\0';
        
        /* Final trim in case there were spaces before the comment */
        trim(val);

        if (strcmp(section, "bar") == 0) {
            if      (!strcmp(key, "position"))      cfg->position = (!strcmp(val, "top")) ? POS_TOP : POS_BOTTOM;
            else if (!strcmp(key, "mode")) {
                if      (!strcmp(val, "floating")) cfg->mode = MODE_FLOATING;
                else if (!strcmp(val, "island"))   cfg->mode = MODE_ISLAND;
                else                               cfg->mode = MODE_NORMAL;
            }
            else if (!strcmp(key, "margin"))        cfg->margin        = atoi(val);
            else if (!strcmp(key, "height"))        cfg->height        = atoi(val);
            else if (!strcmp(key, "border_radius")) cfg->border_radius = atoi(val);
            else if (!strcmp(key, "padding_h"))     cfg->padding_h     = atoi(val);
            else if (!strcmp(key, "padding_v"))     cfg->padding_v     = atoi(val);
            else if (!strcmp(key, "spacing"))       cfg->spacing       = atoi(val);
        } else if (!strcmp(section, "colors")) {
            if      (!strcmp(key, "background"))   strcpy(cfg->colors.background,    val);
            else if (!strcmp(key, "accent"))       strcpy(cfg->colors.accent,         val);
            else if (!strcmp(key, "foreground"))   strcpy(cfg->colors.foreground,     val);
            else if (!strcmp(key, "dim_foreground")) strcpy(cfg->colors.dim_foreground, val);
            else if (!strcmp(key, "border"))       strcpy(cfg->colors.border,         val);
        } else if (!strcmp(section, "font")) {
            if      (!strcmp(key, "family")) strcpy(cfg->font.family, val);
            else if (!strcmp(key, "size"))   cfg->font.size = atoi(val);
        } else if (!strcmp(section, "workspaces")) {
            if      (!strcmp(key, "count"))        cfg->workspaces.count = atoi(val);
            else if (!strcmp(key, "icon_empty"))   strcpy(cfg->workspaces.icon_empty,    val);
            else if (!strcmp(key, "icon_occupied"))strcpy(cfg->workspaces.icon_occupied,  val);
            else if (!strcmp(key, "show_empty"))   cfg->workspaces.show_empty = (!strcmp(val, "true"));
        } else if (!strcmp(section, "left")) {
            if (!strcmp(key, "widgets")) strcpy(cfg->left.widgets, val);
        } else if (!strcmp(section, "center")) {
            if (!strcmp(key, "widgets")) strcpy(cfg->center.widgets, val);
        } else if (!strcmp(section, "right")) {
            if (!strcmp(key, "widgets")) strcpy(cfg->right.widgets, val);
        } else if (!strcmp(section, "clock")) {
            if      (!strcmp(key, "time_format")) strcpy(cfg->clock.time_format, val);
            else if (!strcmp(key, "date_format")) strcpy(cfg->clock.date_format, val);
        } else if (!strcmp(section, "media")) {
            if      (!strcmp(key, "show_title"))       cfg->media.show_title      = (!strcmp(val, "true"));
            else if (!strcmp(key, "show_artist"))      cfg->media.show_artist     = (!strcmp(val, "true"));
            else if (!strcmp(key, "background"))       cfg->media.background      = (!strcmp(val, "true"));
            else if (!strcmp(key, "max_title_width"))  cfg->media.max_title_width = atoi(val);
        } else if (!strcmp(section, "volume")) {
            if      (!strcmp(key, "app"))          strcpy(cfg->volume.app, val);
            else if (!strcmp(key, "show_percent")) cfg->volume.show_percent = (!strcmp(val, "true"));
        } else if (!strcmp(section, "metrics")) {
            if (!strcmp(key, "layout")) {
                /* Format: "ram cpu ; disk temp" – rows separated by ';' */
                char tmp[256]; strncpy(tmp, val, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
                char *sp2, *row1 = strtok_r(tmp, ";", &sp2);
                char *row2 = strtok_r(NULL, ";", &sp2);
                if (row1) {
                    char *sp3, *m = strtok_r(row1, " ", &sp3); int i = 0;
                    while (m && i < 3) { cfg->metrics.layout[0][i++] = parse_metric_type(m); m = strtok_r(NULL, " ", &sp3); }
                    while (i < 3) cfg->metrics.layout[0][i++] = M_NONE;
                }
                if (row2) {
                    char *sp3, *m = strtok_r(row2, " ", &sp3); int i = 0;
                    while (m && i < 3) { cfg->metrics.layout[1][i++] = parse_metric_type(m); m = strtok_r(NULL, " ", &sp3); }
                    while (i < 3) cfg->metrics.layout[1][i++] = M_NONE;
                }
            } else if (!strcmp(key, "use_bars"))   cfg->metrics.use_bars = (!strcmp(val, "true"));
            else if  (!strcmp(key, "temp_path"))   strcpy(cfg->metrics.temp_path, val);
        }
    }
    fclose(f);
}
