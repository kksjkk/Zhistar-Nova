#include "browser.h"
#include <string.h>
#include <time.h>

static void on_history_clear_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    ZhiBrowser *browser = user_data;
    g_list_free_full(browser->history, g_free);
    browser->history = NULL;

    const gchar *home = g_get_home_dir();
    if (home) {
        gchar *path = g_build_filename(home, ".config/zhistar", "history.txt", NULL);
        g_file_set_contents(path, "", -1, NULL);
        g_free(path);
    }
    zhi_browser_update_status(browser, T(browser, "历史记录已清除", "History cleared"));
}

typedef struct {
    ZhiBrowser *browser;
    GtkWidget   *list;
    GtkWidget   *search_entry;
    GtkWidget   *empty_label;
    const gchar *filter;
} HistoryCtx;

static gchar *format_history_date(const gchar *timestamp_str) {
    if (!timestamp_str) return g_strdup("");
    long ts = atol(timestamp_str);
    struct tm *tm_info = localtime((time_t *)&ts);
    gchar *buf = g_new0(gchar, 64);
    strftime(buf, 64, "%Y-%m-%d", tm_info);
    return buf;
}

static gchar *format_history_time_short(const gchar *timestamp_str) {
    if (!timestamp_str) return g_strdup("");
    long ts = atol(timestamp_str);
    struct tm *tm_info = localtime((time_t *)&ts);
    gchar *buf = g_new0(gchar, 16);
    strftime(buf, 16, "%H:%M", tm_info);
    return buf;
}

static gboolean matches_filter(const gchar *entry, const gchar *filter) {
    if (!filter || strlen(filter) == 0) return TRUE;
    gchar **parts = g_strsplit(entry, "\t", 3);
    gboolean match = FALSE;
    if (parts[0]) {
        gchar *lower_entry = g_utf8_strdown(entry, -1);
        gchar *lower_filter = g_utf8_strdown(filter, -1);
        match = strstr(lower_entry, lower_filter) != NULL;
        g_free(lower_entry);
        g_free(lower_filter);
    }
    g_strfreev(parts);
    return match;
}

static void on_history_delete_clicked(GtkButton *btn, gpointer user_data) {
    HistoryCtx *ctx = user_data;
    ZhiBrowser *browser = ctx->browser;
    const gchar *url = g_object_get_data(G_OBJECT(btn), "url");
    const gchar *ts = g_object_get_data(G_OBJECT(btn), "timestamp");
    if (!url || !ts) return;

    GList *to_remove = NULL;
    for (GList *l = browser->history; l; l = l->next) {
        gchar *entry = l->data;
        if (g_str_has_prefix(entry, ts) && strstr(entry, url)) {
            to_remove = l;
            break;
        }
    }
    if (to_remove) {
        g_free(to_remove->data);
        browser->history = g_list_delete_link(browser->history, to_remove);
    }

    const gchar *home = g_get_home_dir();
    if (home) {
        gchar *path = g_build_filename(home, ".config/zhistar", "history.txt", NULL);
        FILE *f = fopen(path, "w");
        if (f) {
            for (GList *l = browser->history; l; l = l->next)
                fprintf(f, "%s\n", (gchar *)l->data);
            fclose(f);
        }
        g_free(path);
    }

    GtkWidget *row = GTK_WIDGET(btn);
    GtkWidget *parent = gtk_widget_get_parent(row);
    if (parent) {
        GtkWidget *sep = g_object_get_data(G_OBJECT(row), "separator");
        gtk_widget_destroy(row);
        if (sep) gtk_widget_destroy(sep);
    }
}

static void on_history_row_clicked(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *url = g_object_get_data(G_OBJECT(btn), "url");
    if (url && browser->active_tab)
        zhi_browser_navigate(browser, browser->active_tab, url);
}

