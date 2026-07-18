#ifndef LIVEPAPER_CONFIG_H
#define LIVEPAPER_CONFIG_H

#include "../../include/config.h"

void save_config(const char *wallpaper, const char *monitor, int delay);
void save_config_with_fit(const char *wallpaper, const char *monitor, int delay, const char *fit);
int remove_monitor_config(const char *monitor);
int load_config(LivepaperConfig *cfg);

#endif
