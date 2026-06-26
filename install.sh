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

#define APP_NAME "livepaper"

Display *g_display = NULL;
Window g_window = 0;
pid_t g_mpv_pid = -1;

char config_dir[1024];
char config_file[1024];
char pid_file[1024];
char autostart_file[1024];

void build_paths()
{
    const char *home = getenv("HOME");

    snprintf(config_dir, sizeof(config_dir), "%s/.config/livepaper", home);
    snprintf(config_file, sizeof(config_file), "%s/config.ini", config_dir);
    snprintf(pid_file, sizeof(pid_file), "%s/livepaper.pid", config_dir);
    snprintf(autostart_file, sizeof(autostart_file), "%s/.config/autostart/livepaper.desktop", home);
}

void ensure_dirs()
{
    const char *home = getenv("HOME");

    mkdir(config_dir, 0755);

    char autostart_dir[1024];
    snprintf(autostart_dir, sizeof(autostart_dir), "%s/.config/autostart", home);
    mkdir(autostart_dir, 0755);

    char wallpaper_dir[1024];
    snprintf(wallpaper_dir, sizeof(wallpaper_dir), "%s/Wideo/Livepaper", home);
    mkdir(wallpaper_dir, 0755);
}

void cleanup(int sig)
{
    printf("\nStopping Livepaper...\n");

    if (g_mpv_pid > 0)
    {
        kill(g_mpv_pid, SIGTERM);
        waitpid(g_mpv_pid, NULL, 0);
    }

    if (g_display && g_window)
    {
        XDestroyWindow(g_display, g_window);
        XFlush(g_display);
    }

    if (g_display)
        XCloseDisplay(g_display);

    remove(pid_file);
    exit(0);
}

int is_nemo_desktop(Display *d, Window win)
{
    XClassHint class_hint;

    if (XGetClassHint(d, win, &class_hint))
    {
        int match = 0;

        if (class_hint.res_name && strcmp(class_hint.res_name, "nemo-desktop") == 0)
            match = 1;

        if (class_hint.res_class && strcmp(class_hint.res_class, "Nemo-desktop") == 0)
            match = 1;

        if (class_hint.res_name)
            XFree(class_hint.res_name);

        if (class_hint.res_class)
            XFree(class_hint.res_class);

        if (match)
            return 1;
    }

    char *name = NULL;

    if (XFetchName(d, win, &name) > 0 && name)
    {
        int match = strcmp(name, "Pulpit") == 0;
        XFree(name);

        if (match)
            return 1;
    }

    return 0;
}

Window find_nemo_desktop(Display *d, Window root)
{
    if (is_nemo_desktop(d, root))
        return root;

    Window parent;
    Window root_return;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(d, root, &root_return, &parent, &children, &nchildren))
        return 0;

    for (unsigned int i = 0; i < nchildren; i++)
    {
        Window found = find_nemo_desktop(d, children[i]);

        if (found)
        {
            if (children)
                XFree(children);

            return found;
        }
    }

    if (children)
        XFree(children);

    return 0;
}

Window wait_for_nemo_desktop(Display *d, int timeout_seconds)
{
    Window root = DefaultRootWindow(d);

    for (int i = 0; i < timeout_seconds; i++)
    {
        Window nemo = find_nemo_desktop(d, root);

        if (nemo)
        {
            printf("Nemo desktop found: 0x%lx\n", nemo);
            return nemo;
        }

        sleep(1);
    }

    printf("Nemo desktop not found. Continuing anyway.\n");
    return 0;
}

void keep_layer_order(Display *d, Window livepaper)
{
    Window root = DefaultRootWindow(d);
    Window nemo = find_nemo_desktop(d, root);

    XLowerWindow(d, livepaper);

    if (nemo)
        XRaiseWindow(d, nemo);

    XFlush(d);
}

void set_window_type_desktop(Display *d, Window win)
{
    Atom prop = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    Atom value = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XChangeProperty(d, win, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)&value, 1);
}

void set_window_states(Display *d, Window win)
{
    Atom prop = XInternAtom(d, "_NET_WM_STATE", False);

    Atom states[4];
    states[0] = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
    states[1] = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", False);
    states[2] = XInternAtom(d, "_NET_WM_STATE_STICKY", False);
    states[3] = XInternAtom(d, "_NET_WM_STATE_BELOW", False);

    XChangeProperty(d, win, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)states, 4);
}

int read_pid()
{
    FILE *f = fopen(pid_file, "r");
    if (!f)
        return -1;

    int pid = -1;
    fscanf(f, "%d", &pid);
    fclose(f);

    return pid;
}

void write_pid(pid_t pid)
{
    FILE *f = fopen(pid_file, "w");
    if (!f)
        return;

    fprintf(f, "%d\n", pid);
    fclose(f);
}

