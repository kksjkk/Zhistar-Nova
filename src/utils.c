#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

gchar *sanitize_utf8(const gchar *str) {
    if (!str) return g_strdup("New Tab");
    if (g_utf8_validate(str, -1, NULL)) return g_strdup(str);
    GString *out = g_string_new(NULL);
    const gchar *p = str;
    const gchar *end = str + strlen(str);
    while (p < end) {
        guchar c = (guchar)*p;
        if (c < 0x80) {
            g_string_append_c(out, *p);
            p++;
        } else if ((c & 0xE0) == 0xC0 && p + 1 < end
                   && (guchar)p[1] >= 0x80 && (guchar)p[1] < 0xC0) {
            g_string_append_printf(out, "%c%c", p[0], p[1]);
            p += 2;
        } else if ((c & 0xF0) == 0xE0 && p + 2 < end
                   && (guchar)p[1] >= 0x80 && (guchar)p[1] < 0xC0
                   && (guchar)p[2] >= 0x80 && (guchar)p[2] < 0xC0) {
            g_string_append_printf(out, "%c%c%c", p[0], p[1], p[2]);
            p += 3;
        } else if ((c & 0xF8) == 0xF0 && p + 3 < end
                   && (guchar)p[1] >= 0x80 && (guchar)p[1] < 0xC0
                   && (guchar)p[2] >= 0x80 && (guchar)p[2] < 0xC0
                   && (guchar)p[3] >= 0x80 && (guchar)p[3] < 0xC0) {
            g_string_append_printf(out, "%c%c%c%c", p[0], p[1], p[2], p[3]);
            p += 4;
        } else {
            p++;
        }
    }
    gchar *result = g_string_free(out, FALSE);
    if (strlen(result) == 0) {
        g_free(result);
        return g_strdup("New Tab");
    }
    return result;
}

gchar *dl_format_size(guint64 bytes) {
    if (bytes < 1024) return g_strdup_printf("%lu B", (unsigned long)bytes);
    if (bytes < 1024 * 1024) return g_strdup_printf("%.1f KB", bytes / 1024.0);
    if (bytes < 1024 * 1024 * 1024) return g_strdup_printf("%.1f MB", bytes / (1024.0 * 1024));
    return g_strdup_printf("%.2f GB", bytes / (1024.0 * 1024 * 1024));
}

gchar *dl_format_speed(gdouble bytes_per_sec) {
    if (bytes_per_sec < 1) return g_strdup("--");
    if (bytes_per_sec < 1024) return g_strdup_printf("%.0f B/s", bytes_per_sec);
    if (bytes_per_sec < 1024 * 1024) return g_strdup_printf("%.1f KB/s", bytes_per_sec / 1024.0);
    return g_strdup_printf("%.1f MB/s", bytes_per_sec / (1024.0 * 1024));
}

gchar *zhi_format_eta(guint64 bytes_remaining, gdouble speed, const gchar *zh, const gchar *en) {
    if (speed < 1) return g_strdup("--:--");
    gint seconds = (gint)(bytes_remaining / speed);
    if (seconds < 0) seconds = 0;
    gint h = seconds / 3600;
    gint m = (seconds % 3600) / 60;
    gint s = seconds % 60;
    (void)zh; (void)en;
    if (h > 0) return g_strdup_printf("%d:%02d:%02d", h, m, s);
    return g_strdup_printf("%02d:%02d", m, s);
}

gint zhi_search_engine_index(const gchar *url) {
    if (!url) return 0;
    if (g_str_has_prefix(url, "https://www.google.com")) return 1;
    if (g_str_has_prefix(url, "https://duckduckgo.com")) return 2;
    if (g_str_has_prefix(url, "https://search.yahoo.com")) return 3;
    if (g_str_has_prefix(url, "https://www.baidu")) return 4;
    if (g_str_has_prefix(url, "https://yandex")) return 5;
    return 0;
}

const gchar *zhi_search_engine_name(const gchar *url, const gchar *zh, const gchar *en) {
    (void)zh; (void)en;
    if (!url) return "Search";
    if (g_str_has_prefix(url, "https://www.bing")) return "Bing";
    if (g_str_has_prefix(url, "https://www.google")) return "Google";
    if (g_str_has_prefix(url, "https://duckduckgo")) return "DuckDuckGo";
    if (g_str_has_prefix(url, "https://search.yahoo")) return "Yahoo";
    if (g_str_has_prefix(url, "https://www.baidu")) return zh ? zh : "Baidu";
    return "Search";
}
