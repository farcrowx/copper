#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static GtkWidget *treeview, *liststore, *window, *search_entry;
static gchar *current_dir = NULL;
static gboolean show_hidden = FALSE;
static gboolean list_view = TRUE;

enum { COL_ICON, COL_NAME, COL_SIZE, COL_TYPE, COL_PATH, N_COLS };

static const char *get_icon(const char *name, gboolean is_dir) {
    if (is_dir) return "folder";
    const char *ext = strrchr(name, '.');
    if (!ext) return "text-x-generic";
    if (g_str_has_suffix(ext, ".png") || g_str_has_suffix(ext, ".jpg")) return "image-x-generic";
    if (g_str_has_suffix(ext, ".mp3") || g_str_has_suffix(ext, ".ogg")) return "audio-x-generic";
    if (g_str_has_suffix(ext, ".mp4") || g_str_has_suffix(ext, ".mkv")) return "video-x-generic";
    return "text-x-generic";
}

static void refresh_list(void) {
    gtk_list_store_clear(GTK_LIST_STORE(liststore));
    GDir *dir = g_dir_open(current_dir, 0, NULL);
    if (!dir) return;
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        if (!show_hidden && name[0] == '.') continue;
        gchar *path = g_build_filename(current_dir, name, NULL);
        struct stat st;
        if (lstat(path, &st) != 0) { g_free(path); continue; }
        gboolean is_dir = S_ISDIR(st.st_mode);
        gchar *size = g_format_size(st.st_size);
        GtkTreeIter iter;
        gtk_list_store_append(GTK_LIST_STORE(liststore), &iter);
        gtk_list_store_set(GTK_LIST_STORE(liststore), &iter,
            COL_ICON, get_icon(name, is_dir), COL_NAME, name,
            COL_SIZE, is_dir ? "" : size, COL_TYPE, is_dir ? "Directory" : "File",
            COL_PATH, path, -1);
        g_free(size); g_free(path);
    }
    g_dir_close(dir);
}

static void on_open(GtkWidget *w, gpointer d) {
    GtkTreeIter iter; GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if (!gtk_tree_selection_get_selected(sel, NULL, &iter)) return;
    gchar *path; gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, COL_PATH, &path, -1);
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        g_free(current_dir); current_dir = g_strdup(path); refresh_list();
    } else {
        gchar *cmd = g_strdup_printf("xdg-open '%s' &", path);
        system(cmd); g_free(cmd);
    }
    g_free(path);
}

static void on_new_folder(GtkWidget *w, gpointer d) {
    gchar *path = g_build_filename(current_dir, "New Folder", NULL);
    mkdir(path, 0755); g_free(path); refresh_list();
}

static void on_delete(GtkWidget *w, gpointer d) {
    GtkTreeIter iter; GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if (!gtk_tree_selection_get_selected(sel, NULL, &iter)) return;
    gchar *path; gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, COL_PATH, &path, -1);
    gchar *cmd = g_strdup_printf("rm -rf '%s'", path);
    system(cmd); g_free(cmd); g_free(path); refresh_list();
}

static void on_rename(GtkWidget *w, gpointer d) {
    GtkTreeIter iter; GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if (!gtk_tree_selection_get_selected(sel, NULL, &iter)) return;
    gchar *old_path; gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, COL_PATH, &old_path, -1);
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Rename", GTK_WINDOW(window),
        GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), g_path_get_basename(old_path));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), entry, TRUE, TRUE, 5);
    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        gchar *new_path = g_build_filename(current_dir, gtk_entry_get_text(GTK_ENTRY(entry)), NULL);
        rename(old_path, new_path); g_free(new_path); refresh_list();
    }
    gtk_widget_destroy(dlg); g_free(old_path);
}

static void on_copy(GtkWidget *w, gpointer d) {
    GtkTreeIter iter; GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if (!gtk_tree_selection_get_selected(sel, NULL, &iter)) return;
    gchar *path; gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, COL_PATH, &path, -1);
    gchar *cmd = g_strdup_printf("cp -r '%s' '%s.bak'", path, path);
    system(cmd); g_free(cmd); g_free(path); refresh_list();
}

static void on_move(GtkWidget *w, gpointer d) {
    GtkTreeIter iter; GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if (!gtk_tree_selection_get_selected(sel, NULL, &iter)) return;
    gchar *path; gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, COL_PATH, &path, -1);
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Move to...", GTK_WINDOW(window),
        GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), current_dir);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), entry, TRUE, TRUE, 5);
    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        gchar *dest = g_build_filename(gtk_entry_get_text(GTK_ENTRY(entry)), g_path_get_basename(path), NULL);
        rename(path, dest); g_free(dest); refresh_list();
    }
    gtk_widget_destroy(dlg); g_free(path);
}

