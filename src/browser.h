#ifndef ZHISTAR_BROWSER_H
#define ZHISTAR_BROWSER_H

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include "config.h"
#include "theme.h"
#include "utils.h"

#define APP_NAME "ZhiStar Nova"
#define APP_VERSION "1.0.0-alpha"
#define APP_CODENAME "Nova-Engine"

#define ZHI_TAB_CLOSE_FADE_STEP  0.15
#define ZHI_TAB_CLOSE_ANIM_MS    120
#define ZHI_DOWNLOAD_RETRY_MS    2000
#define ZHI_STATUS_CLEAR_MS      3000
#define ZHI_HISTORY_FLUSH_S      5
#define ZHI_HISTORY_MAX          10000
#define ZHI_STARFIELD_INTERVAL_MS 66
#define ZHI_DL_POLL_INTERVAL_MS  500

typedef struct ZhiBrowser ZhiBrowser;
typedef struct ZhiTab ZhiTab;

const gchar *zhi_i18n(ZhiBrowser *browser, const gchar *zh, const gchar *en);
#define T(browser, zh, en) zhi_i18n(browser, zh, en)

struct ZhiTab {
    GtkWidget  *page;
    GtkWidget  *nav_box;
    GtkWidget  *url_entry;
    GtkWidget  *content;
    GtkWidget  *web_view;
    GtkWidget  *web_scroll;
    GtkWidget  *back_btn;
    GtkWidget  *fwd_btn;
    GtkWidget  *reload_btn;
    GtkWidget  *find_bar;
    GtkWidget  *find_entry;
    GtkWidget  *find_label;
    GtkWidget  *tab_button;
    gchar      *url;
    gchar      *title;
    gboolean    is_home;
    gboolean    is_pinned;
    gboolean    custom_title;
    gboolean    navigating;
    gulong      title_handler_id;
    gulong      load_handler_id;
    GList      *connected_domains;
    GList      *nav_history;
    GList      *nav_current;

    gboolean    download_retry_pending;
    gint        download_retry_count;
    guint       download_retry_timer_id;
    gboolean    closing;
    gdouble     zoom_level;
    GtkWidget  *ssl_icon;
    GtkWidget  *loading_bar;
};

struct ZhiBrowser {
    GtkWidget    *window;
    GtkWidget    *main_vbox;
    GtkWidget    *tab_bar;
    GtkWidget    *tab_scroll;
    GtkWidget    *add_tab_btn;
    GtkWidget    *notebook;
    GtkWidget    *menu_btn;
    GtkWidget    *menu_popover;
    GtkWidget    *status_bar;

    ZhiConfig    *config;
    ZhiTheme     *theme;
    ZhiLang       lang;

    GList        *tabs;
    ZhiTab       *active_tab;
    gint          active_tab_index;

    GtkWidget    *settings_page;
    GtkWidget    *history_page;
    GtkWidget    *css_editor_page;
    GtkWidget    *about_page;

    GList        *history;
    GList        *pinned_bookmarks;
    gboolean      destroying;
    gboolean      status_hover;

    GList        *downloads;
    GtkWidget    *dl_btn;
    GtkWidget    *dl_btn_drawing;
    GtkWidget    *dl_popover;
    GtkWidget    *dl_list_box;
    gint          active_download_count;
    gdouble       dl_progress;
    guint         dl_anim_id;
    gdouble       dl_anim_offset;
    guint         dl_flash_timer_id;
    gboolean      dl_flash_active;
    GtkWidget    *dl_summary_label;
    guint         history_flush_timer;
    GList        *history_write_buf;

    guint         status_clear_timer_id;
    gboolean      tab_bar_update_pending;
    gchar        *adblock_custom_css;

    ZhiTab       *drag_tab;
    GtkWidget    *drag_gap;
    gint          drag_idx;
    GList        *closed_tabs;
    guint         find_count_handler_id;
};

ZhiBrowser *zhi_browser_new(void);
void        zhi_browser_free(ZhiBrowser *browser);
void        zhi_browser_show(ZhiBrowser *browser);
gchar      *sanitize_utf8(const gchar *str);
ZhiTab     *zhi_browser_add_tab(ZhiBrowser *browser, const gchar *url);
void        zhi_browser_close_tab(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_close_tab_now(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_switch_tab(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_navigate(ZhiBrowser *browser, ZhiTab *tab, const gchar *url);
void        zhi_browser_go_back(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_go_forward(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_reload(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_add_history(ZhiBrowser *browser, const gchar *url, const gchar *title);
void        zhi_browser_flush_history(ZhiBrowser *browser);
void        zhi_browser_update_status(ZhiBrowser *browser, const gchar *msg);
void        zhi_browser_set_tab_loading(ZhiBrowser *browser, ZhiTab *tab, gboolean loading);
void        zhi_browser_update_tab_title(ZhiBrowser *browser, ZhiTab *tab, const gchar *query, const gchar *engine_name);
void        zhi_browser_pin_tab(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_unpin_tab(ZhiBrowser *browser, ZhiTab *tab);
void        zhi_browser_add_pinned_bookmark(ZhiBrowser *browser, const gchar *url, const gchar *title);
void        zhi_browser_remove_pinned_bookmark(ZhiBrowser *browser, const gchar *url);
void        zhi_browser_load_pinned_bookmarks(ZhiBrowser *browser);
void        zhi_browser_save_pinned_bookmarks(ZhiBrowser *browser);
void        zhi_browser_load_history(ZhiBrowser *browser);
void        zhi_browser_save_history(ZhiBrowser *browser);
void        zhi_browser_save_download_record(const gchar *uri, const gchar *filename, guint64 size);
void        zhi_browser_start_download(ZhiBrowser *browser, const gchar *uri);

GtkWidget *zhi_new_tab_page_new(ZhiBrowser *browser);
GtkWidget *zhi_settings_page_new(ZhiBrowser *browser);
GtkWidget *zhi_history_page_new(ZhiBrowser *browser);
GtkWidget *zhi_downloads_page_new(ZhiBrowser *browser);
GtkWidget *zhi_css_editor_page_new(ZhiBrowser *browser);
GtkWidget *zhi_bookmarks_page_new(ZhiBrowser *browser);

gchar   **zhi_get_saved_argv(void);
gint      zhi_get_saved_argc(void);
gchar    *find_resource(const gchar *filename);

void zhi_browser_show_menu(ZhiBrowser *browser);
void zhi_browser_reopen_closed_tab(ZhiBrowser *browser);
void zhi_browser_zoom_in(ZhiBrowser *browser);
void zhi_browser_zoom_out(ZhiBrowser *browser);
void zhi_browser_zoom_reset(ZhiBrowser *browser);
void zhi_browser_copy_page_link(ZhiBrowser *browser);
void zhi_browser_show_page_info(ZhiBrowser *browser);
void zhi_browser_add_closed_tab(ZhiBrowser *browser, const gchar *url, const gchar *title);
void zhi_browser_toggle_fullscreen(ZhiBrowser *browser);
void zhi_browser_save_page(ZhiBrowser *browser);
void zhi_browser_print_page(ZhiBrowser *browser);

#endif
