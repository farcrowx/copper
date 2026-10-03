#include <gtk/gtk.h>
#include <stdlib.h>

static GtkWidget *stack;

static void add_page(const char *name, const char *icon) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *label = gtk_label_new(name);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_LARGE));
    gtk_label_set_attributes(GTK_LABEL(label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(page), label, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, name, name);
}

static void add_appearance(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *theme_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(theme_box), gtk_label_new("Theme:"), FALSE, FALSE, 0);
    GtkWidget *theme = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme), "Light");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme), "Dark");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme), "System");
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme), 2);
    gtk_box_pack_start(GTK_BOX(theme_box), theme, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), theme_box, FALSE, FALSE, 0);
    GtkWidget *wp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(wp_box), gtk_label_new("Wallpaper:"), FALSE, FALSE, 0);
    GtkWidget *wp_btn = gtk_file_chooser_button_new("Select Wallpaper", GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_box_pack_start(GTK_BOX(wp_box), wp_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), wp_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Appearance", "Appearance");
}

static void add_network(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *wifi_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(wifi_box), gtk_label_new("WiFi:"), FALSE, FALSE, 0);
    GtkWidget *wifi_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(wifi_switch), TRUE);
    gtk_box_pack_start(GTK_BOX(wifi_box), wifi_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), wifi_box, FALSE, FALSE, 0);
    GtkWidget *net_list = gtk_list_box_new();
    const char *nets[] = {"HomeNetwork", "OfficeWiFi", "Guest", "CoffeeShop"};
    for (int i = 0; i < 4; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new(nets[i]), TRUE, TRUE, 0);
        GtkWidget *btn = gtk_button_new_with_label("Connect");
        gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(net_list), row, -1);
    }
    gtk_box_pack_start(GTK_BOX(page), net_list, TRUE, TRUE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Network", "Network");
}

static void add_sound(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *vol_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(vol_box), gtk_label_new("Volume:"), FALSE, FALSE, 0);
    GtkWidget *vol = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_range_set_value(GTK_RANGE(vol), 50);
    gtk_box_pack_start(GTK_BOX(vol_box), vol, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), vol_box, FALSE, FALSE, 0);
    GtkWidget *out_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(out_box), gtk_label_new("Output:"), FALSE, FALSE, 0);
    GtkWidget *out = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(out), "Speakers");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(out), "Headphones");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(out), "HDMI");
    gtk_combo_box_set_active(GTK_COMBO_BOX(out), 0);
    gtk_box_pack_start(GTK_BOX(out_box), out, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), out_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Sound", "Sound");
}

static void add_display(void) {
    GtkWidget *page = gtk_box_newGTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *res_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(res_box), gtk_label_new("Resolution:"), FALSE, FALSE, 0);
    GtkWidget *res = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(res), "1920x1080");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(res), "1366x768");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(res), "2560x1440");
    gtk_combo_box_set_active(GTK_COMBO_BOX(res), 0);
    gtk_box_pack_start(GTK_BOX(res_box), res, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), res_box, FALSE, FALSE, 0);
    GtkWidget *rate_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(rate_box), gtk_label_new("Refresh Rate:"), FALSE, FALSE, 0);
    GtkWidget *rate = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rate), "60 Hz");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rate), "75 Hz");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rate), "144 Hz");
    gtk_combo_box_set_active(GTK_COMBO_BOX(rate), 0);
    gtk_box_pack_start(GTK_BOX(rate_box), rate, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), rate_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Display", "Display");
}

static void add_keyboard(void) {
    GtkWidget *page = gtk_box_newGTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *layout_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(layout_box), gtk_label_new("Layout:"), FALSE, FALSE, 0);
    GtkWidget *layout = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(layout), "English (US)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(layout), "English (UK)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(layout), "Spanish");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(layout), "French");
    gtk_combo_box_set_active(GTK_COMBO_BOX(layout), 0);
    gtk_box_pack_start(GTK_BOX(layout_box), layout, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), layout_box, FALSE, FALSE, 0);
    GtkWidget *sc_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(sc_box), gtk_label_new("Shortcuts:"), FALSE, FALSE, 0);
    GtkWidget *sc_btn = gtk_button_new_with_label("Configure...");
    gtk_box_pack_start(GTK_BOX(sc_box), sc_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), sc_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Keyboard", "Keyboard");
}

