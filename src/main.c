#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include "../include/config.h"
#include "../include/process.h"

static Display *g_display = NULL;
static Window g_window = 0;
static pid_t g_mpv_pid = -1;
static int g_lock_fd = -1;

static LivepaperPaths g_paths;


static int safe_snprintf(char *dst, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(dst, size, fmt, args);
    va_end(args);

    if (ret < 0 || (size_t)ret >= size)
        return 0;

    return 1;
}

static void build_paths(void)
{
    const char *home = getenv("HOME");

    if (!home || strlen(home) == 0)
    {
        fprintf(stderr, "HOME is not set.\n");
        exit(1);
    }

    if (!safe_snprintf(g_paths.config_dir, sizeof(g_paths.config_dir), "%s/.config/livepaper", home) ||
        !safe_snprintf(g_paths.config_file, sizeof(g_paths.config_file), "%s/config.ini", g_paths.config_dir) ||
        !safe_snprintf(g_paths.pid_file, sizeof(g_paths.pid_file), "%s/livepaper.pid", g_paths.config_dir) ||
        !safe_snprintf(g_paths.lock_file, sizeof(g_paths.lock_file), "%s/livepaper.lock", g_paths.config_dir) ||
        !safe_snprintf(g_paths.autostart_file, sizeof(g_paths.autostart_file), "%s/.config/autostart/livepaper.desktop", home))
    {
        fprintf(stderr, "Path is too long.\n");
        exit(1);
    }
}

static void mkdir_p(const char *path)
{
    char tmp[PATH_BUF];

    if (!safe_snprintf(tmp, sizeof(tmp), "%s", path))
    {
        fprintf(stderr, "Path is too long: %s\n", path);
        exit(1);
    }

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            {
                perror(tmp);
                exit(1);
            }

            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
    {
        perror(tmp);
        exit(1);
    }
}

static void ensure_dirs(void)
{
    const char *home = getenv("HOME");
    char autostart_dir[PATH_BUF];
    char wallpaper_dir[PATH_BUF];

    mkdir_p(g_paths.config_dir);

    if (safe_snprintf(autostart_dir, sizeof(autostart_dir), "%s/.config/autostart", home))
        mkdir_p(autostart_dir);

    if (safe_snprintf(wallpaper_dir, sizeof(wallpaper_dir), "%s/Wideo/Livepaper", home))
        mkdir_p(wallpaper_dir);
}

int process_running(pid_t pid)
{
    if (pid <= 0)
        return 0;

    return kill(pid, 0) == 0 || errno == EPERM;
}


static int command_success(const char *cmd)
{
    int ret = system(cmd);
    return ret == 0;
}

static int desktop_processes_ready(void)
{
    /* Required: Nemo desktop and Cinnamon session.
       Muffin can appear under slightly different timing/name conditions,
       so it must not block wallpaper startup. */
    int nemo = command_success("pgrep -x nemo-desktop >/dev/null 2>&1");
    int cinnamon = command_success("pgrep -x cinnamon >/dev/null 2>&1");

    return nemo && cinnamon;
}