static void rebuild_history_list(HistoryCtx *ctx) {

    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(children);

    ZhiBrowser *browser = ctx->browser;
    gint shown = 0;
    gchar *last_date = NULL;
    for (GList *l = browser->history; l && shown < 500; l = l->next) {
        if (!l->data) continue;
        gchar *entry = l->data;
        if (!matches_filter(entry, ctx->filter)) continue;
        gchar **parts = g_strsplit(entry, "\t", 3);
        if (!parts[0] || !parts[1]) { g_strfreev(parts); continue; }
        const gchar *ts_str = parts[0];
        const gchar *url = parts[1];
        const gchar *title_str = parts[2] ? parts[2] : url;

        gchar *date_str = format_history_date(ts_str);
        if (!last_date || g_strcmp0(date_str, last_date) != 0) {
            GtkWidget *hdr = gtk_label_new(date_str);
            gtk_widget_set_halign(hdr, GTK_ALIGN_START);
            gtk_widget_set_margin_start(hdr, 12);
            gtk_widget_set_margin_top(hdr, 8);
            gtk_widget_set_margin_bottom(hdr, 4);
            GtkStyleContext *hctx = gtk_widget_get_style_context(hdr);
            gtk_style_context_add_class(hctx, "history-date-header");
            gtk_box_pack_start(GTK_BOX(ctx->list), hdr, FALSE, FALSE, 0);
            g_free(last_date);
            last_date = g_strdup(date_str);
        }
        g_free(date_str);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);
        gtk_widget_set_margin_top(row, 2);
        gtk_widget_set_margin_bottom(row, 2);

        gchar *time_str = format_history_time_short(ts_str);
        GtkWidget *time_lbl = gtk_label_new(time_str);
        gtk_widget_set_valign(time_lbl, GTK_ALIGN_CENTER);
        GtkStyleContext *ttctx = gtk_widget_get_style_context(time_lbl);
        gtk_style_context_add_class(ttctx, "history-item-time");
        gtk_box_pack_start(GTK_BOX(row), time_lbl, FALSE, FALSE, 0);
        g_free(time_str);

        GtkWidget *nav_btn = gtk_button_new();
        GtkStyleContext *rctx = gtk_widget_get_style_context(nav_btn);
        gtk_style_context_add_class(rctx, "menu-item-btn");
        GtkWidget *nav_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *icon = gtk_image_new_from_icon_name("web-browser-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(nav_hbox), icon, FALSE, FALSE, 0);
        gchar *safe_title = sanitize_utf8(title_str);
        GtkWidget *title_lbl = gtk_label_new(safe_title);
        g_free(safe_title);
        gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(title_lbl), 40);
        GtkStyleContext *lctx = gtk_widget_get_style_context(title_lbl);
        gtk_style_context_add_class(lctx, "history-item-title");
        gtk_box_pack_start(GTK_BOX(nav_hbox), title_lbl, FALSE, FALSE, 0);
        GtkWidget *url_lbl = gtk_label_new(url);
        gtk_widget_set_halign(url_lbl, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(url_lbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(url_lbl), 50);
        GtkStyleContext *uctx = gtk_widget_get_style_context(url_lbl);
        gtk_style_context_add_class(uctx, "history-item-url");
        gtk_box_pack_start(GTK_BOX(nav_hbox), url_lbl, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(nav_btn), nav_hbox);
        g_object_set_data_full(G_OBJECT(nav_btn), "url", g_strdup(url), g_free);
        g_signal_connect(nav_btn, "clicked", G_CALLBACK(on_history_row_clicked), browser);
        gtk_box_pack_start(GTK_BOX(row), nav_btn, TRUE, TRUE, 0);

        GtkWidget *del_btn = gtk_button_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        GtkStyleContext *dctx = gtk_widget_get_style_context(del_btn);
        gtk_style_context_add_class(dctx, "history-delete-btn");
        g_object_set_data_full(G_OBJECT(del_btn), "url", g_strdup(url), g_free);
        g_object_set_data_full(G_OBJECT(del_btn), "timestamp", g_strdup(ts_str), g_free);
        g_signal_connect(del_btn, "clicked", G_CALLBACK(on_history_delete_clicked), ctx);
        gtk_box_pack_start(GTK_BOX(row), del_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ctx->list), row, FALSE, FALSE, 0);
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        g_object_set_data(G_OBJECT(del_btn), "separator", sep);
        gtk_box_pack_start(GTK_BOX(ctx->list), sep, FALSE, FALSE, 0);
        shown++;
        g_strfreev(parts);
    }
    g_free(last_date);
    if (shown == 0) {
        gtk_widget_show(ctx->empty_label);
        gtk_box_pack_start(GTK_BOX(ctx->list), ctx->empty_label, TRUE, TRUE, 0);
    } else {
        gtk_widget_hide(ctx->empty_label);
    }
}

