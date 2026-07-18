#include <gtk/gtk.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

#define CONFIG_DIR ".config/livepaper"
#define CONFIG_FILE ".config/livepaper/config.ini"
#define THUMB_DIR ".cache/livepaper/thumbnails"

GtkWidget *wallpaper_grid;
GtkWidget *monitor_combo;
GtkWidget *status_label;

char selected_wallpaper[PATH_MAX] = "";

static char *make_home_path(const char *relative)
{
    static char path[PATH_MAX];
    const char *home = getenv("HOME");

    snprintf(path, sizeof(path), "%s/%s", home, relative);
    return path;
}

static const char *get_wallpaper_dir(void)
{
    static char path[PATH_MAX];
    const char *videos_dir = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    const char *home = getenv("HOME");

    if (videos_dir && *videos_dir && g_strcmp0(videos_dir, home) != 0)
        snprintf(path, sizeof(path), "%s/Livepaper", videos_dir);
    else
        snprintf(path, sizeof(path), "%s/Videos/Livepaper", home);

    return path;
}

static int is_video_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;

    return g_ascii_strcasecmp(ext, ".mp4") == 0 ||
           g_ascii_strcasecmp(ext, ".mkv") == 0 ||
           g_ascii_strcasecmp(ext, ".webm") == 0 ||
           g_ascii_strcasecmp(ext, ".mov") == 0 ||
           g_ascii_strcasecmp(ext, ".avi") == 0;
}

static void load_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        ".path-bar {"
        "  border: 1px solid alpha(currentColor, 0.25);"
        "  border-radius: 8px;"
        "  padding: 8px;"
        "}"
        ".button-refresh,"
        ".button-apply,"
        ".button-stop {"
        "  min-height: 38px;"
        "  min-width: 92px;"
        "  padding: 8px 16px;"
        "}"
        ".button-refresh {"
        "  background: #38bdf8;"
        "  color: #082f49;"
        "}"
        ".button-refresh:hover {"
        "  background: #7dd3fc;"
        "}"
        ".button-apply {"
        "  background: #22c55e;"
        "  color: #052e16;"
        "}"
        ".button-apply:hover {"
        "  background: #4ade80;"
        "}"
        ".button-stop {"
        "  background: #ef4444;"
        "  color: #450a0a;"
        "}"
        ".button-stop:hover {"
        "  background: #f87171;"
        "}";

    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static void set_status(const char *text)
{
    gtk_label_set_text(GTK_LABEL(status_label), text);
}

static int run_command(const char *cmd)
{
    int ret = system(cmd);

    if (ret == 0)
    {
        set_status("Done.");
        return 1;
    }

    set_status("Command failed.");
    return 0;
}

static char *get_livepaper_command(void)
{
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);

    if (len > 0)
    {
        exe_path[len] = '\0';

        char *exe_dir = g_path_get_dirname(exe_path);
        char *candidate = g_build_filename(exe_dir, "livepaper", NULL);

        if (access(candidate, X_OK) == 0)
        {
            char *quoted = g_shell_quote(candidate);
            g_free(candidate);
            g_free(exe_dir);
            return quoted;
        }

        g_free(candidate);
        g_free(exe_dir);
    }

    return g_strdup("livepaper");
}

static void create_thumbnail(const char *video_path, char *thumb_path, size_t size)
{
    char safe_name[PATH_MAX];
    snprintf(safe_name, sizeof(safe_name), "%s", video_path);

    for (int i = 0; safe_name[i]; i++)
    {
        if (safe_name[i] == '/')
            safe_name[i] = '_';
    }

    char *thumb_name = g_strdup_printf("%s.jpg", safe_name);
    char *built_thumb_path = g_build_filename(make_home_path(THUMB_DIR), thumb_name, NULL);
    g_strlcpy(thumb_path, built_thumb_path, size);
    g_free(thumb_name);
    g_free(built_thumb_path);

    if (strlen(thumb_path) >= size - 1)
        return;

    if (access(thumb_path, F_OK) == 0)
        return;

    char cmd[PATH_MAX * 3];
    char *quoted_video = g_shell_quote(video_path);
    char *quoted_thumb = g_shell_quote(thumb_path);

    snprintf(
        cmd,
        sizeof(cmd),
        "ffmpegthumbnailer -i %s -o %s -s 220 -q 8 >/dev/null 2>&1",
        quoted_video,
        quoted_thumb
    );

    if (system(cmd) != 0)
    {
        g_free(quoted_video);
        g_free(quoted_thumb);
        return;
    }

    g_free(quoted_video);
    g_free(quoted_thumb);
}

