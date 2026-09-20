#include "browser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <libsoup/soup-message-headers.h>

const gchar *zhi_i18n(ZhiBrowser *browser, const gchar *zh, const gchar *en) {
    if (!browser) return zh;
    return browser->lang == LANG_ZH ? zh : en;
}

static void on_back_clicked(GtkButton *btn, gpointer user_data);
static void on_fwd_clicked(GtkButton *btn, gpointer user_data);
static void on_reload_clicked(GtkButton *btn, gpointer user_data);
static void on_url_activate(GtkEntry *entry, gpointer user_data);
static void on_url_focus_in(GtkEntry *entry, GdkEventFocus *e, gpointer user_data);
static void on_url_focus_out(GtkEntry *entry, GdkEventFocus *e, gpointer user_data);
static void on_menu_clicked(GtkButton *btn, gpointer user_data);
static void on_new_tab_clicked(GtkButton *btn, gpointer user_data);
static void on_home_clicked(GtkButton *btn, gpointer user_data);
static gboolean on_window_delete(GtkWidget *w, GdkEvent *e, gpointer user_data);
static void update_tab_bar(ZhiBrowser *browser);
static void on_dl_clear_all(GtkButton *btn, gpointer user_data);
static gboolean dl_anim_tick(gpointer user_data);
static gboolean on_web_view_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GtkWidget *create_nav_bar(ZhiBrowser *browser, ZhiTab *tab);
static void load_url_in_tab(ZhiBrowser *browser, ZhiTab *tab, const gchar *url);
static void on_view_source_ready(GObject *source, GAsyncResult *result, gpointer user_data);
static void on_web_view_load_failed(WebKitWebView *web_view, GError *error, gpointer user_data);
static gboolean on_run_file_chooser(WebKitWebView *web_view, WebKitFileChooserRequest *request, gpointer user_data);
static gboolean on_web_view_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision,
    WebKitPolicyDecisionType type, gpointer user_data);
static void on_web_view_mouse_target(WebKitWebView *web_view, WebKitHitTestResult *hit, guint modifiers, gpointer user_data);
static void on_web_view_drag_data_received(GtkWidget *widget, GdkDragContext *ctx, gint x, gint y,
    GtkSelectionData *data, guint info, guint time, gpointer user_data);

static void on_load_progress_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static gboolean on_history_flush_timer(gpointer user_data);
static void on_tab_ctx_close_others(GtkMenuItem *item, gpointer user_data);
static void on_tab_ctx_close_left(GtkMenuItem *item, gpointer user_data);
static void on_tab_ctx_close_right(GtkMenuItem *item, gpointer user_data);

static gchar *dl_format_eta(guint64 remaining, gdouble speed, ZhiBrowser *browser) {
    return zhi_format_eta(remaining, speed,
        browser && browser->lang == LANG_ZH ? "剩余 " : "Remaining ",
        browser && browser->lang == LANG_ZH ? "剩余 " : "Remaining ");
}

gchar *find_resource(const gchar *filename) {
    gchar *self = g_file_read_link("/proc/self/exe", NULL);
    if (self) {
        gchar *dir = g_path_get_dirname(self);
        gchar *candidate = g_build_filename(dir, filename, NULL);
        if (g_file_test(candidate, G_FILE_TEST_EXISTS)) { g_free(self); return candidate; }
        g_free(candidate);
        gchar *up = g_path_get_dirname(dir);
        g_free(dir);
        candidate = g_build_filename(up, filename, NULL);
        g_free(up);
        g_free(self);
        if (g_file_test(candidate, G_FILE_TEST_EXISTS)) return candidate;
        g_free(candidate);
    }
    gchar *user_data = g_build_filename(g_get_user_data_dir(), "zhistar", filename, NULL);
    if (g_file_test(user_data, G_FILE_TEST_EXISTS)) return user_data;
    g_free(user_data);
    const gchar * const *sys_dirs = g_get_system_data_dirs();
    for (const gchar * const *d = sys_dirs; d && *d; d++) {
        gchar *sys_path = g_build_filename(*d, "zhistar", filename, NULL);
        if (g_file_test(sys_path, G_FILE_TEST_EXISTS)) return sys_path;
        g_free(sys_path);
    }
    if (g_file_test(filename, G_FILE_TEST_EXISTS))
        return g_strdup(filename);
    return NULL;
}

static gboolean anim_flash_restore(gpointer data) {
    GtkWidget *w = data;
    if (GTK_IS_WIDGET(w)) gtk_widget_set_opacity(w, 1.0);
    return G_SOURCE_REMOVE;
}

static void anim_flash(GtkWidget *w) {
    gtk_widget_set_opacity(w, 0.3);
    g_timeout_add(ZHI_TAB_CLOSE_ANIM_MS, anim_flash_restore, w);
}

static void show_load_status(ZhiBrowser *browser, const gchar *fmt, const gchar *host) {
    if (!browser->status_bar) return;
    if (browser->status_hover) return;
    GtkLabel *label = g_object_get_data(G_OBJECT(browser->status_bar), "label");
    if (!label) return;
    if (host) {
        gchar *msg = g_strdup_printf(fmt, host);
        gtk_label_set_text(label, msg);
        g_free(msg);
    } else {
        gtk_label_set_text(label, fmt);
    }
}

static gchar *extract_host(const gchar *url) {
    if (!url) return NULL;
    const gchar *p = url;

    if (g_str_has_prefix(p, "https://")) p += 8;
    else if (g_str_has_prefix(p, "http://")) p += 7;
    else if (g_str_has_prefix(p, "ftp://")) p += 6;
    else return NULL;

    const gchar *atsign = strchr(p, '@');
    if (atsign && atsign < p + 200) {
        const gchar *slash = strchr(p, '/');
        if (!slash || atsign < slash) p = atsign + 1;
    }

    if (*p == '[') {
        const gchar *bracket = strchr(p, ']');
        if (bracket) {
            return g_strndup(p + 1, bracket - p - 1);
        }
    }
    const gchar *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;

    const gchar *colon = NULL;
    for (const gchar *s = p; s < end; s++) {
        if (*s == ':') colon = s;
    }
    if (colon && colon > p) {
        return g_strndup(p, colon - p);
    }
    return g_strndup(p, end - p);
}

static void style_tab_button(GtkWidget *btn, gboolean active) {
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_remove_class(ctx, "tab");
    gtk_style_context_add_class(ctx, "tab");
    if (active) gtk_style_context_add_class(ctx, "active");
    else gtk_style_context_remove_class(ctx, "active");
}

static void update_nav_sensitivity(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab) return;
    gboolean can_back = (tab->nav_current && tab->nav_current->prev);
    gboolean can_fwd = (tab->nav_current && tab->nav_current->next);

    if (!can_back && !can_fwd && WEBKIT_IS_WEB_VIEW(tab->web_view)) {
        can_back = webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(tab->web_view));
        can_fwd = webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(tab->web_view));
    }
    gtk_widget_set_sensitive(tab->back_btn, can_back);
    gtk_widget_set_sensitive(tab->fwd_btn, can_fwd);
}

static void switch_tab_by_btn(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = g_object_get_data(G_OBJECT(btn), "tab");
    if (tab) zhi_browser_switch_tab(browser, tab);
}

static void on_tab_ctx_pin_tab(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = g_object_get_data(G_OBJECT(item), "tab");
    if (!tab) return;
    if (tab->is_pinned)
        zhi_browser_unpin_tab(browser, tab);
    else
        zhi_browser_pin_tab(browser, tab);
}

static void on_tab_ctx_pin_bookmark(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = g_object_get_data(G_OBJECT(item), "tab");
    if (!tab || !tab->url) return;
    zhi_browser_add_pinned_bookmark(browser, tab->url, tab->title);
    zhi_browser_update_status(browser, T(browser, "已固定到书签栏", "Pinned to bookmarks bar"));
}

static void on_tab_ctx_close(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = g_object_get_data(G_OBJECT(item), "tab");
    if (tab) zhi_browser_close_tab(browser, tab);
}

static gboolean on_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 2) {
        if (!zhi_config_get_bool(browser->config, "close_tab_middle_click", TRUE))
            return FALSE;
        ZhiTab *tab = g_object_get_data(G_OBJECT(widget), "tab");
        if (tab) zhi_browser_close_tab(browser, tab);
        return TRUE;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        ZhiTab *tab = g_object_get_data(G_OBJECT(widget), "tab");
        if (!tab) return FALSE;
        GtkWidget *menu = gtk_menu_new();
        const gchar *pin_label = tab->is_pinned ?
            T(browser, "取消固定标签页", "Unpin Tab") :
            T(browser, "固定标签页", "Pin Tab");
        GtkWidget *pin_item = gtk_menu_item_new_with_label(pin_label);
        g_object_set_data(G_OBJECT(pin_item), "tab", tab);
        g_signal_connect(pin_item, "activate", G_CALLBACK(on_tab_ctx_pin_tab), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), pin_item);
        GtkWidget *bookmark_item = gtk_menu_item_new_with_label(
            T(browser, "固定到书签", "Pin to Bookmarks"));
        g_object_set_data(G_OBJECT(bookmark_item), "tab", tab);
        g_signal_connect(bookmark_item, "activate", G_CALLBACK(on_tab_ctx_pin_bookmark), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), bookmark_item);
        GtkWidget *sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
        GtkWidget *close_others_item = gtk_menu_item_new_with_label(
            T(browser, "关闭其他标签页", "Close Other Tabs"));
        g_object_set_data(G_OBJECT(close_others_item), "tab", tab);
        g_signal_connect(close_others_item, "activate", G_CALLBACK(on_tab_ctx_close_others), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), close_others_item);
        GtkWidget *close_left_item = gtk_menu_item_new_with_label(
            T(browser, "关闭左侧标签页", "Close Tabs to the Left"));
        g_object_set_data(G_OBJECT(close_left_item), "tab", tab);
        g_signal_connect(close_left_item, "activate", G_CALLBACK(on_tab_ctx_close_left), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), close_left_item);
        GtkWidget *close_right_item = gtk_menu_item_new_with_label(
            T(browser, "关闭右侧标签页", "Close Tabs to the Right"));
        g_object_set_data(G_OBJECT(close_right_item), "tab", tab);
        g_signal_connect(close_right_item, "activate", G_CALLBACK(on_tab_ctx_close_right), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), close_right_item);
        GtkWidget *sep2 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep2);
        GtkWidget *close_item = gtk_menu_item_new_with_label(
            T(browser, "关闭标签页", "Close Tab"));
        g_object_set_data(G_OBJECT(close_item), "tab", tab);
        g_signal_connect(close_item, "activate", G_CALLBACK(on_tab_ctx_close), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), close_item);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

static gint tab_bar_find_drop_index(GtkWidget *tab_bar, gint x) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(tab_bar));
    gint idx = 0;
    for (GList *l = children; l; l = l->next) {
        GtkWidget *child = l->data;
        GtkStyleContext *sctx = gtk_widget_get_style_context(child);
        if (gtk_style_context_has_class(sctx, "tab-drag-gap")) continue;
        ZhiTab *t = g_object_get_data(G_OBJECT(child), "tab");
        if (!t) {
            GList *inner = gtk_container_get_children(GTK_CONTAINER(child));
            for (GList *k = inner; k; k = k->next) {
                t = g_object_get_data(G_OBJECT(k->data), "tab");
                if (t) break;
            }
            g_list_free(inner);
        }
        if (!t) continue;
        GtkAllocation alloc;
        gtk_widget_get_allocation(child, &alloc);
        if (x < alloc.x + alloc.width / 2) { g_list_free(children); return idx; }
        idx++;
    }
    g_list_free(children);
    return idx;
}

static void on_tab_drag_begin(GtkWidget *widget, GdkDragContext *ctx, gpointer user_data) {
    (void)ctx;
    ZhiBrowser *browser = user_data;
    if (g_list_length(browser->tabs) < 2) return;
    ZhiTab *tab = g_object_get_data(G_OBJECT(widget), "tab");
    if (!tab) return;
    if (browser->drag_gap) {
        gtk_widget_destroy(browser->drag_gap);
        browser->drag_gap = NULL;
    }
    browser->drag_tab = tab;
    browser->drag_idx = -1;
    browser->drag_gap = NULL;
    GtkStyleContext *sctx = gtk_widget_get_style_context(widget);
    gtk_style_context_add_class(sctx, "tab-dragging");
}

static gboolean on_tab_bar_drag_motion(GtkWidget *widget, GdkDragContext *ctx, gint x, gint y, guint time, gpointer user_data) {
    (void)ctx; (void)y; (void)time;
    ZhiBrowser *browser = user_data;
    if (!browser->drag_tab) return FALSE;

    gint drop_idx = tab_bar_find_drop_index(widget, x);

    if (drop_idx != browser->drag_idx) {
        if (browser->drag_gap) {
            gtk_widget_destroy(browser->drag_gap);
            browser->drag_gap = NULL;
        }
        browser->drag_idx = drop_idx;
        browser->drag_gap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkStyleContext *gctx = gtk_widget_get_style_context(browser->drag_gap);
        gtk_style_context_add_class(gctx, "tab-drag-gap");
        gtk_widget_set_size_request(browser->drag_gap, 4, -1);
        gtk_widget_show(browser->drag_gap);
        gtk_box_pack_start(GTK_BOX(widget), browser->drag_gap, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(widget), browser->drag_gap, drop_idx);
    }
    gdk_drag_status(ctx, GDK_ACTION_MOVE, time);
    return TRUE;
}

static void on_tab_bar_drag_leave(GtkWidget *widget, GdkDragContext *ctx, guint time, gpointer user_data) {
    (void)widget; (void)ctx; (void)time;
    ZhiBrowser *browser = user_data;
    if (browser->drag_gap) {
        gtk_widget_destroy(browser->drag_gap);
        browser->drag_gap = NULL;
    }
}

static void on_tab_drag_end(GtkWidget *widget, GdkDragContext *ctx, gpointer user_data) {
    (void)ctx;
    ZhiBrowser *browser = user_data;
    GtkStyleContext *sctx = gtk_widget_get_style_context(widget);
    gtk_style_context_remove_class(sctx, "tab-dragging");

    if (browser->drag_tab && browser->drag_idx >= 0) {
        gint src_idx = g_list_index(browser->tabs, browser->drag_tab);
        gint dst_idx = browser->drag_idx;
        ZhiTab *dragged = browser->drag_tab;
        if (dragged->is_pinned) {
            GList *l = g_list_nth(browser->tabs, dst_idx);
            if (l && l->data && !((ZhiTab *)l->data)->is_pinned) {
                for (l = l->prev; l; l = l->prev) {
                    if (((ZhiTab *)l->data)->is_pinned) { dst_idx = g_list_position(browser->tabs, l) + 1; break; }
                }
                if (!l) dst_idx = 0;
            }
        } else {
            GList *l = g_list_nth(browser->tabs, dst_idx);
            if (l && l->data && ((ZhiTab *)l->data)->is_pinned) {
                for (l = l->next; l; l = l->next) {
                    if (!((ZhiTab *)l->data)->is_pinned) { dst_idx = g_list_position(browser->tabs, l); break; }
                }
                if (!l) dst_idx = g_list_length(browser->tabs);
            }
        }
        if (src_idx >= 0 && src_idx != dst_idx) {
            ZhiTab *tab = browser->drag_tab;
            browser->tabs = g_list_remove(browser->tabs, tab);
            if (dst_idx > src_idx) dst_idx--;
            browser->tabs = g_list_insert(browser->tabs, tab, dst_idx);
            gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), tab->page);
            if (page_num >= 0) {
                g_object_ref(tab->page);
                gtk_notebook_reorder_child(GTK_NOTEBOOK(browser->notebook), tab->page, dst_idx);
                g_object_unref(tab->page);
            }
            browser->active_tab_index = g_list_index(browser->tabs, browser->active_tab);
        }
    }

    if (browser->drag_gap) {
        gtk_widget_destroy(browser->drag_gap);
        browser->drag_gap = NULL;
    }
    browser->drag_tab = NULL;
    browser->drag_idx = -1;
    update_tab_bar(browser);
}

static gboolean on_tab_close_btn_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)event;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = g_object_get_data(G_OBJECT(widget), "tab");
    if (tab) zhi_browser_close_tab(browser, tab);
    return TRUE;
}

static gboolean do_tab_bar_update(gpointer user_data) {
    ZhiBrowser *browser = user_data;
    browser->tab_bar_update_pending = FALSE;
    update_tab_bar(browser);
    return G_SOURCE_REMOVE;
}

static void schedule_tab_bar_update(ZhiBrowser *browser) {
    if (browser->tab_bar_update_pending) return;
    browser->tab_bar_update_pending = TRUE;
    g_idle_add(do_tab_bar_update, browser);
}