static void on_history_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    HistoryCtx *ctx = user_data;
    ctx->filter = gtk_entry_get_text(GTK_ENTRY(entry));
    rebuild_history_list(ctx);
}

GtkWidget *zhi_history_page_new(ZhiBrowser *browser) {
    HistoryCtx *ctx = g_new0(HistoryCtx, 1);
    ctx->browser = browser;
    ctx->filter = "";
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_name(vbox, "history-page");
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_vexpand(vbox, TRUE);
    g_object_set_data_full(G_OBJECT(vbox), "history-ctx", ctx, g_free);

    GtkWidget *title = gtk_label_new(T(browser, "历史记录", "History"));
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_start(title, 12);
    gtk_widget_set_margin_top(title, 12);
    gtk_widget_set_margin_bottom(title, 4);
    GtkStyleContext *tctx = gtk_widget_get_style_context(title);
    gtk_style_context_add_class(tctx, "settings-title");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
    GtkWidget *clear_btn = gtk_button_new_with_label(T(browser, "清除所有历史记录", "Clear All History"));
    gtk_widget_set_halign(clear_btn, GTK_ALIGN_END);
    gtk_widget_set_margin_end(clear_btn, 12);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_history_clear_clicked), browser);
    gtk_box_pack_start(GTK_BOX(vbox), clear_btn, FALSE, FALSE, 0);

    ctx->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctx->search_entry),
        T(browser, "搜索历史记录...", "Search history..."));
    gtk_widget_set_margin_start(ctx->search_entry, 12);
    gtk_widget_set_margin_end(ctx->search_entry, 12);
    g_signal_connect(ctx->search_entry, "search-changed", G_CALLBACK(on_history_search_changed), ctx);
    gtk_box_pack_start(GTK_BOX(vbox), ctx->search_entry, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    ctx->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(ctx->list, "history-list");
    ctx->empty_label = gtk_label_new(T(browser, "暂无历史记录", "No history yet"));
    gtk_widget_set_halign(ctx->empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(ctx->empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ctx->empty_label, 48);
    gtk_style_context_add_class(gtk_widget_get_style_context(ctx->empty_label), "settings-label");
    rebuild_history_list(ctx);
    gtk_container_add(GTK_CONTAINER(scroll), ctx->list);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    return vbox;
}

static void on_css_save(GtkButton *btn, gpointer user_data) {
    (void)btn;
    GtkWidget *textview = user_data;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    const gchar *home = g_get_home_dir();
    gchar *dir = g_build_filename(home, ".config/zhistar", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, "ui.css", NULL);
    g_free(dir);
    gboolean is_empty = (!text || g_strstrip(text)[0] == '\0');
    FILE *f = fopen(path, "w");
    if (f) {
        if (!is_empty) fputs(text, f);
        if (ferror(f)) g_warning("Failed to save CSS to %s", path);
        fclose(f);
    }
    if (is_empty) remove(path);
    g_free(path); g_free(text);
    ZhiBrowser *browser = g_object_get_data(G_OBJECT(textview), "browser");
    if (browser) {
        GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(browser->window));
        zhi_theme_apply(browser->theme, screen);
        gchar *generated = zhi_theme_generate_css(browser->theme);
        gtk_text_buffer_set_text(buf, generated, -1);
        g_free(generated);
        zhi_browser_update_status(browser, T(browser, "已恢复默认主题", "Default theme restored"));
    }
}

static void on_css_apply(GtkButton *btn, gpointer user_data) {
    (void)btn;
    ZhiBrowser *browser = user_data;
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(browser->window));
    zhi_theme_apply(browser->theme, screen);
    zhi_browser_update_status(browser, T(browser, "自定义 CSS 已应用", "Custom CSS applied"));
}