static void add_mouse(void) {
    GtkWidget *page = gtk_box_newGTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *speed_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(speed_box), gtk_label_new("Speed:"), FALSE, FALSE, 0);
    GtkWidget *speed = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_range_set_value(GTK_RANGE(speed), 50);
    gtk_box_pack_start(GTK_BOX(speed_box), speed, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), speed_box, FALSE, FALSE, 0);
    GtkWidget *hand_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(hand_box), gtk_label_new("Handedness:"), FALSE, FALSE, 0);
    GtkWidget *hand = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(hand), "Right");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(hand), "Left");
    gtk_combo_box_set_active(GTK_COMBO_BOX(hand), 0);
    gtk_box_pack_start(GTK_BOX(hand_box), hand, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), hand_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Mouse", "Mouse");
}

static void add_power(void) {
    GtkWidget *page = gtk_box_newGTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *suspend_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(suspend_box), gtk_label_new("Suspend after:"), FALSE, FALSE, 0);
    GtkWidget *suspend = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(suspend), "Never");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(suspend), "15 minutes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(suspend), "30 minutes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(suspend), "1 hour");
    gtk_combo_box_set_active(GTK_COMBO_BOX(suspend), 1);
    gtk_box_pack_start(GTK_BOX(suspend_box), suspend, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), suspend_box, FALSE, FALSE, 0);
    GtkWidget *off_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(off_box), gtk_label_new("Screen off after:"), FALSE, FALSE, 0);
    GtkWidget *off = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(off), "Never");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(off), "5 minutes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(off), "10 minutes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(off), "30 minutes");
    gtk_combo_box_set_active(GTK_COMBO_BOX(off), 2);
    gtk_box_pack_start(GTK_BOX(off_box), off, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), off_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Power", "Power");
}

static void add_users(void) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *user_list = gtk_list_box_new();
    const char *users[] = {"admin", "guest", "user1"};
    for (int i = 0; i < 3; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new(users[i]), TRUE, TRUE, 0);
        GtkWidget *rm_btn = gtk_button_new_with_label("Remove");
        gtk_box_pack_start(GTK_BOX(row), rm_btn, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(user_list), row, -1);
    }
    gtk_box_pack_start(GTK_BOX(page), user_list, TRUE, TRUE, 0);
    GtkWidget *add_btn = gtk_button_new_with_label("Add User");
    gtk_box_pack_start(GTK_BOX(page), add_btn, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Users", "Users");
}

static void add_datetime(void) {
    GtkWidget *page = gtk_box_newGTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *tz_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(tz_box), gtk_label_new("Timezone:"), FALSE, FALSE, 0);
    GtkWidget *tz = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tz), "UTC");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tz), "America/New_York");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tz), "Europe/London");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tz), "Asia/Tokyo");
    gtk_combo_box_set_active(GTK_COMBO_BOX(tz), 0);
    gtk_box_pack_start(GTK_BOX(tz_box), tz, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), tz_box, FALSE, FALSE, 0);
    GtkWidget *ntp_box = gtk_box_newGTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(ntp_box), gtk_label_new("NTP:"), FALSE, FALSE, 0);
    GtkWidget *ntp_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(ntp_switch), TRUE);
    gtk_box_pack_start(GTK_BOX(ntp_box), ntp_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ntp_box, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "Date & Time", "Date & Time");
}

static void add_about(void) {
    GtkWidget *page = gtk_box_newGTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page), 15);
    GtkWidget *logo = gtk_image_new_from_icon_name("computer", GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start(GTK_BOX(page), logo, FALSE, FALSE, 10);
    GtkWidget *ver = gtk_label_new("Copper Linux 1.0");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_X_LARGE));
    gtk_label_set_attributes(GTK_LABEL(ver), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(page), ver, FALSE, FALSE, 0);
    GtkWidget *desc = gtk_label_new("A lightweight, modern Linux distribution\nBuilt with GTK3 and powered by community.");
    gtk_box_pack_start(GTK_BOX(page), desc, FALSE, FALSE, 10);
    GtkWidget *credits = gtk_label_new("Credits:\n  Development Team\n  Community Contributors\n  Open Source Projects");
    gtk_box_pack_start(GTK_BOX(page), credits, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(stack), page, "About Copper", "About Copper");
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Copper Settings");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 550);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    GtkWidget *sidebar = gtk_stack_sidebar_new();
    gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, FALSE, 0);

    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
    gtk_box_pack_start(GTK_BOX(hbox), stack, TRUE, TRUE, 0);

    add_appearance(); add_network(); add_sound(); add_display();
    add_keyboard(); add_mouse(); add_power(); add_users();
    add_datetime(); add_about();

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
