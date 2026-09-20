#include "browser.h"
#include <string.h>

typedef struct {
    ZhiBrowser *browser;
    GtkWidget   *root_widget;
    GtkWidget   *notebook;
} SettingsCtx;

static void on_language_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    const gchar *langs[] = {"zh", "en", NULL};
    if (idx >= 0 && langs[idx]) {
        ctx->browser->lang = (idx == 0) ? LANG_ZH : LANG_EN;
        zhi_config_set(ctx->browser->config, "language", langs[idx]);
        zhi_config_save(ctx->browser->config);

        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(ctx->browser->window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
            "%s", T(ctx->browser,
                "语言已切换，需要重启才能生效。",
                "Language changed. Restart required to apply."));
        gtk_window_set_title(GTK_WINDOW(dlg), T(ctx->browser, "重启浏览器", "Restart Browser"));
        gtk_dialog_add_button(GTK_DIALOG(dlg), T(ctx->browser, "稍后重启", "Later"), GTK_RESPONSE_REJECT);
        gtk_dialog_add_button(GTK_DIALOG(dlg), T(ctx->browser, "立即重启", "Restart Now"), GTK_RESPONSE_ACCEPT);
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (resp == GTK_RESPONSE_ACCEPT) {
            gchar *real_path = g_file_read_link("/proc/self/exe", NULL);
            if (real_path) {
                gchar **argv = zhi_get_saved_argv();
                gint argc = zhi_get_saved_argc();
                gchar **new_argv = g_new(gchar*, argc + 1);
                new_argv[0] = real_path;
                for (int i = 1; i < argc; i++)
                    new_argv[i] = argv ? g_strdup(argv[i]) : NULL;
                new_argv[argc] = NULL;
                GError *spawn_err = NULL;
                g_spawn_async(NULL, new_argv, NULL,
                    G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_SEARCH_PATH,
                    NULL, NULL, NULL, &spawn_err);
                if (spawn_err) {
                    g_warning("Failed to restart: %s", spawn_err->message);
                    g_error_free(spawn_err);
                }
                for (int i = 0; i < argc; i++) g_free(new_argv[i]);
                g_free(new_argv);
                g_free(real_path);
            }
            gtk_main_quit();
        }
    }
}

static void on_homepage_changed(GtkEntry *entry, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    const gchar *text = gtk_entry_get_text(entry);
    zhi_config_set(ctx->browser->config, "homepage", text);
    zhi_config_save_debounced(ctx->browser->config);
    zhi_browser_update_status(ctx->browser, T(ctx->browser, "主页已更新", "Homepage updated"));
}

static void on_search_engine_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    const gchar *engines[] = {
        "https://www.bing.com/search?q=",
        "https://www.google.com/search?q=",
        "https://duckduckgo.com/?q=",
        "https://search.yahoo.com/search?p=",
        "https://www.baidu.com/s?wd=",
        NULL
    };
    if (idx >= 0 && engines[idx]) {
        zhi_config_set(ctx->browser->config, "search_engine", engines[idx]);
        zhi_config_save_debounced(ctx->browser->config);
        zhi_browser_update_status(ctx->browser, T(ctx->browser, "搜索引擎已更新", "Search engine updated"));
    }
}

static void on_session_restore_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    zhi_config_set_bool(ctx->browser->config, "restore_session", gtk_switch_get_active(sw));
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_show_bookmarks_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    zhi_config_set_bool(ctx->browser->config, "show_bookmarks_bar", gtk_switch_get_active(sw));
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_show_statusbar_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    gboolean active = gtk_switch_get_active(sw);
    zhi_config_set_bool(ctx->browser->config, "show_status_bar", active);
    zhi_config_save_debounced(ctx->browser->config);
    if (ctx->browser->status_bar)
        gtk_widget_set_visible(ctx->browser->status_bar, active);
}

static void on_theme_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(ctx->browser->window));
    zhi_theme_set_theme(ctx->browser->theme,
        idx == 0 ? ZHI_THEME_DARK : ZHI_THEME_LIGHT, screen);
    zhi_config_save_debounced(ctx->browser->config);
    zhi_browser_update_status(ctx->browser, idx == 0 ?
        T(ctx->browser, "已应用深色主题", "Dark theme applied") :
        T(ctx->browser, "已应用浅色主题", "Light theme applied"));
}

