#include "theme.h"
#include <string.h>
#include <stdlib.h>

#define ACCENT_DEFAULT "#2dd4a8"
#define BG_LIGHT        "#f5f5f5"
#define BG_DARK         "#1e1e2e"
#define SURFACE_LIGHT   "#ffffff"
#define SURFACE_DARK    "#2b2b3d"
#define FG_LIGHT        "#1e1e2e"
#define FG_DARK         "#e0e0e0"
#define TAB_LIGHT       "#e8e8e8"
#define TAB_DARK        "#252536"
#define TAB_ACTIVE_LIGHT "#ffffff"
#define TAB_ACTIVE_DARK  "#32324a"
#define BORDER_LIGHT    "#d0d0d0"
#define BORDER_DARK     "#3a3a4f"

ZhiTheme *zhi_theme_new(ZhiConfig *config) {
    ZhiTheme *theme = g_new0(ZhiTheme, 1);
    theme->config = config;
    theme->provider = gtk_css_provider_new();

    const gchar *t = zhi_config_get(config, "theme", "dark");
    theme->theme = (g_strcmp0(t, "light") == 0) ? ZHI_THEME_LIGHT : ZHI_THEME_DARK;

    const gchar *d = zhi_config_get(config, "density", "standard");
    theme->density = (g_strcmp0(d, "compact") == 0) ? ZHI_DENSITY_COMPACT : ZHI_DENSITY_STANDARD;

    theme->accent_color = g_strdup(zhi_config_get(config, "accent_color", ACCENT_DEFAULT));
    theme->font_size = zhi_config_get_int(config, "font_size", 13);

    return theme;
}

void zhi_theme_free(ZhiTheme *theme) {
    if (!theme) return;
    g_object_unref(theme->provider);
    g_free(theme->accent_color);
    g_free(theme);
}

static void hex_to_rgb(const gchar *hex, guchar *r, guchar *g, guchar *b) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) {
        *r = *g = *b = 128;
        return;
    }
    gchar *end = NULL;
    *r = (guchar)strtol(hex + 1, &end, 16);
    *g = (guchar)strtol(end, &end, 16);
    *b = (guchar)strtol(end, NULL, 16);
}

