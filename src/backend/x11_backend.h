#ifndef LIVEPAPER_X11_BACKEND_H
#define LIVEPAPER_X11_BACKEND_H

#include "../../include/config.h"

int run_wallpaper(const LivepaperConfig *cfg);
void cleanup(int sig);
void list_monitors(void);

#endif