static void on_density_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(ctx->browser->window));
    zhi_theme_set_density(ctx->browser->theme,
        idx == 0 ? ZHI_DENSITY_STANDARD : ZHI_DENSITY_COMPACT, screen);
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_font_size_changed(GtkSpinButton *spin, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint val = (gint)gtk_spin_button_get_value(spin);
    zhi_theme_set_font_size(ctx->browser->theme, val);
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(ctx->browser->window));
    zhi_theme_apply(ctx->browser->theme, screen);
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_accent_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    const gchar *colors[] = {
        "#2dd4a8", "#4fc3f7", "#7c4dff", "#ff7043",
        "#66bb6a", "#ffa726", "#ef5350", "#ab47bc",
        NULL
    };
    if (idx >= 0 && colors[idx]) {
        zhi_theme_set_accent(ctx->browser->theme, colors[idx]);
        GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(ctx->browser->window));
        zhi_theme_apply(ctx->browser->theme, screen);
        zhi_config_save_debounced(ctx->browser->config);
    }
}

static void on_tracking_protection_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    const gchar *modes[] = {"standard", "strict", "custom", NULL};
    if (idx >= 0 && modes[idx]) {
        zhi_config_set(ctx->browser->config, "tracking_protection", modes[idx]);
        zhi_config_save_debounced(ctx->browser->config);
    }
}

static void on_accept_cookies_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    const gchar *policies[] = {"all", "first-party", "none", NULL};
    if (idx >= 0 && policies[idx]) {
        zhi_config_set(ctx->browser->config, "cookie_policy", policies[idx]);
        zhi_config_save_debounced(ctx->browser->config);
    }
}

static void on_do_not_track_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    zhi_config_set_bool(ctx->browser->config, "do_not_track", gtk_switch_get_active(sw));
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_ad_block_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    zhi_config_set_bool(ctx->browser->config, "ad_block", gtk_switch_get_active(sw));
    zhi_config_save_debounced(ctx->browser->config);
    zhi_browser_update_status(ctx->browser, gtk_switch_get_active(sw) ?
        T(ctx->browser, "广告拦截已开启", "Ad blocker enabled") :
        T(ctx->browser, "广告拦截已关闭", "Ad blocker disabled"));
}

static void on_clear_data_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    SettingsCtx *ctx = user_data;

    g_list_free_full(ctx->browser->history, g_free);
    ctx->browser->history = NULL;

    WebKitWebContext *wctx = webkit_web_context_get_default();
    WebKitWebsiteDataManager *dm = webkit_web_context_get_website_data_manager(wctx);
    if (dm) {
        webkit_website_data_manager_clear(dm,
            WEBKIT_WEBSITE_DATA_ALL,
            0, NULL, NULL, NULL);
    }

    const gchar *home = g_get_home_dir();
    if (home) {
        gchar *path = g_build_filename(home, ".config/zhistar", "history.txt", NULL);
        g_file_set_contents(path, "", -1, NULL);
        g_free(path);
    }
    zhi_browser_update_status(ctx->browser, T(ctx->browser, "浏览数据已清除", "Browsing data cleared"));
}

static void on_proxy_enabled_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    gboolean active = gtk_switch_get_active(sw);
    zhi_config_set_bool(ctx->browser->config, "proxy_enabled", active);
    zhi_config_save_debounced(ctx->browser->config);
    GtkWidget *proxy_entry = g_object_get_data(G_OBJECT(ctx->root_widget), "proxy-entry");
    if (proxy_entry) gtk_widget_set_visible(proxy_entry, active);
}