GtkWidget *zhi_css_editor_page_new(ZhiBrowser *browser) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(vbox, "css-editor-page");
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_vexpand(vbox, TRUE);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 24);
    gtk_widget_set_margin_end(hbox, 24);
    gtk_widget_set_margin_top(hbox, 20);
    gtk_widget_set_margin_bottom(hbox, 8);
    GtkWidget *title = gtk_label_new(T(browser, "CSS 编辑器 - ui.css", "CSS Editor - ui.css"));
    GtkStyleContext *tctx = gtk_widget_get_style_context(title);
    gtk_style_context_add_class(tctx, "settings-title");
    gtk_box_pack_start(GTK_BOX(hbox), title, TRUE, TRUE, 0);
    GtkWidget *save_btn = gtk_button_new_with_label(T(browser, "保存", "Save"));
    gtk_box_pack_end(GTK_BOX(hbox), save_btn, FALSE, FALSE, 0);
    GtkWidget *apply_btn = gtk_button_new_with_label(T(browser, "应用", "Apply"));
    gtk_box_pack_end(GTK_BOX(hbox), apply_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget *textview = gtk_text_view_new();
    gtk_widget_set_name(textview, "css-textview");
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(textview), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(textview), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(textview), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(textview), 8);
    const gchar *home = g_get_home_dir();
    gchar *css_path = g_build_filename(home, ".config/zhistar/ui.css", NULL);
    gboolean loaded = FALSE;
    if (g_file_test(css_path, G_FILE_TEST_EXISTS)) {
        gchar *contents = NULL;
        g_file_get_contents(css_path, &contents, NULL, NULL);
        if (contents && g_strstrip(contents)[0] != '\0') {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
            gtk_text_buffer_set_text(buf, contents, -1);
            loaded = TRUE;
        }
        g_free(contents);
    }
    g_free(css_path);
    if (!loaded) {
        gchar *generated = zhi_theme_generate_css(browser->theme);
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buf, generated, -1);
        g_free(generated);
    }
    gtk_container_add(GTK_CONTAINER(scroll), textview);
    g_object_set_data(G_OBJECT(textview), "browser", browser);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_css_save), textview);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_css_apply), browser);
    return vbox;
}

static gchar *dl_history_path(void) {
    const gchar *home = g_get_home_dir();
    return home ? g_build_filename(home, ".config/zhistar", "downloads.txt", NULL) : NULL;
}

void zhi_browser_save_download_record(const gchar *uri, const gchar *filename, guint64 size) {
    gchar *path = dl_history_path();
    if (!path) return;
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%ld\t%s\t%s\t%lu\n", (long)time(NULL), uri, filename ? filename : "", (unsigned long)size);
        fclose(f);
    }
    g_free(path);
}

static GList *load_download_history(void) {
    GList *list = NULL;
    gchar *path = dl_history_path();
    if (!path) return NULL;
    gchar *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (gint i = 0; lines[i]; i++) {
            gchar *stripped = g_strstrip(lines[i]);
            if (strlen(stripped) == 0) continue;

            if (!g_utf8_validate(stripped, -1, NULL)) continue;
            gchar **parts = g_strsplit(stripped, "\t", 4);
            if (!parts[0] || !parts[1]) { g_strfreev(parts); continue; }

            if (strlen(parts[1]) == 0) { g_strfreev(parts); continue; }
            g_strfreev(parts);
            list = g_list_append(list, g_strdup(stripped));
        }
        g_strfreev(lines);
        g_free(contents);
    }
    g_free(path);
    return list;
}

typedef struct {
    ZhiBrowser *browser;
    GtkWidget   *list;
    GtkWidget   *search_entry;
    GtkWidget   *empty_label;
    const gchar *filter;
} DownloadsCtx;

static void on_dl_history_copy_link(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(btn), "uri");
    if (!uri) return;
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, uri, -1);
}

