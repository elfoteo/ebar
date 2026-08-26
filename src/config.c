#include "config.h"
#include "icons.h"
#include "constants.h"
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
    fprintf(f, "mode            = normal          # normal | floating | island | chromeos\n");
    fprintf(f, "margin          = 8               # outer gap in px (used when floating)\n");
    fprintf(f, "height          = 36              # bar height in px\n");
    fprintf(f, "border_radius   = 12              # corner radius px (floating / island)\n");
    fprintf(f, "padding_h       = 12              # horizontal inner padding px\n");
    fprintf(f, "padding_v       = 5               # vertical inner padding px\n");
    fprintf(f, "spacing         = 12              # spacing between widgets px\n\n");

    fprintf(f, "[colors]\n");
    fprintf(f, "# Use any valid CSS colour: #RRGGBB, rgba(r,g,b,a), etc.\n");
    fprintf(f, "background      = rgba(0,0,0,0.2)\n");
    fprintf(f, "accent          = #D35D6E\n");
    fprintf(f, "foreground      = #ffffff\n");
    fprintf(f, "dim_foreground  = rgba(255,255,255,0.6)\n");
    fprintf(f, "border          = rgba(255,255,255,0.2) # pill / container border colour\n");
    fprintf(f, "ring_color      = rgba(255,255,255,0.9) # colour of circular progress rings\n\n");

    fprintf(f, "[font]\n");
    fprintf(f, "family          = JetBrainsMonoNerdFont\n");
    fprintf(f, "size            = 13\n\n");

    fprintf(f, "[workspaces]\n");
    fprintf(f, "count           = 10\n");
    fprintf(f, "icon_empty      = " ICON_WS_EMPTY "\n");
    fprintf(f, "icon_occupied   = " ICON_WS_OCCUPIED "\n");
    fprintf(f, "show_empty      = true\n\n");

    fprintf(f, "[left]\n");
    fprintf(f, "# Options: workspaces, clock, media, volume, metrics, nightlight\n");
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
    fprintf(f, "temp_path       = " DEFAULT_TEMP_PATH "\n\n");

    fprintf(f, "[nightlight]\n");
    fprintf(f, "# Requires hyprsunset running (exec-once = hyprsunset in hyprland.conf)\n");
    fprintf(f, "temp_max        = 6500   # white-point temperature (K)\n");
    fprintf(f, "temp_min        = 5400   # warm night temperature (K)\n");
    fprintf(f, "gamma_max       = 100    # full brightness gamma\n");
    fprintf(f, "gamma_min       = 75     # reduced gamma at max level\n");
    fprintf(f, "step            = 5      # level change per scroll tick (0-100 range)\n");
    fprintf(f, "curve           = ease   # ease | linear\n\n");

    fprintf(f, "[launcher]\n");
    fprintf(f, "# Format: app = action:icon_path\n");
    fprintf(f, "# Icon path can be absolute or relative to ~/.config/ebar/\n");
    fprintf(f, "app             = firefox:firefox.svg\n\n");

    fprintf(f, "[chromeos]\n");
    fprintf(f, "accent_color    = #0179d4\n");
    fprintf(f, "screenshot_app  = ~/coding/c/escreen/launch.sh\n\n");

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
    g_strlcpy(cfg->colors.background,    "rgba(0,0,0,0.2)", sizeof(cfg->colors.background));
    g_strlcpy(cfg->colors.accent,         "#0179d4",         sizeof(cfg->colors.accent));
    g_strlcpy(cfg->colors.foreground,     "#ffffff",          sizeof(cfg->colors.foreground));
    g_strlcpy(cfg->colors.dim_foreground, "rgba(255,255,255,0.6)", sizeof(cfg->colors.dim_foreground));
    g_strlcpy(cfg->colors.border,         "rgba(255,255,255,0.2)", sizeof(cfg->colors.border));
    g_strlcpy(cfg->colors.ring_color,     "rgba(255,255,255,0.9)", sizeof(cfg->colors.ring_color));

    g_strlcpy(cfg->font.family, "JetBrainsMonoNerdFont", sizeof(cfg->font.family));
    cfg->font.size = 13;

    cfg->workspaces.count = 10;
    g_strlcpy(cfg->workspaces.icon_empty,    "", sizeof(cfg->workspaces.icon_empty));
    g_strlcpy(cfg->workspaces.icon_occupied, "", sizeof(cfg->workspaces.icon_occupied));
    cfg->workspaces.show_empty = 1;

    g_strlcpy(cfg->left.widgets,   "workspaces",          sizeof(cfg->left.widgets));
    g_strlcpy(cfg->center.widgets, "media",               sizeof(cfg->center.widgets));
    g_strlcpy(cfg->right.widgets,  "metrics, volume, clock", sizeof(cfg->right.widgets));

    g_strlcpy(cfg->clock.time_format, "%H:%M",     sizeof(cfg->clock.time_format));
    g_strlcpy(cfg->clock.date_format, "%d/%m/%Y",  sizeof(cfg->clock.date_format));

    cfg->media.show_title  = 1;
    cfg->media.show_artist = 1;
    cfg->media.background  = 1;
    cfg->media.max_title_width = 400;
    g_strlcpy(cfg->volume.app, "pavucontrol", sizeof(cfg->volume.app));
    cfg->volume.show_percent = 0;

    cfg->metrics.layout[0][0] = M_RAM;  cfg->metrics.layout[0][1] = M_CPU;  cfg->metrics.layout[0][2] = M_NONE;
    cfg->metrics.layout[1][0] = M_DISK; cfg->metrics.layout[1][1] = M_TEMP; cfg->metrics.layout[1][2] = M_NONE;
    cfg->metrics.use_bars = 1;
        g_strlcpy(cfg->metrics.temp_path, DEFAULT_TEMP_PATH, sizeof(cfg->metrics.temp_path));

    cfg->nightlight.temp_max  = 6500;
    cfg->nightlight.temp_min  = 5400;
    cfg->nightlight.gamma_max = 100.0;
    cfg->nightlight.gamma_min = 75.0;
    cfg->nightlight.step      = 5;
    g_strlcpy(cfg->nightlight.curve, "ease", sizeof(cfg->nightlight.curve));

    float def_levels[] = {100, 82, 68, 54, 42, 31, 21, 12, 4, 1, 0};
    cfg->brightness.count = 11;
    for (int i = 0; i < 11; i++) cfg->brightness.levels[i] = def_levels[i];
    cfg->brightness.transition_ms = 200;

    cfg->launcher.count = 0;
    g_strlcpy(cfg->chromeos.accent_color, "#0179d4", sizeof(cfg->chromeos.accent_color));
    g_strlcpy(cfg->chromeos.screenshot_app, "~/coding/c/escreen/launch.sh", sizeof(cfg->chromeos.screenshot_app));

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
            g_strlcpy(section, line + 1, sizeof(section));
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
                else if (!strcmp(val, "chromeos")) cfg->mode = MODE_CHROMEOS;
                else                               cfg->mode = MODE_NORMAL;
            }
            else if (!strcmp(key, "margin"))        cfg->margin        = atoi(val);
            else if (!strcmp(key, "height"))        cfg->height        = atoi(val);
            else if (!strcmp(key, "border_radius")) cfg->border_radius = atoi(val);
            else if (!strcmp(key, "padding_h"))     cfg->padding_h     = atoi(val);
            else if (!strcmp(key, "padding_v"))     cfg->padding_v     = atoi(val);
            else if (!strcmp(key, "spacing"))       cfg->spacing       = atoi(val);
        } else if (!strcmp(section, "colors")) {
            if      (!strcmp(key, "background"))   g_strlcpy(cfg->colors.background,    val, sizeof(cfg->colors.background));
            else if (!strcmp(key, "accent"))       g_strlcpy(cfg->colors.accent,         val, sizeof(cfg->colors.accent));
            else if (!strcmp(key, "foreground"))   g_strlcpy(cfg->colors.foreground,     val, sizeof(cfg->colors.foreground));
            else if (!strcmp(key, "dim_foreground")) g_strlcpy(cfg->colors.dim_foreground, val, sizeof(cfg->colors.dim_foreground));
            else if (!strcmp(key, "border"))       g_strlcpy(cfg->colors.border,         val, sizeof(cfg->colors.border));
            else if (!strcmp(key, "ring_color"))   g_strlcpy(cfg->colors.ring_color,     val, sizeof(cfg->colors.ring_color));
        } else if (!strcmp(section, "font")) {
            if      (!strcmp(key, "family")) g_strlcpy(cfg->font.family, val, sizeof(cfg->font.family));
            else if (!strcmp(key, "size"))   cfg->font.size = atoi(val);
        } else if (!strcmp(section, "workspaces")) {
            if      (!strcmp(key, "count"))        cfg->workspaces.count = atoi(val);
            else if (!strcmp(key, "icon_empty"))   g_strlcpy(cfg->workspaces.icon_empty,    val, sizeof(cfg->workspaces.icon_empty));
            else if (!strcmp(key, "icon_occupied"))g_strlcpy(cfg->workspaces.icon_occupied,  val, sizeof(cfg->workspaces.icon_occupied));
            else if (!strcmp(key, "show_empty"))   cfg->workspaces.show_empty = (!strcmp(val, "true"));
        } else if (!strcmp(section, "left")) {
            if (!strcmp(key, "widgets")) g_strlcpy(cfg->left.widgets, val, sizeof(cfg->left.widgets));
        } else if (!strcmp(section, "center")) {
            if (!strcmp(key, "widgets")) g_strlcpy(cfg->center.widgets, val, sizeof(cfg->center.widgets));
        } else if (!strcmp(section, "right")) {
            if (!strcmp(key, "widgets")) g_strlcpy(cfg->right.widgets, val, sizeof(cfg->right.widgets));
        } else if (!strcmp(section, "clock")) {
            if      (!strcmp(key, "time_format")) g_strlcpy(cfg->clock.time_format, val, sizeof(cfg->clock.time_format));
            else if (!strcmp(key, "date_format")) g_strlcpy(cfg->clock.date_format, val, sizeof(cfg->clock.date_format));
        } else if (!strcmp(section, "media")) {
            if      (!strcmp(key, "show_title"))       cfg->media.show_title      = (!strcmp(val, "true"));
            else if (!strcmp(key, "show_artist"))      cfg->media.show_artist     = (!strcmp(val, "true"));
            else if (!strcmp(key, "background"))       cfg->media.background      = (!strcmp(val, "true"));
            else if (!strcmp(key, "max_title_width"))  cfg->media.max_title_width = atoi(val);
        } else if (!strcmp(section, "volume")) {
            if      (!strcmp(key, "app"))          g_strlcpy(cfg->volume.app, val, sizeof(cfg->volume.app));
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
            else if  (!strcmp(key, "temp_path"))   g_strlcpy(cfg->metrics.temp_path, val, sizeof(cfg->metrics.temp_path));
        } else if (!strcmp(section, "nightlight")) {
            if      (!strcmp(key, "temp_max"))  cfg->nightlight.temp_max  = atoi(val);
            else if (!strcmp(key, "temp_min"))  cfg->nightlight.temp_min  = atoi(val);
            else if (!strcmp(key, "gamma_max")) cfg->nightlight.gamma_max = atof(val);
            else if (!strcmp(key, "gamma_min")) cfg->nightlight.gamma_min = atof(val);
            else if (!strcmp(key, "step"))      cfg->nightlight.step      = atoi(val);
            else if (!strcmp(key, "curve"))     strncpy(cfg->nightlight.curve, val, sizeof(cfg->nightlight.curve)-1);
        } else if (!strcmp(section, "brightness")) {
            if (!strcmp(key, "levels")) {
                char tmp[256]; strncpy(tmp, val, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
                char *sp, *tok = strtok_r(tmp, " ", &sp);
                cfg->brightness.count = 0;
                while (tok && cfg->brightness.count < 16) {
                    cfg->brightness.levels[cfg->brightness.count++] = atof(tok);
                    tok = strtok_r(NULL, " ", &sp);
                }
            } else if (!strcmp(key, "transition_ms")) cfg->brightness.transition_ms = atoi(val);
        } else if (!strcmp(section, "launcher")) {
            if (!strcmp(key, "app") && cfg->launcher.count < MAX_LAUNCHER_APPS) {
                char *colon = strchr(val, ':');
                if (colon) {
                    *colon = '\0';
                    g_strlcpy(cfg->launcher.apps[cfg->launcher.count].action, val, sizeof(cfg->launcher.apps[0].action));
                    char *icon_path = colon + 1;
                    if (icon_path[0] == '/') {
                        g_strlcpy(cfg->launcher.apps[cfg->launcher.count].icon_path, icon_path,
                                  sizeof(cfg->launcher.apps[0].icon_path));
                    } else {
                        snprintf(cfg->launcher.apps[cfg->launcher.count].icon_path,
                                 sizeof(cfg->launcher.apps[0].icon_path), 
                                 "%s/.config/ebar/%s", getenv("HOME"), icon_path);
                    }
                    cfg->launcher.count++;
                }
            }
        } else if (!strcmp(section, "chromeos")) {
            if      (!strcmp(key, "accent_color")) g_strlcpy(cfg->chromeos.accent_color, val, sizeof(cfg->chromeos.accent_color));
            else if (!strcmp(key, "screenshot_app")) g_strlcpy(cfg->chromeos.screenshot_app, val, sizeof(cfg->chromeos.screenshot_app));
        }

    }
    fclose(f);
}
