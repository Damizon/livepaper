#include "config.h"

#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_string(char *dst, size_t size, const char *src)
{
    size_t len;

    if (size == 0)
        return;

    if (!src)
        src = "";

    len = strlen(src);
    if (len >= size)
        len = size - 1;

    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void init_config(LivepaperConfig *cfg)
{
    cfg->wallpaper[0] = '\0';
    strcpy(cfg->monitor, "all");
    strcpy(cfg->mode, "all");
    strcpy(cfg->fit, "normal");
    cfg->monitor_count = 0;
    cfg->delay = 0;
}

static int is_global_monitor(const char *monitor)
{
    return !monitor ||
           strcmp(monitor, "all") == 0 ||
           strcmp(monitor, "stretched") == 0 ||
           monitor[0] == '\0';
}

static void clamp_delay(LivepaperConfig *cfg)
{
    if (cfg->delay < 0)
        cfg->delay = 0;
    if (cfg->delay > 5)
        cfg->delay = 5;
}

static const char *normalize_fit(const char *fit)
{
    if (!fit || fit[0] == '\0')
        return "normal";

    if (strcmp(fit, "normal") == 0 ||
        strcmp(fit, "cover") == 0 ||
        strcmp(fit, "stretch") == 0)
    {
        return fit;
    }

    return "normal";
}

static void set_monitor_wallpaper(LivepaperConfig *cfg, const char *monitor, const char *wallpaper)
{
    for (int i = 0; i < cfg->monitor_count; i++)
    {
        if (strcmp(cfg->monitors[i].monitor, monitor) == 0)
        {
            copy_string(cfg->monitors[i].wallpaper, sizeof(cfg->monitors[i].wallpaper), wallpaper);
            return;
        }
    }

    if (cfg->monitor_count >= MAX_MONITOR_CONFIGS)
    {
        fprintf(stderr, "Too many monitor wallpapers configured.\n");
        exit(1);
    }

    copy_string(cfg->monitors[cfg->monitor_count].monitor, sizeof(cfg->monitors[cfg->monitor_count].monitor), monitor);
    copy_string(cfg->monitors[cfg->monitor_count].wallpaper, sizeof(cfg->monitors[cfg->monitor_count].wallpaper), wallpaper);
    cfg->monitor_count++;
}

static void write_config_file(const LivepaperConfig *cfg)
{
    LivepaperPaths *paths = livepaper_paths();
    FILE *f = fopen(paths->config_file, "w");

    if (!f)
    {
        fprintf(stderr, "Cannot write config file.\n");
        exit(1);
    }

    fprintf(f, "mode=%s\n", cfg->mode);
    fprintf(f, "wallpaper=%s\n", cfg->wallpaper);
    fprintf(f, "monitor=%s\n", cfg->monitor);
    fprintf(f, "fit=%s\n", cfg->fit);
    fprintf(f, "delay=%d\n", cfg->delay);

    for (int i = 0; i < cfg->monitor_count; i++)
    {
        fprintf(
            f,
            "screen.%s=%s\n",
            cfg->monitors[i].monitor,
            cfg->monitors[i].wallpaper
        );
    }

    fclose(f);
}

void save_config(const char *wallpaper, const char *monitor, int delay)
{
    save_config_with_fit(wallpaper, monitor, delay, NULL);
}

void save_config_with_fit(const char *wallpaper, const char *monitor, int delay, const char *fit)
{
    LivepaperPaths *paths = livepaper_paths();
    LivepaperConfig cfg;

    ensure_dirs();
    init_config(&cfg);
    load_config(&cfg);

    if (delay < 0)
        delay = cfg.delay;

    cfg.delay = delay;
    clamp_delay(&cfg);
    copy_string(cfg.fit, sizeof(cfg.fit), normalize_fit(fit ? fit : cfg.fit));

    if (is_global_monitor(monitor))
    {
        copy_string(cfg.wallpaper, sizeof(cfg.wallpaper), wallpaper);
        copy_string(cfg.monitor, sizeof(cfg.monitor), monitor && monitor[0] ? monitor : "all");
        copy_string(cfg.mode, sizeof(cfg.mode), cfg.monitor);
        cfg.monitor_count = 0;
    }
    else
    {
        strcpy(cfg.mode, "per-monitor");
        strcpy(cfg.monitor, "per-monitor");
        copy_string(cfg.wallpaper, sizeof(cfg.wallpaper), wallpaper);
        set_monitor_wallpaper(&cfg, monitor, wallpaper);
    }

    write_config_file(&cfg);

    printf("Config saved:\n%s\n", paths->config_file);
}

int remove_monitor_config(const char *monitor)
{
    LivepaperPaths *paths = livepaper_paths();
    LivepaperConfig cfg;
    int removed = 0;

    ensure_dirs();

    if (is_global_monitor(monitor))
        return 0;

    init_config(&cfg);
    if (!load_config(&cfg))
        return 0;

    if (strcmp(cfg.mode, "per-monitor") != 0)
        return 0;

    for (int i = 0; i < cfg.monitor_count; i++)
    {
        if (strcmp(cfg.monitors[i].monitor, monitor) == 0)
        {
            for (int j = i; j < cfg.monitor_count - 1; j++)
                cfg.monitors[j] = cfg.monitors[j + 1];

            cfg.monitor_count--;
            removed = 1;
            break;
        }
    }

    if (!removed)
        return cfg.monitor_count > 0;

    if (cfg.monitor_count == 0)
    {
        cfg.wallpaper[0] = '\0';
        strcpy(cfg.monitor, "all");
        strcpy(cfg.mode, "all");
    }

    write_config_file(&cfg);
    printf("Config saved:\n%s\n", paths->config_file);

    return cfg.monitor_count > 0;
}

int load_config(LivepaperConfig *cfg)
{
    LivepaperPaths *paths = livepaper_paths();
    FILE *f = fopen(paths->config_file, "r");
    if (!f)
        return 0;

    char line[PATH_BUF];

    init_config(cfg);

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "wallpaper=", 10) == 0)
        {
            copy_string(cfg->wallpaper, sizeof(cfg->wallpaper), line + 10);
            cfg->wallpaper[strcspn(cfg->wallpaper, "\n")] = 0;
        }
        else if (strncmp(line, "mode=", 5) == 0)
        {
            copy_string(cfg->mode, sizeof(cfg->mode), line + 5);
            cfg->mode[strcspn(cfg->mode, "\n")] = 0;
        }
        else if (strncmp(line, "monitor=", 8) == 0)
        {
            copy_string(cfg->monitor, sizeof(cfg->monitor), line + 8);
            cfg->monitor[strcspn(cfg->monitor, "\n")] = 0;
        }
        else if (strncmp(line, "fit=", 4) == 0)
        {
            copy_string(cfg->fit, sizeof(cfg->fit), line + 4);
            cfg->fit[strcspn(cfg->fit, "\n")] = 0;
            copy_string(cfg->fit, sizeof(cfg->fit), normalize_fit(cfg->fit));
        }
        else if (strncmp(line, "delay=", 6) == 0)
        {
            cfg->delay = atoi(line + 6);
            if (cfg->delay < 0)
                cfg->delay = 0;
            if (cfg->delay > 5)
                cfg->delay = 5;
        }
        else if (strncmp(line, "screen.", 7) == 0)
        {
            char *equals = strchr(line + 7, '=');

            if (equals && cfg->monitor_count < MAX_MONITOR_CONFIGS)
            {
                *equals = '\0';
                copy_string(
                    cfg->monitors[cfg->monitor_count].monitor,
                    sizeof(cfg->monitors[cfg->monitor_count].monitor),
                    line + 7
                );

                copy_string(
                    cfg->monitors[cfg->monitor_count].wallpaper,
                    sizeof(cfg->monitors[cfg->monitor_count].wallpaper),
                    equals + 1
                );
                cfg->monitors[cfg->monitor_count].wallpaper[strcspn(cfg->monitors[cfg->monitor_count].wallpaper, "\n")] = 0;

                cfg->monitor_count++;
            }
        }
    }

    fclose(f);

    if (strcmp(cfg->mode, "per-monitor") == 0)
        return cfg->monitor_count > 0;

    if (strcmp(cfg->monitor, "per-monitor") == 0 && cfg->monitor_count > 0)
        strcpy(cfg->mode, "per-monitor");
    else if (strcmp(cfg->mode, "all") != 0 && strcmp(cfg->mode, "stretched") != 0)
        copy_string(cfg->mode, sizeof(cfg->mode), cfg->monitor);

    return strlen(cfg->wallpaper) > 0 || cfg->monitor_count > 0;
}
