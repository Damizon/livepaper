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

static Display *g_display = NULL;
static Window g_window = 0;
static pid_t g_mpv_pid = -1;

void cleanup(int sig)
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
    printf("MPV PID: %d\n", g_mpv_pid);

    refresh_muffin_stacking_once(d, win);

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
