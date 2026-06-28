#include "x11_backend.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include "../../include/process.h"
#include "../session/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static Display *g_display = NULL;
static Window g_window = 0;
static pid_t g_mpv_pid = -1;

void x11_backend_set_window(Display *display, Window window)
{
    g_display = display;
    g_window = window;
}

void x11_backend_set_mpv_pid(pid_t pid)
{
    g_mpv_pid = pid;
}

pid_t x11_backend_get_mpv_pid(void)
{
    return g_mpv_pid;
}

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
