#include "lock.h"

#include "../core/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_lock_fd = -1;

int acquire_lock(void)
{
    LivepaperPaths *paths = livepaper_paths();
    g_lock_fd = open(paths->lock_file, O_CREAT | O_RDWR, 0644);

    if (g_lock_fd < 0)
    {
        perror("Cannot open lock file");
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;

    if (fcntl(g_lock_fd, F_SETLK, &fl) < 0)
    {
        if (errno == EACCES || errno == EAGAIN)
            fprintf(stderr, "Another Livepaper instance is already running.\n");
        else
            perror("Cannot acquire lock");

        close(g_lock_fd);
        g_lock_fd = -1;
        return 0;
    }

    return 1;
}

void release_lock(void)
{
    if (g_lock_fd >= 0)
    {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}
