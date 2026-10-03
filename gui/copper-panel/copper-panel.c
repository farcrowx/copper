/* copper-panel.c — Top panel for Copper Linux (GTK3) */
#include <gtk/gtk.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define PANEL_H 30

static GtkWidget *clock_label;
static GtkWidget *tray_box;

static void launch_launcher(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (system("copper-launcher &") == -1) { /* ignore */ }
}

static gboolean update_clock(gpointer data) {
    (void)data;
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%a %b %d  %H:%M", lt);
    gtk_label_set_text(GTK_LABEL(clock_label), buf);
    return G_SOURCE_CONTINUE;
}

static void power_action(GtkWidget *w, gpointer data) {
    (void)w;
    const char *cmd = (const char *)data;
    if (system(cmd) == -1) { /* ignore */ }
    gtk_main_quit();
}

static void show_power_menu(GtkWidget *w, gpointer data) {
    (void)data;
    GtkWidget *menu = gtk_menu_new();
    struct { const char *label; const char *cmd; } items[] = {
        {"Logout",  "copper-session logout"},
        {"Restart", "copper-power --restart"},
        {"Shutdown","copper-power --shutdown"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(items); i++) {
        GtkWidget *mi = gtk_menu_item_new_with_label(items[i].label);
        g_signal_connect(mi, "activate", G_CALLBACK(power_action),
                         (gpointer)items[i].cmd);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), w,
                             GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void add_tray_icon(GtkWidget *box, const char *icon, const char *tip) {
    GtkWidget *img = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_SMALL_TOOLBAR);
    GtkWidget *btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(btn), img);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, tip);
    gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 2);
}

static void add_task(GtkWidget *box, const char *title) {
    GtkWidget *btn = gtk_button_new_with_label(title);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 4);
}

static void apply_css(void) {
    const char *css =
        "window { background: #2d2d2d; }"
        "button { background: transparent; border: none; color: #eee; }"
        "button:hover { background: #444; }"
        "label { color: #eee; }";
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    apply_css();

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Copper Panel");
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_type_hint(win, GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_keep_above(win, TRUE);
    gtk_window_stick(win);
    gtk_widget_set_size_request(win, -1, PANEL_H);

    GdkScreen *screen = gdk_screen_get_default();
    GdkRectangle mon;
    gdk_screen_get_monitor_geometry(screen, 0, &mon);
    gtk_window_set_default_size(win, mon.width, PANEL_H);
    gtk_window_move(win, mon.x, mon.y);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(win), bar);

    GtkWidget *launcher_btn = gtk_button_new_with_label("⏻ Menu");
    g_signal_connect(launcher_btn, "clicked", G_CALLBACK(launch_launcher), NULL);
    gtk_box_pack_start(GTK_BOX(bar), launcher_btn, FALSE, FALSE, 4);

    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(bar), sep1, FALSE, FALSE, 2);

    GtkWidget *tasks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(bar), tasks, TRUE, TRUE, 0);
    add_task(tasks, "Terminal");
    add_task(tasks, "Files");
    add_task(tasks, "Browser");

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(bar), sep2, FALSE, FALSE, 2);

    tray_box = gtk_box_new GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_end(GTK_BOX(bar), tray_box, FALSE, FALSE, 4);
    add_tray_icon(tray_box, "network-wireless-signal-good-symbolic", "Network");
    add_tray_icon(tray_box, "audio-volume-high-symbolic", "Audio");
    add_tray_icon(tray_box, "battery-good-symbolic", "Battery");

    clock_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(bar), clock_label, FALSE, FALSE, 6);

    GtkWidget *power_btn = gtk_button_new_with_label("⏻");
    g_signal_connect(power_btn, "clicked", G_CALLBACK(show_power_menu), NULL);
    gtk_box_pack_end(GTK_BOX(bar), power_btn, FALSE, FALSE, 4);

    update_clock(NULL);
    g_timeout_add_seconds(30, update_clock, NULL);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}