gchar *zhi_theme_generate_css(ZhiTheme *theme) {
    gboolean dark = (theme->theme == ZHI_THEME_DARK);

    const gchar *bg         = dark ? BG_DARK         : BG_LIGHT;
    const gchar *fg         = dark ? FG_DARK         : FG_LIGHT;
    const gchar *border     = dark ? BORDER_DARK     : BORDER_LIGHT;
    const gchar *tab_color  = dark ? TAB_DARK        : TAB_LIGHT;
    const gchar *tab_active = dark ? TAB_ACTIVE_DARK  : TAB_ACTIVE_LIGHT;
    const gchar *tab_hover  = dark ? "#333348"        : "#d8d8d8";
    const gchar *url_bg     = dark ? "#353549"        : "#ffffff";
    const gchar *url_fg     = dark ? "#e0e0e0"        : "#333333";
    const gchar *url_border = dark ? "#4a4a60"        : "#c0c0c0";
    const gchar *menu_bg    = dark ? "#2b2b3d"        : "#ffffff";
    const gchar *menu_hover = dark ? "#3a3a50"        : "#e8e8e8";
    const gchar *muted      = dark ? "#666680"        : "#999999";
    const gchar *scrollbar  = dark ? "#4a4a60"        : "#c0c0c0";
    const gchar *scroll_hv  = dark ? "#5a5a70"        : "#a0a0a0";
    const gchar *close_hover  = dark ? "#3a3a50"      : "#e0e0e0";
    const gchar *close_active = dark ? "#4a4a60"      : "#d0d0d0";

    guchar ar, ag, ab;
    hex_to_rgb(theme->accent_color, &ar, &ag, &ab);

    gchar *template_data = NULL;
    const gchar *home = g_get_home_dir();

    gchar *self = g_file_read_link("/proc/self/exe", NULL);
    if (self) {
        gchar *dir = g_path_get_dirname(self);
        const gchar *rel_paths[] = {"data/zhistar.css", "../data/zhistar.css", "../../data/zhistar.css"};
        for (int i = 0; i < 3; i++) {
            gchar *candidate = g_build_filename(dir, rel_paths[i], NULL);
            if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR)) {
                g_file_get_contents(candidate, &template_data, NULL, NULL);
                g_free(candidate);
                if (template_data) break;
            }
            g_free(candidate);
        }
        g_free(dir);
        g_free(self);
    }

    if (!template_data) {
        const gchar *search_paths[] = {
            "data/zhistar.css",
            "../data/zhistar.css",
            "../../data/zhistar.css",
            NULL
        };
        for (int i = 0; search_paths[i]; i++) {
            if (g_file_test(search_paths[i], G_FILE_TEST_IS_REGULAR)) {
                g_file_get_contents(search_paths[i], &template_data, NULL, NULL);
                if (template_data) break;
            }
        }
    }
    if (!template_data && home) {
        gchar *home_path = g_build_filename(home, ".config/zhistar/zhistar.css", NULL);
        if (g_file_test(home_path, G_FILE_TEST_IS_REGULAR))
            g_file_get_contents(home_path, &template_data, NULL, NULL);
        g_free(home_path);
    }

    if (!template_data) {

        return g_strdup_printf(
            "window{background-color:%s;}"
            "*{color:%s;font-size:%dpx;}"
            "#tab-scroll{background-color:%s;}"
            ".tab{background-color:%s;padding:4px 8px;border-radius:6px;margin:2px;min-width:120px;}"
            ".tab.active{background-color:%s;font-weight:bold;}"
            ".tab:hover{background-color:%s;}"
            ".tab-close-btn{background:transparent;border:none;padding:0;min-width:20px;min-height:20px;border-radius:4px;cursor:pointer;}"
            ".tab-close-btn:hover{background-color:%s;}"
            ".tab-close-btn:active{background-color:%s;}"
            "#nav-bar{background-color:%s;padding:2px 4px;}"
            "#url-bar{background-color:%s;color:%s;border:1px solid %s;border-radius:6px;padding:4px 8px;}"
            "#url-bar:focus{border-color:%s;}"
            "#loading-bar progress, #loading-bar trough{min-height:2px;}"
            ".ssl-secure{color:#66bb6a;}"
            ".ssl-insecure{color:#ef5350;}"
            "#status-bar{background-color:%s;color:%s;padding:2px 8px;font-size:11px;}"
            ".menu-item-btn{background:transparent;border:none;padding:4px 8px;text-align:left;}"
            ".menu-item-btn:hover{background-color:%s;}"
            ".menu-popover-anim{background-color:%s;border:1px solid %s;border-radius:8px;}"
            ".settings-title{font-size:18px;font-weight:bold;padding:12px;}"
            ".settings-label{padding:4px 12px;}"
            ".settings-row{padding:8px 12px;}"
            ".history-item-title{font-weight:500;}"
            ".history-item-url{color:%s;font-size:11px;}"
            ".history-item-time{color:%s;font-size:11px;min-width:50px;}"
            ".history-date-header{font-weight:bold;padding:4px 12px;color:%s;}"
            ".about-title{font-size:20px;font-weight:bold;}"
            ".about-version{color:%s;font-size:12px;}",
            bg, fg, theme->font_size,
            bg, tab_color, tab_active, tab_hover,
            close_hover, close_active,
            bg, url_bg, url_fg, url_border, theme->accent_color,
            bg, fg, menu_hover, menu_bg, border,
            muted, muted, muted, muted);
    }

    gboolean compact = (theme->density == ZHI_DENSITY_COMPACT);
    struct { const gchar *key; const gchar *val; guint len; } reps[] = {
        {"DENSITY_TAB_PAD_V",    compact ? "2px" : "4px", 0},
        {"DENSITY_TAB_PAD_H",    compact ? "4px" : "8px", 0},
        {"DENSITY_NAV_PAD_V",    compact ? "2px" : "4px", 0},
        {"DENSITY_NAV_PAD_H",    compact ? "4px" : "8px", 0},
        {"DENSITY_NAV_HEIGHT",   compact ? "32px" : "40px", 0},
        {"SCROLLBAR_HOVER_COLOR", scroll_hv, 0},
        {"TAB_ACTIVE_COLOR",      tab_active, 0},
        {"SCROLLBAR_COLOR",       scrollbar, 0},
        {"URL_BORDER_COLOR",      url_border, 0},
        {"MENU_HOVER_COLOR",      menu_hover, 0},
        {"ACCENT_COLOR",          theme->accent_color, 0},
        {"BORDER_COLOR",          border, 0},
        {"TAB_HOVER_COLOR",       tab_hover, 0},
        {"CLOSE_HOVER_COLOR",     close_hover, 0},
        {"CLOSE_ACTIVE_COLOR",    close_active, 0},
        {"URL_BG_COLOR",          url_bg, 0},
        {"URL_FG_COLOR",          url_fg, 0},
        {"MENU_BG_COLOR",         menu_bg, 0},
        {"TAB_COLOR",             tab_color, 0},
        {"FG_COLOR",              fg, 0},
        {"BG_COLOR",              bg, 0},
        {"MUTED_COLOR",           muted, 0},
        {NULL, NULL, 0}
    };
    for (int i = 0; reps[i].key; i++)
        reps[i].len = strlen(reps[i].key);

    GString *out = g_string_new("");
    gchar *p = template_data;

    while (*p) {

        if (g_str_has_prefix(p, "ACCENT_COLOR_RAW")) {
            g_string_append_printf(out, "%d,%d,%d", ar, ag, ab);
            p += 16;
            continue;
        }

        if (g_str_has_prefix(p, "FONT_SIZE")) {
            g_string_append_printf(out, "%d", theme->font_size);
            p += 9;
            continue;
        }

        if (g_ascii_isupper(*p) || *p == '_') {
            gchar *end = p + 1;
            while (*end && (g_ascii_isalnum(*end) || *end == '_')) end++;
            guint len = end - p;
            gboolean found = FALSE;
            for (int i = 0; reps[i].key; i++) {
                if (reps[i].len == len && g_str_has_prefix(p, reps[i].key)) {
                    g_string_append(out, reps[i].val);
                    p = end;
                    found = TRUE;
                    break;
                }
            }
            if (found) continue;
        }
        g_string_append_c(out, *p);
        p++;
    }

    g_free(template_data);
    return g_string_free(out, FALSE);
}