int process_running(pid_t pid)
{
    if (pid <= 0)
        return 0;

    return kill(pid, 0) == 0;
}

void save_config(const char *wallpaper, const char *monitor)
{
    ensure_dirs();

    FILE *f = fopen(config_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot write config file.\n");
        exit(1);
    }

    fprintf(f, "wallpaper=%s\n", wallpaper);
    fprintf(f, "monitor=%s\n", monitor);
    fclose(f);

    printf("Config saved:\n%s\n", config_file);
}

int load_config(char *wallpaper, size_t wallpaper_size, char *monitor, size_t monitor_size)
{
    FILE *f = fopen(config_file, "r");
    if (!f)
        return 0;

    char line[2048];

    strcpy(monitor, "all");
    wallpaper[0] = '\0';

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "wallpaper=", 10) == 0)
        {
            strncpy(wallpaper, line + 10, wallpaper_size - 1);
            wallpaper[wallpaper_size - 1] = '\0';
            wallpaper[strcspn(wallpaper, "\n")] = 0;
        }
        else if (strncmp(line, "monitor=", 8) == 0)
        {
            strncpy(monitor, line + 8, monitor_size - 1);
            monitor[monitor_size - 1] = '\0';
            monitor[strcspn(monitor, "\n")] = 0;
        }
    }

    fclose(f);

    return strlen(wallpaper) > 0;
}

void create_autostart()
{
    char exe_path[1024];

    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1)
    {
        fprintf(stderr, "Cannot detect executable path.\n");
        return;
    }

    exe_path[len] = '\0';

    FILE *f = fopen(autostart_file, "w");
    if (!f)
    {
        fprintf(stderr, "Cannot create autostart file.\n");
        return;
    }

    fprintf(f,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Livepaper\n"
        "Exec=%s start\n"
        "Hidden=false\n"
        "NoDisplay=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-GNOME-Autostart-Delay=5\n",
        exe_path
    );

    fclose(f);

    printf("Autostart enabled.\n");
}

void remove_autostart()
{
    remove(autostart_file);
    printf("Autostart disabled.\n");
}

void list_monitors()
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
                printf("%s %dx%d+%d+%d\n",
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

int run_wallpaper(const char *video_path)
{
    Display *d = XOpenDisplay(NULL);

    if (!d)
    {
        fprintf(stderr, "Cannot open X display.\n");
        return 1;
    }

    wait_for_nemo_desktop(d, 30);

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

    g_mpv_pid = fork();

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

            "--hwdec=auto",
            "--profile=fast",
            "--vd-lavc-threads=2",

            "--really-quiet",
            video_path,
            NULL
        );

        perror("Failed to start mpv");
        exit(1);
    }

    printf("Livepaper started.\n");
    printf("Window ID: %lu\n", win);
    printf("MPV PID: %d\n", g_mpv_pid);

    for (int i = 0; i < 30; i++)
    {
        keep_layer_order(d, win);
        sleep(1);
    }

    while (1)
    {
        sleep(2);

        keep_layer_order(d, win);

        if (waitpid(g_mpv_pid, NULL, WNOHANG) > 0)
        {
            printf("MPV exited.\n");
            cleanup(0);
        }
    }

    return 0;
}

void start_livepaper()
{
    int old_pid = read_pid();

    if (process_running(old_pid))
    {
        printf("Livepaper is already running. PID: %d\n", old_pid);
        return;
    }

    char wallpaper[2048];
    char monitor[256];

    if (!load_config(wallpaper, sizeof(wallpaper), monitor, sizeof(monitor)))
    {
        fprintf(stderr, "No wallpaper configured. Use:\n");
        fprintf(stderr, "  livepaper apply /path/to/video.mp4\n");
        exit(1);
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        fprintf(stderr, "Failed to fork.\n");
        exit(1);
    }

    if (pid > 0)
    {
        write_pid(pid);
        create_autostart();
        printf("Livepaper started in background. PID: %d\n", pid);
        return;
    }

    setsid();

    write_pid(getpid());

    run_wallpaper(wallpaper);
}

void stop_livepaper()
{
    int pid = read_pid();

    if (!process_running(pid))
    {
        printf("Livepaper is not running.\n");
        remove(pid_file);
        remove_autostart();
        return;
    }

    kill(pid, SIGTERM);
    remove(pid_file);
    remove_autostart();

    printf("Livepaper stopped.\n");
}

void status_livepaper()
{
    int pid = read_pid();

    if (process_running(pid))
        printf("Livepaper is running. PID: %d\n", pid);
    else
        printf("Livepaper is stopped.\n");
}

void print_help()
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
        "  livepaper apply ~/Wideo/Livepaper/wallpaper.mp4\n"
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

        if (argc >= 4)
            monitor = argv[3];

        save_config(argv[2], monitor);
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