static void do_redownload(ZhiBrowser *browser, const gchar *uri) {
    if (!uri || !browser) return;

    GList *to_remove = NULL;
    for (GList *l = browser->downloads; l; l = l->next) {
        ZhiDownload *dl = l->data;
        if (dl->uri && g_strcmp0(dl->uri, uri) == 0) {
            if (dl->wk_download) {
                dl->user_cancelled = TRUE;
                g_signal_handlers_disconnect_by_data(dl->wk_download, dl);
                webkit_download_cancel(dl->wk_download);
                dl->wk_download = NULL;
            }
            if (dl->poll_timer_id) { g_source_remove(dl->poll_timer_id); dl->poll_timer_id = 0; }
            dl->state = ZHI_DL_CANCELLED;
            if (dl->row_widget) gtk_widget_destroy(dl->row_widget);
            to_remove = g_list_append(to_remove, dl);
        }
    }
    for (GList *l = to_remove; l; l = l->next) {
        ZhiDownload *dl = l->data;
        browser->downloads = g_list_remove(browser->downloads, dl);
        browser->active_download_count--;
        g_free(dl->uri); g_free(dl->filename); g_free(dl->dest_path);
        g_free(dl);
    }
    g_list_free(to_remove);

    gchar *load_url = g_strdup(uri);
    if (!g_str_has_prefix(uri, "http://") && !g_str_has_prefix(uri, "https://") &&
        !g_str_has_prefix(uri, "about:") && !g_str_has_prefix(uri, "file://")) {
        g_free(load_url);
        load_url = g_strdup_printf("https://%s", uri);
    }
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(browser->active_tab->web_view), load_url);
    g_free(load_url);
}

static void on_dl_history_redownload(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(btn), "uri");
    do_redownload(browser, uri);
}

static void on_dl_history_redownload_menu(GtkMenuItem *item, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(item), "uri");
    do_redownload(browser, uri);
}

static void on_dl_history_copy_link_menu(GtkMenuItem *item, gpointer user_data) {
    (void)user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(item), "uri");
    if (!uri) return;
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, uri, -1);
}

static void on_dl_history_open_folder(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    const gchar *dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!dir) dir = g_get_home_dir();
    const gchar *argv[] = {"xdg-open", dir, NULL};
    GError *err = NULL;
    g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
}

static void on_dl_history_row_menu(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return;
    ZhiBrowser *browser = user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(widget), "uri");
    if (!uri || !browser) return;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *redl = gtk_menu_item_new_with_label(T(browser, "重新下载", "Redownload"));
    g_object_set_data(G_OBJECT(redl), "uri", (gchar *)uri);
    g_signal_connect(redl, "activate", G_CALLBACK(on_dl_history_redownload_menu), browser);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), redl);
    GtkWidget *copy = gtk_menu_item_new_with_label(T(browser, "复制链接", "Copy Link"));
    g_object_set_data(G_OBJECT(copy), "uri", (gchar *)uri);
    g_signal_connect(copy, "activate", G_CALLBACK(on_dl_history_copy_link_menu), browser);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
    GtkWidget *open_folder = gtk_menu_item_new_with_label(T(browser, "打开下载文件夹", "Open Download Folder"));
    g_signal_connect(open_folder, "activate", G_CALLBACK(on_dl_history_open_folder), browser);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_folder);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

static void on_dl_history_delete(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(btn), "uri");
    const gchar *ts = g_object_get_data(G_OBJECT(btn), "timestamp");
    if (!uri || !ts) return;

    gchar *path = dl_history_path();
    if (!path) return;
    GList *all = load_download_history();
    GList *new_list = NULL;
    for (GList *l = all; l; l = l->next) {
        gchar *entry = l->data;
        if (g_str_has_prefix(entry, ts) && strstr(entry, uri)) {
            g_free(entry);
        } else {
            new_list = g_list_append(new_list, entry);
        }
    }
    g_list_free(all);
    FILE *f = fopen(path, "w");
    if (f) {
        for (GList *l = new_list; l; l = l->next)
            fprintf(f, "%s\n", (gchar *)l->data);
        fclose(f);
    }
    g_list_free_full(new_list, g_free);
    g_free(path);

    GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(btn));
    GtkWidget *event_box = row ? gtk_widget_get_parent(row) : NULL;
    GtkWidget *sep = event_box ? g_object_get_data(G_OBJECT(event_box), "separator") : NULL;
    if (event_box) gtk_widget_destroy(event_box);
    if (sep) gtk_widget_destroy(sep);
}

