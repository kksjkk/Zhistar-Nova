#include "browser.h"
#include <string.h>
#include <math.h>

typedef struct {
    gdouble x, y;
    gdouble size;
    gdouble brightness;
    gdouble twinkle_speed;
    gdouble twinkle_phase;
} Star;

typedef struct {
    Star *stars;
    gint  num_stars;
    gint  width, height;
    guint timer_id;
    gboolean destroyed;
} StarfieldData;

static void init_stars(StarfieldData *sf, gint count) {
    sf->num_stars = count;
    sf->stars = g_new0(Star, count);
    for (int i = 0; i < count; i++) {
        sf->stars[i].x = g_random_double();
        sf->stars[i].y = g_random_double();
        sf->stars[i].size = g_random_double_range(0.3, 2.0);
        sf->stars[i].brightness = g_random_double_range(0.3, 1.0);
        sf->stars[i].twinkle_speed = g_random_double_range(0.3, 2.0);
        sf->stars[i].twinkle_phase = g_random_double() * G_PI * 2;
    }
}

static gboolean on_starfield_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    StarfieldData *sf = g_object_get_data(G_OBJECT(widget), "starfield");
    if (!sf || sf->destroyed) return FALSE;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    sf->width = alloc.width;
    sf->height = alloc.height;
    cairo_set_source_rgba(cr, 0.08, 0.08, 0.14, 1.0);
    cairo_paint(cr);
    gdouble time_val = g_get_monotonic_time() / 1000000.0;
    for (int i = 0; i < sf->num_stars; i++) {
        Star *s = &sf->stars[i];
        gdouble twinkle = 0.5 + 0.5 * sin(time_val * s->twinkle_speed + s->twinkle_phase);
        gdouble alpha = s->brightness * (0.4 + 0.6 * twinkle);
        gdouble px = s->x * sf->width;
        gdouble py = s->y * sf->height;
        cairo_set_source_rgba(cr, 0.85, 0.92, 1.0, alpha);
        cairo_arc(cr, px, py, s->size, 0, G_PI * 2);
        cairo_fill(cr);
    }
    return TRUE;
}

static gboolean on_starfield_timeout(GtkWidget *widget) {
    StarfieldData *sf = g_object_get_data(G_OBJECT(widget), "starfield");
    if (!sf || sf->destroyed) return G_SOURCE_REMOVE;
    if (gtk_widget_get_mapped(widget))
        gtk_widget_queue_draw(widget);
    return G_SOURCE_CONTINUE;
}

static void on_starfield_destroy(GtkWidget *widget, gpointer data) {
    (void)data;
    StarfieldData *sf = g_object_get_data(G_OBJECT(widget), "starfield");
    if (sf) {
        sf->destroyed = TRUE;
        if (sf->timer_id > 0) {
            g_source_remove(sf->timer_id);
            sf->timer_id = 0;
        }
    }
}

static void starfield_data_free(gpointer data) {
    StarfieldData *sf = data;
    if (sf) {
        if (sf->timer_id > 0) g_source_remove(sf->timer_id);
        g_free(sf->stars);
        g_free(sf);
    }
}

static void on_starfield_map(GtkWidget *widget, gpointer data) {
    (void)data;
    StarfieldData *sf = g_object_get_data(G_OBJECT(widget), "starfield");
    if (sf && !sf->destroyed && sf->timer_id == 0)
        sf->timer_id = g_timeout_add(66, (GSourceFunc)on_starfield_timeout, widget);
}

static void on_starfield_unmap(GtkWidget *widget, gpointer data) {
    (void)data;
    StarfieldData *sf = g_object_get_data(G_OBJECT(widget), "starfield");
    if (sf && sf->timer_id > 0) {
        g_source_remove(sf->timer_id);
        sf->timer_id = 0;
    }
}

static GtkWidget *create_starfield(void) {
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    StarfieldData *sf = g_new0(StarfieldData, 1);
    sf->destroyed = FALSE;
    sf->timer_id = 0;
    init_stars(sf, 100);
    g_object_set_data_full(G_OBJECT(area), "starfield", sf, starfield_data_free);
    g_signal_connect(area, "draw", G_CALLBACK(on_starfield_draw), NULL);
    sf->timer_id = g_timeout_add(ZHI_STARFIELD_INTERVAL_MS, (GSourceFunc)on_starfield_timeout, area);
    g_signal_connect(area, "map", G_CALLBACK(on_starfield_map), NULL);
    g_signal_connect(area, "unmap", G_CALLBACK(on_starfield_unmap), NULL);
    g_signal_connect(area, "destroy", G_CALLBACK(on_starfield_destroy), NULL);
    return area;
}

