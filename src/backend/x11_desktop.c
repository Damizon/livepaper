#include "x11_desktop.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int size_matches_desktop_area(Display *d, int width, int height)
{
    int screen = DefaultScreen(d);
    int screen_w = DisplayWidth(d, screen);
    int screen_h = DisplayHeight(d, screen);
    Window root = RootWindow(d, screen);
    XRRScreenResources *res;

    if (width >= screen_w * 0.7 && height >= screen_h * 0.7)
        return 1;

    res = XRRGetScreenResourcesCurrent(d, root);
    if (!res)
        return 0;

    for (int i = 0; i < res->noutput; i++)
    {
        XRROutputInfo *output = XRRGetOutputInfo(d, res, res->outputs[i]);
        int match = 0;

        if (output && output->connection == RR_Connected && output->crtc)
        {
            XRRCrtcInfo *crtc = XRRGetCrtcInfo(d, res, output->crtc);

            if (crtc)
            {
                match = width >= (int)(crtc->width * 0.7) &&
                        height >= (int)(crtc->height * 0.7);
                XRRFreeCrtcInfo(crtc);
            }
        }

        if (output)
            XRRFreeOutputInfo(output);

        if (match)
        {
            XRRFreeScreenResources(res);
            return 1;
        }
    }

    XRRFreeScreenResources(res);
    return 0;
}