static void clear_grid(void)
{
    GtkWidget *child = gtk_widget_get_first_child(wallpaper_grid);

    while (child)
    {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_grid_remove(GTK_GRID(wallpaper_grid), child);
        child = next;
    }
}

static void on_wallpaper_clicked(GtkButton *button, gpointer data)
{
    (void)data;

    const char *path = g_object_get_data(G_OBJECT(button), "wallpaper-path");

    if (!path)
        return;

    strncpy(selected_wallpaper, path, sizeof(selected_wallpaper) - 1);
    selected_wallpaper[sizeof(selected_wallpaper) - 1] = '\0';

    set_status(selected_wallpaper);
}

static GtkWidget *create_wallpaper_card(const char *file_name, const char *full_path)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    gtk_widget_set_size_request(button, 180, 150);

    char thumb_path[PATH_MAX];
    create_thumbnail(full_path, thumb_path, sizeof(thumb_path));

    GtkWidget *picture;

    if (access(thumb_path, F_OK) == 0)
        picture = gtk_picture_new_for_filename(thumb_path);
    else
        picture = gtk_picture_new();

    gtk_widget_set_size_request(picture, 160, 90);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);

    GtkWidget *label = gtk_label_new(file_name);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(label, 160, -1);

    gtk_box_append(GTK_BOX(box), picture);
    gtk_box_append(GTK_BOX(box), label);

    gtk_button_set_child(GTK_BUTTON(button), box);

    g_object_set_data_full(
        G_OBJECT(button),
        "wallpaper-path",
        g_strdup(full_path),
        g_free
    );

    g_signal_connect(button, "clicked", G_CALLBACK(on_wallpaper_clicked), NULL);

    return button;
}

static void refresh_wallpapers(void)
{
    clear_grid();

    const char *wallpaper_dir = get_wallpaper_dir();

    g_mkdir_with_parents(wallpaper_dir, 0755);
    g_mkdir_with_parents(make_home_path(THUMB_DIR), 0755);

    char folder[PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", wallpaper_dir);

    DIR *dir = opendir(folder);

    if (!dir)
    {
        set_status("Cannot open wallpaper folder.");
        return;
    }

    struct dirent *entry;
    int index = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (!is_video_file(entry->d_name))
            continue;

        char full_path[PATH_MAX];
        char *built_path = g_build_filename(folder, entry->d_name, NULL);
        g_strlcpy(full_path, built_path, sizeof(full_path));
        g_free(built_path);

        if (strlen(full_path) >= sizeof(full_path) - 1)
            continue;

        GtkWidget *card = create_wallpaper_card(entry->d_name, full_path);

        int col = index % 3;
        int row = index / 3;

        gtk_grid_attach(GTK_GRID(wallpaper_grid), card, col, row, 1, 1);

        index++;
    }

    closedir(dir);

    if (index == 0)
        set_status("No videos found in the Livepaper video folder.");
    else
        set_status("Wallpapers loaded.");
}

static void refresh_monitors(void)
{
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(monitor_combo));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(monitor_combo), "all");

    char *livepaper_cmd = get_livepaper_command();
    char *cmd = g_strdup_printf("%s monitors", livepaper_cmd);
    FILE *fp = popen(cmd, "r");

    if (!fp)
    {
        g_free(cmd);
        g_free(livepaper_cmd);
        gtk_combo_box_set_active(GTK_COMBO_BOX(monitor_combo), 0);
        return;
    }

    char line[512];

    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = 0;

        if (strlen(line) == 0)
            continue;

        if (strcmp(line, "all") == 0)
            continue;

        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(monitor_combo), line);
    }

    pclose(fp);
    g_free(cmd);
    g_free(livepaper_cmd);

    gtk_combo_box_set_active(GTK_COMBO_BOX(monitor_combo), 0);
}

static void on_apply_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (strlen(selected_wallpaper) == 0)
    {
        set_status("Select wallpaper first.");
        return;
    }

    char *monitor = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(monitor_combo));

    if (!monitor)
        monitor = g_strdup("all");

    char *livepaper_cmd = get_livepaper_command();
    char *quoted_wallpaper = g_shell_quote(selected_wallpaper);
    char *quoted_monitor = g_shell_quote(monitor);
    char *cmd = g_strdup_printf(
        "%s apply %s %s 0 && %s stop && %s start",
        livepaper_cmd,
        quoted_wallpaper,
        quoted_monitor,
        livepaper_cmd,
        livepaper_cmd
    );

    if (run_command(cmd))
        set_status("Wallpaper applied and started.");

    g_free(cmd);
    g_free(livepaper_cmd);
    g_free(quoted_wallpaper);
    g_free(quoted_monitor);
    g_free(monitor);
}