static void rebuild_downloads_list(DownloadsCtx *ctx) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(ctx->list));
    for (GList *l = children; l; l = l->next) gtk_widget_destroy(l->data);
    g_list_free(children);

    GList *history = load_download_history();
    if (!history) {
        if (ctx->empty_label) gtk_widget_show(ctx->empty_label);
        return;
    }
    if (ctx->empty_label) gtk_widget_hide(ctx->empty_label);

    gint shown = 0;
    gchar *last_date = NULL;
    for (GList *l = history; l && shown < 500; l = l->next) {
        gchar *entry = l->data;
        if (!entry) continue;
        if (ctx->filter && strlen(ctx->filter) > 0) {
            gchar *lower = g_utf8_strdown(entry, -1);
            gchar *lf = g_utf8_strdown(ctx->filter, -1);
            gboolean match = strstr(lower, lf) != NULL;
            g_free(lower); g_free(lf);
            if (!match) continue;
        }
        gchar **parts = g_strsplit(entry, "\t", 4);
        if (!parts[0] || !parts[1]) { g_strfreev(parts); continue; }
        const gchar *ts_str = parts[0];
        const gchar *url = parts[1];
        const gchar *fname = parts[2] ? parts[2] : url;
        const gchar *size_str = parts[3] ? parts[3] : "0";

        gchar *date_str = format_history_date(ts_str);
        if (!last_date || g_strcmp0(date_str, last_date) != 0) {
            GtkWidget *hdr = gtk_label_new(date_str);
            gtk_widget_set_halign(hdr, GTK_ALIGN_START);
            gtk_widget_set_margin_start(hdr, 12);
            gtk_widget_set_margin_top(hdr, 8);
            gtk_widget_set_margin_bottom(hdr, 4);
            GtkStyleContext *hctx = gtk_widget_get_style_context(hdr);
            gtk_style_context_add_class(hctx, "history-date-header");
            gtk_box_pack_start(GTK_BOX(ctx->list), hdr, FALSE, FALSE, 0);
            g_free(last_date);
            last_date = g_strdup(date_str);
        }
        g_free(date_str);

        gchar *uri_dup = g_strdup(url);
        gchar *ts_dup = g_strdup(ts_str);

        GtkWidget *event_box = gtk_event_box_new();
        gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), FALSE);
        g_object_set_data_full(G_OBJECT(event_box), "uri", uri_dup, g_free);
        g_object_set_data_full(G_OBJECT(event_box), "timestamp", ts_dup, g_free);
        g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_dl_history_row_menu), ctx->browser);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(vbox, TRUE);

        GtkWidget *title_lbl = gtk_label_new(fname);
        gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(title_lbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(title_lbl), 60);
        GtkStyleContext *tctx = gtk_widget_get_style_context(title_lbl);
        gtk_style_context_add_class(tctx, "history-item-title");
        gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

        GtkWidget *url_lbl = gtk_label_new(url);
        gtk_widget_set_halign(url_lbl, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(url_lbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(url_lbl), 60);
        GtkStyleContext *uctx = gtk_widget_get_style_context(url_lbl);
        gtk_style_context_add_class(uctx, "history-item-url");
        gtk_box_pack_start(GTK_BOX(vbox), url_lbl, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(row), vbox, TRUE, TRUE, 0);

        guint64 sz = (guint64)atol(size_str);
        gchar *sz_str = sz > 0 ? g_strdup_printf("%lu B", (unsigned long)sz) : g_strdup("");
        GtkWidget *size_lbl = gtk_label_new(sz_str);
        GtkStyleContext *sctx = gtk_widget_get_style_context(size_lbl);
        gtk_style_context_add_class(sctx, "history-item-time");
        gtk_box_pack_start(GTK_BOX(row), size_lbl, FALSE, FALSE, 0);
        g_free(sz_str);

        gchar *time_str = format_history_time_short(ts_str);
        GtkWidget *time_lbl = gtk_label_new(time_str);
        GtkStyleContext *mctx = gtk_widget_get_style_context(time_lbl);
        gtk_style_context_add_class(mctx, "history-item-time");
        gtk_box_pack_start(GTK_BOX(row), time_lbl, FALSE, FALSE, 0);
        g_free(time_str);

        GtkWidget *redl_btn = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_tooltip_text(redl_btn, T(ctx->browser, "重新下载", "Redownload"));
        g_object_set_data_full(G_OBJECT(redl_btn), "uri", g_strdup(uri_dup), g_free);
        g_signal_connect(redl_btn, "clicked", G_CALLBACK(on_dl_history_redownload), ctx->browser);
        GtkStyleContext *rctx = gtk_widget_get_style_context(redl_btn);
        gtk_style_context_add_class(rctx, "history-delete-btn");
        gtk_box_pack_start(GTK_BOX(row), redl_btn, FALSE, FALSE, 0);

        GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_tooltip_text(copy_btn, T(ctx->browser, "复制链接", "Copy Link"));
        g_object_set_data_full(G_OBJECT(copy_btn), "uri", g_strdup(uri_dup), g_free);
        g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_dl_history_copy_link), NULL);
        GtkStyleContext *ccctx = gtk_widget_get_style_context(copy_btn);
        gtk_style_context_add_class(ccctx, "history-delete-btn");
        gtk_box_pack_start(GTK_BOX(row), copy_btn, FALSE, FALSE, 0);

        GtkWidget *del_btn = gtk_button_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        g_object_set_data_full(G_OBJECT(del_btn), "uri", g_strdup(uri_dup), g_free);
        g_object_set_data_full(G_OBJECT(del_btn), "timestamp", g_strdup(ts_dup), g_free);
        g_signal_connect(del_btn, "clicked", G_CALLBACK(on_dl_history_delete), ctx);
        GtkStyleContext *dctx = gtk_widget_get_style_context(del_btn);
        gtk_style_context_add_class(dctx, "history-delete-btn");
        gtk_box_pack_start(GTK_BOX(row), del_btn, FALSE, FALSE, 0);

        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        g_object_set_data(G_OBJECT(event_box), "separator", sep);
        gtk_container_add(GTK_CONTAINER(event_box), row);
        gtk_box_pack_start(GTK_BOX(ctx->list), event_box, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ctx->list), sep, FALSE, FALSE, 0);
        gtk_widget_show_all(event_box);
        shown++;
        g_strfreev(parts);
    }
    g_free(last_date);
    g_list_free_full(history, g_free);
}