static Window find_nemo_desktop(Display *d, Window root)
{
    if (is_nemo_desktop(d, root))
    {
        XWindowAttributes attr;

        if (XGetWindowAttributes(d, root, &attr))
        {
            if (size_matches_desktop_area(d, attr.width, attr.height))
                return root;
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

static Window xwinwrap_find_subwindow(Display *d, Window win, int w, int h)
{
    unsigned int i;
    unsigned int j;
    int screen = DefaultScreen(d);
    int display_width = DisplayWidth(d, screen);
    int display_height = DisplayHeight(d, screen);

    for (i = 0; i < 10; i++)
    {
        Window troot;
        Window parent;
        Window *children = NULL;
        unsigned int n = 0;

        if (!XQueryTree(d, win, &troot, &parent, &children, &n))
            break;

        for (j = 0; j < n; j++)
        {
            XWindowAttributes attrs;

            if (XGetWindowAttributes(d, children[j], &attrs))
            {
                if (attrs.map_state != 0 &&
                    attrs.class == InputOutput &&
                    ((attrs.width == display_width && attrs.height == display_height) ||
                     (attrs.width == w && attrs.height == h)))
                {
                    win = children[j];
                    break;
                }
            }
        }

        if (children)
            XFree(children);

        if (j == n)
            break;
    }

    return win;
}

DesktopWindowSelection xwinwrap_find_desktop_window(Display *d)
{
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    Window win = root;
    Window troot;
    Window parent;
    Window *children = NULL;
    unsigned int n = 0;
    unsigned char *buf = NULL;
    Atom type;
    int format;
    unsigned long nitems;
    unsigned long bytes;
    DesktopWindowSelection selection;
    selection.root = root;
    selection.desktop = root;
    selection.window = root;
    selection.swm_vroot_found = 0;
    selection.nemo_found = 0;

    XQueryTree(d, root, &troot, &parent, &children, &n);
    for (int i = 0; i < (int)n; i++)
    {
        if (XGetWindowProperty(d, children[i], XInternAtom(d, "__SWM_VROOT", False),
                               0, 1, False, XA_WINDOW, &type, &format,
                               &nitems, &bytes, &buf) == Success &&
            type == XA_WINDOW)
        {
            win = *(Window *)buf;
            XFree(buf);
            XFree(children);
            selection.root = win;
            selection.desktop = win;
            selection.window = win;
            selection.swm_vroot_found = 1;
            selection.nemo_found = is_nemo_desktop(d, win);
            return selection;
        }

        if (buf)
        {
            XFree(buf);
            buf = NULL;
        }
    }

    if (children)
        XFree(children);

    Window nemo = find_nemo_desktop(d, root);

    if (nemo)
    {
        selection.root = root;
        selection.desktop = nemo;
        selection.window = nemo;
        selection.nemo_found = 1;
        return selection;
    }

    win = xwinwrap_find_subwindow(d, root, -1, -1);
    win = xwinwrap_find_subwindow(
        d,
        win,
        DisplayWidth(d, screen),
        DisplayHeight(d, screen)
    );

    if (buf)
        XFree(buf);

    selection.root = root;
    selection.desktop = win;
    selection.window = win;

    return selection;
}

int enable_input_passthrough(Display *d, Window win)
{
    int event_base;
    int error_base;

    if (!XShapeQueryExtension(d, &event_base, &error_base))
        return 0;

    Region empty = XCreateRegion();

    if (!empty)
        return 0;

    XShapeCombineRegion(d, win, ShapeInput, 0, 0, empty, ShapeSet);
    XDestroyRegion(empty);

    return 1;
}


int wait_for_desktop_ready(Display *d, int timeout_seconds)
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
                if (attr.map_state == IsViewable &&
                    size_matches_desktop_area(d, attr.width, attr.height))
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

void set_window_type_desktop(Display *d, Window win)
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

void set_window_states(Display *d, Window win)
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

void keep_layer_order(Display *d, Window livepaper)
{
    XLowerWindow(d, livepaper);
    XFlush(d);
}

static Window get_window_parent(Display *d, Window win)
{
    Window root_return;
    Window parent = 0;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(d, win, &root_return, &parent, &children, &nchildren))
        return 0;

    if (children)
        XFree(children);

    return parent;
}

static void force_nemo_above_livepaper(Display *d, Window livepaper, Window nemo)
{
    Window livepaper_parent;
    Window nemo_parent;

    if (!nemo)
        return;

    livepaper_parent = get_window_parent(d, livepaper);
    nemo_parent = get_window_parent(d, nemo);

    if (livepaper_parent && livepaper_parent == nemo_parent)
    {
        Window order[2];
        order[0] = nemo;
        order[1] = livepaper;
        XRestackWindows(d, order, 2);
        XSync(d, False);
    }

    XRaiseWindow(d, nemo);
    XSync(d, False);

    XLowerWindow(d, livepaper);
    XSync(d, False);
}

static void send_active_window(Display *d, Window root, Window target)
{
    Atom active_window = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = target;
    ev.xclient.message_type = active_window;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;
    ev.xclient.data.l[1] = CurrentTime;
    ev.xclient.data.l[2] = None;

    XSendEvent(
        d,
        root,
        False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &ev
    );
    XSync(d, False);
}

static void pulse_managed_window(Display *d, Window root)
{
    int screen = DefaultScreen(d);
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.background_pixel = BlackPixel(d, screen);
    attrs.event_mask = StructureNotifyMask;

    Window pulse = XCreateWindow(
        d,
        root,
        -100,
        -100,
        1,
        1,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWBackPixel | CWEventMask,
        &attrs
    );

    XStoreName(d, pulse, "Livepaper stacking refresh");

    Atom state_prop = XInternAtom(d, "_NET_WM_STATE", False);
    Atom states[2];
    states[0] = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
    states[1] = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", False);
    XChangeProperty(
        d,
        pulse,
        state_prop,
        XA_ATOM,
        32,
        PropModeReplace,
        (unsigned char *)states,
        2
    );
    XSync(d, False);

    XMapWindow(d, pulse);
    XSync(d, False);

    usleep(100000);

    XUnmapWindow(d, pulse);
    XSync(d, False);

    XDestroyWindow(d, pulse);
    XSync(d, False);
}

void refresh_muffin_stacking_once(Display *d, Window livepaper)
{
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    Window nemo;

    usleep(500000);

    nemo = find_nemo_desktop(d, root);
    if (!nemo)
    {
        XLowerWindow(d, livepaper);
        XSync(d, False);
        return;
    }

    force_nemo_above_livepaper(d, livepaper, nemo);
    send_active_window(d, root, nemo);
    pulse_managed_window(d, root);
    force_nemo_above_livepaper(d, livepaper, nemo);
}
