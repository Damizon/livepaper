#include "runtime.h"

#include "lock.h"
#include "../core/paths.h"

#include <stdio.h>

void remove_runtime_files(void)
{
    LivepaperPaths *paths = livepaper_paths();
    remove(paths->pid_file);
    release_lock();
}
