#ifndef CONFIG_H
#define CONFIG_H

#define PATH_BUF 2048

typedef struct
{
    char wallpaper[PATH_BUF];
    char monitor[256];
    int delay;
} LivepaperConfig;

typedef struct
{
    char config_dir[PATH_BUF];
    char config_file[PATH_BUF];
    char pid_file[PATH_BUF];
    char lock_file[PATH_BUF];
    char autostart_file[PATH_BUF];
} LivepaperPaths;

#endif