static void on_newtab_search(GtkEntry *entry, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *text = gtk_entry_get_text(entry);
    if (!text || strlen(text) == 0) return;
    ZhiTab *tab = browser->active_tab;
    if (!tab) return;
    if (g_str_has_prefix(text, "http://") || g_str_has_prefix(text, "https://") ||
        g_str_has_prefix(text, "about:")) {
        zhi_browser_navigate(browser, tab, text);
    } else if (strchr(text, '.') && !strchr(text, ' ')) {
        gchar *url = g_strdup_printf("https://%s", text);
        zhi_browser_navigate(browser, tab, url);
        g_free(url);
    } else {
        const gchar *engine = zhi_config_get(browser->config, "search_engine",
            "https://www.bing.com/search?q=");
        gchar *encoded = g_uri_escape_string(text, NULL, TRUE);
        gchar *url = g_strdup_printf("%s%s", engine, encoded);
        g_free(encoded);
        const gchar *engine_name = "Search";
        if (g_str_has_prefix(engine, "https://www.bing")) engine_name = "Bing";
        else if (g_str_has_prefix(engine, "https://www.google")) engine_name = "Google";
        else if (g_str_has_prefix(engine, "https://duckduckgo")) engine_name = "DuckDuckGo";
        else if (g_str_has_prefix(engine, "https://search.yahoo")) engine_name = "Yahoo";
        else if (g_str_has_prefix(engine, "https://www.baidu")) engine_name = T(browser, "百度", "Baidu");
        zhi_browser_navigate(browser, tab, url);
        zhi_browser_update_tab_title(browser, tab, text, engine_name);
        g_free(url);
    }
}

static void load_logo_image(GtkWidget *image) {
    gchar *logo_path = find_resource("logo.png");
    if (logo_path) {
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(logo_path, 64, 64, TRUE, NULL);
        if (pixbuf) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
            g_object_unref(pixbuf);
        }
        g_free(logo_path);
        return;
    }
    gtk_image_set_from_icon_name(GTK_IMAGE(image), "web-browser-symbolic", GTK_ICON_SIZE_DIALOG);
}

static void on_pinned_bookmark_clicked(GtkButton *btn, gpointer user_data) {
    ZhiBrowser *browser = user_data;
    const gchar *url = g_object_get_data(G_OBJECT(btn), "url");
    if (!url) return;
    ZhiTab *tab = browser->active_tab;
    if (tab) zhi_browser_navigate(browser, tab, url);
}

static gboolean on_pinned_bookmark_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);

static void on_unpin_bookmark(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    ZhiBrowser *browser = user_data;
    const gchar *url = g_object_get_data(G_OBJECT(item), "url");
    if (url) {
        zhi_browser_remove_pinned_bookmark(browser, url);
        zhi_browser_update_status(browser, T(browser, "已取消固定", "Unpinned"));

        GList *tabs = browser->tabs;
        for (GList *l = tabs; l; l = l->next) {
            ZhiTab *tab = l->data;
            if (!tab->is_home) continue;
            GtkWidget *overlay = g_object_get_data(G_OBJECT(tab->content), "newtab-overlay");
            if (!overlay) continue;
            GtkWidget *old_pinned = g_object_get_data(G_OBJECT(overlay), "pinned-box");
            if (old_pinned) {
                gtk_widget_destroy(old_pinned);
                g_object_set_data(G_OBJECT(overlay), "pinned-box", NULL);
            }

            GList *children = gtk_container_get_children(GTK_CONTAINER(overlay));
            GtkWidget *vbox = NULL;
            for (GList *cl = children; cl; cl = cl->next) {
                if (GTK_IS_BOX(cl->data) && gtk_widget_get_halign(cl->data) == GTK_ALIGN_CENTER) {
                    vbox = cl->data;
                    break;
                }
            }
            g_list_free(children);
            if (!vbox) continue;

            GtkWidget *pinned_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_halign(pinned_box, GTK_ALIGN_CENTER);
            gtk_widget_set_margin_top(pinned_box, 16);
            gtk_widget_set_margin_bottom(pinned_box, 8);
            gboolean has_pinned = FALSE;
            for (GList *pl = browser->pinned_bookmarks; pl; pl = pl->next) {
                gchar *entry = pl->data;
                gchar **parts = g_strsplit(entry, "\x1F", 2);
                if (!parts[0]) { g_strfreev(parts); continue; }
                has_pinned = TRUE;
                const gchar *purl = parts[0];
                const gchar *ptitle = parts[1] ? parts[1] : parts[0];
                GtkWidget *item_btn = gtk_button_new();
                GtkStyleContext *ibctx = gtk_widget_get_style_context(item_btn);
                gtk_style_context_add_class(ibctx, "pinned-bookmark-item");
                GtkWidget *item_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                gtk_widget_set_margin_start(item_hbox, 8);
                gtk_widget_set_margin_end(item_hbox, 8);
                gtk_widget_set_margin_top(item_hbox, 4);
                gtk_widget_set_margin_bottom(item_hbox, 4);
                GtkWidget *icon = gtk_image_new_from_icon_name("web-browser-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
                gtk_box_pack_start(GTK_BOX(item_hbox), icon, FALSE, FALSE, 0);
                gchar *safe = sanitize_utf8(ptitle);
                GtkWidget *lbl = gtk_label_new(safe);
                g_free(safe);
                gtk_label_set_max_width_chars(GTK_LABEL(lbl), 15);
                gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
                gtk_box_pack_start(GTK_BOX(item_hbox), lbl, FALSE, FALSE, 0);
                gtk_container_add(GTK_CONTAINER(item_btn), item_hbox);
                g_object_set_data_full(G_OBJECT(item_btn), "url", g_strdup(purl), g_free);
                g_signal_connect(item_btn, "clicked", G_CALLBACK(on_pinned_bookmark_clicked), browser);
                g_signal_connect(item_btn, "button-press-event", G_CALLBACK(on_pinned_bookmark_button_press), browser);
                gtk_box_pack_start(GTK_BOX(pinned_box), item_btn, FALSE, FALSE, 0);
                g_strfreev(parts);
            }
            g_object_set_data(G_OBJECT(overlay), "pinned-box", has_pinned ? pinned_box : NULL);
            if (has_pinned) {

                gtk_box_pack_start(GTK_BOX(vbox), pinned_box, FALSE, FALSE, 0);
                gtk_box_reorder_child(GTK_BOX(vbox), pinned_box, 1);
                gtk_widget_show_all(pinned_box);
            }
        }
    }
}

static gboolean on_pinned_bookmark_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    ZhiBrowser *browser = user_data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        const gchar *url = g_object_get_data(G_OBJECT(widget), "url");
        if (!url) return FALSE;
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *unpin_item = gtk_menu_item_new_with_label(
            T(browser, "取消固定此书签", "Unpin this bookmark"));
        g_object_set_data(G_OBJECT(unpin_item), "url", (gpointer)url);
        g_signal_connect(unpin_item, "activate", G_CALLBACK(on_unpin_bookmark), browser);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), unpin_item);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