static pid_t read_pid(void)
{
    FILE *f = fopen(g_paths.pid_file, "r");
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

static void write_pid(pid_t pid)
{
    FILE *f = fopen(g_paths.pid_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot write PID file: %s\n", g_paths.pid_file);
        return;
    }

    fprintf(f, "%d\n", pid);
    fclose(f);
}

static int acquire_lock(void)
{
    g_lock_fd = open(g_paths.lock_file, O_CREAT | O_RDWR, 0644);

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

static void release_lock(void)
{
    if (g_lock_fd >= 0)
    {
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}

static void remove_runtime_files(void)
{
    remove(g_paths.pid_file);
    release_lock();
}

static void cleanup(int sig)
{
    (void)sig;

    printf("\nStopping Livepaper...\n");

    if (g_mpv_pid > 0)
    {
        kill(g_mpv_pid, SIGTERM);

        for (int i = 0; i < 20; i++)
        {
            if (waitpid(g_mpv_pid, NULL, WNOHANG) > 0)
                break;

            usleep(100000);
        }

        if (process_running(g_mpv_pid))
            kill(g_mpv_pid, SIGKILL);
    }

    if (g_display && g_window)
    {
        XDestroyWindow(g_display, g_window);
        XFlush(g_display);
        g_window = 0;
    }

    if (g_display)
    {
        XCloseDisplay(g_display);
        g_display = NULL;
    }

    remove_runtime_files();
    exit(0);
}

static int is_nemo_desktop(Display *d, Window win)
{
    XClassHint hint;

    if (XGetClassHint(d, win, &hint))
    {
        int match = 0;

        if (hint.res_name && strcmp(hint.res_name, "nemo-desktop") == 0)
            match = 1;

        if (hint.res_class && strcmp(hint.res_class, "Nemo-desktop") == 0)
            match = 1;

        if (hint.res_name)
            XFree(hint.res_name);

        if (hint.res_class)
            XFree(hint.res_class);

        if (match)
            return 1;
    }

    char *name = NULL;

    if (XFetchName(d, win, &name) > 0 && name)
    {
        int match = strcmp(name, "Pulpit") == 0 ||
                    strcmp(name, "Desktop") == 0;

        XFree(name);

        if (match)
            return 1;
    }

    return 0;
}

static Window find_nemo_desktop(Display *d, Window root)
{
    if (is_nemo_desktop(d, root))
    {
        XWindowAttributes attr;

        if (XGetWindowAttributes(d, root, &attr))
        {
            int screen_w = DisplayWidth(d, DefaultScreen(d));
            int screen_h = DisplayHeight(d, DefaultScreen(d));

            if (attr.width >= screen_w * 0.7 &&
                attr.height >= screen_h * 0.7)
            {
                return root;
            }
        }
    }

    Window root_return;
    Window parent;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(d, root, &root_return, &parent, &children, &nchildren))
        return 0;

    for (unsigned int i = 0; i < nchildren; i++)
    {
        Window found = find_nemo_desktop(d, children[i]);

        if (found)
        {
            XFree(children);
            return found;
        }
    }

    if (children)
        XFree(children);

    return 0;
}


static int wait_for_desktop_ready(Display *d, int timeout_seconds)
{
    Window root = DefaultRootWindow(d);
    int stable_checks = 0;

    printf("Waiting for Cinnamon/Nemo desktop readiness...\n");

    for (int i = 0; i < timeout_seconds * 2; i++)
    {
        Window nemo = find_nemo_desktop(d, root);
        int processes_ok = desktop_processes_ready();
        int window_ok = 0;

        if (nemo)
        {
            XWindowAttributes attr;

            if (XGetWindowAttributes(d, nemo, &attr))
            {
                int screen_w = DisplayWidth(d, DefaultScreen(d));
                int screen_h = DisplayHeight(d, DefaultScreen(d));

                if (attr.map_state == IsViewable &&
                    attr.width >= screen_w * 0.7 &&
                    attr.height >= screen_h * 0.7)
                {
                    window_ok = 1;
                }
            }
        }

        if (processes_ok && window_ok)
        {
            stable_checks++;

            /* 2 stable checks = about 1 second. The optional config delay
               is applied after this, so this should stay short. */
            if (stable_checks >= 2)
            {
                printf("Desktop ready. Nemo: 0x%lx\n", nemo);
                return 1;
            }
        }
        else
        {
            stable_checks = 0;

            if (i % 4 == 0)
            {
                printf("Waiting... processes=%s desktop_window=%s\n",
                       processes_ok ? "ok" : "no",
                       window_ok ? "ok" : "no");
            }
        }

        usleep(500000);
    }

    printf("Desktop readiness timeout. Continuing anyway.\n");
    return 0;
}

static void set_window_type_desktop(Display *d, Window win)
{
    Atom prop = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom value = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XChangeProperty(
        d,
        win,
        prop,
        XA_ATOM,
        32,
        PropModeReplace,
        (unsigned char *)&value,
        1
    );
}

static void set_window_states(Display *d, Window win)
{
    Atom prop = XInternAtom(d, "_NET_WM_STATE", False);

    Atom states[4];
    states[0] = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
    states[1] = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", False);
    states[2] = XInternAtom(d, "_NET_WM_STATE_STICKY", False);
    states[3] = XInternAtom(d, "_NET_WM_STATE_BELOW", False);

    XChangeProperty(
        d,
        win,
        prop,
        XA_ATOM,
        32,
        PropModeReplace,
        (unsigned char *)states,
        4
    );
}

static void keep_layer_order(Display *d, Window livepaper)
{
    XLowerWindow(d, livepaper);
    XFlush(d);
}

static void save_config(const char *wallpaper, const char *monitor, int delay)
{
    ensure_dirs();

    FILE *old = fopen(g_paths.config_file, "r");
    int existing_delay = 5;

    if (old)
    {
        char line[PATH_BUF];

        while (fgets(line, sizeof(line), old))
        {
            if (strncmp(line, "delay=", 6) == 0)
                existing_delay = atoi(line + 6);
        }

        fclose(old);
    }

    if (delay < 0)
        delay = existing_delay;

    if (delay < 0)
        delay = 0;
    if (delay > 5)
        delay = 5;

    FILE *f = fopen(g_paths.config_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot write config file.\n");
        exit(1);
    }

    fprintf(f, "wallpaper=%s\n", wallpaper);
    fprintf(f, "monitor=%s\n", monitor ? monitor : "all");
    fprintf(f, "delay=%d\n", delay);
    fclose(f);

    printf("Config saved:\n%s\n", g_paths.config_file);
}

static int load_config(LivepaperConfig *cfg)
{
    FILE *f = fopen(g_paths.config_file, "r");
    if (!f)
        return 0;

    char line[PATH_BUF];

    cfg->wallpaper[0] = '\0';
    strcpy(cfg->monitor, "all");
    cfg->delay = 0;

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "wallpaper=", 10) == 0)
        {
            strncpy(cfg->wallpaper, line + 10, sizeof(cfg->wallpaper) - 1);
            cfg->wallpaper[sizeof(cfg->wallpaper) - 1] = '\0';
            cfg->wallpaper[strcspn(cfg->wallpaper, "\n")] = 0;
        }
        else if (strncmp(line, "monitor=", 8) == 0)
        {
            strncpy(cfg->monitor, line + 8, sizeof(cfg->monitor) - 1);
            cfg->monitor[sizeof(cfg->monitor) - 1] = '\0';
            cfg->monitor[strcspn(cfg->monitor, "\n")] = 0;
        }
        else if (strncmp(line, "delay=", 6) == 0)
        {
            cfg->delay = atoi(line + 6);
            if (cfg->delay < 0)
                cfg->delay = 0;
            if (cfg->delay > 5)
                cfg->delay = 5;
        }
    }

    fclose(f);

    return strlen(cfg->wallpaper) > 0;
}