static void update_tab_bar(ZhiBrowser *browser) {

    GList *children = gtk_container_get_children(GTK_CONTAINER(browser->tab_bar));
    for (GList *l = children; l; l = l->next) {
        GtkWidget *btn = l->data;
        ZhiTab *btn_tab = g_object_get_data(G_OBJECT(btn), "tab");
        if (!btn_tab || !g_list_find(browser->tabs, btn_tab)) {
            gtk_widget_destroy(btn);
        }
    }
    g_list_free(children);

    for (GList *l = browser->tabs; l; l = l->next) {
        ZhiTab *tab = l->data;
        if (tab->tab_button && GTK_IS_WIDGET(tab->tab_button)) continue;

        GtkWidget *outer_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        g_object_set_data(G_OBJECT(outer_hbox), "tab", tab);

        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *icon = gtk_image_new_from_icon_name("web-browser-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
        gchar *safe_title = sanitize_utf8(tab->title);
        GtkWidget *label = gtk_label_new(safe_title);
        g_free(safe_title);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label), tab->is_pinned ? 6 : 20);
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        if (tab->is_pinned) {
            GtkStyleContext *lctx = gtk_widget_get_style_context(label);
            gtk_style_context_add_class(lctx, "tab");
        }
        gtk_box_pack_start(GTK_BOX(hbox), label, !tab->is_pinned, FALSE, 0);

        GtkWidget *tab_btn = gtk_button_new();
        gtk_container_add(GTK_CONTAINER(tab_btn), hbox);
        gtk_widget_set_hexpand(tab_btn, FALSE);
        gint btn_width = tab->is_pinned ? 72 : 120;
        if (!tab->is_pinned) btn_width += 24;
        gtk_widget_set_size_request(tab_btn, btn_width, -1);
        g_object_set_data(G_OBJECT(tab_btn), "tab", tab);
        g_signal_connect(tab_btn, "clicked", G_CALLBACK(switch_tab_by_btn), browser);
        g_signal_connect(tab_btn, "button-press-event", G_CALLBACK(on_tab_button_press), browser);
        gtk_box_pack_start(GTK_BOX(outer_hbox), tab_btn, TRUE, TRUE, 0);

        if (!tab->is_pinned) {
            GtkWidget *close_box = gtk_event_box_new();
            GtkStyleContext *cbctx = gtk_widget_get_style_context(close_box);
            gtk_style_context_add_class(cbctx, "tab-close-btn");
            GtkWidget *close_icon = gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
            gtk_widget_set_valign(close_icon, GTK_ALIGN_CENTER);
            gtk_widget_set_halign(close_icon, GTK_ALIGN_CENTER);
            gtk_container_add(GTK_CONTAINER(close_box), close_icon);
            gtk_widget_set_size_request(close_box, 20, 20);
            g_object_set_data(G_OBJECT(close_box), "tab", tab);
            g_signal_connect(close_box, "button-release-event", G_CALLBACK(on_tab_close_btn_clicked), browser);
            gtk_box_pack_start(GTK_BOX(outer_hbox), close_box, FALSE, FALSE, 0);
        }

        gtk_drag_source_set(tab_btn, GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_MOVE);
        g_signal_connect(tab_btn, "drag-begin", G_CALLBACK(on_tab_drag_begin), browser);
        g_signal_connect(tab_btn, "drag-end", G_CALLBACK(on_tab_drag_end), browser);

        tab->tab_button = outer_hbox;
        gtk_box_pack_start(GTK_BOX(browser->tab_bar), outer_hbox, FALSE, FALSE, 0);
    }

    gint idx = 0;
    for (GList *l = browser->tabs; l; l = l->next) {
        ZhiTab *tab = l->data;
        if (!tab->tab_button || !GTK_IS_WIDGET(tab->tab_button)) continue;

        GtkStyleContext *sctx = gtk_widget_get_style_context(tab->tab_button);
        if (gtk_style_context_has_class(sctx, "tab-drag-gap")) continue;
        gtk_box_reorder_child(GTK_BOX(browser->tab_bar), tab->tab_button, idx);
        idx++;
    }

    children = gtk_container_get_children(GTK_CONTAINER(browser->tab_bar));
    for (GList *l = children; l; l = l->next) {
        GtkWidget *outer = l->data;
        ZhiTab *btn_tab = g_object_get_data(G_OBJECT(outer), "tab");
        if (!btn_tab) {
            GList *inner_list = gtk_container_get_children(GTK_CONTAINER(outer));
            for (GList *k = inner_list; k; k = k->next) {
                if (GTK_IS_BUTTON(k->data)) {
                    btn_tab = g_object_get_data(G_OBJECT(k->data), "tab");
                    if (btn_tab) break;
                }
            }
            g_list_free(inner_list);
        }
        if (!btn_tab) continue;

        GtkWidget *btn = NULL;
        GList *inner_list = gtk_container_get_children(GTK_CONTAINER(outer));
        for (GList *k = inner_list; k; k = k->next) {
            if (GTK_IS_BUTTON(k->data)) { btn = k->data; break; }
        }
        g_list_free(inner_list);
        if (!btn) btn = outer;

        style_tab_button(btn, btn_tab == browser->active_tab);

        const gchar *last_title = g_object_get_data(G_OBJECT(btn), "last-title");
        if (last_title && g_strcmp0(last_title, btn_tab->title) == 0) {
        gtk_widget_show(outer);
        gtk_widget_show_all(btn);
            continue;
        }
        g_object_set_data_full(G_OBJECT(btn), "last-title", g_strdup(btn_tab->title), g_free);

        GtkBox *inner = GTK_BOX(gtk_bin_get_child(GTK_BIN(btn)));
        if (inner) {
            GList *inner_children = gtk_container_get_children(GTK_CONTAINER(inner));
            for (GList *k = inner_children; k; k = k->next) {
                if (GTK_IS_LABEL(k->data)) {
                    gchar *safe = sanitize_utf8(btn_tab->title);
                    gtk_label_set_text(GTK_LABEL(k->data), safe);
                    g_free(safe);
                    break;
                }
            }
            g_list_free(inner_children);
        }
        gtk_widget_show_all(outer);
    }
    g_list_free(children);
}

#define ZHI_MAX_CONCURRENT_DOWNLOADS 20

static gboolean dl_is_download_url(const gchar *uri) {
    if (!uri) return FALSE;
    gchar *clean = g_strdup(uri);
    gchar *q = strchr(clean, '?');
    if (q) *q = '\0';
    gchar *h = strchr(clean, '#');
    if (h) *h = '\0';
    gchar *lower = g_utf8_strdown(clean, -1);
    g_free(clean);
    const gchar *exts[] = {
        ".exe", ".msi", ".deb", ".rpm", ".apk", ".dmg", ".pkg",
        ".zip", ".tar.gz", ".tgz", ".tar.bz2", ".tar.xz",
        ".rar", ".7z", ".cab",
        ".iso", ".img", ".bin",
        ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".pdf",
        ".mp3", ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv",
        ".wav", ".flac", ".aac", ".ogg",
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg", ".webp",
        ".ipa",
        NULL
    };
    for (const gchar **e = exts; *e; e++) {
        if (g_str_has_suffix(lower, *e)) {
            g_free(lower);
            return TRUE;
        }
    }
    g_free(lower);
    return FALSE;
}

typedef struct {
    ZhiBrowser *browser;
    ZhiTab     *tab;
} DownloadRetryData;

static gboolean on_download_retry_timer(gpointer user_data) {
    DownloadRetryData *data = user_data;
    if (!data || !data->tab || !data->browser) { g_free(data); return G_SOURCE_REMOVE; }
    if (data->tab->download_retry_pending) {
        data->tab->download_retry_pending = FALSE;
        data->tab->download_retry_timer_id = 0;
        if (WEBKIT_IS_WEB_VIEW(data->tab->web_view)) {
            webkit_web_view_reload(WEBKIT_WEB_VIEW(data->tab->web_view));
        }
    }
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void on_web_view_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    if (browser->destroying || !browser->active_tab) return;
    ZhiTab *tab = g_object_get_data(G_OBJECT(web_view), "zhi-tab");
    if (!tab || tab != browser->active_tab) return;
    switch (event) {
    case WEBKIT_LOAD_STARTED: {
        tab->navigating = FALSE;
        tab->custom_title = FALSE;
        if (tab->download_retry_timer_id) {
            g_source_remove(tab->download_retry_timer_id);
            tab->download_retry_timer_id = 0;
        }
        tab->download_retry_pending = FALSE;

        g_list_free_full(tab->connected_domains, g_free);
        tab->connected_domains = NULL;
        const gchar *uri = webkit_web_view_get_uri(web_view);
        if (uri) {
            g_free(tab->url);
            tab->url = g_strdup(uri);
            if (tab->url_entry && !tab->is_home)
                gtk_entry_set_text(GTK_ENTRY(tab->url_entry), uri);
            gchar *host = extract_host(uri);
            if (host) {
                show_load_status(browser, T(browser, "正在解析主机 %s ...", "Resolving host %s ..."), host);
                g_free(host);
            } else {
                show_load_status(browser, T(browser, "正在加载...", "Loading..."), NULL);
            }
            if (tab->ssl_icon) {
                gboolean ssl = g_str_has_prefix(uri, "https://");
                gtk_image_set_from_icon_name(GTK_IMAGE(tab->ssl_icon),
                    ssl ? "channel-secure-symbolic" : "channel-insecure-symbolic",
                    GTK_ICON_SIZE_SMALL_TOOLBAR);
                gtk_widget_set_tooltip_text(tab->ssl_icon,
                    ssl ? T(browser, "安全连接", "Secure connection") :
                          T(browser, "不安全连接", "Insecure connection"));
            }
        }
        if (tab->loading_bar) {
            gtk_widget_show(tab->loading_bar);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(tab->loading_bar), 0.0);
        }
        zhi_browser_set_tab_loading(browser, tab, TRUE);
        update_nav_sensitivity(browser);
        break;
    }
    case WEBKIT_LOAD_REDIRECTED: {
        const gchar *uri = webkit_web_view_get_uri(web_view);
        if (uri) {
            g_free(tab->url);
            tab->url = g_strdup(uri);
            if (tab->url_entry && !tab->is_home)
                gtk_entry_set_text(GTK_ENTRY(tab->url_entry), uri);
            gchar *host = extract_host(uri);
            if (host) {
                show_load_status(browser, T(browser, "正在跳转到 %s ...", "Redirecting to %s ..."), host);
                g_free(host);
            }
        }
        break;
    }
    case WEBKIT_LOAD_COMMITTED: {
        const gchar *uri = webkit_web_view_get_uri(web_view);
        if (uri) {
            g_free(tab->url);
            tab->url = g_strdup(uri);
            if (tab->url_entry && !tab->is_home)
                gtk_entry_set_text(GTK_ENTRY(tab->url_entry), uri);
            gchar *host = extract_host(uri);
            if (host) {
                show_load_status(browser, T(browser, "正在渲染 %s ...", "Rendering %s ..."), host);

                gboolean found = FALSE;
                for (GList *d = tab->connected_domains; d; d = d->next) {
                    if (g_strcmp0(d->data, host) == 0) { found = TRUE; break; }
                }
                if (!found) {
                    tab->connected_domains = g_list_append(tab->connected_domains, host);
                } else {
                    g_free(host);
                }
            }
        }

        if (uri && !tab->navigating) {
            GList *tail = g_list_last(tab->nav_history);
            if (!tail || g_strcmp0(tail->data, uri) != 0) {

                if (tab->nav_current && tab->nav_current->next) {
                    GList *del = tab->nav_current->next;
                    while (del) {
                        GList *next = del->next;
                        g_free(del->data);
                        del = next;
                    }
                    g_list_free(tab->nav_current->next);
                    tab->nav_current->next = NULL;
                }
                tab->nav_history = g_list_append(tab->nav_history, g_strdup(uri));
                tab->nav_current = g_list_last(tab->nav_history);
            }
        }
        if (!tab->is_home) {
            const gchar *visible = gtk_stack_get_visible_child_name(GTK_STACK(tab->content));
            if (visible && g_strcmp0(visible, "webview") != 0)
                gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "webview");
        }

        if (uri && dl_is_download_url(uri) && !tab->download_retry_pending && tab->download_retry_count < 1) {
            tab->download_retry_pending = TRUE;
            tab->download_retry_count++;
            if (tab->download_retry_timer_id) {
                g_source_remove(tab->download_retry_timer_id);
                tab->download_retry_timer_id = 0;
            }
            DownloadRetryData *data = g_new(DownloadRetryData, 1);
            data->browser = browser;
            data->tab = tab;
            tab->download_retry_timer_id = g_timeout_add(ZHI_DOWNLOAD_RETRY_MS, on_download_retry_timer, data);
        }
        update_nav_sensitivity(browser);
        break;
    }
    case WEBKIT_LOAD_FINISHED:
        if (tab->loading_bar) gtk_widget_hide(tab->loading_bar);
        if (!tab->is_home && tab->url && !g_str_has_prefix(tab->url, "about:")) {
            zhi_browser_add_history(browser, tab->url, tab->title);
        }
        show_load_status(browser, T(browser, "完成", "Done"), NULL);
        update_nav_sensitivity(browser);
        break;
    }
}

static void on_web_view_load_failed(WebKitWebView *web_view, GError *error, gpointer user_data) {
    (void)web_view;
    (void)error;
    ZhiBrowser *browser = user_data;
    if (browser->destroying || !browser->active_tab) return;
    ZhiTab *tab = g_object_get_data(G_OBJECT(web_view), "zhi-tab");
    if (!tab || tab != browser->active_tab) return;
    const gchar *uri = webkit_web_view_get_uri(web_view);
    const gchar *failed_url = uri ? uri : "";
    const gchar *err_msg = error ? error->message : "Unknown error";
    gchar *escaped_url = g_markup_escape_text(failed_url, -1);
    gchar *escaped_err = g_markup_escape_text(err_msg, -1);
    gchar *html = g_strdup_printf(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>无法访问</title>"
        "<style>"
        "body{margin:0;padding:0;display:flex;justify-content:center;align-items:center;"
        "min-height:100vh;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
        "background:#1e1e2e;color:#e0e0e0;}"
        ".container{text-align:center;max-width:500px;padding:40px;}"
        ".icon{font-size:64px;margin-bottom:20px;opacity:0.6;}"
        "h1{font-size:22px;margin:0 0 12px;color:#e0e0e0;font-weight:500;}"
        ".url{color:#666680;font-size:13px;margin:8px 0 20px;word-break:break-all;}"
        ".hint{color:#999;font-size:14px;line-height:1.8;text-align:left;margin:0 auto;}"
        ".hint b{color:#2dd4a8;}"
        ".err{color:#666680;font-size:12px;margin-top:20px;}"
        ".retry{margin-top:24px;padding:10px 28px;background:#2dd4a8;color:#1e1e2e;"
        "border:none;border-radius:8px;font-size:14px;cursor:pointer;font-weight:600;}"
        ".retry:hover{background:#25b892;}"
        "</style></head><body><div class=\"container\">"
        "<div class=\"icon\">&#x1f310;</div>"
        "<h1>无法访问此网站</h1>"
        "<div class=\"url\">%s</div>"
        "<div class=\"hint\">"
        "<b>&#x2022;</b> 请检查您的网络连接是否正常<br>"
        "<b>&#x2022;</b> 请确认网址拼写是否正确<br>"
        "<b>&#x2022;</b> 如果使用 VPN 或代理，请确认其已开启<br>"
        "<b>&#x2022;</b> 该网站可能暂时不可用，请稍后重试"
        "</div>"
        "<div class=\"err\">技术详情：%s</div>"
        "<button class=\"retry\" onclick=\"window.location.reload()\">重新加载</button>"
        "</div></body></html>",
        escaped_url, escaped_err);
    g_free(escaped_url);
    g_free(escaped_err);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(tab->web_view), html, failed_url);
    g_free(html);
    zhi_browser_set_tab_loading(browser, tab, FALSE);
    update_nav_sensitivity(browser);
}

static void on_web_view_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    ZhiBrowser *browser = user_data;
    if (browser->destroying) return;
    ZhiTab *tab = g_object_get_data(G_OBJECT(web_view), "zhi-tab");
    if (!tab) return;
    if (tab->custom_title) return;
    const gchar *title = webkit_web_view_get_title(web_view);
    if (title && strlen(title) > 0) {
        g_free(tab->title);
        tab->title = sanitize_utf8(title);
        if (tab == browser->active_tab)
            schedule_tab_bar_update(browser);
    }
}

static void on_find_next(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(tab->find_entry));
    if (!text || strlen(text) == 0) return;
    WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(tab->web_view));
    webkit_find_controller_search(fc, text,
        WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND, G_MAXUINT);
}

static void on_find_prev(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(tab->find_entry));
    if (!text || strlen(text) == 0) return;
    WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(tab->web_view));
    webkit_find_controller_search(fc, text,
        WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND | WEBKIT_FIND_OPTIONS_BACKWARDS, G_MAXUINT);
}

static void on_find_close(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !tab->find_bar || !GTK_IS_WIDGET(tab->find_bar)) return;
    gtk_widget_hide(tab->find_bar);
    if (WEBKIT_IS_WEB_VIEW(tab->web_view)) {
        WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(tab->web_view));
        webkit_find_controller_search_finish(fc);
        if (browser->find_count_handler_id) {
            g_signal_handler_disconnect(fc, browser->find_count_handler_id);
            browser->find_count_handler_id = 0;
        }
    }
}

static void on_find_count(WebKitFindController *fc, guint count, gpointer user_data) {
    (void)fc;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !tab->find_label) return;
    gchar *msg = g_strdup_printf("%u", count);
    gtk_label_set_text(GTK_LABEL(tab->find_label), msg);
    g_free(msg);
}

static void on_tab_ctx_close_others(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *target = g_object_get_data(G_OBJECT(item), "tab");
    if (!target) return;
    GList *copy = g_list_copy(browser->tabs);
    for (GList *l = copy; l; l = l->next) {
        ZhiTab *t = l->data;
        if (t != target) zhi_browser_close_tab_now(browser, t);
    }
    g_list_free(copy);
}

static void on_tab_ctx_close_left(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *target = g_object_get_data(G_OBJECT(item), "tab");
    if (!target) return;
    GList *copy = g_list_copy(browser->tabs);
    for (GList *l = copy; l; l = l->next) {
        ZhiTab *t = l->data;
        if (t == target) break;
        zhi_browser_close_tab_now(browser, t);
    }
    g_list_free(copy);
}

static void on_tab_ctx_close_right(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *target = g_object_get_data(G_OBJECT(item), "tab");
    if (!target) return;
    gboolean past = FALSE;
    GList *copy = g_list_copy(browser->tabs);
    for (GList *l = copy; l; l = l->next) {
        ZhiTab *t = l->data;
        if (t == target) { past = TRUE; continue; }
        if (past) zhi_browser_close_tab_now(browser, t);
    }
    g_list_free(copy);
}

static void update_dl_button(ZhiBrowser *browser);
static gboolean dl_flash_timeout(gpointer user_data);