void zhi_theme_apply(ZhiTheme *theme, GdkScreen *screen) {
    gchar *css = zhi_theme_generate_css(theme);

    const gchar *home = g_get_home_dir();
    if (home) {
        gchar *user_css_path = g_build_filename(home, ".config/zhistar/ui.css", NULL);
        if (g_file_test(user_css_path, G_FILE_TEST_IS_REGULAR)) {
            gchar *user_css = NULL;
            g_file_get_contents(user_css_path, &user_css, NULL, NULL);
            if (user_css && g_strstrip(user_css)[0] != '\0') {
                gchar *combined = g_strdup_printf("%s\n/* User Custom CSS */\n%s", css, user_css);
                g_free(css);
                css = combined;
            }
            g_free(user_css);
        }
        g_free(user_css_path);
    }

    GError *err = NULL;
    gtk_css_provider_load_from_data(theme->provider, css, -1, &err);
    if (err) {
        g_warning("CSS load error: %s", err->message);
        g_error_free(err);
    }
    gtk_style_context_add_provider_for_screen(screen,
        GTK_STYLE_PROVIDER(theme->provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css);
}

void zhi_theme_set_theme(ZhiTheme *theme, ZhiThemeType type, GdkScreen *screen) {
    theme->theme = type;
    zhi_config_set(theme->config, "theme", type == ZHI_THEME_DARK ? "dark" : "light");
    if (screen) zhi_theme_apply(theme, screen);
}

void zhi_theme_set_density(ZhiTheme *theme, ZhiDensityType density, GdkScreen *screen) {
    theme->density = density;
    zhi_config_set(theme->config, "density", density == ZHI_DENSITY_COMPACT ? "compact" : "standard");
    if (screen) zhi_theme_apply(theme, screen);
}

void zhi_theme_set_accent(ZhiTheme *theme, const gchar *color) {
    g_free(theme->accent_color);
    theme->accent_color = g_strdup(color);
    zhi_config_set(theme->config, "accent_color", color);
}

void zhi_theme_set_font_size(ZhiTheme *theme, gint size) {
    theme->font_size = size;
    zhi_config_set_int(theme->config, "font_size", size);
}

gint zhi_theme_get_accent_index(ZhiTheme *theme) {
    const gchar *colors[] = {"#2dd4a8", "#4fc3f7", "#7c4dff", "#ff7043",
        "#66bb6a", "#ffa726", "#ef5350", "#ab47bc", NULL};
    if (!theme->accent_color) return 0;
    for (gint i = 0; colors[i]; i++) {
        if (g_strcmp0(theme->accent_color, colors[i]) == 0) return i;
    }
    return 0;
}