static void create_autostart(void)
{
    char exe_path[PATH_BUF];

    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1)
    {
        fprintf(stderr, "Cannot detect executable path.\n");
        return;
    }

    exe_path[len] = '\0';

    FILE *f = fopen(g_paths.autostart_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot create autostart file.\n");
        return;
    }

    fprintf(
        f,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Livepaper\n"
        "Exec=%s start\n"
        "Hidden=false\n"
        "NoDisplay=false\n"
        "X-GNOME-Autostart-enabled=true\n",
        exe_path
    );

    fclose(f);

    printf("Autostart enabled.\n");
}

static void remove_autostart(void)
{
    remove(g_paths.autostart_file);
    printf("Autostart disabled.\n");
}

static void list_monitors(void)
{
    Display *d = XOpenDisplay(NULL);
    if (!d)
    {
        fprintf(stderr, "Cannot open X display.\n");
        return;
    }

    Window root = DefaultRootWindow(d);

    XRRScreenResources *res = XRRGetScreenResourcesCurrent(d, root);
    if (!res)
    {
        fprintf(stderr, "Cannot read XRandR screen resources.\n");
        XCloseDisplay(d);
        return;
    }

    printf("all\n");

    for (int i = 0; i < res->noutput; i++)
    {
        XRROutputInfo *output = XRRGetOutputInfo(d, res, res->outputs[i]);

        if (output && output->connection == RR_Connected && output->crtc)
        {
            XRRCrtcInfo *crtc = XRRGetCrtcInfo(d, res, output->crtc);

            if (crtc)
            {
                printf(
                    "%s %dx%d+%d+%d\n",
                    output->name,
                    crtc->width,
                    crtc->height,
                    crtc->x,
                    crtc->y
                );

                XRRFreeCrtcInfo(crtc);
            }
        }

        if (output)
            XRRFreeOutputInfo(output);
    }

    XRRFreeScreenResources(res);
    XCloseDisplay(d);
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

    Window root = RootWindow(d, screen);

    Window win = XCreateSimpleWindow(
        d,
        root,
        0,
        0,
        width,
        height,
        0,
        BlackPixel(d, screen),
        BlackPixel(d, screen)
    );

    XStoreName(d, win, "Livepaper");

    set_window_type_desktop(d, win);
    set_window_states(d, win);

    XMapWindow(d, win);
    XLowerWindow(d, win);
    XFlush(d);

    g_display = d;
    g_window = win;

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGHUP, cleanup);

    g_mpv_pid = fork();

    if (g_mpv_pid < 0)
    {
        perror("fork mpv");
        cleanup(0);
    }

    if (g_mpv_pid == 0)
    {
        char wid_arg[128];
        snprintf(wid_arg, sizeof(wid_arg), "--wid=%lu", win);

        execlp(
            "mpv",
            "mpv",
            wid_arg,

            "--loop-file",
            "--no-audio",
            "--no-border",
            "--no-resume-playback",

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
    printf("Window ID: %lu\n", win);
    printf("Livepaper PID: %d\n", getpid());
    printf("MPV PID: %d\n", g_mpv_pid);

    while (1)
    {
        sleep(5);

        keep_layer_order(d, win);

        if (waitpid(g_mpv_pid, NULL, WNOHANG) > 0)
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
        printf("Removing stale PID file.\n");
        remove(g_paths.pid_file);
    }

    LivepaperConfig cfg;

    if (!load_config(&cfg))
    {
        fprintf(stderr, "No wallpaper configured. Use:\n");
        fprintf(stderr, "  livepaper apply /path/to/video.mp4 [monitor] [delay]\n");
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
        "  livepaper apply <video_path> [monitor] [extra_delay_0_5]\n"
        "  livepaper start\n"
        "  livepaper stop\n"
        "  livepaper status\n"
        "  livepaper monitors\n\n"
        "Examples:\n"
        "  livepaper apply ~/Wideo/Livepaper/wallpaper.mp4 all 1\n"
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