static gchar *dl_get_download_dir(void) {

    const gchar *xdg = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (xdg && g_file_test(xdg, G_FILE_TEST_IS_DIR))
        return g_strdup(xdg);

    gchar *dir = g_build_filename(g_get_home_dir(), "Downloads", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static gchar *dl_extract_filename(const gchar *uri) {
    if (!uri) return g_strdup("download");
    gchar *clean = g_strdup(uri);
    gchar *q = strchr(clean, '?');
    if (q) *q = '\0';
    gchar *h = strchr(clean, '#');
    if (h) *h = '\0';
    gchar *slash = strrchr(clean, '/');
    gchar *name = g_strdup(slash ? slash + 1 : clean);
    g_free(clean);
    if (strlen(name) == 0) { g_free(name); name = g_strdup("download"); }
    return name;
}

#define SPEED_WINDOW_SIZE 8

static void dl_speed_init(ZhiDownload *dl) {
    for (gint i = 0; i < SPEED_WINDOW_SIZE; i++) dl->speed_samples[i] = 0;
    dl->speed_sample_idx = 0;
    dl->speed_sample_count = 0;
    dl->last_sample_time = g_get_monotonic_time() / 1000000.0;
    dl->last_sample_bytes = 0;
    dl->speed = 0;
}

static void dl_speed_update(ZhiDownload *dl) {
    gdouble now = g_get_monotonic_time() / 1000000.0;
    gdouble elapsed = now - dl->last_sample_time;
    if (elapsed < 0.3) return;
    gdouble instant_speed = (gdouble)(dl->received - dl->last_sample_bytes) / elapsed;
    dl->speed_samples[dl->speed_sample_idx] = instant_speed;
    dl->speed_sample_idx = (dl->speed_sample_idx + 1) % SPEED_WINDOW_SIZE;
    if (dl->speed_sample_count < SPEED_WINDOW_SIZE) dl->speed_sample_count++;
    dl->last_sample_time = now;
    dl->last_sample_bytes = dl->received;
    gdouble sum = 0, wsum = 0;
    for (gint i = 0; i < dl->speed_sample_count; i++) {
        gdouble w = (i + 1.0);
        sum += dl->speed_samples[i] * w;
        wsum += w;
    }
    dl->speed = wsum > 0 ? sum / wsum : 0;
}

static void dl_update_buttons(ZhiDownload *dl) {
    if (!dl->row_widget) return;
    gboolean show_cancel = FALSE, show_copy = FALSE, show_open = FALSE;
    switch (dl->state) {
    case ZHI_DL_RUNNING:
        show_cancel = TRUE; show_copy = TRUE;
        break;
    case ZHI_DL_FINISHED:
        show_open = TRUE;
        break;
    case ZHI_DL_FAILED:
    case ZHI_DL_CANCELLED:
        show_copy = TRUE;
        break;
    }
    if (dl->cancel_btn) gtk_widget_set_visible(dl->cancel_btn, show_cancel);
    if (dl->copy_btn) gtk_widget_set_visible(dl->copy_btn, show_copy);
    if (dl->open_btn) gtk_widget_set_visible(dl->open_btn, show_open);
}

static void dl_update_row(ZhiDownload *dl) {
    if (!dl->row_widget) return;
    gdouble pct = dl->total_size > 0 ? (gdouble)dl->received / dl->total_size : 0;
    dl->last_update_pct = pct;
    if (dl->progress_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dl->progress_bar), pct);

    gchar *received_str = dl_format_size(dl->received);
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;

    switch (dl->state) {
    case ZHI_DL_RUNNING: {
        gtk_label_set_text(GTK_LABEL(dl->status_label), T(browser, "下载中", "Downloading"));
        if (dl->total_size > 0) {
            guint64 remaining = dl->total_size > dl->received ? dl->total_size - dl->received : 0;
            gchar *total_str = dl_format_size(dl->total_size);
            gchar *speed_str = dl_format_speed(dl->speed);
            gchar *eta_str = dl_format_eta(remaining, dl->speed, browser);
            gchar *s = g_strdup_printf("%s / %s  %s  %s %s", received_str, total_str, speed_str, T(browser, "剩余", "Remaining"), eta_str);
            gtk_label_set_text(GTK_LABEL(dl->size_label), s);
            g_free(s); g_free(total_str); g_free(speed_str); g_free(eta_str);
        } else {
            gchar *speed_str = dl_format_speed(dl->speed);
            gchar *s = g_strdup_printf("%s  %s", received_str, speed_str);
            gtk_label_set_text(GTK_LABEL(dl->size_label), s);
            g_free(s); g_free(speed_str);
        }
        break;
    }
    case ZHI_DL_FINISHED:
        gtk_label_set_text(GTK_LABEL(dl->status_label), T(browser, "下载完成", "Completed"));
        gtk_label_set_text(GTK_LABEL(dl->size_label), received_str);
        break;
    case ZHI_DL_FAILED:
        gtk_label_set_text(GTK_LABEL(dl->status_label), T(browser, "下载失败", "Failed"));
        gtk_label_set_text(GTK_LABEL(dl->size_label), received_str);
        break;
    case ZHI_DL_CANCELLED:
        gtk_label_set_text(GTK_LABEL(dl->status_label), T(browser, "已取消", "Cancelled"));
        gtk_label_set_text(GTK_LABEL(dl->size_label), received_str);
        break;
    }

    g_free(received_str);
    dl_update_buttons(dl);
    update_dl_button(browser);

    if (browser && browser->dl_summary_label) {
        guint64 total_dl_size = 0;
        guint64 total_dl_received = 0;
        gdouble total_speed = 0;
        gint active = 0;
        for (GList *l = browser->downloads; l; l = l->next) {
            ZhiDownload *d = l->data;
            if (d->state == ZHI_DL_RUNNING) {
                active++;
                total_dl_received += d->received;
                if (d->total_size > 0) total_dl_size += d->total_size;
                total_speed += d->speed;
            }
        }
        if (active > 1) {
            gchar *recv_s = dl_format_size(total_dl_received);
            if (total_dl_size > 0) {
                guint64 rem = total_dl_size > total_dl_received ? total_dl_size - total_dl_received : 0;
                gchar *tot_s = dl_format_size(total_dl_size);
                gchar *spd_s = dl_format_speed(total_speed);
                gchar *eta_s = dl_format_eta(rem, total_speed, browser);
                gchar *s = g_strdup_printf("%s %d %s: %s / %s  %s  %s %s", T(browser, "共", "Total"), active, T(browser, "个下载", "downloads"), recv_s, tot_s, spd_s, T(browser, "剩余", "Remaining"), eta_s);
                gtk_label_set_text(GTK_LABEL(browser->dl_summary_label), s);
                g_free(s); g_free(tot_s); g_free(spd_s); g_free(eta_s);
            } else {
                gchar *spd_s = dl_format_speed(total_speed);
                gchar *s = g_strdup_printf("%s %d %s: %s  %s", T(browser, "共", "Total"), active, T(browser, "个下载", "downloads"), recv_s, spd_s);
                gtk_label_set_text(GTK_LABEL(browser->dl_summary_label), s);
                g_free(s); g_free(spd_s);
            }
            g_free(recv_s);
        } else {
            gtk_label_set_text(GTK_LABEL(browser->dl_summary_label), "");
        }
    }
}

static gboolean on_dl_progress_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    ZhiDownload *dl = user_data;
    (void)widget;
    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);
    gdouble cx = w / 2.0, cy = h / 2.0;
    gdouble radius = MIN(w, h) / 2.0 - 4;
    cairo_set_line_width(cr, 3.0);
    cairo_set_source_rgba(cr, 0.4, 0.4, 0.5, 0.3);
    cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
    cairo_stroke(cr);
    gdouble pct = dl->total_size > 0 ? (gdouble)dl->received / dl->total_size : 0;
    if (dl->state == ZHI_DL_FINISHED) {
        cairo_set_source_rgba(cr, 0.18, 0.83, 0.66, 1.0);
        cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
        cairo_stroke(cr);
    } else if (pct > 0 && dl->state == ZHI_DL_RUNNING) {
        cairo_set_source_rgba(cr, 0.18, 0.83, 0.66, 1.0);
        cairo_arc(cr, cx, cy, radius, -G_PI / 2, -G_PI / 2 + 2 * G_PI * pct);
        cairo_stroke(cr);
    }
    return TRUE;
}

static gboolean dl_poll_progress(gpointer user_data) {
    ZhiDownload *dl = user_data;
    if (!dl || dl->state != ZHI_DL_RUNNING) return G_SOURCE_REMOVE;
    if (!dl->wk_download || !WEBKIT_IS_DOWNLOAD(dl->wk_download)) {
        dl->state = ZHI_DL_FAILED;
        dl_update_row(dl);
        return G_SOURCE_REMOVE;
    }
    dl->received = webkit_download_get_received_data_length(dl->wk_download);
    dl_speed_update(dl);
    dl_update_row(dl);
    return G_SOURCE_CONTINUE;
}

static void on_wk_download_progress(WebKitDownload *wk_dl, gdouble progress, gpointer user_data);
static void on_wk_download_finished(WebKitDownload *wk_dl, gpointer user_data);
static void on_wk_download_failed(WebKitDownload *wk_dl, GError *error, gpointer user_data);

static void on_dl_cancel_clicked(GtkButton *btn, gpointer user_data) {
    ZhiDownload *dl = user_data;
    if (!dl) return;
    if (dl->wk_download) {
        dl->user_cancelled = TRUE;
        g_signal_handlers_disconnect_by_data(dl->wk_download, dl);
        webkit_download_cancel(dl->wk_download);
        dl->wk_download = NULL;
    }
    if (dl->poll_timer_id) { g_source_remove(dl->poll_timer_id); dl->poll_timer_id = 0; }
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;

    if (browser) {
        browser->downloads = g_list_remove(browser->downloads, dl);
        browser->active_download_count--;
        update_dl_button(browser);
    }
    if (dl->row_widget) gtk_widget_destroy(dl->row_widget);
    g_free(dl->uri); g_free(dl->filename); g_free(dl->dest_path);
    g_free(dl);
}

static void on_dl_copy_link_clicked(GtkButton *btn, gpointer user_data) {
    ZhiDownload *dl = user_data;
    if (!dl) return;
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, dl->uri, -1);
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;
    if (browser) zhi_browser_update_status(browser, T(browser, "链接已复制", "Link copied"));
}

static void on_dl_open_clicked(GtkButton *btn, gpointer user_data) {
    ZhiDownload *dl = user_data;
    if (!dl || dl->state != ZHI_DL_FINISHED || !dl->dest_path) return;
    gchar *dir = g_path_get_dirname(dl->dest_path);
    const gchar *argv[] = {"xdg-open", dir, NULL};
    GError *err = NULL;
    g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) {
        ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;
        if (browser) zhi_browser_update_status(browser, err->message);
        g_error_free(err);
    }
    g_free(dir);
}

static void on_dl_row_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ZhiDownload *dl = user_data;
    if (dl) {
        if (dl->poll_timer_id) { g_source_remove(dl->poll_timer_id); dl->poll_timer_id = 0; }
        dl->row_widget = NULL;
    }
}

static void on_ctx_redownload(GtkMenuItem *item, gpointer user_data) {
    ZhiDownload *dl = user_data;
    if (!dl) return;
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;
    if (!browser) return;

    const gchar *uri = dl->uri;

    gchar *load_url = g_strdup(uri);
    if (!g_str_has_prefix(uri, "http://") && !g_str_has_prefix(uri, "https://") &&
        !g_str_has_prefix(uri, "about:") && !g_str_has_prefix(uri, "file://")) {
        g_free(load_url);
        load_url = g_strdup_printf("https://%s", uri);
    }
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(browser->active_tab->web_view), load_url);
    g_free(load_url);
}

static void on_ctx_copy_link(GtkMenuItem *item, gpointer user_data) {
    on_dl_copy_link_clicked(NULL, user_data);
}

static void on_ctx_delete_file(GtkMenuItem *item, gpointer user_data) {
    ZhiDownload *dl = user_data;
    if (!dl) return;

    if (dl->wk_download) {
        dl->user_cancelled = TRUE;
        g_signal_handlers_disconnect_by_data(dl->wk_download, dl);
        webkit_download_cancel(dl->wk_download);
        dl->wk_download = NULL;
    }
    if (dl->poll_timer_id) { g_source_remove(dl->poll_timer_id); dl->poll_timer_id = 0; }

    if (dl->dest_path && g_file_test(dl->dest_path, G_FILE_TEST_EXISTS)) {
        remove(dl->dest_path);
    }
    dl->state = ZHI_DL_CANCELLED;
    dl->received = 0;
    dl->total_size = 0;
    dl_update_row(dl);
}

static gboolean on_dl_row_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
    ZhiDownload *dl = user_data;
    if (!dl) {
        dl = g_object_get_data(G_OBJECT(widget), "download");
    }
    if (!dl) return FALSE;
    GtkWidget *menu = gtk_menu_new();
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;
    GtkWidget *redl = gtk_menu_item_new_with_label(T(browser, "重新下载", "Redownload"));
    g_signal_connect(redl, "activate", G_CALLBACK(on_ctx_redownload), dl);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), redl);
    GtkWidget *copy = gtk_menu_item_new_with_label(T(browser, "复制链接", "Copy Link"));
    g_signal_connect(copy, "activate", G_CALLBACK(on_ctx_copy_link), dl);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
    if (dl->dest_path && g_file_test(dl->dest_path, G_FILE_TEST_EXISTS)) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        GtkWidget *del = gtk_menu_item_new_with_label(T(browser, "删除本地文件", "Delete Local File"));
        g_signal_connect(del, "activate", G_CALLBACK(on_ctx_delete_file), dl);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), del);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

static GtkWidget *dl_create_row(ZhiBrowser *browser, ZhiDownload *dl) {

    GtkWidget *event_box = gtk_event_box_new();
    gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), FALSE);
    g_object_set_data(G_OBJECT(event_box), "browser", browser);
    g_object_set_data(G_OBJECT(event_box), "download", dl);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_dl_row_button_press), dl);
    dl->row_widget = event_box;

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(row, 8);
    gtk_widget_set_margin_end(row, 8);
    gtk_widget_set_margin_top(row, 4);
    gtk_widget_set_margin_bottom(row, 4);
    g_signal_connect(event_box, "destroy", G_CALLBACK(on_dl_row_destroy), dl);

    GtkWidget *draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(draw, 32, 32);
    g_signal_connect(draw, "draw", G_CALLBACK(on_dl_progress_draw), dl);
    gtk_box_pack_start(GTK_BOX(row), draw, FALSE, FALSE, 0);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(vbox, TRUE);
    dl->status_label = gtk_label_new(T(browser, "下载中", "Downloading"));
    gtk_widget_set_halign(dl->status_label, GTK_ALIGN_START);
    GtkStyleContext *sctx = gtk_widget_get_style_context(dl->status_label);
    gtk_style_context_add_class(sctx, "history-item-title");
    gtk_box_pack_start(GTK_BOX(vbox), dl->status_label, FALSE, FALSE, 0);
    dl->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_size_request(dl->progress_bar, -1, 4);
    gtk_box_pack_start(GTK_BOX(vbox), dl->progress_bar, FALSE, FALSE, 0);
    dl->size_label = gtk_label_new(T(browser, "等待下载...", "Waiting..."));
    gtk_widget_set_halign(dl->size_label, GTK_ALIGN_START);
    GtkStyleContext *zctx = gtk_widget_get_style_context(dl->size_label);
    gtk_style_context_add_class(zctx, "history-item-time");
    gtk_box_pack_start(GTK_BOX(vbox), dl->size_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), vbox, TRUE, TRUE, 0);

    dl->cancel_btn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(dl->cancel_btn, T(browser, "取消", "Cancel"));
    g_signal_connect(dl->cancel_btn, "clicked", G_CALLBACK(on_dl_cancel_clicked), dl);
    gtk_box_pack_start(GTK_BOX(row), dl->cancel_btn, FALSE, FALSE, 0);

    dl->copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(dl->copy_btn, T(browser, "复制链接", "Copy Link"));
    g_signal_connect(dl->copy_btn, "clicked", G_CALLBACK(on_dl_copy_link_clicked), dl);
    gtk_box_pack_start(GTK_BOX(row), dl->copy_btn, FALSE, FALSE, 0);

    dl->open_btn = gtk_button_new_from_icon_name("folder-open-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(dl->open_btn, T(browser, "打开文件夹", "Open Folder"));
    g_signal_connect(dl->open_btn, "clicked", G_CALLBACK(on_dl_open_clicked), dl);
    gtk_box_pack_start(GTK_BOX(row), dl->open_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(event_box), row);
    gtk_widget_show_all(event_box);
    dl_update_buttons(dl);
    return event_box;
}

static void on_wk_download_progress(WebKitDownload *wk_dl, gdouble progress, gpointer user_data) {
    ZhiDownload *dl = user_data;
    dl->received = webkit_download_get_received_data_length(wk_dl);
    if (progress > 0 && dl->received > 0 && dl->total_size == 0)
        dl->total_size = (guint64)(dl->received / progress);
    dl_speed_update(dl);
    dl_update_row(dl);
}

static void on_wk_download_finished(WebKitDownload *wk_dl, gpointer user_data) {
    ZhiDownload *dl = user_data;
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;
    if (dl->poll_timer_id) { g_source_remove(dl->poll_timer_id); dl->poll_timer_id = 0; }

    if (dl->progress_sig && dl->wk_download) {
        g_signal_handler_disconnect(dl->wk_download, dl->progress_sig);
        dl->progress_sig = 0;
    }
    dl->wk_download = NULL;

    if (dl->user_cancelled) {
        dl->user_cancelled = FALSE;
        return;
    }
    dl->state = ZHI_DL_FINISHED;
    dl->received = webkit_download_get_received_data_length(wk_dl);
    if (dl->dest_path) {
        GFile *file = g_file_new_for_path(dl->dest_path);
        GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (info) {
            guint64 disk_size = g_file_info_get_size(info);
            if (disk_size > 0) dl->received = disk_size;
            g_object_unref(info);
        }
        g_object_unref(file);
    }
    if (dl->received > 0) dl->total_size = dl->received;
    dl->speed = 0;
    dl_update_row(dl);
    zhi_browser_save_download_record(dl->uri, dl->filename, dl->received);
    if (dl->filename) {
        GNotification *n = g_notification_new(APP_NAME);
        gchar *body = g_strdup_printf("%s: %s", T(browser, "下载完成", "Download complete"), dl->filename);
        g_notification_set_body(n, body);
        g_free(body);
        GIcon *icon = g_themed_icon_new("download-symbolic");
        g_notification_set_icon(n, icon);
        g_object_unref(icon);
        g_application_send_notification(g_application_get_default(), "download-complete", n);
        g_object_unref(n);
    }
    if (browser) {
        browser->active_download_count--;
        update_dl_button(browser);
    }
}

