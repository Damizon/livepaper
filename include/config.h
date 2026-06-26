#ifndef CONFIG_H
#define CONFIG_H

#define PATH_BUF 2048

typedef struct
{
    char wallpaper[PATH_BUF];
    char monitor[256];
    int delay;
} LivepaperConfig;

#endif