static void on_properties(GtkWidget *w, gpointer d) {
    GtkTreeIter iter; GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if (!gtk_tree_selection_get_selected(sel, NULL, &iter)) return;
    gchar *path, *name; gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, COL_PATH, &path, COL_NAME, &name, -1);
    struct stat st; lstat(path, &st);
    gchar *info = g_strdup_printf("Name: %s\nPath: %s\nSize: %ld bytes\nType: %s",
        name, path, (long)st.st_size, S_ISDIR(st.st_mode) ? "Directory" : "File");
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", info);
    gtk_dialog_run(GTK_DIALOG(dlg)); gtk_widget_destroy(dlg);
    g_free(info); g_free(path); g_free(name);
}

static void on_search(GtkWidget *w, gpointer d) {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (!text || !*text) { refresh_list(); return; }
    gtk_list_store_clear(GTK_LIST_STORE(liststore));
    GDir *dir = g_dir_open(current_dir, 0, NULL);
    if (!dir) return;
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        if (!show_hidden && name[0] == '.') continue;
        if (!g_strrstr(name, text)) continue;
        gchar *path = g_build_filename(current_dir, name, NULL);
        struct stat st; if (lstat(path, &st) != 0) { g_free(path); continue; }
        gboolean is_dir = S_ISDIR(st.st_mode);
        GtkTreeIter iter; gtk_list_store_append(GTK_LIST_STORE(liststore), &iter);
        gtk_list_store_set(GTK_LIST_STORE(liststore), &iter,
            COL_ICON, get_icon(name, is_dir), COL_NAME, name,
            COL_SIZE, is_dir ? "" : g_format_size(st.st_size),
            COL_TYPE, is_dir ? "Directory" : "File", COL_PATH, path, -1);
        g_free(path);
    }
    g_dir_close(dir);
}

static void on_toggle_hidden(GtkWidget *w, gpointer d) {
    show_hidden = !show_hidden; refresh_list();
}

static void on_toggle_view(GtkWidget *w, gpointer d) {
    list_view = !list_view;
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), NULL);
    if (list_view) {
        gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(liststore));
    }
}

static void on_places_open(GtkPlacesSidebar *ps, GFile *file, gpointer d) {
    gchar *path = g_file_get_path(file);
    if (path) { g_free(current_dir); current_dir = g_strdup(path); refresh_list(); g_free(path); }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    current_dir = g_strdup(g_get_home_dir());
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Copper Files");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    GtkWidget *sidebar = gtk_places_sidebar_new();
    g_signal_connect(sidebar, "open-location", G_CALLBACK(on_places_open), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, FALSE, 0);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 5);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    const char *btn_labels[] = {"Open", "New Folder", "Rename", "Delete", "Copy", "Move", "Properties"};
    GCallback handlers[] = {G_CALLBACK(on_open), G_CALLBACK(on_new_folder), G_CALLBACK(on_rename),
        G_CALLBACK(on_delete), G_CALLBACK(on_copy), G_CALLBACK(on_move), G_CALLBACK(on_properties)};
    for (int i = 0; i < 7; i++) {
        GtkWidget *btn = gtk_button_new_with_label(btn_labels[i]);
        g_signal_connect(btn, "clicked", handlers[i], NULL);
        gtk_box_pack_start(GTK_BOX(toolbar), btn, FALSE, FALSE, 0);
    }

    GtkWidget *hidden_btn = gtk_toggle_button_new_with_label("Hidden");
    g_signal_connect(hidden_btn, "toggled", G_CALLBACK(on_toggle_hidden), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), hidden_btn, FALSE, FALSE, 0);

    GtkWidget *view_btn = gtk_button_new_with_label("Toggle View");
    g_signal_connect(view_btn, "clicked", G_CALLBACK(on_toggle_view), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), view_btn, FALSE, FALSE, 0);

    search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search...");
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), search_entry, TRUE, TRUE, 0);

    liststore = gtk_list_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(liststore));
    const char *titles[] = {"", "Name", "Size", "Type"};
    for (int i = 0; i < 4; i++) {
        GtkCellRenderer *renderer = (i == 0) ? gtk_cell_renderer_pixbuf_new() : gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titles[i], renderer,
            "icon-name", i == 0 ? COL_ICON : (i == 1 ? COL_NAME : (i == 2 ? COL_SIZE : COL_TYPE)), NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    }
    g_signal_connect(treeview, "row-activated", G_CALLBACK(on_open), NULL);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), treeview);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    refresh_list();
    gtk_widget_show_all(window);
    gtk_main();
    g_free(current_dir);
    return 0;
}
