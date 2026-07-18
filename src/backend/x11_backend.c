#include "x11_backend.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include "../../include/process.h"
#include "../../include/config.h"
#include "x11_desktop.h"
#include "../session/lock.h"
#include "../session/pidfile.h"
#include "../session/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_WALLPAPER_INSTANCES 16

static Display *g_display = NULL;

typedef struct MonitorGeometry
{
    char name[128];
    int x;
    int y;
    int width;
    int height;
} MonitorGeometry;

typedef struct WallpaperInstance
{
    Window window;
    pid_t mpv_pid;
    MonitorGeometry geometry;
} WallpaperInstance;

static WallpaperInstance g_instances[MAX_WALLPAPER_INSTANCES];
static int g_instance_count = 0;

static int monitor_matches(const char *requested, const char *name)
{
    size_t len;

    if (!requested || strcmp(requested, "all") == 0)
        return 0;

    len = strlen(name);

    return strcmp(requested, name) == 0 ||
           (strncmp(requested, name, len) == 0 &&
            (requested[len] == ' ' || requested[len] == '\0'));
}

static MonitorGeometry get_root_geometry(Display *d, int screen)
{
    MonitorGeometry geometry;
    Window root = RootWindow(d, screen);
    XWindowAttributes attrs;

    strcpy(geometry.name, "all");
    geometry.x = 0;
    geometry.y = 0;
    geometry.width = DisplayWidth(d, screen);
    geometry.height = DisplayHeight(d, screen);

    if (XGetWindowAttributes(d, root, &attrs))
    {
        geometry.width = attrs.width;
        geometry.height = attrs.height;
    }

    return geometry;
}

static int get_monitor_geometry(Display *d, const char *requested, MonitorGeometry *geometry)
{
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    XRRScreenResources *res;
    int found = 0;

    *geometry = get_root_geometry(d, screen);

    if (!requested || strcmp(requested, "all") == 0 || requested[0] == '\0')
        return 1;

    res = XRRGetScreenResourcesCurrent(d, root);
    if (!res)
        return 0;

    for (int i = 0; i < res->noutput; i++)
    {
        XRROutputInfo *output = XRRGetOutputInfo(d, res, res->outputs[i]);

        if (output && output->connection == RR_Connected && output->crtc &&
            monitor_matches(requested, output->name))
        {
            XRRCrtcInfo *crtc = XRRGetCrtcInfo(d, res, output->crtc);

            if (crtc)
            {
                geometry->x = crtc->x;
                geometry->y = crtc->y;
                geometry->width = crtc->width;
                geometry->height = crtc->height;
                snprintf(geometry->name, sizeof(geometry->name), "%s", output->name);
                found = 1;
                XRRFreeCrtcInfo(crtc);
            }
        }

        if (output)
            XRRFreeOutputInfo(output);

        if (found)
            break;
    }

    XRRFreeScreenResources(res);
    return found;
}

static int get_all_monitor_geometries(Display *d, MonitorGeometry *geometries, int max_geometries)
{
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    XRRScreenResources *res;
    int count = 0;

    res = XRRGetScreenResourcesCurrent(d, root);
    if (!res)
        return 0;

    for (int i = 0; i < res->noutput && count < max_geometries; i++)
    {
        XRROutputInfo *output = XRRGetOutputInfo(d, res, res->outputs[i]);

        if (output && output->connection == RR_Connected && output->crtc)
        {
            XRRCrtcInfo *crtc = XRRGetCrtcInfo(d, res, output->crtc);

            if (crtc)
            {
                snprintf(
                    geometries[count].name,
                    sizeof(geometries[count].name),
                    "%s",
                    output->name
                );
                geometries[count].x = crtc->x;
                geometries[count].y = crtc->y;
                geometries[count].width = crtc->width;
                geometries[count].height = crtc->height;
                count++;
                XRRFreeCrtcInfo(crtc);
            }
        }

        if (output)
            XRRFreeOutputInfo(output);
    }

    XRRFreeScreenResources(res);
    return count;
}

static void stop_mpv(pid_t pid)
{
    if (pid <= 0)
        return;

    kill(pid, SIGTERM);

    for (int i = 0; i < 20; i++)
    {
        if (waitpid(pid, NULL, WNOHANG) > 0)
            return;

        usleep(100000);
    }

    if (process_running(pid))
        kill(pid, SIGKILL);
}

