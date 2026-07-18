#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/process.h"
#include "backend/x11_backend.h"
#include "core/config.h"
#include "core/paths.h"
#include "session/autostart.h"
#include "session/pidfile.h"
#include "session/runtime.h"

int process_running(pid_t pid)
{
    if (pid <= 0)
        return 0;

    return kill(pid, 0) == 0 || errno == EPERM;
}

static void start_livepaper(void)
{
    pid_t old_pid = read_pid();

    if (process_running(old_pid))
    {
        printf("Livepaper is already running. PID: %d\n", old_pid);
        return;
    }

    if (old_pid > 0)
    {
        LivepaperPaths *paths = livepaper_paths();
        printf("Removing stale PID file.\n");
        remove(paths->pid_file);
    }

    LivepaperConfig cfg;

    if (!load_config(&cfg))
    {
        fprintf(stderr, "No wallpaper configured. Use:\n");
        fprintf(stderr, "  livepaper apply /path/to/video.mp4 [monitor]\n");
        exit(1);
    }

    if (strcmp(cfg.mode, "per-monitor") == 0)
    {
        for (int i = 0; i < cfg.monitor_count; i++)
        {
            if (access(cfg.monitors[i].wallpaper, R_OK) != 0)
            {
                fprintf(
                    stderr,
                    "Wallpaper file is not readable for monitor %s:\n%s\n",
                    cfg.monitors[i].monitor,
                    cfg.monitors[i].wallpaper
                );
                exit(1);
            }
        }
    }
    else if (access(cfg.wallpaper, R_OK) != 0)
    {
        fprintf(stderr, "Wallpaper file is not readable:\n%s\n", cfg.wallpaper);
        exit(1);
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("Failed to fork");
        exit(1);
    }

    if (pid > 0)
    {
        write_pid(pid);
        create_autostart();
        printf("Livepaper starting in background. PID: %d\n", pid);
        return;
    }

    if (setsid() < 0)
    {
        perror("setsid");
        exit(1);
    }

    run_wallpaper(&cfg);
}

static void stop_livepaper(int disable_autostart)
{
    pid_t pid = read_pid();

    if (!process_running(pid))
    {
        printf("Livepaper is not running.\n");
        remove_runtime_files();
        if (disable_autostart)
            remove_autostart();
        return;
    }

    printf("Stopping Livepaper PID: %d\n", pid);

    /*
       The daemon calls setsid(), so its PID is also its process group ID.
       Sending to -pid stops both livepaper and mpv if mpv is still in that group.
    */
    kill(-pid, SIGTERM);
    kill(pid, SIGTERM);

    for (int i = 0; i < 30; i++)
    {
        if (!process_running(pid))
            break;

        usleep(100000);
    }

    if (process_running(pid))
    {
        fprintf(stderr, "Livepaper did not stop gracefully. Killing.\n");
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
    }

    remove_runtime_files();
    if (disable_autostart)
        remove_autostart();

    printf("Livepaper stopped.\n");
}

static void stop_livepaper_monitor(const char *monitor)
{
    pid_t pid;
    int has_remaining;

    if (!monitor || strcmp(monitor, "all") == 0 || strcmp(monitor, "stretched") == 0)
    {
        stop_livepaper(1);
        return;
    }

    has_remaining = remove_monitor_config(monitor);
    pid = read_pid();

    if (!process_running(pid))
    {
        if (!has_remaining)
        {
            remove_runtime_files();
            remove_autostart();
        }

        printf("Wallpaper stopped for monitor: %s\n", monitor);
        return;
    }

    stop_livepaper(!has_remaining);

    if (has_remaining)
        start_livepaper();

    printf("Wallpaper stopped for monitor: %s\n", monitor);
}

static void status_livepaper(void)
{
    pid_t pid = read_pid();

    if (process_running(pid))
        printf("Livepaper is running. PID: %d\n", pid);
    else
        printf("Livepaper is stopped.\n");
}

static void print_help(void)
{
    printf(
        "Livepaper\n\n"
        "Usage:\n"
        "  livepaper apply <video_path> [monitor] [delay] [fit]\n"
        "  livepaper start\n"
        "  livepaper stop [monitor|all]\n"
        "  livepaper status\n"
        "  livepaper monitors\n\n"
        "Examples:\n"
        "  livepaper apply ~/Videos/Livepaper/wallpaper.mp4 all\n"
        "  livepaper start\n"
        "  livepaper stop\n"
    );
}

int main(int argc, char **argv)
{
    build_paths();
    ensure_dirs();

    if (argc < 2)
    {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "apply") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Missing video path.\n");
            return 1;
        }

        const char *monitor = "all";
        int delay = -1;
        const char *fit = NULL;

        if (argc >= 4)
            monitor = argv[3];

        if (argc >= 5)
            delay = atoi(argv[4]);

        if (argc >= 6)
            fit = argv[5];

        save_config_with_fit(argv[2], monitor, delay, fit);
        return 0;
    }

    if (strcmp(argv[1], "start") == 0)
    {
        start_livepaper();
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        const char *monitor = "all";

        if (argc >= 3)
            monitor = argv[2];

        stop_livepaper_monitor(monitor);
        return 0;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        status_livepaper();
        return 0;
    }

    if (strcmp(argv[1], "monitors") == 0)
    {
        list_monitors();
        return 0;
    }

    print_help();
    return 0;
}
