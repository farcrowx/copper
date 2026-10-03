/*
 * copper-wm: A lightweight X11 window manager using Xlib.
 * Compile: musl-gcc -std=c11 -Wall -Wextra -O2 -o copper-wm copper-wm.c -lX11
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_WINDOWS 64
#define TITLE_H 24
#define TASKBAR_H 28
#define BORDER 2

typedef struct {
    Window win, frame;
    int x, y, w, h;
    int minimized, maximized;
    int saved_x, saved_y, saved_w, saved_h;
    char title[256];
} Client;

static Display *dpy;
static Window root, taskbar;
static Client clients[MAX_WINDOWS];
static int nclients = 0, focused = -1, screen;
static GC gc_title, gc_title_focused, gc_taskbar;
static GC gc_btn_close, gc_btn_min, gc_btn_max;
static int dragging = 0, drag_start_x, drag_start_y;
static int drag_win_x, drag_win_y, drag_win_w, drag_win_h;
static Client *drag_client = NULL;

static Client *find_client(Window w) {
    for (int i = 0; i < nclients; i++)
        if (clients[i].win == w || clients[i].frame == w) return &clients[i];
    return NULL;
}

static void draw_title_bar(Client *c) {
    int is_focused = (focused >= 0 && &clients[focused] == c);
    GC gc = is_focused ? gc_title_focused : gc_title;
    XSetForeground(dpy, gc, is_focused ? 0x4477aa : 0x333333);
    XFillRectangle(dpy, c->frame, gc, 0, 0, c->w + BORDER * 2, TITLE_H);
    XSetForeground(dpy, gc, 0xffffff);
    XDrawString(dpy, c->frame, gc, 6, TITLE_H - 8, c->title, strlen(c->title));
    int fw = c->w + BORDER * 2, bx = fw - TITLE_H;
    XSetForeground(dpy, gc_btn_close, 0xcc4444);
    XFillRectangle(dpy, c->frame, gc_btn_close, bx + 2, 2, TITLE_H - 4, TITLE_H - 4);
    XSetForeground(dpy, gc_btn_close, 0xffffff);
    XDrawString(dpy, c->frame, gc_btn_close, bx + 8, TITLE_H - 8, "X", 1);
    bx -= TITLE_H;
    XSetForeground(dpy, gc_btn_min, 0x44aa44);
    XFillRectangle(dpy, c->frame, gc_btn_min, bx + 2, 2, TITLE_H - 4, TITLE_H - 4);
    XSetForeground(dpy, gc_btn_min, 0xffffff);
    XDrawString(dpy, c->frame, gc_btn_min, bx + 8, TITLE_H - 8, "_", 1);
    bx -= TITLE_H;
    XSetForeground(dpy, gc_btn_max, 0x4444cc);
    XFillRectangle(dpy, c->frame, gc_btn_max, bx + 2, 2, TITLE_H - 4, TITLE_H - 4);
    XSetForeground(dpy, gc_btn_max, 0xffffff);
    XDrawString(dpy, c->frame, gc_btn_max, bx + 8, TITLE_H - 8, "+", 1);
}

static void draw_taskbar(void) {
    XSetForeground(dpy, gc_taskbar, 0x222222);
    XFillRectangle(dpy, taskbar, gc_taskbar, 0, 0, DisplayWidth(dpy, screen), TASKBAR_H);
    int x = 4;
    for (int i = 0; i < nclients; i++) {
        int len = strlen(clients[i].title), w = len * 7 + 16;
        GC gc = (i == focused) ? gc_title_focused : gc_taskbar;
        XSetForeground(dpy, gc, (i == focused) ? 0x4477aa : 0x333333);
        XFillRectangle(dpy, taskbar, gc, x, 2, w, TASKBAR_H - 4);
        XSetForeground(dpy, gc, 0xffffff);
        XDrawString(dpy, taskbar, gc, x + 8, TASKBAR_H - 10, clients[i].title, len);
        x += w + 4;
    }
}

static void focus_client(int idx) {
    if (idx < 0 || idx >= nclients) return;
    focused = idx;
    XSetInputFocus(dpy, clients[idx].win, RevertToPointerRoot, CurrentTime);
    XRaiseWindow(dpy, clients[idx].frame);
    for (int i = 0; i < nclients; i++) draw_title_bar(&clients[i]);
    draw_taskbar();
}

static void close_client(int idx) {
    if (idx < 0 || idx >= nclients) return;
    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = clients[idx].win;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, clients[idx].win, False, NoEventMask, &ev);
    XUnmapWindow(dpy, clients[idx].frame);
    XReparentWindow(dpy, clients[idx].win, root, clients[idx].x, clients[idx].y);
    XDestroyWindow(dpy, clients[idx].frame);
    for (int i = idx; i < nclients - 1; i++) clients[i] = clients[i + 1];
    nclients--;
    if (focused >= nclients) focused = nclients - 1;
    if (focused >= 0) focus_client(focused);
    draw_taskbar();
}

static void minimize_client(int idx) {
    if (idx < 0 || idx >= nclients) return;
    clients[idx].minimized = 1;
    XUnmapWindow(dpy, clients[idx].frame);
    if (focused == idx) {
        focused = -1;
        for (int i = 0; i < nclients; i++)
            if (!clients[i].minimized) { focus_client(i); break; }
    }
    draw_taskbar();
}

static void maximize_client(int idx) {
    if (idx < 0 || idx >= nclients) return;
    Client *c = &clients[idx];
    if (c->maximized) {
        c->maximized = 0;
        XMoveResizeWindow(dpy, c->frame, c->saved_x, c->saved_y,
                          c->saved_w + BORDER * 2, c->saved_h + TITLE_H + BORDER);
        XMoveResizeWindow(dpy, c->win, BORDER, TITLE_H, c->saved_w, c->saved_h);
        c->x = c->saved_x; c->y = c->saved_y;
        c->w = c->saved_w; c->h = c->saved_h;
    } else {
        c->maximized = 1;
        c->saved_x = c->x; c->saved_y = c->y;
        c->saved_w = c->w; c->saved_h = c->h;
        int sw = DisplayWidth(dpy, screen), sh = DisplayHeight(dpy, screen) - TASKBAR_H;
        XMoveResizeWindow(dpy, c->frame, 0, 0, sw, sh);
        XMoveResizeWindow(dpy, c->win, BORDER, TITLE_H, sw - BORDER * 2, sh - TITLE_H - BORDER);
        c->x = 0; c->y = 0; c->w = sw - BORDER * 2; c->h = sh - TITLE_H - BORDER;
    }
    draw_title_bar(c);
}

static void handle_map_request(XMapRequestEvent *ev) {
    if (find_client(ev->window)) { XMapWindow(dpy, ev->window); return; }
    if (nclients >= MAX_WINDOWS) return;
    Client *c = &clients[nclients];
    c->win = ev->window; c->minimized = 0; c->maximized = 0;
    XWindowAttributes wa;
    XGetWindowAttributes(dpy, c->win, &wa);
    c->x = wa.x; c->y = wa.y; c->w = wa.width; c->h = wa.height;
    char *name = NULL;
    XFetchName(dpy, c->win, &name);
    if (name) { strncpy(c->title, name, sizeof(c->title) - 1); c->title[sizeof(c->title) - 1] = '\0'; XFree(name); }
    else strcpy(c->title, "Untitled");
    XSetWindowAttributes swa = {0};
    swa.background_pixel = 0x333333;
    swa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ExposureMask | ButtonPressMask;
    c->frame = XCreateWindow(dpy, root, c->x, c->y, c->w + BORDER * 2, c->h + TITLE_H + BORDER,
                             0, CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixel | CWEventMask, &swa);
    XReparentWindow(dpy, c->win, c->frame, BORDER, TITLE_H);
    XSelectInput(dpy, c->win, PropertyChangeMask | StructureNotifyMask);
    XMapWindow(dpy, c->frame); XMapWindow(dpy, c->win);
    nclients++; focus_client(nclients - 1);
}

static void handle_unmap_notify(XUnmapEvent *ev) {
    Client *c = find_client(ev->window);
    if (!c) return;
    int idx = c - clients;
    XUnmapWindow(dpy, c->frame);
    XReparentWindow(dpy, c->win, root, c->x, c->y);
    XDestroyWindow(dpy, c->frame);
    for (int i = idx; i < nclients - 1; i++) clients[i] = clients[i + 1];
    nclients--;
    if (focused >= nclients) focused = nclients - 1;
    if (focused >= 0) focus_client(focused);
    draw_taskbar();
}

static void handle_configure_request(XConfigureRequestEvent *ev) {
    Client *c = find_client(ev->window);
    if (!c) {
        XWindowChanges wc = {0};
        wc.x = ev->x; wc.y = ev->y; wc.width = ev->width; wc.height = ev->height;
        wc.border_width = ev->border_width; wc.sibling = ev->above; wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
        return;
    }
    if (ev->value_mask & CWWidth) c->w = ev->width;
    if (ev->value_mask & CWHeight) c->h = ev->height;
    XMoveResizeWindow(dpy, c->win, BORDER, TITLE_H, c->w, c->h);
    XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w + BORDER * 2, c->h + TITLE_H + BORDER);
    draw_title_bar(c);
}

static void handle_button_press(XButtonEvent *ev) {
    if (ev->window == taskbar) {
        int x = 4;
        for (int i = 0; i < nclients; i++) {
            int len = strlen(clients[i].title), w = len * 7 + 16;
            if (ev->x >= x && ev->x < x + w) {
                if (clients[i].minimized) { clients[i].minimized = 0; XMapWindow(dpy, clients[i].frame); }
                focus_client(i); return;
            }
            x += w + 4;
        }
        return;
    }
    if (ev->window == root && (ev->state & Mod1Mask)) {
        Window child; int rx, ry; unsigned int mask;
        XQueryPointer(dpy, root, &ev->window, &child, &ev->x_root, &ev->y_root, &rx, &ry, &mask);
    }
    Client *c = find_client(ev->window);
    if (!c) return;
    int idx = c - clients;
    if (ev->window == c->frame && ev->y < TITLE_H && !(ev->state & Mod1Mask)) {
        int fw = c->w + BORDER * 2, bx = fw - TITLE_H;
        if (ev->x >= bx && ev->x < bx + TITLE_H) { close_client(idx); return; }
        bx -= TITLE_H;
        if (ev->x >= bx && ev->x < bx + TITLE_H) { minimize_client(idx); return; }
        bx -= TITLE_H;
        if (ev->x >= bx && ev->x < bx + TITLE_H) { maximize_client(idx); return; }
    }
    if (idx != focused) focus_client(idx);
    if (ev->state & Mod1Mask) {
        if (ev->button == Button1) {
            dragging = 1; drag_client = c;
            drag_start_x = ev->x_root; drag_start_y = ev->y_root;
            drag_win_x = c->x; drag_win_y = c->y;
            XRaiseWindow(dpy, c->frame);
        } else if (ev->button == Button3) {
            dragging = 2; drag_client = c;
            drag_start_x = ev->x_root; drag_start_y = ev->y_root;
            drag_win_w = c->w; drag_win_h = c->h;
            XRaiseWindow(dpy, c->frame);
        }
    }
}

static void handle_motion_notify(XMotionEvent *ev) {
    if (!dragging || !drag_client) return;
    Client *c = drag_client;
    int dx = ev->x_root - drag_start_x, dy = ev->y_root - drag_start_y;
    if (dragging == 1) {
        c->x = drag_win_x + dx; c->y = drag_win_y + dy;
        XMoveWindow(dpy, c->frame, c->x, c->y);
    } else if (dragging == 2) {
        c->w = drag_win_w + dx; c->h = drag_win_h + dy;
        if (c->w < 50) c->w = 50;
        if (c->h < 50) c->h = 50;
        XMoveResizeWindow(dpy, c->win, BORDER, TITLE_H, c->w, c->h);
        XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w + BORDER * 2, c->h + TITLE_H + BORDER);
        draw_title_bar(c);
    }
}

static void handle_button_release(XButtonEvent *ev) {
    (void)ev; dragging = 0; drag_client = NULL;
}

static void handle_key_press(XKeyEvent *ev) {
    KeySym ks = XKeycodeToKeysym(dpy, ev->keycode, 0);
    if (ev->state & Mod1Mask) {
        if (ks == XK_Tab) {
            if (nclients > 0) {
                int next = (focused + 1) % nclients, count = 0;
                while (clients[next].minimized && count < nclients) { next = (next + 1) % nclients; count++; }
                if (!clients[next].minimized) focus_client(next);
            }
        } else if (ks == XK_F4) {
            if (focused >= 0) close_client(focused);
        }
    }
    if (ev->state & Mod4Mask) {
        if (ks == XK_space || ks == XK_Return) {
            if (fork() == 0) {
                execlp("copper-launcher", "copper-launcher", NULL);
                _exit(1);
            }
        }
    }
}

static void handle_expose(XExposeEvent *ev) {
    Client *c = find_client(ev->window);
    if (c) draw_title_bar(c);
    if (ev->window == taskbar) draw_taskbar();
}

static void handle_destroy_notify(XDestroyWindowEvent *ev) {
    Client *c = find_client(ev->window);
    if (!c) return;
    int idx = c - clients;
    XUnmapWindow(dpy, c->frame); XDestroyWindow(dpy, c->frame);
    for (int i = idx; i < nclients - 1; i++) clients[i] = clients[i + 1];
    nclients--;
    if (focused >= nclients) focused = nclients - 1;
    if (focused >= 0) focus_client(focused);
    draw_taskbar();
}

static void handle_property_notify(XPropertyEvent *ev) {
    Client *c = find_client(ev->window);
    if (!c) return;
    if (ev->atom == XA_WM_NAME || ev->atom == XInternAtom(dpy, "_NET_WM_NAME", False)) {
        char *name = NULL;
        XFetchName(dpy, c->win, &name);
        if (name) { strncpy(c->title, name, sizeof(c->title) - 1); c->title[sizeof(c->title) - 1] = '\0'; XFree(name); }
        draw_title_bar(c); draw_taskbar();
    }
}

static void handle_client_message(XClientMessageEvent *ev) {
    if (ev->message_type == XInternAtom(dpy, "WM_PROTOCOLS", False) &&
        ev->data.l[0] == XInternAtom(dpy, "WM_DELETE_WINDOW", False)) {
        Client *c = find_client(ev->window);
        if (c) close_client(c - clients);
    }
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "copper-wm: cannot open display\n"); return 1; }
    screen = DefaultScreen(dpy); root = RootWindow(dpy, screen);
    XGCValues gcv = {0};
    gcv.font = XLoadFont(dpy, "fixed");
    gcv.foreground = 0x333333; gc_title = XCreateGC(dpy, root, GCForeground | GCFont, &gcv);
    gcv.foreground = 0x4477aa; gc_title_focused = XCreateGC(dpy, root, GCForeground | GCFont, &gcv);
    gcv.foreground = 0x222222; gc_taskbar = XCreateGC(dpy, root, GCForeground | GCFont, &gcv);
    gcv.foreground = 0xcc4444; gc_btn_close = XCreateGC(dpy, root, GCForeground | GCFont, &gcv);
    gcv.foreground = 0x44aa44; gc_btn_min = XCreateGC(dpy, root, GCForeground | GCFont, &gcv);
    gcv.foreground = 0x4444cc; gc_btn_max = XCreateGC(dpy, root, GCForeground | GCFont, &gcv);
    XSetWindowAttributes swa = {0};
    swa.override_redirect = True; swa.background_pixel = 0x222222;
    swa.event_mask = ExposureMask | ButtonPressMask;
    taskbar = XCreateWindow(dpy, root, 0, DisplayHeight(dpy, screen) - TASKBAR_H,
                            DisplayWidth(dpy, screen), TASKBAR_H, 0,
                            CopyFromParent, InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
    XMapWindow(dpy, taskbar);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Tab), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_F4), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_space), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Return), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabButton(dpy, Button1, Mod1Mask, root, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, Mod1Mask, root, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case MapRequest:        handle_map_request(&ev.xmaprequest); break;
        case UnmapNotify:       handle_unmap_notify(&ev.xunmap); break;
        case ConfigureRequest:  handle_configure_request(&ev.xconfigurerequest); break;
        case ButtonPress:       handle_button_press(&ev.xbutton); break;
        case ButtonRelease:     handle_button_release(&ev.xbutton); break;
        case MotionNotify:      handle_motion_notify(&ev.xmotion); break;
        case KeyPress:          handle_key_press(&ev.xkey); break;
        case Expose:            handle_expose(&ev.xexpose); break;
        case DestroyNotify:     handle_destroy_notify(&ev.xdestroywindow); break;
        case PropertyNotify:    handle_property_notify(&ev.xproperty); break;
        case ClientMessage:     handle_client_message(&ev.xclient); break;
        default: break;
        }
    }
    XCloseDisplay(dpy);
    return 0;
}