static void on_wk_download_failed(WebKitDownload *wk_dl, GError *error, gpointer user_data) {
    ZhiDownload *dl = user_data;
    ZhiBrowser *browser = dl->row_widget ? g_object_get_data(G_OBJECT(dl->row_widget), "browser") : NULL;
    if (dl->poll_timer_id) { g_source_remove(dl->poll_timer_id); dl->poll_timer_id = 0; }
    dl->wk_download = NULL;
    if (dl->user_cancelled) {
        dl->user_cancelled = FALSE;
        return;
    }
    dl->state = ZHI_DL_FAILED;
    dl_update_row(dl);
    if (browser) {
        browser->active_download_count--;
        update_dl_button(browser);
    }
    (void)error;
}

static ZhiDownload *dl_add(ZhiBrowser *browser, WebKitDownload *wk_dl, const gchar *uri) {

    gint active = 0;
    for (GList *l = browser->downloads; l; l = l->next) {
        ZhiDownload *d = l->data;
        if (d->state == ZHI_DL_RUNNING) active++;
    }
    if (active >= ZHI_MAX_CONCURRENT_DOWNLOADS) {
        if (wk_dl) {

            g_signal_handlers_disconnect_by_data(wk_dl, browser);
            webkit_download_cancel(wk_dl);
        }
        zhi_browser_update_status(browser, T(browser, "已达到最大并发下载数（20）", "Max concurrent downloads reached (20)"));
        return NULL;
    }

    GList *to_remove = NULL;
    for (GList *l = browser->downloads; l; l = l->next) {
        ZhiDownload *old = l->data;
        if (old->uri && g_strcmp0(old->uri, uri) == 0 &&
            (old->state == ZHI_DL_RUNNING || old->state == ZHI_DL_FINISHED)) {
            if (old->wk_download) {
                old->user_cancelled = TRUE;
                g_signal_handlers_disconnect_by_data(old->wk_download, old);
                webkit_download_cancel(old->wk_download);
                old->wk_download = NULL;
            }
            if (old->poll_timer_id) { g_source_remove(old->poll_timer_id); old->poll_timer_id = 0; }
            old->state = ZHI_DL_CANCELLED;
            if (old->row_widget) gtk_widget_destroy(old->row_widget);
            to_remove = g_list_append(to_remove, old);
        }
    }
    for (GList *l = to_remove; l; l = l->next) {
        ZhiDownload *old = l->data;
        browser->downloads = g_list_remove(browser->downloads, old);
        browser->active_download_count--;
        g_free(old->uri); g_free(old->filename); g_free(old->dest_path);
        g_free(old);
    }
    g_list_free(to_remove);

    ZhiDownload *dl = g_new0(ZhiDownload, 1);
    dl->uri = g_strdup(uri);
    dl->state = ZHI_DL_RUNNING;
    dl->wk_download = wk_dl;

    gchar *dir = dl_get_download_dir();
    dl->filename = dl_extract_filename(uri);
    dl->dest_path = g_build_filename(dir, dl->filename, NULL);

    if (g_file_test(dl->dest_path, G_FILE_TEST_EXISTS)) {
        gchar *dot = strrchr(dl->filename, '.');
        gint counter = 1;
        while (g_file_test(dl->dest_path, G_FILE_TEST_EXISTS)) {
            g_free(dl->dest_path);
            gchar *new_name = NULL;
            if (dot) {
                gchar *name_part = g_strndup(dl->filename, dot - dl->filename);
                new_name = g_strdup_printf("%s (%d)%s", name_part, counter, dot);
                g_free(name_part);
            } else {
                new_name = g_strdup_printf("%s (%d)", dl->filename, counter);
            }
            dl->dest_path = g_build_filename(dir, new_name, NULL);
            g_free(new_name);
            counter++;
        }
    }
    gchar *parent = g_path_get_dirname(dl->dest_path);
    g_mkdir_with_parents(parent, 0755);
    g_free(parent);
    g_free(dir);

    if (wk_dl) {
        webkit_download_set_destination(wk_dl, dl->dest_path);
        dl->progress_sig = g_signal_connect(wk_dl, "notify::progress",
            G_CALLBACK(on_wk_download_progress), dl);
        g_signal_connect(wk_dl, "finished",
            G_CALLBACK(on_wk_download_finished), dl);
        g_signal_connect(wk_dl, "failed", G_CALLBACK(on_wk_download_failed), dl);
    }

    dl_speed_init(dl);
    browser->downloads = g_list_append(browser->downloads, dl);
    browser->active_download_count++;

    GtkWidget *row = dl_create_row(browser, dl);
    gtk_list_box_insert(GTK_LIST_BOX(browser->dl_list_box), row, 0);
    if (browser->dl_popover) gtk_widget_show_all(browser->dl_popover);

    dl->poll_timer_id = g_timeout_add(ZHI_DL_POLL_INTERVAL_MS, dl_poll_progress, dl);

    if (!browser->dl_flash_active && !browser->dl_anim_id) {
        browser->dl_flash_active = TRUE;
        browser->dl_anim_offset = 0;
        browser->dl_flash_timer_id = g_timeout_add(2000,
            dl_flash_timeout, browser);
        browser->dl_anim_id = g_timeout_add(50, dl_anim_tick, browser);
    }
    update_dl_button(browser);
    return dl;
}

static gboolean dl_flash_timeout(gpointer user_data) {
    ZhiBrowser *browser = user_data;
    browser->dl_flash_active = FALSE;
    browser->dl_flash_timer_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean dl_anim_tick(gpointer user_data) {
    ZhiBrowser *browser = user_data;
    if (!browser->dl_btn_drawing || !GTK_IS_WIDGET(browser->dl_btn_drawing)) {
        browser->dl_anim_id = 0;
        return G_SOURCE_REMOVE;
    }
    if (browser->active_download_count <= 0 && !browser->dl_flash_active) {
        browser->dl_anim_id = 0;
        browser->dl_anim_offset = 0;
        gtk_widget_queue_draw(browser->dl_btn_drawing);
        return G_SOURCE_REMOVE;
    }
    if (browser->dl_flash_active) {

        browser->dl_anim_offset += 0.3;
        if (browser->dl_anim_offset > 2.0 * G_PI)
            browser->dl_anim_offset -= 2.0 * G_PI;
    } else if (browser->active_download_count > 0) {

        browser->dl_anim_offset += 0.15;
        if (browser->dl_anim_offset > 2.0 * G_PI)
            browser->dl_anim_offset -= 2.0 * G_PI;
    }
    gtk_widget_queue_draw(browser->dl_btn_drawing);
    return G_SOURCE_CONTINUE;
}

static gboolean on_dl_btn_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    gint w = gtk_widget_get_allocated_width(widget);
    gint h = gtk_widget_get_allocated_height(widget);
    gdouble cx = w / 2.0, cy = h / 2.0;
    gdouble radius = MIN(w, h) / 2.0 - 3;

    if (browser->dl_flash_active) {

        gdouble flash_alpha = 0.3 + 0.7 * fabs(sin(browser->dl_anim_offset));
        cairo_set_line_width(cr, 3.0);
        cairo_set_source_rgba(cr, 1.0, 0.85, 0.2, flash_alpha);
        cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, 0.18, 0.83, 0.66, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, cx, cy - radius * 0.5);
        cairo_line_to(cr, cx, cy + radius * 0.3);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - radius * 0.3, cy);
        cairo_line_to(cr, cx, cy + radius * 0.35);
        cairo_line_to(cr, cx + radius * 0.3, cy);
        cairo_stroke(cr);
    } else if (browser->active_download_count > 0) {

        cairo_set_line_width(cr, 3.0);
        cairo_set_source_rgba(cr, 0.4, 0.4, 0.5, 0.25);
        cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
        cairo_stroke(cr);
        if (browser->dl_progress > 0.001) {

            gdouble end_angle = -G_PI / 2.0 + 2.0 * G_PI * browser->dl_progress;
            cairo_set_source_rgba(cr, 0.18, 0.83, 0.66, 0.95);
            cairo_arc(cr, cx, cy, radius, -G_PI / 2.0, end_angle);
            cairo_stroke(cr);
        } else {

            cairo_set_source_rgba(cr, 0.18, 0.83, 0.66, 0.6);
            cairo_arc(cr, cx, cy, radius, browser->dl_anim_offset, browser->dl_anim_offset + G_PI * 0.5);
            cairo_stroke(cr);
        }

        cairo_set_source_rgba(cr, 0.85, 0.85, 0.88, 0.9);
        cairo_set_line_width(cr, 1.8);
        cairo_move_to(cr, cx, cy - radius * 0.35);
        cairo_line_to(cr, cx, cy + radius * 0.2);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - radius * 0.2, cy + 0.02);
        cairo_line_to(cr, cx, cy + radius * 0.28);
        cairo_line_to(cr, cx + radius * 0.2, cy + 0.02);
        cairo_stroke(cr);
    } else {

        cairo_set_source_rgba(cr, 0.18, 0.83, 0.66, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, cx, cy - radius * 0.5);
        cairo_line_to(cr, cx, cy + radius * 0.3);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - radius * 0.3, cy);
        cairo_line_to(cr, cx, cy + radius * 0.35);
        cairo_line_to(cr, cx + radius * 0.3, cy);
        cairo_stroke(cr);
    }
    return TRUE;
}

static void update_dl_button(ZhiBrowser *browser) {
    if (!browser || !browser->dl_btn) return;
    gdouble total_progress = 0;
    gint active = 0;
    for (GList *l = browser->downloads; l; l = l->next) {
        ZhiDownload *dl = l->data;
        if (dl->state == ZHI_DL_RUNNING) {
            active++;
            if (dl->total_size > 0)
                total_progress += (gdouble)dl->received / dl->total_size;
        }
    }
    browser->dl_progress = active > 0 ? total_progress / active : 0;
    browser->active_download_count = active;
    if (active <= 0 && !browser->dl_flash_active && browser->dl_anim_id) {
        g_source_remove(browser->dl_anim_id);
        browser->dl_anim_id = 0;
        browser->dl_anim_offset = 0;
    }
    if (browser->dl_btn_drawing && GTK_IS_WIDGET(browser->dl_btn_drawing))
        gtk_widget_queue_draw(browser->dl_btn_drawing);
}

static void on_dl_btn_clicked(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    if (!browser->dl_popover || !GTK_IS_WIDGET(browser->dl_popover)) return;
    if (gtk_widget_get_visible(browser->dl_popover))
        gtk_widget_hide(browser->dl_popover);
    else {
        gtk_widget_show_all(browser->dl_popover);

        for (GList *l = browser->downloads; l; l = l->next) {
            ZhiDownload *d = l->data;
            dl_update_buttons(d);
        }
    }
}

static void on_dl_hist_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    ZhiBrowser *browser = g_object_get_data(G_OBJECT(btn), "browser");
    if (!browser) return;
    if (browser->active_tab)
        zhi_browser_navigate(browser, browser->active_tab, "about:zhistar:downloads");
}

static void on_dl_clear_all(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    GList *next = browser->downloads;
    while (next) {
        ZhiDownload *dl = next->data;
        next = next->next;
        if (dl->state != ZHI_DL_RUNNING) {
            if (dl->row_widget) gtk_widget_destroy(dl->row_widget);
            browser->downloads = g_list_remove(browser->downloads, dl);
            g_free(dl->uri); g_free(dl->filename); g_free(dl->dest_path);
            g_free(dl);
        }
    }
}

static GtkWidget *dl_create_popover(ZhiBrowser *browser) {
    GtkWidget *popover = gtk_popover_new(browser->dl_btn);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_size_request(popover, 420, 300);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 8);
    gtk_widget_set_margin_bottom(header, 8);
    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<b>%s</b>", T(browser, "下载管理器", "Downloads"));
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
    GtkWidget *clear_btn = gtk_button_new_with_label(T(browser, "清除已完成", "Clear Completed"));
    GtkStyleContext *cctx = gtk_widget_get_style_context(clear_btn);
    gtk_style_context_add_class(cctx, "flat");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_dl_clear_all), browser);
    gtk_box_pack_start(GTK_BOX(header), clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    browser->dl_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(browser->dl_list_box), GTK_SELECTION_NONE);
    GtkStyleContext *lctx = gtk_widget_get_style_context(browser->dl_list_box);
    gtk_style_context_add_class(lctx, "boxed-list");
    gtk_container_add(GTK_CONTAINER(scroll), browser->dl_list_box);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    browser->dl_summary_label = gtk_label_new("");
    gtk_widget_set_margin_start(browser->dl_summary_label, 12);
    gtk_widget_set_margin_end(browser->dl_summary_label, 12);
    gtk_widget_set_margin_top(browser->dl_summary_label, 4);
    gtk_widget_set_halign(browser->dl_summary_label, GTK_ALIGN_START);
    GtkStyleContext *sumctx = gtk_widget_get_style_context(browser->dl_summary_label);
    gtk_style_context_add_class(sumctx, "history-item-time");
    gtk_box_pack_start(GTK_BOX(vbox), browser->dl_summary_label, FALSE, FALSE, 0);
    GtkWidget *hist_btn = gtk_button_new_with_label(T(browser, "查看下载历史", "Download History"));
    GtkStyleContext *hctx = gtk_widget_get_style_context(hist_btn);
    gtk_style_context_add_class(hctx, "flat");
    gtk_widget_set_margin_start(hist_btn, 8);
    gtk_widget_set_margin_end(hist_btn, 8);
    gtk_widget_set_margin_top(hist_btn, 4);
    gtk_widget_set_margin_bottom(hist_btn, 4);
    gtk_box_pack_start(GTK_BOX(vbox), hist_btn, FALSE, FALSE, 0);
    g_signal_connect_swapped(hist_btn, "clicked", G_CALLBACK(gtk_widget_hide), popover);
    g_object_set_data(G_OBJECT(hist_btn), "browser", browser);
    g_signal_connect(hist_btn, "clicked", G_CALLBACK(on_dl_hist_btn_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(popover), vbox);
    return popover;
}

void zhi_browser_start_download(ZhiBrowser *browser, const gchar *uri) {
    if (!browser || !uri) return;
    gchar *dir = dl_get_download_dir();
    gchar *fname = dl_extract_filename(uri);
    gchar *dest = g_build_filename(dir, fname, NULL);
    g_free(dir); g_free(fname);
    g_object_set_data_full(G_OBJECT(browser->window), "pending-download-uri",
        g_strdup(uri), g_free);
    g_object_set_data_full(G_OBJECT(browser->window), "pending-download-path",
        dest, g_free);
    webkit_web_context_download_uri(webkit_web_context_get_default(), uri);
}

static void on_download_started(WebKitWebContext *ctx, WebKitDownload *download, gpointer user_data) {
    (void)ctx;
    ZhiBrowser *browser = user_data;
    gchar *uri = g_object_get_data(G_OBJECT(browser->window), "pending-download-uri");
    gchar *path = g_object_get_data(G_OBJECT(browser->window), "pending-download-path");
    if (path) {
        webkit_download_set_destination(download, path);
        g_object_set_data(G_OBJECT(browser->window), "pending-download-path", NULL);
    }
    const gchar *dl_uri = uri ? uri : (webkit_download_get_destination(download) ? webkit_download_get_destination(download) : "unknown");
    ZhiDownload *dl = dl_add(browser, download, dl_uri);

    if (dl) {
        gpointer cl_ptr = g_object_get_data(G_OBJECT(browser->window), "pending-download-total-size");
        if (cl_ptr) dl->total_size = GPOINTER_TO_UINT(cl_ptr);
    }
    g_object_set_data(G_OBJECT(browser->window), "pending-download-uri", NULL);
}

