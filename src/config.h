#ifndef ZHISTAR_CONFIG_H
#define ZHISTAR_CONFIG_H

#include <gtk/gtk.h>

#define ZHISTAR_CONFIG_DIR  ".config/zhistar"
#define ZHISTAR_CONFIG_FILE "config.ini"
#define ZHISTAR_CSS_FILE    "ui.css"

typedef struct {
    GHashTable *entries;
    gchar      *config_path;
    gchar      *css_path;
    guint       save_timeout_id;
} ZhiConfig;

ZhiConfig  *zhi_config_new(void);
void        zhi_config_free(ZhiConfig *cfg);
gboolean    zhi_config_load(ZhiConfig *cfg);
gboolean    zhi_config_save(ZhiConfig *cfg);
void        zhi_config_save_debounced(ZhiConfig *cfg);
const gchar *zhi_config_get(ZhiConfig *cfg, const gchar *key, const gchar *def);
void        zhi_config_set(ZhiConfig *cfg, const gchar *key, const gchar *value);
gint        zhi_config_get_int(ZhiConfig *cfg, const gchar *key, gint def);
gboolean    zhi_config_get_bool(ZhiConfig *cfg, const gchar *key, gboolean def);
void        zhi_config_set_int(ZhiConfig *cfg, const gchar *key, gint value);
void        zhi_config_set_bool(ZhiConfig *cfg, const gchar *key, gboolean value);
gchar      *zhi_config_get_config_dir(void);

#endif
