#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/process.h"
#include "backend/x11_backend.h"
#include "backend/x11_desktop.h"
#include "core/config.h"
#include "core/paths.h"
#include "session/autostart.h"
#include "session/lock.h"
#include "session/pidfile.h"
#include "session/runtime.h"

int process_running(pid_t pid)
{
    if (pid <= 0)
        return 0;

    return kill(pid, 0) == 0 || errno == EPERM;
}

static int run_wallpaper(const LivepaperConfig *cfg)
{
    if (!acquire_lock())
        return 1;

    write_pid(getpid());

    Display *d = XOpenDisplay(NULL);

    if (!d)
    {
        fprintf(stderr, "Cannot open X display.\n");
        cleanup(0);
        return 1;
    }

    wait_for_desktop_ready(d, 60);

    if (cfg->delay > 0)
    {
        printf("Extra delay after desktop ready: %d seconds\n", cfg->delay);
        sleep(cfg->delay);
    }

    int screen = DefaultScreen(d);
    int width = DisplayWidth(d, screen);
    int height = DisplayHeight(d, screen);
    DesktopWindowSelection desktop = xwinwrap_find_desktop_window(d);
    Window create_parent = RootWindow(d, screen);
    int depth = CopyFromParent;
    int flags = CWBackPixel | CWEventMask;
    Visual *visual = CopyFromParent;

    printf("Selected desktop parent window ID: 0x%lx\n", desktop.desktop);
    printf("Livepaper create parent window ID: 0x%lx\n", create_parent);
    printf("xwinwrap root window ID: 0x%lx\n", desktop.root);
    printf("__SWM_VROOT found: %s\n", desktop.swm_vroot_found ? "yes" : "no");
    printf("Nemo desktop found: %s\n", desktop.nemo_found ? "yes" : "no");

    XWindowAttributes desktop_attrs;
    if (XGetWindowAttributes(d, desktop.desktop, &desktop_attrs))
    {
        printf(
            "Selected desktop parent attrs: class=%s depth=%d size=%dx%d\n",
            desktop_attrs.class == InputOutput ? "InputOutput" :
            desktop_attrs.class == InputOnly ? "InputOnly" : "unknown",
            desktop_attrs.depth,
            desktop_attrs.width,
            desktop_attrs.height
        );
    }

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixel = BlackPixel(d, screen);
    attrs.event_mask = StructureNotifyMask | ExposureMask;

    Window win = XCreateWindow(
        d,
        create_parent,
        0,
        0,
        width,
        height,
        0,
        depth,
        InputOutput,
        visual,
        flags,
        &attrs
    );

    XStoreName(d, win, "Livepaper");
    set_window_type_desktop(d, win);
    set_window_states(d, win);

    XLowerWindow(d, win);
    int input_passthrough = enable_input_passthrough(d, win);
    XMapWindow(d, win);
    XLowerWindow(d, win);
    XSync(d, False);

    x11_backend_set_window(d, win);

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGHUP, cleanup);

    x11_backend_set_mpv_pid(fork());

    if (x11_backend_get_mpv_pid() < 0)
    {
        perror("fork mpv");
        cleanup(0);
    }

    if (x11_backend_get_mpv_pid() == 0)
    {
        char wid_arg[128];
        snprintf(wid_arg, sizeof(wid_arg), "--wid=0x%lx", win);

        execlp(
            "mpv",
            "mpv",
            wid_arg,

            "--loop-file",
            "--no-audio",
            "--no-border",
            "--no-resume-playback",
            "--stop-screensaver=no",

            "--osc=no",
            "--osd-level=0",
            "--input-default-bindings=no",
            "--input-vo-keyboard=no",
            "--no-input-cursor",

            "--hwdec=auto",
            "--profile=fast",
            "--vd-lavc-threads=2",

            "--really-quiet",
            cfg->wallpaper,
            NULL
        );

        perror("Failed to start mpv");
        _exit(1);
    }

    printf("Livepaper started.\n");
    printf("Created Livepaper child window ID: 0x%lx\n", win);
    printf("X Shape input pass-through enabled: %s\n", input_passthrough ? "yes" : "no");
    printf("Livepaper PID: %d\n", getpid());
    printf("MPV PID: %d\n", x11_backend_get_mpv_pid());

    refresh_muffin_stacking_once(d, win);

    while (1)
    {
        sleep(5);

        keep_layer_order(d, win);

        if (waitpid(x11_backend_get_mpv_pid(), NULL, WNOHANG) > 0)
        {
            printf("MPV exited.\n");
            cleanup(0);
        }
    }

    return 0;
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

    if (access(cfg.wallpaper, R_OK) != 0)
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

static void stop_livepaper(void)
{
    pid_t pid = read_pid();

    if (!process_running(pid))
    {
        printf("Livepaper is not running.\n");
        remove_runtime_files();
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
    remove_autostart();

    printf("Livepaper stopped.\n");
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
        "  livepaper apply <video_path> [monitor]\n"
        "  livepaper start\n"
        "  livepaper stop\n"
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

        if (argc >= 4)
            monitor = argv[3];

        if (argc >= 5)
            delay = atoi(argv[4]);

        save_config(argv[2], monitor, delay);
        return 0;
    }

    if (strcmp(argv[1], "start") == 0)
    {
        start_livepaper();
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        stop_livepaper();
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
