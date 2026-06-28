#include "autostart.h"

#include "../core/paths.h"

#include <stdio.h>
#include <unistd.h>

void create_autostart(void)
{
    LivepaperPaths *paths = livepaper_paths();
    char exe_path[PATH_BUF];

    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1)
    {
        fprintf(stderr, "Cannot detect executable path.\n");
        return;
    }

    exe_path[len] = '\0';

    FILE *f = fopen(paths->autostart_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot create autostart file.\n");
        return;
    }

    fprintf(
        f,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Livepaper\n"
        "Exec=%s start\n"
        "Hidden=false\n"
        "NoDisplay=false\n"
        "X-GNOME-Autostart-enabled=true\n",
        exe_path
    );

    fclose(f);

    printf("Autostart enabled.\n");
}

void remove_autostart(void)
{
    LivepaperPaths *paths = livepaper_paths();
    remove(paths->autostart_file);
    printf("Autostart disabled.\n");
}