ZhiTab *zhi_browser_add_tab(ZhiBrowser *browser, const gchar *url) {
    ZhiTab *tab = g_new0(ZhiTab, 1);
    tab->url = g_strdup(url ? url : "about:blank");
    tab->title = sanitize_utf8(T(browser, "新标签页", "New Tab"));
    tab->is_home = TRUE;
    tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    tab->nav_box = create_nav_bar(browser, tab);
    gtk_box_pack_start(GTK_BOX(tab->page), tab->nav_box, FALSE, FALSE, 0);
    tab->content = gtk_stack_new();
    gtk_widget_set_hexpand(tab->content, TRUE);
    gtk_widget_set_vexpand(tab->content, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(tab->content), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(tab->content), 150);
    GtkWidget *newtab = zhi_new_tab_page_new(browser);
    gtk_stack_add_named(GTK_STACK(tab->content), newtab, "newtab");
    g_object_set_data(G_OBJECT(tab->content), "newtab-overlay", newtab);
    tab->web_view = webkit_web_view_new();
    WebKitSettings *wk_settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(tab->web_view));
    webkit_settings_set_enable_javascript(wk_settings, TRUE);
    webkit_settings_set_enable_developer_extras(wk_settings, TRUE);
    webkit_settings_set_user_agent_with_application_details(wk_settings, "ZhiStarNova", APP_VERSION);
    webkit_settings_set_enable_smooth_scrolling(wk_settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(wk_settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    WebKitWebContext *wctx = webkit_web_context_get_default();

    {
        static gboolean cookie_initialized = FALSE;
        if (!cookie_initialized) {
            const gchar *cp = zhi_config_get(browser->config, "cookie_policy", "all");
            WebKitCookieManager *cm = webkit_web_context_get_cookie_manager(wctx);
            if (g_strcmp0(cp, "none") == 0)
                webkit_cookie_manager_set_accept_policy(cm, WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
            else if (g_strcmp0(cp, "first-party") == 0)
                webkit_cookie_manager_set_accept_policy(cm, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
            else
                webkit_cookie_manager_set_accept_policy(cm, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
            cookie_initialized = TRUE;
        }
    }

    webkit_web_context_set_cache_model(wctx, WEBKIT_CACHE_MODEL_WEB_BROWSER);

    if (zhi_config_get_bool(browser->config, "proxy_enabled", FALSE)) {
        const gchar *proxy_url = zhi_config_get(browser->config, "proxy", "");
        if (proxy_url && strlen(proxy_url) > 0) {
            WebKitWebsiteDataManager *dm = webkit_web_view_get_website_data_manager(WEBKIT_WEB_VIEW(tab->web_view));
            WebKitNetworkProxySettings *ps = webkit_network_proxy_settings_new(proxy_url, NULL);
            webkit_website_data_manager_set_network_proxy_settings(dm,
                WEBKIT_NETWORK_PROXY_MODE_CUSTOM, ps);
            webkit_network_proxy_settings_free(ps);
        }
    }

    WebKitSettings *wk_settings2 = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(tab->web_view));
    webkit_settings_set_allow_file_access_from_file_urls(wk_settings2, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(wk_settings2, TRUE);

    static gboolean download_connected = FALSE;
    if (!download_connected) {
        g_signal_connect(wctx, "download-started", G_CALLBACK(on_download_started), browser);
        download_connected = TRUE;
    }

    if (zhi_config_get_bool(browser->config, "ad_block", TRUE)) {
        WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(tab->web_view));

        static const gchar *adblock_css =

            "[id*=\"google_ads\"], [id*=\"google_ad\"], [id*=\"gpt-ad\"],"
            "[class*=\"google_ads\"], [class*=\"google-ad\"], [class*=\"adsbygoogle\"],"
            "[id*=\"aswift\"], [id*=\"adngin\"],"

            "[id*=\"ad-\"], [id*=\"ads-\"], [id*=\"ads_\"], [id*=\"_ads\"],"
            "[id*=\"advert\"], [id*=\"banner-ad\"], [id*=\"sponsor\"],"
            "[class*=\"ad-banner\"], [class*=\"ad-block\"], [class*=\"ad-box\"],"
            "[class*=\"ad-container\"], [class*=\"ad-wrapper\"], [class*=\"ad-unit\"],"
            "[class*=\"ad-slot\"], [class*=\"ad-space\"], [class*=\"ad-label\"],"
            "[class*=\"ad-text\"], [class*=\"ad-row\"], [class*=\"ad-side\"],"
            "[class*=\"ads-\"][class*=\"container\"], [class*=\"ads_\"][class*=\"box\"],"
            "[class*=\"ad_container\"], [class*=\"adwrapper\"], [class*=\"adblock\"],"
            "[class*=\"sponsored\"], [class*=\"promo\"], [class*=\"promotion\"],"

            "iframe[src*=\"doubleclick\"], iframe[src*=\"googlesyndication\"],"
            "iframe[src*=\"googleadservices\"], iframe[src*=\"adnxs\"],"
            "iframe[src*=\"adsrvr.org\"], iframe[src*=\"taboola\"],"
            "iframe[src*=\"outbrain\"], iframe[src*=\"zergnet\"],"
            "iframe[src*=\"amazon-adsystem\"], iframe[src*=\"clkrevolution\"],"
            "iframe[src*=\"serving-sys\"], iframe[src*=\"eyeblaster\"],"
            "iframe[src*=\"adform\"], iframe[src*=\"mathtag\"],"
            "iframe[src*=\"demdex\"], iframe[src*=\"everesttech\"],"

            "[class*=\"modal-ad\"], [class*=\"popup-ad\"], [class*=\"overlay-ad\"],"
            "[class*=\"interstitial\"], [class*=\"lightbox-ad\"],"
            "[id*=\"overlay\"], [class*=\"overlay\"][class*=\"ad\"],"

            "[class*=\"native-ad\"], [class*=\"native_ad\"],"
            "[class*=\"advertorial\"], [class*=\"sponsored-content\"],"

            ".ad, .ads, .adv, .advp, .adp, .adsbygoogle,"
            "[data-ad-slot], [data-ad-unit], [data-ad-client],"
            "[data-dfp-unit], [data-placeholder-ad],"
            "ins.adsbygoogle, .adsbygoogle,"

            "[class*=\"ad-module\"], [class*=\"ad-module-container\"],"
            "[id*=\"BAIDU_SSP__wrapper\"], [id*=\"baiduad\"],"
            "[class*=\"tb-ad\"], [class*=\"tm-promo\"],"

            "iframe[width=\"0\"], iframe[height=\"0\"],"
            "iframe[style*=\"display:none\"], iframe[style*=\"visibility:hidden\"],"
            "iframe[style*=\"width:0\"], iframe[style*=\"height:0\"] {"
            "display: none !important; visibility: hidden !important;"
            "width: 0 !important; height: 0 !important;"
            "max-height: 0 !important; overflow: hidden !important; }";
        WebKitUserStyleSheet *sheet = webkit_user_style_sheet_new(
            adblock_css,
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_STYLE_LEVEL_USER,
            NULL, NULL);
        webkit_user_content_manager_add_style_sheet(ucm, sheet);
        webkit_user_style_sheet_unref(sheet);

        if (browser->adblock_custom_css && strlen(browser->adblock_custom_css) > 0) {
            WebKitUserStyleSheet *custom_sheet = webkit_user_style_sheet_new(
                browser->adblock_custom_css,
                WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                WEBKIT_USER_STYLE_LEVEL_USER,
                NULL, NULL);
            webkit_user_content_manager_add_style_sheet(ucm, custom_sheet);
            webkit_user_style_sheet_unref(custom_sheet);
        }
    }
    tab->web_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(tab->web_scroll), tab->web_view);
    gtk_stack_add_named(GTK_STACK(tab->content), tab->web_scroll, "webview");
    g_object_set_data(G_OBJECT(tab->web_view), "zhi-tab", tab);
    tab->title_handler_id = g_signal_connect(tab->web_view, "notify::title",
        G_CALLBACK(on_web_view_title_changed), browser);
    tab->load_handler_id = g_signal_connect(tab->web_view, "load-changed",
        G_CALLBACK(on_web_view_load_changed), browser);
    g_signal_connect(tab->web_view, "notify::estimated-load-progress",
        G_CALLBACK(on_load_progress_changed), browser);
    g_signal_connect(tab->web_view, "button-press-event",
        G_CALLBACK(on_web_view_button_press), browser);
    g_signal_connect(tab->web_view, "load-failed",
        G_CALLBACK(on_web_view_load_failed), browser);
    g_signal_connect(tab->web_view, "decide-policy",
        G_CALLBACK(on_web_view_decide_policy), browser);
    g_signal_connect(tab->web_view, "mouse-target-changed",
        G_CALLBACK(on_web_view_mouse_target), browser);
    g_signal_connect(tab->web_view, "run-file-chooser",
        G_CALLBACK(on_run_file_chooser), browser);
    gtk_drag_dest_set(tab->web_view, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(tab->web_view);
    g_signal_connect(tab->web_view, "drag-data-received",
        G_CALLBACK(on_web_view_drag_data_received), browser);

    tab->find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(tab->find_bar, 8);
    gtk_widget_set_margin_end(tab->find_bar, 8);
    gtk_widget_set_margin_top(tab->find_bar, 4);
    gtk_widget_set_margin_bottom(tab->find_bar, 4);
    tab->find_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(tab->find_entry, TRUE);
    g_signal_connect(tab->find_entry, "activate", G_CALLBACK(on_find_next), browser);
    g_signal_connect(tab->find_entry, "stop-search", G_CALLBACK(on_find_close), browser);
    gtk_box_pack_start(GTK_BOX(tab->find_bar), tab->find_entry, TRUE, TRUE, 0);
    tab->find_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(tab->find_bar), tab->find_label, FALSE, FALSE, 4);
    GtkWidget *find_prev = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    g_signal_connect(find_prev, "clicked", G_CALLBACK(on_find_prev), browser);
    gtk_box_pack_start(GTK_BOX(tab->find_bar), find_prev, FALSE, FALSE, 0);
    GtkWidget *find_next = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    g_signal_connect(find_next, "clicked", G_CALLBACK(on_find_next), browser);
    gtk_box_pack_start(GTK_BOX(tab->find_bar), find_next, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(tab->find_bar, TRUE);
    gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "newtab");
    gtk_box_pack_start(GTK_BOX(tab->page), tab->content, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab->page), tab->find_bar, FALSE, FALSE, 0);
    gtk_widget_show_all(tab->page);
    const gchar *pos = zhi_config_get(browser->config, "new_tab_position", "end");
    gint idx;
    if (g_strcmp0(pos, "start") == 0) {
        idx = gtk_notebook_insert_page(GTK_NOTEBOOK(browser->notebook), tab->page, NULL, 0);
        browser->tabs = g_list_prepend(browser->tabs, tab);
    } else if (g_strcmp0(pos, "after-current") == 0 && browser->active_tab) {
        GList *link = g_list_find(browser->tabs, browser->active_tab);
        gint after = link ? g_list_position(browser->tabs, link) + 1 : (gint)g_list_length(browser->tabs);
        idx = gtk_notebook_insert_page(GTK_NOTEBOOK(browser->notebook), tab->page, NULL, after);
        browser->tabs = g_list_insert(browser->tabs, tab, after);
    } else {
        idx = gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), tab->page, NULL);
        browser->tabs = g_list_append(browser->tabs, tab);
    }
    browser->active_tab = tab;
    browser->active_tab_index = idx;
    if (tab->nav_box) {
        browser->dl_btn = g_object_get_data(G_OBJECT(tab->nav_box), "dl_btn");
        browser->dl_btn_drawing = browser->dl_btn;
    }
    if (browser->dl_popover && browser->dl_btn)
        gtk_popover_set_relative_to(GTK_POPOVER(browser->dl_popover), browser->dl_btn);

    tab->nav_history = g_list_append(NULL, g_strdup(tab->url));
    tab->nav_current = tab->nav_history;
    update_tab_bar(browser);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), idx);
    return tab;
}

typedef struct {
    ZhiBrowser *browser;
    ZhiTab     *tab;
    gint        idx;
} TabCloseAnim;

static gboolean on_tab_close_fade_step(gpointer data) {
    TabCloseAnim *anim = data;
    if (!anim || !anim->tab || !GTK_IS_WIDGET(anim->tab->page) ||
        !g_list_find(anim->browser->tabs, anim->tab)) {
        g_free(anim);
        return G_SOURCE_REMOVE;
    }
    gdouble opacity = gtk_widget_get_opacity(anim->tab->page);
    opacity -= ZHI_TAB_CLOSE_FADE_STEP;
    if (opacity <= 0.0) {

        gtk_widget_set_opacity(anim->tab->page, 0.0);
        zhi_browser_add_closed_tab(anim->browser, anim->tab->url, anim->tab->title);

        if (anim->tab->web_view)
            g_signal_handlers_disconnect_by_data(anim->tab->web_view, anim->browser);
        anim->browser->tabs = g_list_remove(anim->browser->tabs, anim->tab);
        gtk_notebook_remove_page(GTK_NOTEBOOK(anim->browser->notebook), anim->idx);
        g_free(anim->tab->url); g_free(anim->tab->title);
        g_list_free_full(anim->tab->connected_domains, g_free);
        g_list_free_full(anim->tab->nav_history, g_free);
        g_free(anim->tab);
        if (anim->browser->tabs) {
            gint ni = MIN(anim->idx, (gint)g_list_length(anim->browser->tabs) - 1);
            zhi_browser_switch_tab(anim->browser, g_list_nth_data(anim->browser->tabs, ni));
        } else {
            anim->browser->active_tab = NULL;
            zhi_browser_add_tab(anim->browser, NULL);
        }
        update_tab_bar(anim->browser);
        g_free(anim);
        return G_SOURCE_REMOVE;
    }
    gtk_widget_set_opacity(anim->tab->page, opacity);
    return G_SOURCE_CONTINUE;
}

void zhi_browser_close_tab(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab || tab->closing) return;
    gint idx = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), tab->page);
    if (idx < 0) return;
    tab->closing = TRUE;

    TabCloseAnim *anim = g_new(TabCloseAnim, 1);
    anim->browser = browser;
    anim->tab = tab;
    anim->idx = idx;
    g_timeout_add(ZHI_TAB_CLOSE_ANIM_MS / 4, on_tab_close_fade_step, anim);
}

void zhi_browser_close_tab_now(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab) return;
    gint idx = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), tab->page);
    if (idx < 0) return;
    zhi_browser_add_closed_tab(browser, tab->url, tab->title);
    if (tab->web_view)
        g_signal_handlers_disconnect_by_data(tab->web_view, browser);
    browser->tabs = g_list_remove(browser->tabs, tab);
    gtk_notebook_remove_page(GTK_NOTEBOOK(browser->notebook), idx);
    g_free(tab->url); g_free(tab->title);
    g_list_free_full(tab->connected_domains, g_free);
    g_list_free_full(tab->nav_history, g_free);
    g_free(tab);
    if (browser->tabs) {
        gint ni = MIN(idx, (gint)g_list_length(browser->tabs) - 1);
        zhi_browser_switch_tab(browser, g_list_nth_data(browser->tabs, ni));
    } else {
        browser->active_tab = NULL;
        zhi_browser_add_tab(browser, NULL);
    }
    update_tab_bar(browser);
}

void zhi_browser_switch_tab(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab) return;
    gint idx = gtk_notebook_page_num(GTK_NOTEBOOK(browser->notebook), tab->page);
    if (idx < 0) return;
    if (browser->menu_popover) {
        gtk_widget_destroy(browser->menu_popover);
        browser->menu_popover = NULL;
    }
    browser->active_tab = tab;
    browser->active_tab_index = idx;
    if (tab->nav_box) {
        browser->dl_btn = g_object_get_data(G_OBJECT(tab->nav_box), "dl_btn");
        browser->dl_btn_drawing = browser->dl_btn;
    }
    if (browser->dl_popover && browser->dl_btn)
        gtk_popover_set_relative_to(GTK_POPOVER(browser->dl_popover), browser->dl_btn);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), idx);
    update_tab_bar(browser);
    if (tab->url) zhi_browser_update_status(browser, tab->url);
}

void zhi_browser_set_tab_loading(ZhiBrowser *browser, ZhiTab *tab, gboolean loading) {
    if (!tab) return;
    GList *children = gtk_container_get_children(GTK_CONTAINER(browser->tab_bar));
    for (GList *l = children; l; l = l->next) {
        ZhiTab *t = g_object_get_data(G_OBJECT(l->data), "tab");
        if (t == tab) {
            GtkStyleContext *ctx = gtk_widget_get_style_context(l->data);
            if (loading) gtk_style_context_add_class(ctx, "loading");
            else gtk_style_context_remove_class(ctx, "loading");
            break;
        }
    }
    g_list_free(children);
}

void zhi_browser_update_tab_title(ZhiBrowser *browser, ZhiTab *tab, const gchar *query, const gchar *engine_name) {
    if (!tab) return;
    tab->custom_title = TRUE;
    g_free(tab->title);
    if (engine_name && strlen(engine_name) > 0) {
        gchar *raw = g_strdup_printf("%s - %s", query, engine_name);
        tab->title = sanitize_utf8(raw);
        g_free(raw);
    } else {
        tab->title = sanitize_utf8(query);
    }
    update_tab_bar(browser);
}

void zhi_browser_pin_tab(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab) return;
    tab->is_pinned = TRUE;
    update_tab_bar(browser);
    zhi_browser_update_status(browser, T(browser, "标签页已固定", "Tab pinned"));
}

void zhi_browser_unpin_tab(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab) return;
    tab->is_pinned = FALSE;
    update_tab_bar(browser);
    zhi_browser_update_status(browser, T(browser, "标签页已取消固定", "Tab unpinned"));
}

void zhi_browser_add_pinned_bookmark(ZhiBrowser *browser, const gchar *url, const gchar *title) {
    if (!url) return;
    for (GList *l = browser->pinned_bookmarks; l; l = l->next) {
        gchar *entry = l->data;
        gchar **parts = g_strsplit(entry, "\x1F", 2);
        gboolean match = (parts[0] && g_strcmp0(parts[0], url) == 0);
        g_strfreev(parts);
        if (match) return;
    }
    browser->pinned_bookmarks = g_list_append(browser->pinned_bookmarks,
        g_strdup_printf("%s\x1F%s", url, title ? title : url));
    zhi_browser_save_pinned_bookmarks(browser);
}

void zhi_browser_remove_pinned_bookmark(ZhiBrowser *browser, const gchar *url) {
    if (!url) return;
    GList *l = browser->pinned_bookmarks;
    while (l) {
        gchar *entry = l->data;
        gchar **parts = g_strsplit(entry, "\x1F", 2);
        if (parts[0] && g_strcmp0(parts[0], url) == 0) {
            GList *next = l->next;
            browser->pinned_bookmarks = g_list_remove_link(browser->pinned_bookmarks, l);
            g_free(entry);
            g_strfreev(parts);
            l = next;
        } else {
            g_strfreev(parts);
            l = l->next;
        }
    }
    zhi_browser_save_pinned_bookmarks(browser);
}

void zhi_browser_save_pinned_bookmarks(ZhiBrowser *browser) {
    const gchar *home = g_get_home_dir();
    gchar *dir = g_build_filename(home, ".config/zhistar", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, "pinned_bookmarks.txt", NULL);
    g_free(dir);
    FILE *f = fopen(path, "w");
    if (f) {
        for (GList *l = browser->pinned_bookmarks; l; l = l->next) {
            fprintf(f, "%s\n", (gchar *)l->data);
        }
        fclose(f);
    }
    g_free(path);
}

void zhi_browser_load_pinned_bookmarks(ZhiBrowser *browser) {
    g_list_free_full(browser->pinned_bookmarks, g_free);
    browser->pinned_bookmarks = NULL;
    const gchar *home = g_get_home_dir();
    gchar *path = g_build_filename(home, ".config/zhistar/pinned_bookmarks.txt", NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) { g_free(path); return; }
    gchar *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (gint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (strlen(line) > 0) {
                browser->pinned_bookmarks = g_list_append(browser->pinned_bookmarks, g_strdup(line));
            }
        }
        g_strfreev(lines);
        g_free(contents);
    }
    g_free(path);
}

