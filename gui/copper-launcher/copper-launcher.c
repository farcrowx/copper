/* copper-launcher.c — Application launcher for Copper Linux (GTK3) */
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

#define COLS 4
#define MAX_APPS 64

typedef struct {
    const char *name;
    const char *icon;
    const char *cmd;
    const char *cat;
} App;

static App apps[MAX_APPS];
static int app_count = 0;
static GtkWidget *grid;
static GtkWidget *search_entry;
static GtkWidget *cat_box;
static int current_cat = 0; /* 0=All */
static int selected_idx = -1;

static const char *categories[] = {
    "All", "Accessories", "Graphics", "Internet", "Office", "System"
};
#define NUM_CATS 6

static void add_app(const char *name, const char *icon,
                    const char *cmd, const char *cat) {
    if (app_count >= MAX_APPS) return;
    apps[app_count].name = name;
    apps[app_count].icon = icon;
    apps[app_count].cmd = cmd;
    apps[app_count].cat = cat;
    app_count++;
}

static void populate_apps(void) {
    add_app("Terminal", "utilities-terminal", "xterm", "Accessories");
    add_app("Copper Shell", "utilities-terminal", "copper-sh", "Accessories");
    add_app("Files", "system-file-manager", "copper-files", "Accessories");
    add_app("Settings", "preferences-system", "copper-settings", "System");
    add_app("Network", "network-wireless", "copper-network", "System");
    add_app("Power", "battery-good", "copper-power", "System");
    add_app("Copper Charge", "system-software-update", "copper charge", "System");
    add_app("Copper Rollback", "edit-undo", "copper rollback", "System");
}

static void launch_app(GtkWidget *w, gpointer data) {
    (void)w;
    const char *cmd = (const char *)data;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s &", cmd);
    if (system(buf) == -1) { /* ignore */ }
    gtk_main_quit();
}

static void rebuild_grid(void) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(grid));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    const char *query = gtk_entry_get_text(GTK_ENTRY(search_entry));
    int idx = 0;
    for (int i = 0; i < app_count; i++) {
        if (current_cat > 0 && strcmp(apps[i].cat, categories[current_cat]) != 0)
            continue;
        if (query && *query &&
            !strstr(apps[i].name, query))
            continue;

        GtkWidget *btn = gtk_button_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *img = gtk_image_new_from_icon_name(apps[i].icon, GTK_ICON_SIZE_DIALOG);
        GtkWidget *lbl = gtk_label_new(apps[i].name);
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(btn), box);
        gtk_button_set_relief(btn, GTK_RELIEF_NONE);
        g_signal_connect(btn, "clicked", G_CALLBACK(launch_app),
                         (gpointer)apps[i].cmd);

        int row = idx / COLS, col = idx % COLS;
        gtk_grid_attach(GTK_GRID(grid), btn, col, row, 1, 1);
        idx++;
    }
    gtk_widget_show_all(grid);
    selected_idx = -1;
}

static void on_search_changed(GtkEntry *e, gpointer d) {
    (void)e; (void)d;
    rebuild_grid();
}

static void on_cat_clicked(GtkWidget *w, gpointer data) {
    (void)w;
    current_cat = GPOINTER_TO_INT(data);
    rebuild_grid();
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer d) {
    (void)w; (void)d;
    if (ev->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Return && selected_idx >= 0) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(grid));
        GtkWidget *btn = GTK_WIDGET(g_list_nth_data(children, selected_idx));
        if (btn) gtk_button_clicked(GTK_BUTTON(btn));
        g_list_free(children);
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Down || ev->keyval == GDK_KEY_Up ||
        ev->keyval == GDK_KEY_Left || ev->keyval == GDK_KEY_Right) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(grid));
        int n = g_list_length(children);
        if (n == 0) { g_list_free(children); return FALSE; }
        if (selected_idx < 0) selected_idx = 0;
        else {
            if (ev->keyval == GDK_KEY_Down)  selected_idx += COLS;
            if (ev->keyval == GDK_KEY_Up)    selected_idx -= COLS;
            if (ev->keyval == GDK_KEY_RIGHT) selected_idx += 1;
            if (ev->keyval == GDK_KEY_LEFT)  selected_idx -= 1;
            if (selected_idx < 0) selected_idx = 0;
            if (selected_idx >= n) selected_idx = n - 1;
        }
        GtkWidget *btn = GTK_WIDGET(g_list_nth_data(children, selected_idx));
        if (btn) gtk_widget_grab_focus(btn);
        g_list_free(children);
        return TRUE;
    }
    return FALSE;
}

static void apply_css(void) {
    const char *css =
        "window { background: #1e1e1e; }"
        "button { background: #2d2d2d; border: 1px solid #444; "
        "         border-radius: 6px; color: #eee; padding: 8px; }"
        "button:hover { background: #3a3a3a; }"
        "button:focus { background: #4a4a4a; border-color: #666; }"
        "entry { background: #2d2d2d; color: #eee; border: 1px solid #555; "
        "         border-radius: 4px; padding: 6px; }"
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
    populate_apps();

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(win, "Copper Launcher");
    gtk_window_set_decorated(FALSE);
    gtk_window_set_type_hint(win, GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_position(win, GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(win, 640, 420);
    gtk_container_set_border_width(win, 12);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(win, vbox);

    search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(search_entry, "Search applications…");
    g_signal_connect(search_entry, "changed", G_CALLBACK(on_search_changed), NULL);
    gtk_box_pack_start(vbox, search_entry, FALSE, FALSE, 0);

    GtkWidget *hbox = gtk_box_new(HORIZONTAL, 8);
    gtk_box_pack_start(vbox, hbox, TRUE, TRUE, 0);

    cat_box = gtk_box_new(VERTICAL, 4);
    gtk_box_pack_start(hbox, cat_box, FALSE, FALSE, 0);
    for (int i = 0; i < NUM_CATS; i++) {
        GtkWidget *b = gtk_button_new_with_label(categories[i]);
        g_signal_connect(b, "clicked", G_CALLBACK(on_cat_clicked),
                         GINT_TO_POINTER(i));
        gtk_box_pack_start(cat_box, b, FALSE, FALSE, 0);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(hbox, scroll, TRUE, TRUE, 0);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(grid, 8);
    gtk_grid_set_column_spacing(grid, 8);
    gtk_container_add(scroll, grid);

    rebuild_grid();

    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(win);
    gtk_widget_grab_focus(search_entry);
    gtk_main();
    return 0;
}
