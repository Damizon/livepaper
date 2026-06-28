#ifndef LIVEPAPER_PATHS_H
#define LIVEPAPER_PATHS_H

#include <stddef.h>
#include "../../include/config.h"

LivepaperPaths *livepaper_paths(void);
void build_paths(void);
void mkdir_p(const char *path);
int read_xdg_videos_dir(char *dst, size_t size);
void build_wallpaper_dir(char *dst, size_t size);
void ensure_dirs(void);

#endif
