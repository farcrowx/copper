#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *window, *battery_label, *battery_bar, *time_label;

static void update_battery(void) {
    gchar *output = NULL;
    GError *err = NULL;
    if (g_spawn_command_line_sync("cat /sys/class/power_supply/BAT0/capacity 2>/dev/null", &output, NULL, NULL, &err) && output) {
        int pct = atoi(output);
        gchar *text = g_strdup_printf("Battery: %d%%", pct);
        gtk_label_set_text(GTK_LABEL(battery_label), text);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(battery_bar), pct / 100.0);
        g_free(text);
        g_free(output);
    } else {
        gtk_label_set_text(GTK_LABEL(battery_label), "Battery: N/A");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(battery_bar), 0);
        if (err) g_error_free(err);
    }

    if (g_spawn_command_line_sync("cat /sys/class/power_supply/BAT0/status 2>/dev/null", &output, NULL, NULL, NULL) && output) {
        g_strstrip(output);
        gchar *text = g_strdup_printf("Status: %s", output);
        gtk_label_set_text(GTK_LABEL(time_label), text);
        g_free(text);
        g_free(output);
    }
}

static void on_suspend(GtkWidget *w, gpointer d) {
    system("echo mem > /sys/power/state 2>/dev/null");
}

static void on_hibernate(GtkWidget *w, gpointer d) {
    system("echo disk > /sys/power/state 2>/dev/null");
}

static void on_shutdown(GtkWidget *w, gpointer d) {
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Are you sure you want to shut down?");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES) {
        system("poweroff");
    }
    gtk_widget_destroy(dlg);
}

static void on_restart(GtkWidget *w, gpointer d) {
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Are you sure you want to restart?");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES) {
        system("reboot");
    }
    gtk_widget_destroy(dlg);
}

static void on_logout(GtkWidget *w, gpointer d) {
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Are you sure you want to log out?");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES) {
        system("copper-session logout");
    }
    gtk_widget_destroy(dlg);
}

static gboolean on_timer(gpointer data) {
    update_battery();
    return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title GTK_WINDOW(window), "Copper Power");
    gtk_window_set_default_size GTK_WINDOW(window), 350, 250);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *icon = gtk_image_new_from_icon_name("battery-good", GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start(GTK_BOX(vbox), icon, FALSE, FALSE, 0);

    battery_label = gtk_label_new("Battery: --%");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_LARGE));
    gtk_label_set_attributes(GTK_LABEL(battery_label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(vbox), battery_label, FALSE, FALSE, 0);

    battery_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(battery_bar), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), battery_bar, FALSE, FALSE, 0);

    time_label = gtk_label_new("Status: --");
    gtk_box_pack_start(GTK_BOX(vbox), time_label, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 10);

    GtkWidget *btn_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(btn_grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(btn_grid), 5);
    gtk_box_pack_start(GTK_BOX(vbox), btn_grid, FALSE, FALSE, 0);

    GtkWidget *suspend_btn = gtk_button_new_with_label("Suspend");
    g_signal_connect(suspend_btn, "clicked", G_CALLBACK(on_suspend), NULL);
    gtk_grid_attach(GTK_GRID(btn_grid), suspend_btn, 0, 0, 1, 1);

    GtkWidget *hibernate_btn = gtk_button_new_with_label("Hibernate");
    g_signal_connect(hibernate_btn, "clicked", G_CALLBACK(on_hibernate), NULL);
    gtk_grid_attach(GTK_GRID(btn_grid), hibernate_btn, 1, 0, 1, 1);

    GtkWidget *restart_btn = gtk_button_new_with_label("Restart");
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_restart), NULL);
    gtk_grid_attach(GTK_GRID(btn_grid), restart_btn, 0, 1, 1, 1);

    GtkWidget *shutdown_btn = gtk_button_new_with_label("Shutdown");
    g_signal_connect(shutdown_btn, "clicked", G_CALLBACK(on_shutdown), NULL);
    gtk_grid_attach(GTK_GRID(btn_grid), shutdown_btn, 1, 1, 1, 1);

    GtkWidget *logout_btn = gtk_button_new_with_label("Log Out");
    g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_logout), NULL);
    gtk_grid_attach(GTK_GRID(btn_grid), logout_btn, 0, 2, 2, 1);

    update_battery();
    g_timeout_add_seconds(30, on_timer, NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