static Window create_wallpaper_window(
    Display *d,
    Window parent,
    int screen,
    const MonitorGeometry *geometry
)
{
    XSetWindowAttributes attrs;
    Window win;

    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixel = BlackPixel(d, screen);
    attrs.event_mask = StructureNotifyMask | ExposureMask;

    win = XCreateWindow(
        d,
        parent,
        geometry->x,
        geometry->y,
        geometry->width,
        geometry->height,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWBackPixel | CWEventMask,
        &attrs
    );

    XStoreName(d, win, "Livepaper");
    set_window_type_desktop(d, win);
    set_window_states(d, win);

    XLowerWindow(d, win);
    XMapWindow(d, win);
    XLowerWindow(d, win);

    return win;
}

static pid_t start_mpv(Window win, const char *wallpaper)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork mpv");
        return -1;
    }

    if (pid == 0)
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
            wallpaper,
            NULL
        );

        perror("Failed to start mpv");
        _exit(1);
    }

    return pid;
}

void cleanup(int sig)
{
    (void)sig;

    printf("\nStopping Livepaper...\n");

    for (int i = 0; i < g_instance_count; i++)
    {
        stop_mpv(g_instances[i].mpv_pid);
        g_instances[i].mpv_pid = -1;
    }

    if (g_display)
    {
        for (int i = 0; i < g_instance_count; i++)
            if (g_instances[i].window)
                XDestroyWindow(g_display, g_instances[i].window);

        XFlush(g_display);
    }

    if (g_display)
    {
        XCloseDisplay(g_display);
        g_display = NULL;
    }

    g_instance_count = 0;
    remove_runtime_files();
    exit(0);
}

int run_wallpaper(const LivepaperConfig *cfg)
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
    MonitorGeometry geometries[MAX_WALLPAPER_INSTANCES];
    int geometry_count = 0;
    DesktopWindowSelection desktop = xwinwrap_find_desktop_window(d);
    Window create_parent = RootWindow(d, screen);

    if (strcmp(cfg->monitor, "all") == 0)
    {
        geometry_count = get_all_monitor_geometries(
            d,
            geometries,
            MAX_WALLPAPER_INSTANCES
        );

        if (geometry_count == 0)
        {
            fprintf(stderr, "Cannot read monitor list. Falling back to root geometry.\n");
            geometries[0] = get_root_geometry(d, screen);
            geometry_count = 1;
        }
    }
    else if (!get_monitor_geometry(d, cfg->monitor, &geometries[0]))
    {
        fprintf(
            stderr,
            "Monitor '%s' was not found. Falling back to all monitors.\n",
            cfg->monitor
        );
        geometries[0] = get_root_geometry(d, screen);
        geometry_count = 1;
    }
    else
    {
        geometry_count = 1;
    }

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

    g_display = d;

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGHUP, cleanup);

    for (int i = 0; i < geometry_count; i++)
    {
        Window win;
        pid_t mpv_pid;
        int input_passthrough;

        printf(
            "Livepaper geometry for monitor '%s': %dx%d+%d+%d\n",
            geometries[i].name,
            geometries[i].width,
            geometries[i].height,
            geometries[i].x,
            geometries[i].y
        );

        win = create_wallpaper_window(d, create_parent, screen, &geometries[i]);
        input_passthrough = enable_input_passthrough(d, win);
        XSync(d, False);

        mpv_pid = start_mpv(win, cfg->wallpaper);
        if (mpv_pid < 0)
            cleanup(0);

        g_instances[g_instance_count].window = win;
        g_instances[g_instance_count].mpv_pid = mpv_pid;
        g_instances[g_instance_count].geometry = geometries[i];
        g_instance_count++;

        printf("Created Livepaper child window ID: 0x%lx\n", win);
        printf("X Shape input pass-through enabled: %s\n", input_passthrough ? "yes" : "no");
        printf("MPV PID: %d\n", mpv_pid);
    }

    printf("Livepaper started.\n");
    printf("Livepaper PID: %d\n", getpid());

    for (int i = 0; i < g_instance_count; i++)
        refresh_muffin_stacking_once(d, g_instances[i].window);

    while (1)
    {
        sleep(5);

        for (int i = 0; i < g_instance_count; i++)
        {
            keep_layer_order(d, g_instances[i].window);

            if (waitpid(g_instances[i].mpv_pid, NULL, WNOHANG) > 0)
            {
                printf("MPV exited for monitor '%s'.\n", g_instances[i].geometry.name);
                cleanup(0);
            }
        }
    }

    return 0;
}

void list_monitors(void)
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
