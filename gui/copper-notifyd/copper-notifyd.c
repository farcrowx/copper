#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

static GtkWidget *window, *box;
static guint32 notification_id = 0;
static GDBusConnection *bus = NULL;

static void dismiss_notification(GtkWidget *b, gpointer data) {
    GtkWidget *bubble = GTK_WIDGET(data);
    gtk_widget_destroy(bubble);
}

static void show_notification(const gchar *app, const gchar *summary, const gchar *body) {
    GtkWidget *bubble = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(bubble), 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(bubble), "notification-bubble");

    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_box_pack_start(GTK_BOX(bubble), icon, FALSE, FALSE, 0);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<b>%s</b>", summary);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    if (body && *body) {
        GtkWidget *body_label = gtk_label_new(body);
        gtk_label_set_xalign(GTK_LABEL(body_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(body_label), TRUE);
        gtk_box_pack_start(GTK_BOX(vbox), body_label, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(bubble), vbox, TRUE, TRUE, 0);

    GtkWidget *close_btn = gtk_button_new_with_label("×");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(dismiss_notification), bubble);
    gtk_box_pack_start(GTK_BOX(bubble), close_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), bubble, FALSE, FALSE, 0);
    gtk_widget_show_all(bubble);

    g_timeout_add_seconds(5, (GSourceFunc)dismiss_notification, bubble);
}

static void on_dbus_signal(GDBusConnection *conn, const gchar *sender, const gchar *path,
    const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
    if (g_strcmp0(signal, "Notify") != 0) return;
    gchar *app = NULL, *summary = NULL, *body = NULL;
    g_variant_get(params, "(susssasa{sv}i)", &app, NULL, NULL, &summary, &body, NULL, NULL, NULL);
    show_notification(app, summary, body);
    g_free(app); g_free(summary); g_free(body);
}

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer data) {
    bus = conn;
    g_dbus_connection_signal_subscribe(conn, "org.freedesktop.Notifications",
        "org.freedesktop.Notifications", "Notify", "/org/freedesktop/Notifications",
        NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_dbus_signal, NULL, NULL);
}

static void on_name_acquired(GDBusConnection *conn, const gchar *name, gpointer data) {}

static void on_name_lost(GDBusConnection *conn, const gchar *name, gpointer data) {
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Copper Notifications");
    gtk_window_set_default_size(GTK_WINDOW(window), 350, 100);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_BOTTOM_RIGHT);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above GTK_WINDOW(window), TRUE);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(window), scrolled);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(scrolled), box);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".notification-bubble { background: #2d2d2d; border-radius: 8px; color: white; }"
        ".notification-bubble label { color: white; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
        G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, on_name_acquired,
        on_name_lost, NULL, NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
