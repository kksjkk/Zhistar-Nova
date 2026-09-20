#ifndef ZHISTAR_THEME_H
#define ZHISTAR_THEME_H

#include <gtk/gtk.h>
#include "config.h"

typedef enum {
    ZHI_THEME_LIGHT,
    ZHI_THEME_DARK
} ZhiThemeType;

typedef enum {
    ZHI_DENSITY_STANDARD,
    ZHI_DENSITY_COMPACT
} ZhiDensityType;

typedef struct {
    ZhiConfig   *config;
    ZhiThemeType  theme;
    ZhiDensityType density;
    gchar        *accent_color;
    gint          font_size;
    GtkCssProvider *provider;
} ZhiTheme;

ZhiTheme *zhi_theme_new(ZhiConfig *config);
void      zhi_theme_free(ZhiTheme *theme);
void      zhi_theme_apply(ZhiTheme *theme, GdkScreen *screen);
void      zhi_theme_set_theme(ZhiTheme *theme, ZhiThemeType type, GdkScreen *screen);
void      zhi_theme_set_density(ZhiTheme *theme, ZhiDensityType density, GdkScreen *screen);
void      zhi_theme_set_accent(ZhiTheme *theme, const gchar *color);
void      zhi_theme_set_font_size(ZhiTheme *theme, gint size);
gchar    *zhi_theme_generate_css(ZhiTheme *theme);
gint zhi_theme_get_accent_index(ZhiTheme *theme);

#endif