static void on_stop_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    char *monitor = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(monitor_combo));

    if (!monitor)
        monitor = g_strdup("all");

    char *livepaper_cmd = get_livepaper_command();
    char *quoted_monitor = g_shell_quote(monitor);
    char *cmd = g_strdup_printf("%s stop %s", livepaper_cmd, quoted_monitor);

    if (run_command(cmd))
        set_status("Wallpaper stopped.");

    g_free(cmd);
    g_free(livepaper_cmd);
    g_free(quoted_monitor);
    g_free(monitor);
}

static void on_refresh_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    refresh_wallpapers();
    refresh_monitors();
}

static void app_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    load_css();

    GtkWidget *window = gtk_application_window_new(app);

    gtk_window_set_title(GTK_WINDOW(window), "Livepaper");
    gtk_window_set_icon_name(GTK_WINDOW(window), "livepaper");
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 620);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(main_box, 16);
    gtk_widget_set_margin_bottom(main_box, 16);
    gtk_widget_set_margin_start(main_box, 16);
    gtk_widget_set_margin_end(main_box, 16);

    GtkWidget *title = gtk_label_new("Livepaper");
    gtk_widget_add_css_class(title, "title-1");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_box), title);

    GtkWidget *folder_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *folder_label = gtk_label_new(get_wallpaper_dir());
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");

    gtk_widget_add_css_class(folder_box, "path-bar");
    gtk_widget_set_halign(folder_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(folder_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(folder_label), PANGO_ELLIPSIZE_MIDDLE);

    gtk_widget_add_css_class(refresh_button, "button-refresh");

    gtk_box_append(GTK_BOX(folder_box), folder_label);
    gtk_box_append(GTK_BOX(folder_box), refresh_button);
    gtk_box_append(GTK_BOX(main_box), folder_box);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    wallpaper_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(wallpaper_grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(wallpaper_grid), 12);
    gtk_widget_set_margin_top(wallpaper_grid, 8);
    gtk_widget_set_margin_bottom(wallpaper_grid, 8);
    gtk_widget_set_margin_start(wallpaper_grid, 8);
    gtk_widget_set_margin_end(wallpaper_grid, 8);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), wallpaper_grid);
    gtk_box_append(GTK_BOX(main_box), scrolled);

    GtkWidget *settings_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(settings_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(settings_grid), 12);

    GtkWidget *monitor_label = gtk_label_new("Monitor:");
    gtk_widget_set_halign(monitor_label, GTK_ALIGN_START);

    monitor_combo = gtk_combo_box_text_new();

    gtk_grid_attach(GTK_GRID(settings_grid), monitor_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(settings_grid), monitor_combo, 1, 0, 1, 1);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *apply_button = gtk_button_new_with_label("Apply");
    GtkWidget *stop_button = gtk_button_new_with_label("Stop");
    GtkWidget *button_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_widget_set_hexpand(button_spacer, TRUE);
    gtk_widget_set_halign(settings_grid, GTK_ALIGN_END);
    gtk_widget_set_halign(apply_button, GTK_ALIGN_END);
    gtk_widget_set_halign(stop_button, GTK_ALIGN_START);

    gtk_widget_add_css_class(apply_button, "button-apply");
    gtk_widget_add_css_class(stop_button, "button-stop");

    gtk_box_append(GTK_BOX(main_box), settings_grid);
    gtk_box_append(GTK_BOX(button_box), stop_button);
    gtk_box_append(GTK_BOX(button_box), button_spacer);
    gtk_box_append(GTK_BOX(button_box), apply_button);

    gtk_box_append(GTK_BOX(main_box), button_box);

    status_label = gtk_label_new("Ready.");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_box), status_label);

    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(on_stop_clicked), NULL);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), NULL);

    gtk_window_set_child(GTK_WINDOW(window), main_box);

    refresh_wallpapers();
    refresh_monitors();

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
    g_set_prgname("livepaper");
    g_set_application_name("Livepaper");

    GtkApplication *app = gtk_application_new(
        "org.livepaper.gui",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    return status;
}