static void on_proxy_changed(GtkEntry *entry, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    const gchar *text = gtk_entry_get_text(entry);
    zhi_config_set(ctx->browser->config, "proxy", text);
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_cache_size_changed(GtkSpinButton *spin, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint val = (gint)gtk_spin_button_get_value(spin);
    zhi_config_set_int(ctx->browser->config, "cache_size_mb", val);
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_tab_close_middle_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    zhi_config_set_bool(ctx->browser->config, "close_tab_middle_click", gtk_switch_get_active(sw));
    zhi_config_save_debounced(ctx->browser->config);
}

static void on_new_tab_position_changed(GtkComboBoxText *combo, gpointer user_data) {
    SettingsCtx *ctx = user_data;
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    const gchar *positions[] = {"end", "start", "after-current", NULL};
    if (idx >= 0) {
        zhi_config_set(ctx->browser->config, "new_tab_position", positions[idx]);
        zhi_config_save_debounced(ctx->browser->config);
    }
}

static void on_warn_on_close_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    GtkSwitch *sw = GTK_SWITCH(obj);
    SettingsCtx *ctx = user_data;
    if (!ctx || !ctx->browser) return;
    zhi_config_set_bool(ctx->browser->config, "warn_on_close", gtk_switch_get_active(sw));
    zhi_config_save_debounced(ctx->browser->config);
}

static GtkWidget *build_row(const gchar *label, GtkWidget *widget) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_name(row, "settings-row");
    gtk_widget_set_margin_start(row, 16);
    gtk_widget_set_margin_end(row, 16);
    gtk_widget_set_margin_top(row, 6);
    gtk_widget_set_margin_bottom(row, 6);

    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_size_request(lbl, 200, -1);
    GtkStyleContext *lctx = gtk_widget_get_style_context(lbl);
    gtk_style_context_add_class(lctx, "settings-label");
    gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(row), widget, FALSE, FALSE, 0);

    return row;
}

static GtkWidget *build_section(ZhiBrowser *browser, const gchar *zh, const gchar *en) {
    GtkWidget *lbl = gtk_label_new(T(browser, zh, en));
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl, 16);
    gtk_widget_set_margin_top(lbl, 16);
    gtk_widget_set_margin_bottom(lbl, 8);
    GtkStyleContext *ctx = gtk_widget_get_style_context(lbl);
    gtk_style_context_add_class(ctx, "settings-section");
    return lbl;
}

static GtkWidget *build_switch(gboolean initial, GCallback cb, gpointer data) {
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), initial);
    g_signal_connect(sw, "notify::active", cb, data);
    return sw;
}

static GtkWidget *build_combo(const gchar **items, gint active, GCallback cb, gpointer data) {
    GtkWidget *combo = gtk_combo_box_text_new();
    for (int i = 0; items[i]; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), items[i]);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
    g_signal_connect(combo, "changed", cb, data);
    return combo;
}