static void on_dl_search_changed(GtkEditable *editable, gpointer user_data) {
    DownloadsCtx *ctx = user_data;
    ctx->filter = gtk_entry_get_text(GTK_ENTRY(editable));
    rebuild_downloads_list(ctx);
}

static void on_dl_clear_all_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    DownloadsCtx *ctx = user_data;
    gchar *path = dl_history_path();
    if (path) {
        g_file_set_contents(path, "", -1, NULL);
        g_free(path);
    }
    rebuild_downloads_list(ctx);
}

GtkWidget *zhi_downloads_page_new(ZhiBrowser *browser) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(vbox, "history-page");
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_vexpand(vbox, TRUE);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 12);
    gtk_widget_set_margin_bottom(header, 8);
    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<b>%s</b>", T(browser, "下载记录", "Downloads"));
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    GtkWidget *search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), T(browser, "搜索下载记录...", "Search downloads..."));
    gtk_widget_set_hexpand(search_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(header), search_entry, TRUE, TRUE, 0);

    GtkWidget *clear_btn = gtk_button_new_with_label(T(browser, "清除全部", "Clear All"));
    GtkStyleContext *cctx = gtk_widget_get_style_context(clear_btn);
    gtk_style_context_add_class(cctx, "flat");
    gtk_box_pack_start(GTK_BOX(header), clear_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(list, TRUE);

    GtkWidget *empty = gtk_label_new(T(browser, "暂无下载记录", "No downloads yet"));
    gtk_widget_set_margin_top(empty, 40);
    gtk_widget_set_margin_bottom(empty, 40);
    GtkStyleContext *ectx = gtk_widget_get_style_context(empty);
    gtk_style_context_add_class(ectx, "muted");
    gtk_widget_set_no_show_all(empty, TRUE);
    gtk_widget_hide(empty);

    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), empty, TRUE, TRUE, 0);

    DownloadsCtx *ctx = g_new0(DownloadsCtx, 1);
    ctx->browser = browser;
    ctx->list = list;
    ctx->search_entry = search_entry;
    ctx->empty_label = empty;
    ctx->filter = NULL;
    g_object_set_data_full(G_OBJECT(vbox), "ctx", ctx, g_free);

    g_signal_connect(search_entry, "changed", G_CALLBACK(on_dl_search_changed), ctx);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_dl_clear_all_clicked), ctx);

    rebuild_downloads_list(ctx);
    return vbox;
}

