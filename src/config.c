#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

gchar *zhi_config_get_config_dir(void) {
    const gchar *home = g_get_home_dir();
    return g_build_filename(home, ZHISTAR_CONFIG_DIR, NULL);
}

static void ensure_dir(const gchar *path) {
    g_mkdir_with_parents(path, 0755);
}

ZhiConfig *zhi_config_new(void) {
    ZhiConfig *cfg = g_new0(ZhiConfig, 1);
    cfg->entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    cfg->save_timeout_id = 0;
    gchar *dir = zhi_config_get_config_dir();
    ensure_dir(dir);
    cfg->config_path = g_build_filename(dir, ZHISTAR_CONFIG_FILE, NULL);
    cfg->css_path = g_build_filename(dir, ZHISTAR_CSS_FILE, NULL);
    g_free(dir);
    return cfg;
}

void zhi_config_free(ZhiConfig *cfg) {
    if (!cfg) return;
    if (cfg->save_timeout_id > 0) {
        g_source_remove(cfg->save_timeout_id);

        zhi_config_save(cfg);
    }
    g_hash_table_destroy(cfg->entries);
    g_free(cfg->config_path);
    g_free(cfg->css_path);
    g_free(cfg);
}

gboolean zhi_config_load(ZhiConfig *cfg) {
    FILE *f = fopen(cfg->config_path, "r");
    if (!f) return FALSE;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    while ((nread = getline(&line, &len, f)) != -1) {
        if (nread > 0 && line[nread - 1] == '\n') line[nread - 1] = 0;
        if (nread > 1 && line[nread - 2] == '\r') line[nread - 2] = 0;
        if (line[0] == '#' || line[0] == 0) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = g_strstrip(line);
        char *val = g_strstrip(eq + 1);
        if (strlen(key) > 0) {
            gchar *final_val = g_strdup(val);
            gchar *src, *dst;
            for (src = dst = final_val; *src; src++) {
                if (*src == '\\' && *(src + 1) == 'n') { *dst++ = '\n'; src++; }
                else if (*src == '\\' && *(src + 1) == 'r') { *dst++ = '\r'; src++; }
                else *dst++ = *src;
            }
            *dst = 0;
            g_hash_table_insert(cfg->entries, g_strdup(key), final_val);
        }
    }
    free(line);
    fclose(f);
    return TRUE;
}

gboolean zhi_config_save(ZhiConfig *cfg) {
    FILE *f = fopen(cfg->config_path, "w");
    if (!f) return FALSE;
    fprintf(f, "# ZhiStar Nova Configuration\n");
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, cfg->entries);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const gchar *val = (const gchar *)value;
        gchar *safe = NULL;
        if (strchr(val, '\n') || strchr(val, '\r')) {
            safe = g_malloc(strlen(val) * 2 + 1);
            gchar *p = safe;
            for (const gchar *s = val; *s; s++) {
                if (*s == '\n') { *p++ = '\\'; *p++ = 'n'; }
                else if (*s == '\r') { *p++ = '\\'; *p++ = 'r'; }
                else *p++ = *s;
            }
            *p = 0;
            val = safe;
        }
        fprintf(f, "%s=%s\n", (const gchar *)key, val);
        g_free(safe);
    }
    fclose(f);
    return TRUE;
}

static gboolean on_config_save_idle(gpointer data) {
    ZhiConfig *cfg = data;
    cfg->save_timeout_id = 0;
    zhi_config_save(cfg);
    return G_SOURCE_REMOVE;
}

void zhi_config_save_debounced(ZhiConfig *cfg) {
    if (cfg->save_timeout_id > 0) g_source_remove(cfg->save_timeout_id);
    cfg->save_timeout_id = g_timeout_add(500, on_config_save_idle, cfg);
}

const gchar *zhi_config_get(ZhiConfig *cfg, const gchar *key, const gchar *def) {
    const gchar *val = g_hash_table_lookup(cfg->entries, key);
    return val ? val : def;
}

void zhi_config_set(ZhiConfig *cfg, const gchar *key, const gchar *value) {
    g_hash_table_insert(cfg->entries, g_strdup(key), g_strdup(value));
}

gint zhi_config_get_int(ZhiConfig *cfg, const gchar *key, gint def) {
    const gchar *val = g_hash_table_lookup(cfg->entries, key);
    if (!val) return def;
    return atoi(val);
}

gboolean zhi_config_get_bool(ZhiConfig *cfg, const gchar *key, gboolean def) {
    const gchar *val = g_hash_table_lookup(cfg->entries, key);
    if (!val) return def;
    return (g_strcmp0(val, "true") == 0 || g_strcmp0(val, "1") == 0);
}

void zhi_config_set_int(ZhiConfig *cfg, const gchar *key, gint value) {
    gchar *str = g_strdup_printf("%d", value);
    g_hash_table_insert(cfg->entries, g_strdup(key), str);
}

void zhi_config_set_bool(ZhiConfig *cfg, const gchar *key, gboolean value) {
    g_hash_table_insert(cfg->entries, g_strdup(key), g_strdup(value ? "true" : "false"));
}
