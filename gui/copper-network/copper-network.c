#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *window, *status_label, *net_list, *wifi_switch;

static void update_status(void) {
    gboolean wifi_on = gtk_switch_get_active(GTK_SWITCH(wifi_switch));
    if (wifi_on) {
        gtk_label_set_text(GTK_LABEL(status_label), "Connected: HomeNetwork");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "Disconnected");
    }
}

static void on_wifi_toggle(GtkSwitch *sw, gpointer data) {
    update_status();
}

static void on_connect(GtkWidget *btn, gpointer data) {
    gchar *net = (gchar *)data;
    gchar *cmd = g_strdup_printf("nmcli device wifi connect '%s' 2>/dev/null || "
        "nmcli connection up '%s' 2>/dev/null || true", net, net);
    system(cmd);
    g_free(cmd);
    update_status();
}

static void on_disconnect(GtkWidget *btn, gpointer data) {
    system("nmcli networking off 2>/dev/null; sleep 1; nmcli networking on 2>/dev/null");
    update_status();
}

static void on_refresh(GtkWidget *btn, gpointer data) {
    gtk_list_box_select_all(GTK_LIST_BOX(net_list), FALSE);
    GList *children = gtk_container_get_children(GTK_CONTAINER(net_list));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    const char *nets[] = {"HomeNetwork", "OfficeWiFi", "Guest", "CoffeeShop", "NeighborWiFi"};
    for (int i = 0; i < 5; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new(nets[i]), TRUE, TRUE, 0);
        GtkWidget *btn = gtk_button_new_with_label("Connect");
        g_signal_connect(btn, "clicked", G_CALLBACK(on_connect), (gpointer)nets[i]);
        gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(net_list), row, -1);
    }
    gtk_widget_show_all(net_list);
}

static void on_wired_config(GtkWidget *btn, gpointer data) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Wired Connection", GTK_WINDOW(window),
        GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Wired Connection Configuration"), FALSE, FALSE, 0);
    GtkWidget *ip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(ip_box), gtk_label_new("IP Address:"), FALSE, FALSE, 0);
    GtkWidget *ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ip_entry), "192.168.1.100");
    gtk_box_pack_start(GTK_BOX(ip_box), ip_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), ip_box, FALSE, FALSE, 0);
    GtkWidget *gw_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(gw_box), gtk_label_new("Gateway:"), FALSE, FALSE, 0);
    GtkWidget *gw_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gw_entry), "192.168.1.1");
    gtk_box_pack_start(GTK_BOX(gw_box), gw_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), gw_box, FALSE, FALSE, 0);
    GtkWidget *dns_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(dns_box), gtk_label_new("DNS:"), FALSE, FALSE, 0);
    GtkWidget *dns_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(dns_entry), "8.8.8.8");
    gtk_box_pack_start(GTK_BOX(dns_box), dns_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), dns_box, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), box);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title GTK_WINDOW(window), "Copper Network");
    gtk_window_set_default_size GTK_WINDOW(window), 450, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(header), gtk_image_new_from_icon_name("network-wireless", GTK_ICON_SIZE_LARGE_TOOLBAR), FALSE, FALSE, 0);
    status_label = gtk_label_new("Connected: HomeNetwork");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0);
    gtk_box_pack_start(GTK_BOX(header), status_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *wifi_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(wifi_box), gtk_label_new("WiFi:"), FALSE, FALSE, 0);
    wifi_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(wifi_switch), TRUE);
    g_signal_connect(wifi_switch, "notify::active", G_CALLBACK(on_wifi_toggle), NULL);
    gtk_box_pack_start(GTK_BOX(wifi_box), wifi_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), wifi_box, FALSE, FALSE, 0);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), refresh_btn, TRUE, TRUE, 0);
    GtkWidget *disc_btn = gtk_button_new_with_label("Disconnect");
    g_signal_connect(disc_btn, "clicked", G_CALLBACK(on_disconnect), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), disc_btn, TRUE, TRUE, 0);
    GtkWidget *wired_btn = gtk_button_new_with_label("Wired Config");
    g_signal_connect(wired_btn, "clicked", G_CALLBACK(on_wired_config), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), wired_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    net_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(net_list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), net_list);

    on_refresh(NULL, NULL);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
