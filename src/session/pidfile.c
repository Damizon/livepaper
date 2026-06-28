#include "pidfile.h"

#include "../core/paths.h"

#include <stdio.h>

pid_t read_pid(void)
{
    LivepaperPaths *paths = livepaper_paths();
    FILE *f = fopen(paths->pid_file, "r");
    if (!f)
        return -1;

    long pid = -1;
    if (fscanf(f, "%ld", &pid) != 1)
        pid = -1;

    fclose(f);

    if (pid <= 0)
        return -1;

    return (pid_t)pid;
}

void write_pid(pid_t pid)
{
    LivepaperPaths *paths = livepaper_paths();
    FILE *f = fopen(paths->pid_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot write PID file: %s\n", paths->pid_file);
        return;
    }

    fprintf(f, "%d\n", pid);
    fclose(f);
}