GtkWidget *zhi_new_tab_page_new(ZhiBrowser *browser) {
    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_name(overlay, "newtab-page");
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    GtkWidget *stars = create_starfield();
    gtk_widget_set_hexpand(stars, TRUE);
    gtk_widget_set_vexpand(stars, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), stars);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(vbox, TRUE);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    GtkWidget *logo = gtk_image_new();
    gtk_widget_set_name(logo, "newtab-logo");
    load_logo_image(logo);
    gtk_box_pack_start(GTK_BOX(hbox), logo, FALSE, FALSE, 0);
    GtkWidget *title = gtk_label_new(APP_NAME);
    GtkStyleContext *tctx = gtk_widget_get_style_context(title);
    gtk_style_context_add_class(tctx, "newtab-title");
    gtk_box_pack_start(GTK_BOX(hbox), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    GtkWidget *pinned_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(pinned_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(pinned_box, 16);
    gtk_widget_set_margin_bottom(pinned_box, 8);
    gboolean has_pinned = FALSE;
    for (GList *l = browser->pinned_bookmarks; l; l = l->next) {
        gchar *entry = l->data;
        gchar **parts = g_strsplit(entry, "\x1F", 2);
        if (!parts[0]) { g_strfreev(parts); continue; }
        has_pinned = TRUE;
        const gchar *url = parts[0];
        const gchar *title_str = parts[1] ? parts[1] : parts[0];
        GtkWidget *item_btn = gtk_button_new();
        GtkStyleContext *ibctx = gtk_widget_get_style_context(item_btn);
        gtk_style_context_add_class(ibctx, "pinned-bookmark-item");
        GtkWidget *item_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(item_hbox, 8);
        gtk_widget_set_margin_end(item_hbox, 8);
        gtk_widget_set_margin_top(item_hbox, 4);
        gtk_widget_set_margin_bottom(item_hbox, 4);
        GtkWidget *icon = gtk_image_new_from_icon_name("web-browser-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
        gtk_box_pack_start(GTK_BOX(item_hbox), icon, FALSE, FALSE, 0);
        gchar *safe = sanitize_utf8(title_str);
        GtkWidget *lbl = gtk_label_new(safe);
        g_free(safe);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl), 15);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(item_hbox), lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(item_btn), item_hbox);
        g_object_set_data_full(G_OBJECT(item_btn), "url", g_strdup(url), g_free);
        g_signal_connect(item_btn, "clicked", G_CALLBACK(on_pinned_bookmark_clicked), browser);
        g_signal_connect(item_btn, "button-press-event", G_CALLBACK(on_pinned_bookmark_button_press), browser);
        gtk_box_pack_start(GTK_BOX(pinned_box), item_btn, FALSE, FALSE, 0);
        g_strfreev(parts);
    }
    if (has_pinned) {
        gtk_box_pack_start(GTK_BOX(vbox), pinned_box, FALSE, FALSE, 0);
        g_object_set_data(G_OBJECT(overlay), "pinned-box", pinned_box);
    }
    GtkWidget *search_entry = gtk_entry_new();
    gtk_widget_set_name(search_entry, "newtab-search");
    gtk_widget_set_halign(search_entry, GTK_ALIGN_CENTER);
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry),
        T(browser, "搜索或输入网址", "Search or enter URL"));
    gtk_widget_set_size_request(search_entry, 600, -1);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_newtab_search), browser);
    gtk_box_pack_start(GTK_BOX(vbox), search_entry, FALSE, FALSE, 0);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), vbox);
    return overlay;
}
