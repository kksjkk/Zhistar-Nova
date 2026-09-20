#ifndef ZHISTAR_UTILS_H
#define ZHISTAR_UTILS_H

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

typedef enum { LANG_ZH = 0, LANG_EN = 1 } ZhiLang;

typedef enum {
    ZHI_DL_RUNNING,
    ZHI_DL_FINISHED,
    ZHI_DL_FAILED,
    ZHI_DL_CANCELLED
} ZhiDownloadState;

typedef struct ZhiDownload {
    gchar          *uri;
    gchar          *filename;
    gchar          *dest_path;
    guint64         total_size;
    guint64         received;
    gdouble         speed;
    ZhiDownloadState state;
    WebKitDownload *wk_download;
    GtkWidget      *row_widget;
    GtkWidget      *progress_bar;
    GtkWidget      *status_label;
    GtkWidget      *size_label;
    GtkWidget      *cancel_btn;
    GtkWidget      *copy_btn;
    GtkWidget      *open_btn;
    guint           poll_timer_id;
    gulong          progress_sig;

    gdouble         speed_samples[8];
    gint            speed_sample_idx;
    gint            speed_sample_count;
    gdouble         last_sample_time;
    guint64         last_sample_bytes;
    gboolean        user_cancelled;
    gdouble         last_update_pct;
} ZhiDownload;

gchar *sanitize_utf8(const gchar *str);
gchar *dl_format_size(guint64 bytes);
gchar *zhi_format_eta(guint64 bytes_remaining, gdouble speed, const gchar *zh, const gchar *en);
gchar *dl_format_size(guint64 bytes);
gchar *dl_format_speed(gdouble bytes_per_sec);
gint zhi_search_engine_index(const gchar *url);
const gchar *zhi_search_engine_name(const gchar *url, const gchar *zh, const gchar *en);

#endif