static GtkWidget *create_nav_bar(ZhiBrowser *browser, ZhiTab *tab) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(hbox, "nav-bar");
    gtk_widget_set_margin_start(hbox, 4);
    gtk_widget_set_margin_end(hbox, 4);
    gtk_widget_set_margin_top(hbox, 2);
    gtk_widget_set_margin_bottom(hbox, 2);

    tab->ssl_icon = gtk_image_new_from_icon_name("channel-insecure-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_margin_start(tab->ssl_icon, 4);
    gtk_box_pack_start(GTK_BOX(hbox), tab->ssl_icon, FALSE, FALSE, 0);

    tab->back_btn = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(tab->back_btn, T(browser, "后退", "Back"));
    gtk_widget_set_sensitive(tab->back_btn, FALSE);
    g_signal_connect(tab->back_btn, "clicked", G_CALLBACK(on_back_clicked), browser);
    gtk_box_pack_start(GTK_BOX(hbox), tab->back_btn, FALSE, FALSE, 0);
    tab->fwd_btn = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(tab->fwd_btn, T(browser, "前进", "Forward"));
    gtk_widget_set_sensitive(tab->fwd_btn, FALSE);
    g_signal_connect(tab->fwd_btn, "clicked", G_CALLBACK(on_fwd_clicked), browser);
    gtk_box_pack_start(GTK_BOX(hbox), tab->fwd_btn, FALSE, FALSE, 0);
    tab->reload_btn = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(tab->reload_btn, T(browser, "刷新", "Reload"));
    g_signal_connect(tab->reload_btn, "clicked", G_CALLBACK(on_reload_clicked), browser);
    gtk_box_pack_start(GTK_BOX(hbox), tab->reload_btn, FALSE, FALSE, 0);
    GtkWidget *home_btn = gtk_button_new_from_icon_name("go-home-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(home_btn, T(browser, "主页", "Home"));
    g_signal_connect(home_btn, "clicked", G_CALLBACK(on_home_clicked), browser);
    gtk_box_pack_start(GTK_BOX(hbox), home_btn, FALSE, FALSE, 0);
    tab->url_entry = gtk_entry_new();
    gtk_widget_set_name(tab->url_entry, "url-bar");
    gtk_widget_set_hexpand(tab->url_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(tab->url_entry), T(browser, "搜索或输入网址", "Search or enter URL"));
    g_signal_connect(tab->url_entry, "activate", G_CALLBACK(on_url_activate), browser);
    g_signal_connect(tab->url_entry, "focus-in-event", G_CALLBACK(on_url_focus_in), browser);
    g_signal_connect(tab->url_entry, "focus-out-event", G_CALLBACK(on_url_focus_out), browser);
    gtk_box_pack_start(GTK_BOX(hbox), tab->url_entry, TRUE, TRUE, 0);

    GtkWidget *dl_btn = gtk_button_new_from_icon_name("download-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(dl_btn, T(browser, "下载", "Downloads"));
    g_signal_connect(dl_btn, "clicked", G_CALLBACK(on_dl_btn_clicked), browser);
    g_signal_connect(dl_btn, "draw", G_CALLBACK(on_dl_btn_draw), browser);
    g_object_set_data(G_OBJECT(vbox), "dl_btn", dl_btn);
    gtk_box_pack_start(GTK_BOX(hbox), dl_btn, FALSE, FALSE, 0);

    if (!browser->dl_popover)
        browser->dl_popover = dl_create_popover(browser);
    GtkWidget *menu_btn = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(menu_btn, T(browser, "菜单", "Menu"));
    g_signal_connect(menu_btn, "clicked", G_CALLBACK(on_menu_clicked), browser);
    gtk_box_pack_start(GTK_BOX(hbox), menu_btn, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(vbox), "menu_btn", menu_btn);

    tab->loading_bar = gtk_progress_bar_new();
    gtk_widget_set_name(tab->loading_bar, "loading-bar");
    gtk_widget_set_size_request(tab->loading_bar, -1, 2);
    gtk_widget_set_no_show_all(tab->loading_bar, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), tab->loading_bar, FALSE, FALSE, 0);
    return vbox;
}

static void on_load_progress_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    ZhiBrowser *browser = user_data;
    if (!browser || browser->destroying || !browser->active_tab) return;
    ZhiTab *tab = g_object_get_data(G_OBJECT(web_view), "zhi-tab");
    if (!tab || tab != browser->active_tab) return;
    if (tab->loading_bar && gtk_widget_get_visible(tab->loading_bar)) {
        gdouble progress = webkit_web_view_get_estimated_load_progress(web_view);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(tab->loading_bar), progress);
    }
    gdouble progress = webkit_web_view_get_estimated_load_progress(web_view);
    gchar *host = extract_host(tab->url);
    if (progress < 0.15) {
        show_load_status(browser, T(browser, "正在连接 %s ...", "Connecting to %s ..."), host);
    } else if (progress < 0.4) {
        show_load_status(browser, T(browser, "正在等待响应 %s ...", "Waiting for %s ..."), host);
    } else if (progress < 0.85) {
        show_load_status(browser, T(browser, "正在传输数据 %s ...", "Transferring data from %s ..."), host);
    } else {
        show_load_status(browser, T(browser, "正在渲染 %s ...", "Rendering %s ..."), host);
    }
    g_free(host);
}

static void on_web_view_drag_data_received(GtkWidget *widget, GdkDragContext *ctx, gint x, gint y,
    GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    (void)widget; (void)ctx; (void)info;
    ZhiBrowser *browser = user_data;
    if (!browser->active_tab) { gtk_drag_finish(ctx, FALSE, FALSE, time); return; }
    gchar **uris = gtk_selection_data_get_uris(data);
    if (!uris || !uris[0]) { gtk_drag_finish(ctx, FALSE, FALSE, time); g_strfreev(uris); return; }
    GString *js = g_string_new("(() => { const dt = new DataTransfer(); const addFiles = [");
    gboolean first = TRUE;
    for (gint i = 0; uris[i]; i++) {
        gchar *path = g_filename_from_uri(uris[i], NULL, NULL);
        if (!path) continue;
        GFile *file = g_file_new_for_path(path);
        GFileInfo *finfo = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        guint64 fsize = finfo ? g_file_info_get_size(finfo) : 0;
        if (finfo) g_object_unref(finfo);
        g_object_unref(file);
        if (fsize > 50 * 1024 * 1024) { g_free(path); continue; }
        gchar *contents = NULL;
        gsize len = 0;
        if (!g_file_get_contents(path, &contents, &len, NULL)) { g_free(path); continue; }
        gchar *b64 = g_base64_encode((const guchar *)contents, len);
        gchar *fname = g_path_get_basename(path);
        gchar *escaped_fname = g_uri_escape_string(fname, NULL, FALSE);
        g_string_append_printf(js, "%sfetch('data:application/octet-stream;base64,%s').then(r=>r.blob()).then(b=>new File([b],decodeURIComponent('%s')))",
            first ? "" : ",", b64, escaped_fname);
        first = FALSE;
        g_free(escaped_fname);
        g_free(fname);
        g_free(b64);
        g_free(contents);
        g_free(path);
    }
    g_string_append(js, "]; Promise.all(addFiles).then(f=>{f.forEach(b=>dt.items.add(b));");
    g_string_append_printf(js, "const el=document.elementFromPoint(%d,%d);if(el){el.dispatchEvent(new DragEvent('drop',{dataTransfer:dt,bubbles:true}))}})})()", x, y);
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(browser->active_tab->web_view),
        js->str, -1, NULL, NULL, NULL, NULL, NULL);
    g_string_free(js, TRUE);
    gtk_drag_finish(ctx, TRUE, FALSE, time);
    g_strfreev(uris);
}

static gboolean on_run_file_chooser(WebKitWebView *web_view, WebKitFileChooserRequest *request, gpointer user_data) {
    (void)web_view;
    ZhiBrowser *browser = user_data;
    gboolean multi = webkit_file_chooser_request_get_select_multiple(request);
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        T(browser, multi ? "选择文件" : "选择文件", multi ? "Select Files" : "Select File"),
        GTK_WINDOW(browser->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        T(browser, "取消", "Cancel"), GTK_RESPONSE_CANCEL,
        T(browser, "打开", "Open"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), multi);
    GtkFileFilter *f = webkit_file_chooser_request_get_mime_types_filter(request);
    if (f) gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), g_object_ref(f));
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, T(browser, "所有文件", "All Files"));
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        GSList *uris = gtk_file_chooser_get_uris(GTK_FILE_CHOOSER(dlg));
        guint count = g_slist_length(uris);
        const gchar **files = g_new(const gchar *, count + 1);
        guint i = 0;
        for (GSList *s = uris; s; s = s->next) {
            files[i++] = s->data;
        }
        files[i] = NULL;
        webkit_file_chooser_request_select_files(request, files);
        g_free(files);
        g_slist_free_full(uris, g_free);
    } else {
        webkit_file_chooser_request_cancel(request);
    }
    gtk_widget_destroy(dlg);
    return TRUE;
}

typedef struct {
    ZhiBrowser *browser;
    gchar      *uri;
} DeferredNewTab;

static gboolean deferred_new_tab_idle(gpointer user_data) {
    DeferredNewTab *d = user_data;
    ZhiBrowser *browser = d->browser;
    gchar *uri = d->uri;
    if (browser->destroying) { g_free(uri); g_free(d); return G_SOURCE_REMOVE; }
    ZhiTab *new_tab = zhi_browser_add_tab(browser, NULL);
    if (new_tab && WEBKIT_IS_WEB_VIEW(new_tab->web_view)) {
        g_free(new_tab->url);
        new_tab->url = g_strdup(uri);
        if (new_tab->url_entry)
            gtk_entry_set_text(GTK_ENTRY(new_tab->url_entry), uri);
        gtk_stack_set_visible_child_name(GTK_STACK(new_tab->content), "webview");
        new_tab->is_home = FALSE;
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(new_tab->web_view), uri);
        update_tab_bar(browser);
    }
    g_free(uri);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static gboolean on_web_view_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision,
    WebKitPolicyDecisionType type, gpointer user_data) {
    (void)web_view;
    ZhiBrowser *browser = user_data;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationAction *nav = webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        WebKitURIRequest *req = webkit_navigation_action_get_request(nav);
        const gchar *uri = webkit_uri_request_get_uri(req);
        if (uri && strlen(uri) > 0) {
            gchar *new_host = extract_host(uri);
            gchar *cur_host = (browser->active_tab && browser->active_tab->url) ?
                extract_host(browser->active_tab->url) : NULL;
            gboolean same_domain = FALSE;
            if (new_host && cur_host) {
                same_domain = (g_strcmp0(new_host, cur_host) == 0);
                if (!same_domain) {
                    const gchar *np = strrchr(new_host, '.');
                    const gchar *cp = strrchr(cur_host, '.');
                    if (np && cp && g_strcmp0(np, cp) == 0) {
                        gchar *np2 = g_strndup(new_host, np - new_host);
                        gchar *cp2 = g_strndup(cur_host, cp - cur_host);
                        const gchar *np3 = strrchr(np2, '.');
                        const gchar *cp3 = strrchr(cp2, '.');
                        if (np3 && cp3 && g_strcmp0(np3, cp3) == 0)
                            same_domain = TRUE;
                        g_free(np2);
                        g_free(cp2);
                    }
                }
            }
            if (same_domain && browser->active_tab) {
                load_url_in_tab(browser, browser->active_tab, uri);
            } else {
                DeferredNewTab *d = g_new(DeferredNewTab, 1);
                d->browser = browser;
                d->uri = g_strdup(uri);
                g_idle_add(deferred_new_tab_idle, d);
            }
            g_free(new_host);
            g_free(cur_host);
        }
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        WebKitNavigationAction *nav = webkit_navigation_policy_decision_get_navigation_action(
            WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        if (webkit_navigation_action_get_mouse_button(nav) == 2) {
            WebKitURIRequest *req = webkit_navigation_action_get_request(nav);
            const gchar *uri = webkit_uri_request_get_uri(req);
            if (uri && strlen(uri) > 0) {
                DeferredNewTab *d = g_new(DeferredNewTab, 1);
                d->browser = browser;
                d->uri = g_strdup(uri);
                g_idle_add(deferred_new_tab_idle, d);
            }
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }
    }
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *resp = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        WebKitURIResponse *uri_resp = webkit_response_policy_decision_get_response(resp);
        const gchar *mime = webkit_uri_response_get_mime_type(uri_resp);
        gboolean should_download = FALSE;
        if (mime) {
            if (g_str_has_prefix(mime, "audio/") || g_str_has_prefix(mime, "video/"))
                should_download = TRUE;
            else if (g_str_has_prefix(mime, "application/")) {
                if (!g_str_has_prefix(mime, "application/json") &&
                    !g_str_has_prefix(mime, "application/xml") &&
                    !g_str_has_prefix(mime, "application/javascript") &&
                    !g_str_has_prefix(mime, "application/xhtml") &&
                    !g_str_has_prefix(mime, "application/pdf"))
                    should_download = TRUE;
            }
        }
        if (should_download) {
            const gchar *uri = webkit_uri_response_get_uri(uri_resp);

            gchar *dir = dl_get_download_dir();
            gchar *fname = dl_extract_filename(uri);
            gchar *dest = g_build_filename(dir, fname, NULL);
            g_free(dir); g_free(fname);

            g_object_set_data_full(G_OBJECT(browser->window), "pending-download-uri",
                g_strdup(uri), g_free);
            g_object_set_data_full(G_OBJECT(browser->window), "pending-download-path",
                dest, g_free);

            guint64 content_length = 0;
            SoupMessageHeaders *headers = webkit_uri_response_get_http_headers(uri_resp);
            if (headers) {
                const gchar *cl = soup_message_headers_get_one(headers, "Content-Length");
                if (cl) content_length = g_ascii_strtoull(cl, NULL, 10);
            }
            g_object_set_data(G_OBJECT(browser->window),
                "pending-download-total-size", GUINT_TO_POINTER(content_length));
            webkit_policy_decision_download(decision);
            return TRUE;
        }
    }
    return FALSE;
}

static void load_url_in_tab(ZhiBrowser *browser, ZhiTab *tab, const gchar *url) {
    if (!url || strlen(url) == 0) return;

    gchar *norm_url = NULL;
    if (g_str_has_prefix(url, "about:zhistar:"))
        norm_url = g_strdup_printf("about:%s", url + strlen("about:zhistar:"));
    else
        norm_url = g_strdup(url);

    if (g_strcmp0(norm_url, "about:zhistar") == 0 || g_strcmp0(norm_url, "about:config") == 0 ||
        g_strcmp0(norm_url, "about:history") == 0 || g_strcmp0(norm_url, "about:css-editor") == 0 ||
        g_strcmp0(norm_url, "about:bookmarks") == 0 || g_strcmp0(norm_url, "about:downloads") == 0) {
        ZhiTab *new_tab = zhi_browser_add_tab(browser, NULL);
        GtkWidget *page_widget = NULL;
        const gchar *stack_name = NULL;
        const gchar *tab_title = NULL;
        if (g_strcmp0(norm_url, "about:zhistar") == 0 || g_strcmp0(norm_url, "about:config") == 0) {
            page_widget = zhi_settings_page_new(browser);
            stack_name = "settings";
            tab_title = T(browser, "设置", "Settings");
        } else if (g_strcmp0(norm_url, "about:history") == 0) {
            page_widget = zhi_history_page_new(browser);
            stack_name = "history";
            tab_title = T(browser, "历史记录", "History");
        } else if (g_strcmp0(norm_url, "about:bookmarks") == 0) {
            page_widget = zhi_bookmarks_page_new(browser);
            stack_name = "bookmarks";
            tab_title = T(browser, "书签管理", "Bookmarks");
        } else if (g_strcmp0(norm_url, "about:downloads") == 0) {
            page_widget = zhi_downloads_page_new(browser);
            stack_name = "downloads";
            tab_title = T(browser, "下载记录", "Downloads");
        } else {
            page_widget = zhi_css_editor_page_new(browser);
            stack_name = "csseditor";
            tab_title = T(browser, "CSS 编辑器", "CSS Editor");
        }
        gtk_stack_add_named(GTK_STACK(new_tab->content), page_widget, stack_name);
        gtk_widget_show_all(page_widget);
        gtk_stack_set_visible_child_name(GTK_STACK(new_tab->content), stack_name);
        g_free(new_tab->title);
        new_tab->title = sanitize_utf8(tab_title);
        new_tab->is_home = FALSE;
        new_tab->custom_title = TRUE;
        if (new_tab->url_entry) gtk_entry_set_text(GTK_ENTRY(new_tab->url_entry), "");

        new_tab->nav_history = g_list_append(new_tab->nav_history, g_strdup(norm_url));
        new_tab->nav_current = g_list_last(new_tab->nav_history);
        update_tab_bar(browser);
        return;
    }

    g_free(tab->url);
    tab->url = g_strdup(url);
    zhi_browser_set_tab_loading(browser, tab, TRUE);
    if (g_strcmp0(url, "about:blank") == 0 || g_strcmp0(url, "about:newtab") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "newtab");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "新标签页", "New Tab"));
        tab->is_home = TRUE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        zhi_browser_set_tab_loading(browser, tab, FALSE);
    } else {
        if (tab->url_entry)
            gtk_entry_set_text(GTK_ENTRY(tab->url_entry), norm_url);
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "webview");
        tab->is_home = FALSE;
        gchar *load_url = g_strdup(norm_url);
        if (!g_str_has_prefix(norm_url, "http://") && !g_str_has_prefix(norm_url, "https://") &&
            !g_str_has_prefix(norm_url, "about:") && !g_str_has_prefix(norm_url, "file://")) {
            g_free(load_url);
            load_url = g_strdup_printf("https://%s", norm_url);
        }
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->web_view), load_url);
        g_free(load_url);
    }
    g_free(norm_url);
    update_tab_bar(browser);
    update_nav_sensitivity(browser);
    zhi_browser_update_status(browser, url);
}

void zhi_browser_navigate(ZhiBrowser *browser, ZhiTab *tab, const gchar *url) {
    load_url_in_tab(browser, tab, url);
}

static gboolean navigate_about_url(ZhiBrowser *browser, ZhiTab *tab, const gchar *url) {
    if (g_strcmp0(url, "about:blank") == 0 || g_strcmp0(url, "about:newtab") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "newtab");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "新标签页", "New Tab"));
        tab->is_home = TRUE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return TRUE;
    } else if (g_strcmp0(url, "about:zhistar") == 0 || g_strcmp0(url, "about:config") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "settings");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "设置", "Settings"));
        tab->is_home = FALSE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return TRUE;
    } else if (g_strcmp0(url, "about:history") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "history");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "历史记录", "History"));
        tab->is_home = FALSE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return TRUE;
    } else if (g_strcmp0(url, "about:css-editor") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "csseditor");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "CSS 编辑器", "CSS Editor"));
        tab->is_home = FALSE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return TRUE;
    } else if (g_strcmp0(url, "about:bookmarks") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "bookmarks");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "书签管理", "Bookmarks"));
        tab->is_home = FALSE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return TRUE;
    } else if (g_strcmp0(url, "about:downloads") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "downloads");
        g_free(tab->title); tab->title = sanitize_utf8(T(browser, "下载记录", "Downloads"));
        tab->is_home = FALSE; tab->custom_title = TRUE;
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), "");
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return TRUE;
    }
    return FALSE;
}

