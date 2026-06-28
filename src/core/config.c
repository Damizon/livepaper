#include "config.h"

#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void save_config(const char *wallpaper, const char *monitor, int delay)
{
    LivepaperPaths *paths = livepaper_paths();

    ensure_dirs();

    if (delay < 0)
        delay = 0;

    if (delay < 0)
        delay = 0;
    if (delay > 5)
        delay = 5;

    FILE *f = fopen(paths->config_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot write config file.\n");
        exit(1);
    }

    fprintf(f, "wallpaper=%s\n", wallpaper);
    fprintf(f, "monitor=%s\n", monitor ? monitor : "all");
    fprintf(f, "delay=%d\n", delay);
    fclose(f);

    printf("Config saved:\n%s\n", paths->config_file);
}

int load_config(LivepaperConfig *cfg)
{
    LivepaperPaths *paths = livepaper_paths();
    FILE *f = fopen(paths->config_file, "r");
    if (!f)
        return 0;

    char line[PATH_BUF];

    cfg->wallpaper[0] = '\0';
    strcpy(cfg->monitor, "all");
    cfg->delay = 0;

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "wallpaper=", 10) == 0)
        {
            strncpy(cfg->wallpaper, line + 10, sizeof(cfg->wallpaper) - 1);
            cfg->wallpaper[sizeof(cfg->wallpaper) - 1] = '\0';
            cfg->wallpaper[strcspn(cfg->wallpaper, "\n")] = 0;
        }
        else if (strncmp(line, "monitor=", 8) == 0)
        {
            strncpy(cfg->monitor, line + 8, sizeof(cfg->monitor) - 1);
            cfg->monitor[sizeof(cfg->monitor) - 1] = '\0';
            cfg->monitor[strcspn(cfg->monitor, "\n")] = 0;
        }
        else if (strncmp(line, "delay=", 6) == 0)
        {
            cfg->delay = atoi(line + 6);
            if (cfg->delay < 0)
                cfg->delay = 0;
            if (cfg->delay > 5)
                cfg->delay = 5;
        }
    }

    fclose(f);

    return strlen(cfg->wallpaper) > 0;
}