GtkWidget *zhi_settings_page_new(ZhiBrowser *browser) {
    SettingsCtx *ctx = g_new0(SettingsCtx, 1);
    ctx->browser = browser;

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(vbox, "settings-page");
    ctx->root_widget = vbox;

    GtkWidget *title = gtk_label_new(T(browser, "设置", "Settings"));
    gtk_widget_set_name(title, "settings-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_start(title, 24);
    gtk_widget_set_margin_top(title, 20);
    gtk_widget_set_margin_bottom(title, 12);
    GtkStyleContext *tctx = gtk_widget_get_style_context(title);
    gtk_style_context_add_class(tctx, "settings-title");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    ctx->notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(ctx->notebook, TRUE);
    gtk_widget_set_vexpand(ctx->notebook, TRUE);
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(ctx->notebook), GTK_POS_LEFT);
    gtk_widget_set_margin_start(ctx->notebook, 12);
    gtk_widget_set_margin_end(ctx->notebook, 12);
    gtk_widget_set_margin_bottom(ctx->notebook, 12);

    GtkWidget *general = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_pack_start(GTK_BOX(general), build_section(browser, "启动", "Startup"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(general),
        build_row(T(browser, "语言 / Language", "Language / 语言"),
            build_combo((const gchar *[]){"中文", "English", NULL},
                browser->lang == LANG_ZH ? 0 : 1,
                G_CALLBACK(on_language_changed), ctx)),
        FALSE, FALSE, 0);

    GtkWidget *home_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(home_entry),
        zhi_config_get(browser->config, "homepage", "about:newtab"));
    gtk_entry_set_placeholder_text(GTK_ENTRY(home_entry), "about:newtab");
    g_signal_connect(home_entry, "activate", G_CALLBACK(on_homepage_changed), ctx);
    gtk_box_pack_start(GTK_BOX(general),
        build_row(T(browser, "主页地址", "Homepage URL"), home_entry), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(general),
        build_row(T(browser, "恢复上次会话", "Restore last session"),
            build_switch(
                zhi_config_get_bool(browser->config, "restore_session", FALSE),
                G_CALLBACK(on_session_restore_toggled), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(general), build_section(browser, "搜索", "Search"), FALSE, FALSE, 0);

    const gchar *se = zhi_config_get(browser->config, "search_engine", "https://www.bing.com/search?q=");
    gint se_idx = zhi_search_engine_index(se);
    gtk_box_pack_start(GTK_BOX(general),
        build_row(T(browser, "默认搜索引擎", "Default search engine"),
            build_combo((const gchar *[]){"Bing", "Google", "DuckDuckGo", "Yahoo", T(browser, "百度", "Baidu"), NULL},
                se_idx, G_CALLBACK(on_search_engine_changed), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(general), build_section(browser, "界面", "Interface"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(general),
        build_row(T(browser, "显示书签栏", "Show bookmarks bar"),
            build_switch(
                zhi_config_get_bool(browser->config, "show_bookmarks_bar", FALSE),
                G_CALLBACK(on_show_bookmarks_toggled), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(general),
        build_row(T(browser, "显示状态栏", "Show status bar"),
            build_switch(
                zhi_config_get_bool(browser->config, "show_status_bar", TRUE),
                G_CALLBACK(on_show_statusbar_toggled), ctx)),
        FALSE, FALSE, 0);

    GtkWidget *sw_general = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw_general), general);
    gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), sw_general,
        gtk_label_new(T(browser, "  常规  ", "  General  ")));

    GtkWidget *appearance = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_pack_start(GTK_BOX(appearance), build_section(browser, "主题", "Theme"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(appearance),
        build_row(T(browser, "配色方案", "Color theme"),
            build_combo((const gchar *[]){T(browser, "深色", "Dark"), T(browser, "浅色", "Light"), NULL},
                browser->theme->theme == ZHI_THEME_DARK ? 0 : 1,
                G_CALLBACK(on_theme_changed), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(appearance),
        build_row(T(browser, "强调色", "Accent color"),
            build_combo((const gchar *[]){
                T(browser, "青绿", "Teal"), T(browser, "蓝色", "Blue"),
                T(browser, "紫色", "Purple"), T(browser, "橙色", "Orange"),
                T(browser, "绿色", "Green"), T(browser, "琥珀", "Amber"),
                T(browser, "红色", "Red"), T(browser, "紫罗兰", "Violet"), NULL},
                zhi_theme_get_accent_index(browser->theme),
                G_CALLBACK(on_accent_changed), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(appearance), build_section(browser, "字体与密度", "Density & Font"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(appearance),
        build_row(T(browser, "界面密度", "UI density"),
            build_combo((const gchar *[]){T(browser, "标准", "Standard"), T(browser, "紧凑", "Compact"), NULL},
                browser->theme->density == ZHI_DENSITY_STANDARD ? 0 : 1,
                G_CALLBACK(on_density_changed), ctx)),
        FALSE, FALSE, 0);

    GtkWidget *font_spin = gtk_spin_button_new_with_range(10, 24, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(font_spin), browser->theme->font_size);
    g_signal_connect(font_spin, "value-changed", G_CALLBACK(on_font_size_changed), ctx);
    gtk_box_pack_start(GTK_BOX(appearance),
        build_row(T(browser, "字体大小 (px)", "Font size (px)"), font_spin), FALSE, FALSE, 0);

    GtkWidget *sw_appearance = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw_appearance), appearance);
    gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), sw_appearance,
        gtk_label_new(T(browser, "  外观  ", "  Appearance  ")));

    GtkWidget *privacy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_pack_start(GTK_BOX(privacy), build_section(browser, "跟踪保护", "Tracking Protection"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(privacy),
        build_row(T(browser, "广告拦截", "Ad Blocker"),
            build_switch(
                zhi_config_get_bool(browser->config, "ad_block", TRUE),
                G_CALLBACK(on_ad_block_toggled), ctx)),
        FALSE, FALSE, 0);

    const gchar *tp = zhi_config_get(browser->config, "tracking_protection", "standard");
    gint tp_idx = (g_strcmp0(tp, "strict") == 0) ? 1 : (g_strcmp0(tp, "custom") == 0) ? 2 : 0;
    gtk_box_pack_start(GTK_BOX(privacy),
        build_row(T(browser, "保护级别", "Protection level"),
            build_combo((const gchar *[]){T(browser, "标准", "Standard"), T(browser, "严格", "Strict"), T(browser, "自定义", "Custom"), NULL},
                tp_idx, G_CALLBACK(on_tracking_protection_changed), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(privacy),
        build_row(T(browser, "请勿跟踪", "Do Not Track"),
            build_switch(
                zhi_config_get_bool(browser->config, "do_not_track", TRUE),
                G_CALLBACK(on_do_not_track_toggled), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(privacy), build_section(browser, "Cookies", "Cookies"), FALSE, FALSE, 0);

    const gchar *cp = zhi_config_get(browser->config, "cookie_policy", "all");
    gint cp_idx = (g_strcmp0(cp, "first-party") == 0) ? 1 :
                  (g_strcmp0(cp, "none") == 0) ? 2 : 0;
    gtk_box_pack_start(GTK_BOX(privacy),
        build_row(T(browser, "Cookie 策略", "Cookie policy"),
            build_combo((const gchar *[]){T(browser, "全部接受", "Accept all"), T(browser, "仅第一方", "First-party only"), T(browser, "全部拒绝", "Reject all"), NULL},
                cp_idx, G_CALLBACK(on_accept_cookies_changed), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(privacy), build_section(browser, "数据", "Data"), FALSE, FALSE, 0);

    GtkWidget *clear_btn = gtk_button_new_with_label(T(browser, "清除浏览数据", "Clear Browsing Data"));
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_data_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(privacy), build_row("", clear_btn), FALSE, FALSE, 0);

    GtkWidget *sw_privacy = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw_privacy), privacy);
    gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), sw_privacy,
        gtk_label_new(T(browser, "  隐私与安全  ", "  Privacy & Security  ")));

    GtkWidget *tabs_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_pack_start(GTK_BOX(tabs_page), build_section(browser, "标签页行为", "Tab Behavior"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(tabs_page),
        build_row(T(browser, "中键关闭标签页", "Middle-click to close"),
            build_switch(
                zhi_config_get_bool(browser->config, "close_tab_middle_click", TRUE),
                G_CALLBACK(on_tab_close_middle_toggled), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(tabs_page),
        build_row(T(browser, "关闭多标签时警告", "Warn when closing multiple tabs"),
            build_switch(
                zhi_config_get_bool(browser->config, "warn_on_close", FALSE),
                G_CALLBACK(on_warn_on_close_toggled), ctx)),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(tabs_page), build_section(browser, "新标签页位置", "New Tab Position"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(tabs_page),
        build_row(T(browser, "新标签页打开在", "Open new tabs at"),
            build_combo((const gchar *[]){T(browser, "末尾", "End"), T(browser, "开头", "Start"), T(browser, "当前标签之后", "After current tab"), NULL},
                0, G_CALLBACK(on_new_tab_position_changed), ctx)),
        FALSE, FALSE, 0);

    GtkWidget *sw_tabs = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw_tabs), tabs_page);
    gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), sw_tabs,
        gtk_label_new(T(browser, "  标签页  ", "  Tabs  ")));

    GtkWidget *network = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_box_pack_start(GTK_BOX(network), build_section(browser, "代理", "Proxy"), FALSE, FALSE, 0);

    gboolean proxy_on = zhi_config_get_bool(browser->config, "proxy_enabled", FALSE);
    gtk_box_pack_start(GTK_BOX(network),
        build_row(T(browser, "启用代理", "Enable proxy"),
            build_switch(proxy_on, G_CALLBACK(on_proxy_enabled_toggled), ctx)),
        FALSE, FALSE, 0);

    GtkWidget *proxy_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(proxy_entry), zhi_config_get(browser->config, "proxy", ""));
    gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_entry), "socks5://127.0.0.1:1080");
    g_signal_connect(proxy_entry, "activate", G_CALLBACK(on_proxy_changed), ctx);
    GtkWidget *proxy_row = build_row(T(browser, "代理地址", "Proxy URL"), proxy_entry);
    gtk_widget_set_visible(proxy_row, proxy_on);
    g_object_set_data(G_OBJECT(vbox), "proxy-entry", proxy_row);
    gtk_box_pack_start(GTK_BOX(network), proxy_row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(network), build_section(browser, "缓存", "Cache"), FALSE, FALSE, 0);

    GtkWidget *cache_spin = gtk_spin_button_new_with_range(50, 2000, 50);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cache_spin), zhi_config_get_int(browser->config, "cache_size_mb", 500));
    g_signal_connect(cache_spin, "value-changed", G_CALLBACK(on_cache_size_changed), ctx);
    gtk_box_pack_start(GTK_BOX(network), build_row(T(browser, "缓存大小 (MB)", "Cache size (MB)"), cache_spin), FALSE, FALSE, 0);

    GtkWidget *sw_network = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw_network), network);
    gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), sw_network,
        gtk_label_new(T(browser, "  网络  ", "  Network  ")));

    GtkWidget *about = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *about_center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(about_center, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(about_center, 24);

    GtkWidget *logo = gtk_image_new();
    gchar *logo_path = find_resource("logo.png");
    if (logo_path) {
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(logo_path, 80, 80, TRUE, NULL);
        if (pixbuf) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(logo), pixbuf);
            g_object_unref(pixbuf);
        }
        g_free(logo_path);
    }
    gtk_box_pack_start(GTK_BOX(about_center), logo, FALSE, FALSE, 0);

    GtkWidget *app_title = gtk_label_new(APP_NAME);
    GtkStyleContext *atctx = gtk_widget_get_style_context(app_title);
    gtk_style_context_add_class(atctx, "about-title");
    gtk_box_pack_start(GTK_BOX(about_center), app_title, FALSE, FALSE, 0);

    gchar *ver_str = g_strdup_printf("v%s (%s)", APP_VERSION, APP_CODENAME);
    GtkWidget *ver_label = gtk_label_new(ver_str);
    g_free(ver_str);
    GtkStyleContext *vctx = gtk_widget_get_style_context(ver_label);
    gtk_style_context_add_class(vctx, "about-version");
    gtk_box_pack_start(GTK_BOX(about_center), ver_label, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 12);
    gtk_widget_set_margin_bottom(sep, 12);
    gtk_box_pack_start(GTK_BOX(about_center), sep, FALSE, FALSE, 0);

    struct { const char *zh; const char *en; } kernel_lines[] = {
        {"Nova-Engine 渲染引擎",       "Nova-Engine rendering engine"},
        {"Nova-JS 脚本引擎",           "Nova-JS scripting engine"},
        {"Nova-Render 绘图引擎",       "Nova-Render graphics engine"},
        {"ZTP 隐私引擎 | 容器隔离",    "ZTP Privacy Engine | Container Isolation"},
        {"WebExtension MV2/MV3 兼容",  "WebExtension MV2/MV3 compatible"},
        {"基于 WebKitGTK 深度定制 · 自研 UI 与交互引擎",  "Deeply customized on WebKitGTK · Self-developed UI Engine"},
        {"纯 C11 内核 | GTK3 界面",   "Pure C11 core | GTK3 UI"},
        {NULL, NULL}
    };
    for (int i = 0; kernel_lines[i].zh; i++) {
        const gchar *text = browser->lang == LANG_ZH ? kernel_lines[i].zh : kernel_lines[i].en;
        GtkWidget *lbl = gtk_label_new(text);
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        GtkStyleContext *lctx = gtk_widget_get_style_context(lbl);
        gtk_style_context_add_class(lctx, "settings-label");
        gtk_box_pack_start(GTK_BOX(about_center), lbl, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(about), about_center, FALSE, FALSE, 0);

    GtkWidget *sw_about = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw_about), about);
    gtk_notebook_append_page(GTK_NOTEBOOK(ctx->notebook), sw_about,
        gtk_label_new(T(browser, "  关于  ", "  About  ")));

    gtk_box_pack_start(GTK_BOX(vbox), ctx->notebook, TRUE, TRUE, 0);

    g_object_set_data_full(G_OBJECT(vbox), "settings-ctx", ctx, g_free);

    return vbox;
}