void zhi_browser_go_back(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab || tab->navigating) return;

    if (tab->nav_current && tab->nav_current->prev) {
        tab->nav_current = tab->nav_current->prev;
        const gchar *url = tab->nav_current->data;

        g_free(tab->url);
        tab->url = g_strdup(url);
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), url);
        if (navigate_about_url(browser, tab, url)) return;
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "webview");
        tab->is_home = FALSE;
        tab->navigating = TRUE;
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->web_view), url);
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return;
    }

    if (WEBKIT_IS_WEB_VIEW(tab->web_view) && webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(tab->web_view))) {
        tab->navigating = TRUE;
        webkit_web_view_go_back(WEBKIT_WEB_VIEW(tab->web_view));
    }
}

void zhi_browser_go_forward(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab || tab->navigating) return;

    if (tab->nav_current && tab->nav_current->next) {
        tab->nav_current = tab->nav_current->next;
        const gchar *url = tab->nav_current->data;
        g_free(tab->url);
        tab->url = g_strdup(url);
        if (tab->url_entry) gtk_entry_set_text(GTK_ENTRY(tab->url_entry), url);
        if (navigate_about_url(browser, tab, url)) return;
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "webview");
        tab->is_home = FALSE;
        tab->navigating = TRUE;
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->web_view), url);
        update_tab_bar(browser);
        update_nav_sensitivity(browser);
        return;
    }

    if (WEBKIT_IS_WEB_VIEW(tab->web_view) && webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(tab->web_view))) {
        tab->navigating = TRUE;
        webkit_web_view_go_forward(WEBKIT_WEB_VIEW(tab->web_view));
    }
}

void zhi_browser_reload(ZhiBrowser *browser, ZhiTab *tab) {
    if (!tab) return;
    if (tab->is_home) {
        anim_flash(tab->page);
        gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "newtab");
        zhi_browser_set_tab_loading(browser, tab, FALSE);
        return;
    }

    const gchar *visible = gtk_stack_get_visible_child_name(GTK_STACK(tab->content));
    if (visible && g_strcmp0(visible, "webview") != 0) {
        anim_flash(tab->page);
    }
    if (WEBKIT_IS_WEB_VIEW(tab->web_view))
        webkit_web_view_reload(WEBKIT_WEB_VIEW(tab->web_view));
}

static void on_back_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; ZhiBrowser *browser = user_data;
    if (browser->active_tab) zhi_browser_go_back(browser, browser->active_tab);
}

static void on_fwd_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; ZhiBrowser *browser = user_data;
    if (browser->active_tab) zhi_browser_go_forward(browser, browser->active_tab);
}

static void on_reload_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; ZhiBrowser *browser = user_data;
    if (browser->active_tab) zhi_browser_reload(browser, browser->active_tab);
}

static void on_home_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; ZhiBrowser *browser = user_data;
    if (browser->active_tab) {
        const gchar *home = zhi_config_get(browser->config, "homepage", "about:newtab");
        zhi_browser_navigate(browser, browser->active_tab, home);
    }
}

static void on_url_activate(GtkEntry *entry, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *text = gtk_entry_get_text(entry);
    if (!text || strlen(text) == 0) return;
    const gchar *engine_url = zhi_config_get(browser->config, "search_engine",
        "https://www.bing.com/search?q=");
    if (g_str_has_prefix(text, "http://") || g_str_has_prefix(text, "https://") ||
        g_str_has_prefix(text, "about:") || g_str_has_prefix(text, "file://")) {
        zhi_browser_navigate(browser, browser->active_tab, text);
    } else if (strchr(text, '.') && !strchr(text, ' ')) {
        gchar *url = g_strdup_printf("https://%s", text);
        zhi_browser_navigate(browser, browser->active_tab, url);
        g_free(url);
    } else {
        gchar *encoded = g_uri_escape_string(text, NULL, TRUE);
        gchar *url = g_strdup_printf("%s%s", engine_url, encoded);
        g_free(encoded);
        const gchar *engine_name = zhi_search_engine_name(engine_url,
            T(browser, "百度", "Baidu"), "Search");
        ZhiTab *tab = browser->active_tab;
        zhi_browser_navigate(browser, tab, url);
        zhi_browser_update_tab_title(browser, tab, text, engine_name);
        g_free(url);
    }
    gtk_widget_grab_focus(browser->window);
}

static void on_url_focus_in(GtkEntry *entry, GdkEventFocus *e, gpointer user_data) {
    (void)e; (void)user_data;
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(entry)), "focused");
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

static void on_url_focus_out(GtkEntry *entry, GdkEventFocus *e, gpointer user_data) {
    (void)e; (void)user_data;
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(entry)), "focused");
}

static void on_menu_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; ZhiBrowser *browser = user_data;
    zhi_browser_show_menu(browser);
}

static void on_new_tab_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; ZhiBrowser *browser = user_data;
    zhi_browser_add_tab(browser, NULL);
}

static gboolean on_window_delete(GtkWidget *w, GdkEvent *e, gpointer user_data) {
    (void)w; (void)e;
    ZhiBrowser *browser = user_data;

    gint width, height, x, y;
    gtk_window_get_size(GTK_WINDOW(browser->window), &width, &height);
    gtk_window_get_position(GTK_WINDOW(browser->window), &x, &y);
    zhi_config_set_int(browser->config, "window_width", width);
    zhi_config_set_int(browser->config, "window_height", height);
    zhi_config_set_int(browser->config, "window_x", x);
    zhi_config_set_int(browser->config, "window_y", y);

    gint total_tabs = g_list_length(browser->tabs);
    if (total_tabs > 1) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(browser->window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            "%s", T(browser, "确定要关闭所有标签页吗？", "Close all tabs?"));
        gtk_window_set_title(GTK_WINDOW(dlg), T(browser, "确认关闭", "Confirm Close"));
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (resp != GTK_RESPONSE_YES) return TRUE;
    }
    zhi_config_save(browser->config);
    return FALSE;
}

static gboolean on_window_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event;
    ZhiBrowser *browser = user_data;
    if (browser->active_tab && browser->active_tab->url_entry) {
        GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(browser->window));
        if (focused == GTK_WIDGET(browser->active_tab->url_entry))
            gtk_widget_grab_focus(browser->window);
    }
    return FALSE;
}

static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_f) {
        ZhiTab *tab = browser->active_tab;
        if (tab && tab->find_bar && GTK_IS_WIDGET(tab->find_bar)) {
            if (gtk_widget_get_visible(tab->find_bar)) {
                gtk_widget_hide(tab->find_bar);
                if (WEBKIT_IS_WEB_VIEW(tab->web_view)) {
                    WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(tab->web_view));
                    webkit_find_controller_search_finish(fc);
                }
            } else {
                gtk_widget_show_all(tab->find_bar);
                gtk_widget_grab_focus(tab->find_entry);
                if (WEBKIT_IS_WEB_VIEW(tab->web_view)) {
                    WebKitFindController *fc = webkit_web_view_get_find_controller(WEBKIT_WEB_VIEW(tab->web_view));
                    if (browser->find_count_handler_id) {
                        g_signal_handler_disconnect(fc, browser->find_count_handler_id);
                        browser->find_count_handler_id = 0;
                    }
                    browser->find_count_handler_id = g_signal_connect(fc, "counted-matches", G_CALLBACK(on_find_count), browser);
                }
            }
            return TRUE;
        }
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_Tab) {
        gboolean backward = (event->state & GDK_SHIFT_MASK) != 0;
        gint n = g_list_length(browser->tabs);
        if (n > 1) {
            gint idx = browser->active_tab_index;
            if (backward) idx = (idx - 1 + n) % n;
            else idx = (idx + 1) % n;
            ZhiTab *next = g_list_nth_data(browser->tabs, idx);
            if (next) zhi_browser_switch_tab(browser, next);
        }
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_w) {
        ZhiTab *tab = browser->active_tab;
        if (tab) zhi_browser_close_tab(browser, tab);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_t) {
        zhi_browser_add_tab(browser, NULL);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_l) {
        ZhiTab *tab = browser->active_tab;
        if (tab && tab->url_entry) {
            gtk_widget_grab_focus(tab->url_entry);
            gtk_editable_select_region(GTK_EDITABLE(tab->url_entry), 0, -1);
        }
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_F5)) {
        if (browser->active_tab) zhi_browser_reload(browser, browser->active_tab);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F5) {
        zhi_browser_reload(browser, browser->active_tab);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_F11) {
        zhi_browser_toggle_fullscreen(browser);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_h) {
        zhi_browser_navigate(browser, browser->active_tab, "about:history");
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_d) {
        ZhiTab *tab = browser->active_tab;
        if (tab && tab->url) {
            zhi_browser_add_pinned_bookmark(browser, tab->url, tab->title);
            zhi_browser_update_status(browser, T(browser, "已添加到书签", "Bookmark added"));
        }
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_b) {
        zhi_browser_navigate(browser, browser->active_tab, "about:bookmarks");
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_u) {
        if (browser->active_tab) {
            ZhiTab *tab = browser->active_tab;
            if (WEBKIT_IS_WEB_VIEW(tab->web_view)) {
                WebKitWebResource *resource = webkit_web_view_get_main_resource(WEBKIT_WEB_VIEW(tab->web_view));
                if (resource) webkit_web_resource_get_data(resource, NULL, (GAsyncReadyCallback)on_view_source_ready, browser);
            }
        }
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK) && event->keyval == GDK_KEY_T) {
        zhi_browser_reopen_closed_tab(browser);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_equal) {
        zhi_browser_zoom_in(browser);
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_minus) {
        zhi_browser_zoom_out(browser);
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_0) {
        zhi_browser_zoom_reset(browser);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_i) {
        zhi_browser_show_page_info(browser);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_s) {
        zhi_browser_save_page(browser);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_p) {
        zhi_browser_print_page(browser);
        return TRUE;
    }

    return FALSE;
}

static void on_ctx_save_as(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    zhi_browser_save_page(browser);
}

static void on_ctx_print(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    zhi_browser_print_page(browser);
}

static void on_ctx_copy_page_link(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    zhi_browser_copy_page_link(browser);
}

static void on_ctx_view_source(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    WebKitWebResource *resource = webkit_web_view_get_main_resource(WEBKIT_WEB_VIEW(tab->web_view));
    if (!resource) return;
    webkit_web_resource_get_data(resource, NULL,
        (GAsyncReadyCallback)on_view_source_ready, browser);
}

static void on_view_source_ready(GObject *source, GAsyncResult *result, gpointer user_data) {
    (void)source;
    ZhiBrowser *browser = user_data;
    if (browser->destroying) return;
    GError *err = NULL;
    gsize len = 0;
    guchar *data = webkit_web_resource_get_data_finish(WEBKIT_WEB_RESOURCE(source), result, &len, &err);
    if (err) {
        zhi_browser_update_status(browser, T(browser, "无法获取源代码", "Failed to get source"));
        g_error_free(err);
        return;
    }
    if (!data || len == 0) return;
    gchar *source_text = g_strndup((const gchar *)data, len);
    g_free(data);
    ZhiTab *tab = zhi_browser_add_tab(browser, NULL);
    g_free(tab->title);
    tab->title = sanitize_utf8(T(browser, "网页源代码", "Page Source"));
    tab->custom_title = TRUE;
    gtk_stack_set_visible_child_name(GTK_STACK(tab->content), "webview");
    gchar *escaped = g_markup_escape_text(source_text, -1);
    gchar *html = g_strdup_printf(
        "<html><head><meta charset=\"utf-8\"><title>Source</title>"
        "<style>pre{font-family:monospace;font-size:13px;white-space:pre-wrap;word-wrap:break-word;}</style></head>"
        "<body><pre>%s</pre></body></html>", escaped);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(tab->web_view), html, NULL);
    g_free(html);
    g_free(escaped);
    g_free(source_text);
    update_tab_bar(browser);
}

static void on_ctx_inspect(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(tab->web_view));
    if (inspector) webkit_web_inspector_show(inspector);
}

static void on_web_view_mouse_target(WebKitWebView *web_view, WebKitHitTestResult *hit, guint modifiers, gpointer user_data) {
    (void)web_view; (void)modifiers;
    ZhiBrowser *browser = user_data;
    if (!browser->status_bar) return;
    GtkLabel *label = g_object_get_data(G_OBJECT(browser->status_bar), "label");
    if (!label) return;
    if (webkit_hit_test_result_get_context(hit) & WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK) {
        const gchar *uri = webkit_hit_test_result_get_link_uri(hit);
        if (uri) {
            browser->status_hover = TRUE;
            gtk_label_set_text(label, uri);
        }
    } else {
        browser->status_hover = FALSE;
    }
}

static gboolean on_web_view_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *save_item = gtk_menu_item_new_with_label(
            T(browser, "另存为...", "Save As..."));
        g_signal_connect(save_item, "activate", G_CALLBACK(on_ctx_save_as), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), save_item);
        GtkWidget *print_item = gtk_menu_item_new_with_label(
            T(browser, "打印...", "Print..."));
        g_signal_connect(print_item, "activate", G_CALLBACK(on_ctx_print), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), print_item);
        GtkWidget *copy_link_item = gtk_menu_item_new_with_label(
            T(browser, "复制页面链接", "Copy Page Link"));
        g_signal_connect(copy_link_item, "activate", G_CALLBACK(on_ctx_copy_page_link), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_link_item);
        GtkWidget *src_item = gtk_menu_item_new_with_label(
            T(browser, "查看网页源代码", "View Page Source"));
        g_signal_connect(src_item, "activate", G_CALLBACK(on_ctx_view_source), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), src_item);
        GtkWidget *inspect_item = gtk_menu_item_new_with_label(
            T(browser, "检查", "Inspect"));
        g_signal_connect(inspect_item, "activate", G_CALLBACK(on_ctx_inspect), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), inspect_item);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

static void menu_btn_action(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *action = g_object_get_data(G_OBJECT(btn), "action");
    if (!action) return;
    if (browser->menu_popover)
        gtk_popover_popdown(GTK_POPOVER(browser->menu_popover));
    if (g_strcmp0(action, "settings") == 0)
        zhi_browser_navigate(browser, browser->active_tab, "about:zhistar");
    else if (g_strcmp0(action, "bookmarks") == 0)
        zhi_browser_navigate(browser, browser->active_tab, "about:bookmarks");
    else if (g_strcmp0(action, "new-window") == 0) {
        gchar *exe = g_file_read_link("/proc/self/exe", NULL);
        if (exe) {
            gchar *argv[] = {exe, NULL};
            g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
            g_free(exe);
        } else {
            gchar *argv[] = {(gchar *)"ZhiStar-Nova", NULL};
            g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
        }
    }
    else if (g_strcmp0(action, "history") == 0)
        zhi_browser_navigate(browser, browser->active_tab, "about:history");
    else if (g_strcmp0(action, "css-editor") == 0)
        zhi_browser_navigate(browser, browser->active_tab, "about:css-editor");
    else if (g_strcmp0(action, "about") == 0)
        zhi_browser_navigate(browser, browser->active_tab, "about:zhistar");
    else if (g_strcmp0(action, "quit") == 0)
        gtk_main_quit();
}

