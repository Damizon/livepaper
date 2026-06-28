#ifndef LIVEPAPER_CONFIG_H
#define LIVEPAPER_CONFIG_H

#include "../../include/config.h"

void save_config(const char *wallpaper, const char *monitor, int delay);
int load_config(LivepaperConfig *cfg);

#endif