static void on_bm_item_clicked(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *url = g_object_get_data(G_OBJECT(btn), "url");
    if (url) zhi_browser_navigate(browser, browser->active_tab, url);
}

static void on_bm_unpin_clicked(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *url = g_object_get_data(G_OBJECT(btn), "url");
    if (url) {
        zhi_browser_remove_pinned_bookmark(browser, url);
        GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(btn));
        if (row) gtk_widget_destroy(row);
    }
}

static void on_bm_add_clicked(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !tab->url || !tab->title) return;
    zhi_browser_add_pinned_bookmark(browser, tab->url, tab->title);
    zhi_browser_update_status(browser, T(browser, "已添加到书签", "Added to bookmarks"));
}

GtkWidget *zhi_bookmarks_page_new(ZhiBrowser *browser) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_vexpand(vbox, TRUE);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 12);
    gtk_widget_set_margin_bottom(header, 12);
    GtkWidget *title = gtk_label_new(NULL);
    gchar *title_markup = g_markup_printf_escaped(
        "<span size=\"x-large\" weight=\"bold\">%s</span>",
        T(browser, "书签管理", "Bookmarks"));
    gtk_label_set_markup(GTK_LABEL(title), title_markup);
    g_free(title_markup);
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
    GtkWidget *add_btn = gtk_button_new_with_label(T(browser, "+ 添加当前页", "+ Add current page"));
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_bm_add_clicked), browser);
    gtk_box_pack_end(GTK_BOX(header), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scrolled), list);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    if (!browser->pinned_bookmarks) {
        GtkWidget *empty = gtk_label_new(T(browser, "还没有固定书签", "No pinned bookmarks yet"));
        gtk_widget_set_margin_top(empty, 40);
        gtk_widget_set_margin_bottom(empty, 40);
        GtkStyleContext *ectx = gtk_widget_get_style_context(empty);
        gtk_style_context_add_class(ectx, "history-item-url");
        gtk_box_pack_start(GTK_BOX(list), empty, FALSE, FALSE, 0);
    } else {
        for (GList *l = browser->pinned_bookmarks; l; l = l->next) {
            const gchar *entry = l->data;
            gchar **parts = g_strsplit(entry, "\x1F", 2);
            const gchar *url = parts[0];
            const gchar *title_text = parts[1] ? parts[1] : url;

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_margin_start(row, 12);
            gtk_widget_set_margin_end(row, 12);
            gtk_widget_set_margin_top(row, 6);
            gtk_widget_set_margin_bottom(row, 6);

            GtkWidget *icon = gtk_image_new_from_icon_name("starred-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
            gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

            GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            GtkWidget *title_label = gtk_label_new(title_text);
            gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(title_label), 0);
            gtk_box_pack_start(GTK_BOX(vbox2), title_label, FALSE, FALSE, 0);
            GtkWidget *url_label = gtk_label_new(url);
            gtk_label_set_ellipsize(GTK_LABEL(url_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(url_label), 0);
            GtkStyleContext *uctx = gtk_widget_get_style_context(url_label);
            gtk_style_context_add_class(uctx, "history-item-url");
            gtk_box_pack_start(GTK_BOX(vbox2), url_label, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(row), vbox2, TRUE, TRUE, 0);

            GtkWidget *open_btn = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
            g_object_set_data_full(G_OBJECT(open_btn), "url", g_strdup(url), g_free);
            g_signal_connect(open_btn, "clicked", G_CALLBACK(on_bm_item_clicked), browser);
            gtk_box_pack_start(GTK_BOX(row), open_btn, FALSE, FALSE, 0);

            GtkWidget *unpin_btn = gtk_button_new_from_icon_name("list-remove-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
            g_object_set_data_full(G_OBJECT(unpin_btn), "url", g_strdup(url), g_free);
            g_signal_connect(unpin_btn, "clicked", G_CALLBACK(on_bm_unpin_clicked), browser);
            gtk_box_pack_start(GTK_BOX(row), unpin_btn, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(list), row, FALSE, FALSE, 0);

            GtkWidget *rowsep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_box_pack_start(GTK_BOX(list), rowsep, FALSE, FALSE, 0);
            g_strfreev(parts);
        }
    }
    return vbox;
}