void zhi_browser_show_menu(ZhiBrowser *browser) {
    if (browser->menu_popover) {
        if (gtk_widget_get_visible(browser->menu_popover))
            gtk_popover_popdown(GTK_POPOVER(browser->menu_popover));
        else
            gtk_popover_popup(GTK_POPOVER(browser->menu_popover));
        return;
    }
    GtkWidget *menu_btn_ref = NULL;
    if (browser->active_tab && browser->active_tab->nav_box)
        menu_btn_ref = g_object_get_data(G_OBJECT(browser->active_tab->nav_box), "menu_btn");
    if (!menu_btn_ref) return;
    GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(menu_box), 6);
    struct { const char *zh; const char *en; const char *icon; const char *action; } items[] = {
        {"新建窗口", "New Window",   "window-new-symbolic",        "new-window"},
        {"设置",      "Settings",      "preferences-system-symbolic", "settings"},
        {"书签管理",   "Bookmarks",     "user-bookmarks-symbolic",    "bookmarks"},
        {"历史记录",   "History",       "document-open-recent-symbolic","history"},
        {"CSS 编辑器", "CSS Editor",    "text-css-symbolic",           "css-editor"},
        {NULL, NULL, NULL, NULL},
        {"关于",      "About",         "dialog-information-symbolic", "about"},
        {NULL, NULL, NULL, NULL},
        {"退出",      "Quit",          "application-exit-symbolic",   "quit"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(items); i++) {
        if (!items[i].zh) {
            gtk_box_pack_start(GTK_BOX(menu_box),
                gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
            continue;
        }
        const gchar *label = browser->lang == LANG_ZH ? items[i].zh : items[i].en;
        GtkWidget *btn = gtk_button_new();
        GtkStyleContext *bctx = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(bctx, "menu-item-btn");
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_top(row, 6);
        gtk_widget_set_margin_bottom(row, 6);
        gtk_widget_set_margin_start(row, 10);
        gtk_widget_set_margin_end(row, 10);
        GtkWidget *icon = gtk_image_new_from_icon_name(items[i].icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
        GtkWidget *lbl = gtk_label_new(label);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_size_new(12 * PANGO_SCALE));
        gtk_label_set_attributes(GTK_LABEL(lbl), attrs);
        pango_attr_list_unref(attrs);
        gtk_box_pack_start(GTK_BOX(row), lbl, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(btn), row);
        g_object_set_data(G_OBJECT(btn), "action", (gpointer)items[i].action);
        g_signal_connect(btn, "clicked", G_CALLBACK(menu_btn_action), browser);
        gtk_box_pack_start(GTK_BOX(menu_box), btn, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(menu_box);
    browser->menu_popover = gtk_popover_new(menu_btn_ref);
    GtkStyleContext *pctx = gtk_widget_get_style_context(browser->menu_popover);
    gtk_style_context_add_class(pctx, "menu-popover-anim");
    gtk_container_add(GTK_CONTAINER(browser->menu_popover), menu_box);
    gtk_popover_set_relative_to(GTK_POPOVER(browser->menu_popover), menu_btn_ref);
    gtk_popover_set_position(GTK_POPOVER(browser->menu_popover), GTK_POS_BOTTOM);
    gtk_widget_show(browser->menu_popover);
}

void zhi_browser_add_history(ZhiBrowser *browser, const gchar *url, const gchar *title) {
    if (!url) return;
    if (g_str_has_prefix(url, "about:")) return;
    gchar *safe_title = sanitize_utf8(title ? title : url);
    gint64 now = (gint64)time(NULL);
    gchar *entry = g_strdup_printf("%" G_GINT64_FORMAT "\t%s\t%s", now, url, safe_title);
    browser->history = g_list_prepend(browser->history, entry);
    if (g_list_length(browser->history) > ZHI_HISTORY_MAX) {
        GList *last = g_list_last(browser->history);
        browser->history = g_list_delete_link(browser->history, last);
        g_free(last->data);
    }
    browser->history_write_buf = g_list_append(browser->history_write_buf, g_strdup(entry));
    if (!browser->history_flush_timer)
        browser->history_flush_timer = g_timeout_add_seconds(ZHI_HISTORY_FLUSH_S, on_history_flush_timer, browser);
    g_free(safe_title);
}

static gboolean on_history_flush_timer(gpointer user_data) {
    ZhiBrowser *browser = user_data;
    zhi_browser_flush_history(browser);
    return G_SOURCE_REMOVE;
}

void zhi_browser_flush_history(ZhiBrowser *browser) {
    if (!browser->history_write_buf) return;
    const gchar *home = g_get_home_dir();
    if (!home) return;
    gchar *dir = g_build_filename(home, ".config/zhistar", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, "history.txt", NULL);
    g_free(dir);
    FILE *f = fopen(path, "a");
    if (f) {
        for (GList *l = browser->history_write_buf; l; l = l->next)
            fprintf(f, "%s\n", (gchar *)l->data);
        fclose(f);
    }
    g_free(path);
    g_list_free_full(browser->history_write_buf, g_free);
    browser->history_write_buf = NULL;
    browser->history_flush_timer = 0;
}

void zhi_browser_load_history(ZhiBrowser *browser) {
    const gchar *home = g_get_home_dir();
    if (!home) return;
    gchar *path = g_build_filename(home, ".config/zhistar/history.txt", NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) { g_free(path); return; }
    gchar *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (gint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (strlen(line) > 0)
                browser->history = g_list_append(browser->history, g_strdup(line));
        }
        g_strfreev(lines);
        g_free(contents);
    }
    g_free(path);
}

void zhi_browser_save_history(ZhiBrowser *browser) {
    const gchar *home = g_get_home_dir();
    if (!home) return;
    gchar *dir = g_build_filename(home, ".config/zhistar", NULL);
    g_mkdir_with_parents(dir, 0700);
    gchar *path = g_build_filename(dir, "history.txt", NULL);
    g_free(dir);
    FILE *f = fopen(path, "w");
    if (f) {
        for (GList *l = browser->history; l; l = l->next)
            fprintf(f, "%s\n", (gchar *)l->data);
        fclose(f);
    }
    g_free(path);
}

static gboolean on_status_clear(gpointer user_data) {
    ZhiBrowser *browser = user_data;
    if (browser && browser->status_bar && !browser->status_hover) {
        GtkLabel *label = g_object_get_data(G_OBJECT(browser->status_bar), "label");
        if (label) gtk_label_set_text(label, T(browser, "就绪", "Ready"));
    }
    return G_SOURCE_REMOVE;
}

void zhi_browser_update_status(ZhiBrowser *browser, const gchar *msg) {
    if (browser->status_bar && msg) {
        GtkLabel *label = g_object_get_data(G_OBJECT(browser->status_bar), "label");
        if (label) {
            gchar *safe = sanitize_utf8(msg);
            gtk_label_set_text(label, safe);
            g_free(safe);
        }
        if (browser->status_clear_timer_id) g_source_remove(browser->status_clear_timer_id);
        browser->status_clear_timer_id = g_timeout_add(ZHI_STATUS_CLEAR_MS, on_status_clear, browser);
    }
}

static void zhi_browser_restore_session(ZhiBrowser *browser) {
    if (!zhi_config_get_bool(browser->config, "restore_session", FALSE)) return;
    gchar *path = g_build_filename(g_get_home_dir(), ".config/zhistar", "session.txt", NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) { g_free(path); return; }
    gchar *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        gboolean first = TRUE;
        for (gint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (strlen(line) == 0) continue;
            gchar **parts = g_strsplit(line, "\t", 2);
            const gchar *url = parts[0];
            gboolean pinned = (parts[1] && g_strcmp0(parts[1], "pinned") == 0);
            if (first) {
                ZhiTab *tab = browser->active_tab;
                if (tab) {
                    load_url_in_tab(browser, tab, url);
                    if (pinned) zhi_browser_pin_tab(browser, tab);
                    first = FALSE;
                }
            } else {
                ZhiTab *tab = zhi_browser_add_tab(browser, url);
                if (pinned && tab) zhi_browser_pin_tab(browser, tab);
            }
            g_strfreev(parts);
        }
        g_strfreev(lines);
        g_free(contents);
    }
    g_free(path);
}

ZhiBrowser *zhi_browser_new(void) {
    ZhiBrowser *browser = g_new0(ZhiBrowser, 1);
    browser->config = zhi_config_new();
    zhi_config_load(browser->config);
    browser->theme = zhi_theme_new(browser->config);
    const gchar *lang = zhi_config_get(browser->config, "language", "zh");
    browser->lang = (g_strcmp0(lang, "en") == 0) ? LANG_EN : LANG_ZH;
    browser->pinned_bookmarks = NULL;
    zhi_browser_load_pinned_bookmarks(browser);
    browser->history = NULL;
    zhi_browser_load_history(browser);

    {
        const gchar *home = g_get_home_dir();
        if (home) {
            gchar *rules_path = g_build_filename(home, ".config/zhistar", "adblock.txt", NULL);
            if (g_file_test(rules_path, G_FILE_TEST_IS_REGULAR)) {
                gchar *contents = NULL;
                gsize len = 0;
                if (g_file_get_contents(rules_path, &contents, &len, NULL) && contents) {
                    GString *custom_css = g_string_new(NULL);
                    gchar **lines = g_strsplit(contents, "\n", -1);
                    for (gint i = 0; lines[i]; i++) {
                        gchar *line = g_strstrip(lines[i]);
                        if (strlen(line) == 0 || line[0] == '#' || line[0] == '!') continue;
                        if (g_str_has_prefix(line, "@@")) continue;
                        if (line[0] == '@') continue;
                        if (strchr(line, '{') || strchr(line, '}')) continue;
                        g_string_append_printf(custom_css, "%s{display:none !important;}", line);
                    }
                    g_strfreev(lines);
                    if (custom_css->len > 0)
                        browser->adblock_custom_css = g_string_free(custom_css, FALSE);
                    else
                        g_string_free(custom_css, TRUE);
                    g_free(contents);
                }
            }
            g_free(rules_path);
        }
    }

    browser->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(browser->window), APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(browser->window), 1280, 800);
    gint win_w = zhi_config_get_int(browser->config, "window_width", 1280);
    gint win_h = zhi_config_get_int(browser->config, "window_height", 800);
    gint win_x = zhi_config_get_int(browser->config, "window_x", -1);
    gint win_y = zhi_config_get_int(browser->config, "window_y", -1);
    gtk_window_set_default_size(GTK_WINDOW(browser->window), win_w, win_h);
    if (win_x >= 0 && win_y >= 0)
        gtk_window_move(GTK_WINDOW(browser->window), win_x, win_y);
    else
        gtk_window_set_position(GTK_WINDOW(browser->window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(browser->window), TRUE);
    g_signal_connect(browser->window, "delete-event", G_CALLBACK(on_window_delete), browser);
    g_signal_connect(browser->window, "button-press-event", G_CALLBACK(on_window_button_press), browser);
    g_signal_connect(browser->window, "key-press-event", G_CALLBACK(on_window_key_press), browser);
    browser->main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(browser->window), browser->main_vbox);
    GtkWidget *tab_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(browser->main_vbox), tab_hbox, FALSE, FALSE, 0);
    browser->tab_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(browser->tab_scroll, "tab-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(browser->tab_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    browser->tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(browser->tab_scroll), browser->tab_bar);
    gtk_drag_dest_set(browser->tab_bar, 0, NULL, 0, GDK_ACTION_MOVE);
    g_signal_connect(browser->tab_bar, "drag-motion", G_CALLBACK(on_tab_bar_drag_motion), browser);
    g_signal_connect(browser->tab_bar, "drag-leave", G_CALLBACK(on_tab_bar_drag_leave), browser);
    gtk_box_pack_start(GTK_BOX(tab_hbox), browser->tab_scroll, TRUE, TRUE, 0);
    browser->add_tab_btn = gtk_button_new_with_label("+");
    gtk_widget_set_name(browser->add_tab_btn, "add-tab-btn");
    gtk_widget_set_size_request(browser->add_tab_btn, 36, 32);
    g_signal_connect(browser->add_tab_btn, "clicked", G_CALLBACK(on_new_tab_clicked), browser);
    gtk_box_pack_start(GTK_BOX(tab_hbox), browser->add_tab_btn, FALSE, FALSE, 0);
    browser->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(browser->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(browser->notebook), FALSE);
    gtk_widget_set_hexpand(browser->notebook, TRUE);
    gtk_widget_set_vexpand(browser->notebook, TRUE);
    gtk_box_pack_start(GTK_BOX(browser->main_vbox), browser->notebook, TRUE, TRUE, 0);
    browser->status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(browser->status_bar, "status-bar");
    GtkWidget *status_label = gtk_label_new(T(browser, "就绪", "Ready"));
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(status_label), 120);
    g_object_set_data(G_OBJECT(browser->status_bar), "label", status_label);
    gtk_box_pack_start(GTK_BOX(browser->status_bar), status_label, TRUE, TRUE, 8);
    gtk_box_pack_start(GTK_BOX(browser->main_vbox), browser->status_bar, FALSE, FALSE, 0);
    if (!zhi_config_get_bool(browser->config, "show_status_bar", TRUE))
        gtk_widget_set_visible(browser->status_bar, FALSE);
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(browser->window));
    zhi_theme_apply(browser->theme, screen);
    zhi_browser_add_tab(browser, NULL);
    zhi_browser_restore_session(browser);
    return browser;
}

void zhi_browser_free(ZhiBrowser *browser) {
    if (!browser) return;
    zhi_browser_flush_history(browser);
    if (browser->history_flush_timer) { g_source_remove(browser->history_flush_timer); browser->history_flush_timer = 0; }
    if (browser->status_clear_timer_id) { g_source_remove(browser->status_clear_timer_id); browser->status_clear_timer_id = 0; }
    g_free(browser->adblock_custom_css);
    browser->destroying = TRUE;

    for (GList *l = browser->tabs; l; l = l->next) {
        ZhiTab *t = l->data;
        if (t && t->web_view)
            g_signal_handlers_disconnect_by_data(t->web_view, browser);
    }

    if (zhi_config_get_bool(browser->config, "restore_session", FALSE)) {
        gchar *path = g_build_filename(g_get_home_dir(), ".config/zhistar", "session.txt", NULL);
        FILE *f = fopen(path, "w");
        if (f) {
            for (GList *l = browser->tabs; l; l = l->next) {
                ZhiTab *t = l->data;
                if (t && t->url) fprintf(f, "%s\t%s\n", t->url, t->is_pinned ? "pinned" : "");
            }
            fclose(f);
        }
        g_free(path);
    }
    zhi_browser_save_history(browser);
    zhi_config_save(browser->config);

    for (GList *l = browser->tabs; l; l = l->next) {
        ZhiTab *t = l->data;
        if (!t) continue;
        g_free(t->url);
        g_free(t->title);
        g_list_free_full(t->connected_domains, g_free);
        g_list_free_full(t->nav_history, g_free);
        if (t->download_retry_timer_id) g_source_remove(t->download_retry_timer_id);
        g_free(t);
    }
    g_list_free(browser->tabs);

    zhi_config_free(browser->config);
    zhi_theme_free(browser->theme);
    g_list_free_full(browser->pinned_bookmarks, g_free);
    g_list_free_full(browser->history, g_free);
    g_free(browser);
}

void zhi_browser_show(ZhiBrowser *browser) {
    gtk_widget_show_all(browser->window);
}

void zhi_browser_toggle_fullscreen(ZhiBrowser *browser) {
    GdkWindow *win = gtk_widget_get_window(browser->window);
    GdkWindowState state = gdk_window_get_state(win);
    gboolean entering = !(state & GDK_WINDOW_STATE_FULLSCREEN);
    if (entering)
        gtk_window_fullscreen(GTK_WINDOW(browser->window));
    else
        gtk_window_unfullscreen(GTK_WINDOW(browser->window));
    if (browser->tab_bar) {
        if (entering) gtk_widget_hide(browser->tab_bar);
        else gtk_widget_show_all(browser->tab_bar);
    }
    if (browser->status_bar) {
        if (entering) gtk_widget_hide(browser->status_bar);
        else gtk_widget_show_all(browser->status_bar);
    }
    ZhiTab *tab = browser->active_tab;
    if (tab && tab->nav_box) {
        if (entering) gtk_widget_hide(tab->nav_box);
        else gtk_widget_show_all(tab->nav_box);
    }
}

void zhi_browser_print_page(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    WebKitPrintOperation *op = webkit_print_operation_new(WEBKIT_WEB_VIEW(tab->web_view));
    webkit_print_operation_run_dialog(op, GTK_WINDOW(browser->window));
    g_object_unref(op);
}

static void on_save_page_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source;
    (void)user_data;
    GError *err = NULL;
    webkit_web_view_save_to_file_finish(WEBKIT_WEB_VIEW(source), res, &err);
    if (err) {
        g_printerr("save error: %s\n", err->message);
        g_error_free(err);
    }
}

void zhi_browser_save_page(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    const gchar *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(tab->web_view));
    const gchar *name = title ? title : "page";
    gchar *safe = sanitize_utf8(name);
    gchar *fname = g_strdup_printf("%s.mhtml", safe);
    g_free(safe);

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        T(browser, "另存为", "Save As"),
        GTK_WINDOW(browser->window), GTK_FILE_CHOOSER_ACTION_SAVE,
        T(browser, "保存", "Save"), T(browser, "取消", "Cancel"));
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.mhtml");
    gtk_file_filter_set_name(filter, "MHTML Files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(native), fname);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        webkit_web_view_save_to_file(WEBKIT_WEB_VIEW(tab->web_view), file,
            WEBKIT_SAVE_MODE_MHTML, NULL, on_save_page_response, browser);
        g_object_unref(file);
    }
    g_object_unref(native);
    g_free(fname);
}

void zhi_browser_add_closed_tab(ZhiBrowser *browser, const gchar *url, const gchar *title) {
    if (!url || g_str_has_prefix(url, "about:")) return;
    gchar *entry = g_strdup_printf("%s\x1F%s", url, title ? title : url);
    browser->closed_tabs = g_list_prepend(browser->closed_tabs, entry);
    if (g_list_length(browser->closed_tabs) > 50) {
        GList *last = g_list_last(browser->closed_tabs);
        g_free(last->data);
        browser->closed_tabs = g_list_delete_link(browser->closed_tabs, last);
    }
}

void zhi_browser_reopen_closed_tab(ZhiBrowser *browser) {
    if (!browser->closed_tabs) return;
    gchar *entry = browser->closed_tabs->data;
    browser->closed_tabs = g_list_delete_link(browser->closed_tabs, browser->closed_tabs);
    gchar **parts = g_strsplit(entry, "\x1F", 2);
    const gchar *url = parts[0];
    const gchar *title = parts[1];
    ZhiTab *tab = zhi_browser_add_tab(browser, url);
    if (tab && title) {
        g_free(tab->title);
        tab->title = sanitize_utf8(title);
        tab->custom_title = TRUE;
        update_tab_bar(browser);
    }
    g_strfreev(parts);
    g_free(entry);
}

void zhi_browser_zoom_in(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    tab->zoom_level = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(tab->web_view));
    tab->zoom_level = MIN(tab->zoom_level + 0.1, 3.0);
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(tab->web_view), tab->zoom_level);
}

void zhi_browser_zoom_out(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    tab->zoom_level = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(tab->web_view));
    tab->zoom_level = MAX(tab->zoom_level - 0.1, 0.3);
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(tab->web_view), tab->zoom_level);
}

void zhi_browser_zoom_reset(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    tab->zoom_level = 1.0;
    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(tab->web_view), 1.0);
}

void zhi_browser_copy_page_link(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !tab->url) return;
    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, tab->url, -1);
    zhi_browser_update_status(browser, T(browser, "链接已复制", "Link copied"));
}

static void on_page_info_response(GtkDialog *dialog, gint response, gpointer user_data) {
    (void)response;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void zhi_browser_show_page_info(ZhiBrowser *browser) {
    ZhiTab *tab = browser->active_tab;
    if (!tab || !WEBKIT_IS_WEB_VIEW(tab->web_view)) return;
    const gchar *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(tab->web_view));
    const gchar *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(tab->web_view));
    gboolean is_ssl = uri && g_str_has_prefix(uri, "https://");
    const gchar *security = is_ssl ?
        T(browser, "安全连接 (HTTPS)", "Secure connection (HTTPS)") :
        T(browser, "不安全连接 (HTTP)", "Insecure connection (HTTP)");

    gchar *msg = g_strdup_printf(
        "%s: %s\n%s: %s\n%s: %s",
        T(browser, "地址", "URL"), uri ? uri : "",
        T(browser, "标题", "Title"), title ? title : "",
        T(browser, "安全状态", "Security"), security);

    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(browser->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dlg), T(browser, "页面信息", "Page Info"));
    g_signal_connect(dlg, "response", G_CALLBACK(on_page_info_response), NULL);
    gtk_widget_show_all(dlg);
    g_free(msg);
}
