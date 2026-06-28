#include "paths.h"

#include "../utils/string_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static LivepaperPaths g_paths;

LivepaperPaths *livepaper_paths(void)
{
    return &g_paths;
}

void build_paths(void)
{
    const char *home = getenv("HOME");

    if (!home || strlen(home) == 0)
    {
        fprintf(stderr, "HOME is not set.\n");
        exit(1);
    }

    if (!safe_snprintf(g_paths.config_dir, sizeof(g_paths.config_dir), "%s/.config/livepaper", home) ||
        !safe_snprintf(g_paths.config_file, sizeof(g_paths.config_file), "%s/config.ini", g_paths.config_dir) ||
        !safe_snprintf(g_paths.pid_file, sizeof(g_paths.pid_file), "%s/livepaper.pid", g_paths.config_dir) ||
        !safe_snprintf(g_paths.lock_file, sizeof(g_paths.lock_file), "%s/livepaper.lock", g_paths.config_dir) ||
        !safe_snprintf(g_paths.autostart_file, sizeof(g_paths.autostart_file), "%s/.config/autostart/livepaper.desktop", home))
    {
        fprintf(stderr, "Path is too long.\n");
        exit(1);
    }
}

void mkdir_p(const char *path)
{
    char tmp[PATH_BUF];

    if (!safe_snprintf(tmp, sizeof(tmp), "%s", path))
    {
        fprintf(stderr, "Path is too long: %s\n", path);
        exit(1);
    }

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            {
                perror(tmp);
                exit(1);
            }

            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
    {
        perror(tmp);
        exit(1);
    }
}

int read_xdg_videos_dir(char *dst, size_t size)
{
    const char *home = getenv("HOME");
    char config_file[PATH_BUF];
    FILE *f;
    char line[PATH_BUF];

    if (!home || !safe_snprintf(config_file, sizeof(config_file), "%s/.config/user-dirs.dirs", home))
        return 0;

    f = fopen(config_file, "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f))
    {
        const char *prefix = "XDG_VIDEOS_DIR=\"";
        char *value;
        char *end;
        char expanded[PATH_BUF];

        if (strncmp(line, prefix, strlen(prefix)) != 0)
            continue;

        value = line + strlen(prefix);
        end = strchr(value, '"');
        if (!end)
            break;
        *end = '\0';

        if (strcmp(value, "$HOME") == 0)
        {
            if (!safe_snprintf(expanded, sizeof(expanded), "%s", home))
                break;
            value = expanded;
        }
        else if (strncmp(value, "$HOME/", 6) == 0)
        {
            if (!safe_snprintf(expanded, sizeof(expanded), "%s%s", home, value + 5))
                break;
            value = expanded;
        }
        else if (strcmp(value, "${HOME}") == 0)
        {
            if (!safe_snprintf(expanded, sizeof(expanded), "%s", home))
                break;
            value = expanded;
        }
        else if (strncmp(value, "${HOME}/", 8) == 0)
        {
            if (!safe_snprintf(expanded, sizeof(expanded), "%s%s", home, value + 7))
                break;
            value = expanded;
        }
        else if (value[0] != '/')
        {
            if (!safe_snprintf(expanded, sizeof(expanded), "%s/%s", home, value))
                break;
            value = expanded;
        }

        if (strcmp(value, home) == 0)
            break;

        if (!safe_snprintf(dst, size, "%s", value))
            break;

        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

void build_wallpaper_dir(char *dst, size_t size)
{
    const char *home = getenv("HOME");
    char videos_dir[PATH_BUF];

    if (read_xdg_videos_dir(videos_dir, sizeof(videos_dir)))
    {
        if (!safe_snprintf(dst, size, "%s/Livepaper", videos_dir))
        {
            fprintf(stderr, "Path is too long.\n");
            exit(1);
        }
        return;
    }

    if (!safe_snprintf(dst, size, "%s/Videos/Livepaper", home))
    {
        fprintf(stderr, "Path is too long.\n");
        exit(1);
    }
}

void ensure_dirs(void)
{
    const char *home = getenv("HOME");
    char autostart_dir[PATH_BUF];
    char wallpaper_dir[PATH_BUF];

    mkdir_p(g_paths.config_dir);

    if (safe_snprintf(autostart_dir, sizeof(autostart_dir), "%s/.config/autostart", home))
        mkdir_p(autostart_dir);

    build_wallpaper_dir(wallpaper_dir, sizeof(wallpaper_dir));
    mkdir_p(wallpaper_dir);
}
