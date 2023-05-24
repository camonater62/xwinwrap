/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

/*
 * Modified by: Takase
 * Changelog:
 * 1. Add ability to pass WID within an argument
 * 2. Refactor arguments handling
 * 3. Refactor code style
 */

/*
 * Modified by: Ujjwal Verma
 * Changelog:
 * 1. Added daemon mode
 * 2. Added ability to undecorate window
 * 3. Refactored desktop window logic
 * 4. Refactored window hints
 */

/*
 * Modified by: Shantanu Goel
 * Tech Blog: http://tech.shantanugoel.com
 * Blog: http://blog.shantanugoel.com
 * Home Page: http://tech.shantanugoel.com/projects/linux/shantz-xwinwrap
 *
 * Changelog:
 * 15-Jan-09:   1. Fixed the bug where XFetchName returning a NULL for "name"
 *                 resulted in a crash.
 *              2. Provided an option to specify the desktop window name.
 *              3. Added debug messages
 *
 * 24-Aug-08:   1. Fixed the geometry option (-g) so that it works
 *              2. Added override option (-ov), for seamless integration with
 *                 desktop like a background in non-fullscreen modes
 *              3. Added shape option (-sh), to create non-rectangular windows.
 *                 Currently supporting circlular and triangular windows
 */

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define WIDTH  512
#define HEIGHT 384

#define OPAQUE 0xffffffff

#define NAME "xwinwrap"

#define WID_PLACEHOLDER "%WID"

#define ATOM(a) XInternAtom(display, #a, False)

#define SETFLAG(flag, var)                                                                         \
    if (strcmp(argv[i], flag) == 0) {                                                              \
        var = true;                                                                                \
        continue;                                                                                  \
    }

#define SETARG(opt, var)                                                                           \
    if (strcmp(argv[i], opt) == 0) {                                                               \
        if (++i < argc)                                                                            \
            var = argv[i];                                                                         \
        else                                                                                       \
            die("Missing argument for " opt);                                                      \
        continue;                                                                                  \
    }

Display *display = NULL;
int display_width;
int display_height;
int screen;

typedef enum {
    SHAPE_RECT = 0,
    SHAPE_CIRCLE,
    SHAPE_TRIG,
} win_shape;

struct window {
    Window root, window, desktop, child;
    Drawable drawable;
    Visual *visual;
    Colormap colourmap;

    unsigned int width;
    unsigned int height;
    int x;
    int y;
} window;

bool debug = false;

static pid_t pid = 0;

static char **child_argv = NULL;
static int child_argc = 0;

static void die(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "Error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    exit(1);
}

static void set_window_opacity(unsigned int opacity) {
    CARD32 o;
    o = opacity;
    XChangeProperty(display, window.window, ATOM(_NET_WM_WINDOW_OPACITY), XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) &o, 1);
}

static void init_x11() {
    display = XOpenDisplay(NULL);
    if (!display)
        die("Couldn't open display.");
    screen = DefaultScreen(display);
    display_width = DisplayWidth(display, screen);
    display_height = DisplayHeight(display, screen);
}

static int get_argb_visual(Visual **visual, int *depth) {
    XVisualInfo visual_template;
    XVisualInfo *visual_list;
    int nxvisuals = 0, i;

    visual_template.screen = screen;
    visual_list = XGetVisualInfo(display, VisualScreenMask, &visual_template, &nxvisuals);
    for (i = 0; i < nxvisuals; i++) {
        if (visual_list[i].depth == 32
            && (visual_list[i].red_mask == 0xff0000 && visual_list[i].green_mask == 0x00ff00
                && visual_list[i].blue_mask == 0x0000ff)) {
            *visual = visual_list[i].visual;
            *depth = visual_list[i].depth;
            if (debug)
                fprintf(stderr, "Found ARGB Visual\n");
            XFree(visual_list);
            return 1;
        }
    }
    if (debug)
        fprintf(stderr, "No ARGB Visual found");
    XFree(visual_list);
    return 0;
}

static void sig_handler(int sig) {
    kill(pid, sig);
}

static void usage() {
    fprintf(stderr,
        "Usage: %s [-g {w}x{h}+{x}+{y}] [-ni] [-argb] [-fdt] [-fs] [-s] [-st] [-sp] [-a] [-d] "
        "[-b] [-nf] [-o OPACITY] [-sh SHAPE] [-ov] -- COMMAND ARG1 ...\n",
        NAME);
    fprintf(stderr, "Options:\n \
            -g      - Specify Geometry (w=width, h=height, x=x-coord, y=y-coord. ex: -g 640x480+100+100)\n \
            -ni     - Ignore Input\n \
            -argb   - RGB\n \
            -fdt    - force WID window a desktop type window\n \
            -sub    - Set WID placeholder (default is %s)\n \
            -fs     - Full Screen\n \
            -un     - Undecorated\n \
            -s      - Sticky\n \
            -st     - Skip Taskbar\n \
            -sp     - Skip Pager\n \
            -a      - Above\n \
            -b      - Below\n \
            -nf     - No Focus\n \
            -o      - Opacity value between 0 to 1 (ex: -o 0.20)\n \
            -sh     - Shape of window (choose between rectangle, circle or triangle. Default is rectangle)\n \
            -ov     - Set override_redirect flag (For seamless desktop background integration in non-fullscreenmode)\n \
            -d      - Daemonize\n \
            -fa     - Force the child window to attach (no need to provide it with WID)\n \
            -debug  - Enable debug messages\n",
        WID_PLACEHOLDER);
    exit(1);
}

static Window find_subwindow(Window win, int w, int h) {
    unsigned int i, j;
    Window troot, parent, *children;
    unsigned int n;

    /* search subwindows with same size as display or work area */

    for (i = 0; i < 10; i++) {
        XQueryTree(display, win, &troot, &parent, &children, &n);

        for (j = 0; j < n; j++) {
            XWindowAttributes attrs;

            if (XGetWindowAttributes(display, children[j], &attrs)) {
                /* Window must be mapped and same size as display or
                 * work space */
                if (attrs.map_state != 0
                    && ((attrs.width == display_width && attrs.height == display_height)
                        || (attrs.width == w && attrs.height == h))) {
                    win = children[j];
                    break;
                }
            }
        }

        XFree(children);
        if (j == n) {
            break;
        }
    }

    return win;
}

static Window find_desktop_window(Window *p_root, Window *p_desktop) {
    Atom type;
    int format, i;
    unsigned long nitems, bytes;
    unsigned int n;
    Window root = RootWindow(display, screen);
    Window win = root;
    Window troot, parent, *children;
    unsigned char *buf = NULL;

    if (!p_root || !p_desktop) {
        return 0;
    }

    /* some window managers set __SWM_VROOT to some child of root window */

    XQueryTree(display, root, &troot, &parent, &children, &n);
    for (i = 0; i < (int) n; i++) {
        if (XGetWindowProperty(display, children[i], ATOM(__SWM_VROOT), 0, 1, False, XA_WINDOW,
                &type, &format, &nitems, &bytes, &buf)
                == Success
            && type == XA_WINDOW) {
            win = *(Window *) buf;
            XFree(buf);
            XFree(children);
            if (debug) {
                fprintf(
                    stderr, NAME ": desktop window (%lx) found from __SWM_VROOT property\n", win);
            }
            fflush(stderr);
            *p_root = win;
            *p_desktop = win;
            return win;
        }

        if (buf) {
            XFree(buf);
            buf = 0;
        }
    }
    XFree(children);

    /* get subwindows from root */
    win = find_subwindow(root, -1, -1);

    display_width = DisplayWidth(display, screen);
    display_height = DisplayHeight(display, screen);

    win = find_subwindow(win, display_width, display_height);

    if (buf) {
        XFree(buf);
        buf = 0;
    }

    if (win != root && debug) {
        fprintf(
            stderr, NAME ": desktop window (%lx) is subwindow of root window (%lx)\n", win, root);
    } else if (debug) {
        fprintf(stderr, NAME ": desktop window (%lx) is root window\n", win);
    }

    fflush(stderr);

    *p_root = root;
    *p_desktop = win;

    return win;
}

static Window find_child_window(pid_t pid) {
    Atom type;
    int format, i;
    unsigned long nitems, bytes;
    unsigned int n;
    Window root = RootWindow(display, screen);
    Window win = root;
    Window troot, parent, *children;
    unsigned char *buf = NULL;
    pid_t currpid;

    XQueryTree(display, root, &troot, &parent, &children, &n);
    for (i = 0; i < (int) n; i++) {
        int success
            = XGetWindowProperty(display, children[i], XInternAtom(display, "_NET_WM_PID", True), 0,
                1, False, XA_CARDINAL, &type, &format, &nitems, &bytes, &buf);
        if (buf == 0) {
            continue;
        }
        currpid = *((pid_t *) buf);
        if (success == Success && currpid == pid) {
            win = children[i];
            XFree(buf);
            XFree(children);
            if (debug) {
                fprintf(stderr, NAME ": found child window (%lx)\n", win);
            }
            fflush(stderr);
            return win;
        }

        if (buf) {
            XFree(buf);
            buf = 0;
        }
    }
    XFree(children);

    return 0;
}

int main(int argc, char **argv) {
    char wid_arg[255];
    char *wid_placeholder = WID_PLACEHOLDER;
    int status = 0;
    unsigned int opacity = OPAQUE;

    int i;
    bool have_argb_visual = false;
    bool no_input = false;
    bool argb = false;
    bool set_desktop_type = false;
    bool fullscreen = false;
    bool no_focus = false;
    bool override = false;
    bool undecorated = false;
    bool sticky = false;
    bool below = false;
    bool above = false;
    bool skip_taskbar = false;
    bool skip_pager = false;
    bool daemonize = false;
    bool force_attach = false;
    bool help = false;

    win_shape shape = SHAPE_RECT;
    Pixmap mask;
    GC mask_gc;
    XGCValues xgcv;

    window.width = WIDTH;
    window.height = HEIGHT;

    char *geom = NULL;
    char *op = NULL;
    char *sh = NULL;
    for (i = 1; i < argc; i++) {
        SETFLAG("-a", above);
        SETFLAG("-b", below);
        SETFLAG("-d", daemonize);
        SETFLAG("-h", help);
        SETFLAG("-s", sticky);
        SETFLAG("-ni", no_input);
        SETFLAG("-fs", fullscreen);
        SETFLAG("-un", undecorated);
        SETFLAG("-st", skip_taskbar);
        SETFLAG("-sp", skip_pager);
        SETFLAG("-nf", no_focus);
        SETFLAG("-ov", override);
        SETFLAG("-fdt", set_desktop_type);
        SETFLAG("-argb", argb);
        SETFLAG("-debug", debug);
        SETFLAG("-fa", force_attach);
        SETARG("-g", geom);
        SETARG("-o", op);
        SETARG("-sh", sh);
        SETARG("-sub", wid_placeholder);

        if (strcmp(argv[i], "--") == 0)
            break;
        die("Invalid argument '%s'. use -h to get help.", argv[i]);
    }

    if (help)
        usage();
    if (geom != NULL)
        XParseGeometry(geom, &window.x, &window.y, &window.width, &window.height);
    if (op != NULL)
        opacity = (unsigned int) (atof(op) * OPAQUE);
    if (sh != NULL) {
        if (strcmp(sh, "circle") == 0)
            shape = SHAPE_CIRCLE;
        else if (strcmp(sh, "triangle") == 0)
            shape = SHAPE_TRIG;
        else {
            usage();
            return 1;
        }
    }

    if (daemonize) {
        pid_t process_id = 0;
        pid_t sid = 0;
        process_id = fork();
        if (process_id < 0)
            die("fork failed:");

        if (process_id > 0) {
            fprintf(stderr, "pid of child proces: %d \n", process_id);
            exit(0);
        }
        umask(0);
        sid = setsid();
        if (sid < 0)
            die("setsid failed:");

        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    child_argv = malloc((argc - i + 1) * sizeof(char *));
    for (i = i + 1; i < argc; i++) {
        if (strstr(argv[i], wid_placeholder) != NULL) {
            int l = strlen(argv[i]);
            memcpy(wid_arg, argv[i], l);
            wid_arg[l] = '\0';
            child_argv[child_argc] = wid_arg;
        } else {
            child_argv[child_argc] = argv[i];
        }

        child_argc++;
    }

    if (!child_argc)
        die("No command specified. Use -h to get help.");

    child_argv[child_argc] = NULL;
    init_x11();

    if (fullscreen) {
        window.x = 0;
        window.y = 0;
        window.width = DisplayWidth(display, screen);
        window.height = DisplayHeight(display, screen);
    }
    int depth = 0, flags = CWOverrideRedirect | CWBackingStore;
    Visual *visual = NULL;

    if (!find_desktop_window(&window.root, &window.desktop))
        die("Couldn't find desktop window.");

    if (argb && get_argb_visual(&visual, &depth)) {
        have_argb_visual = true;
        window.visual = visual;
        window.colourmap
            = XCreateColormap(display, DefaultRootWindow(display), window.visual, AllocNone);
    } else {
        window.visual = DefaultVisual(display, screen);
        window.colourmap = DefaultColormap(display, screen);
        depth = CopyFromParent;
        visual = CopyFromParent;
    }

    if (override) {
        /* An override_redirect True window.
         * No WM hints or button processing needed. */
        XSetWindowAttributes attrs = { ParentRelative, 0L, 0, 0L, 0, 0, Always, 0L, 0L, False,
            StructureNotifyMask | ExposureMask, 0L, True, 0, 0 };

        if (have_argb_visual) {
            attrs.colormap = window.colourmap;
            flags |= CWBorderPixel | CWColormap;
        } else {
            flags |= CWBackPixel;
        }

        window.window = XCreateWindow(display, window.desktop, window.x, window.y, window.width,
            window.height, 0, depth, InputOutput, visual, flags, &attrs);
        XLowerWindow(display, window.window);

        fprintf(stderr, NAME ": window type - override\n");
        fflush(stderr);
    } else {
        XSetWindowAttributes attrs = { ParentRelative, 0L, 0, 0L, 0, 0, Always, 0L, 0L, False,
            StructureNotifyMask | ExposureMask | ButtonPressMask | ButtonReleaseMask, 0L, False, 0,
            0 };

        XWMHints wmHint;
        Atom xa;

        if (have_argb_visual) {
            attrs.colormap = window.colourmap;
            flags |= CWBorderPixel | CWColormap;
        } else {
            flags |= CWBackPixel;
        }

        window.window = XCreateWindow(display, window.root, window.x, window.y, window.width,
            window.height, 0, depth, InputOutput, visual, flags, &attrs);

        wmHint.flags = InputHint | StateHint;
        // wmHint.input = undecorated ? False : True;
        wmHint.input = !no_focus;
        wmHint.initial_state = NormalState;

        XSetWMProperties(display, window.window, NULL, NULL, argv, argc, NULL, &wmHint, NULL);

        xa = ATOM(_NET_WM_WINDOW_TYPE);

        Atom prop;
        if (set_desktop_type) {
            prop = ATOM(_NET_WM_WINDOW_TYPE_DESKTOP);
        } else {
            prop = ATOM(_NET_WM_WINDOW_TYPE_NORMAL);
        }

        XChangeProperty(
            display, window.window, xa, XA_ATOM, 32, PropModeReplace, (unsigned char *) &prop, 1);

        if (undecorated) {
            xa = ATOM(_MOTIF_WM_HINTS);
            if (xa != None) {
                long prop[5] = { 2, 0, 0, 0, 0 };
                XChangeProperty(
                    display, window.window, xa, xa, 32, PropModeReplace, (unsigned char *) prop, 5);
            }
        }

        /* Below other windows */
        if (below) {

            xa = ATOM(_WIN_LAYER);
            if (xa != None) {
                long prop = 0;

                XChangeProperty(display, window.window, xa, XA_CARDINAL, 32, PropModeAppend,
                    (unsigned char *) &prop, 1);
            }

            xa = ATOM(_NET_WM_STATE);
            if (xa != None) {
                Atom xa_prop = ATOM(_NET_WM_STATE_BELOW);

                XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                    (unsigned char *) &xa_prop, 1);
            }
        }

        /* Above other windows */
        if (above) {

            xa = ATOM(_WIN_LAYER);
            if (xa != None) {
                long prop = 6;

                XChangeProperty(display, window.window, xa, XA_CARDINAL, 32, PropModeAppend,
                    (unsigned char *) &prop, 1);
            }

            xa = ATOM(_NET_WM_STATE);
            if (xa != None) {
                Atom xa_prop = ATOM(_NET_WM_STATE_ABOVE);

                XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                    (unsigned char *) &xa_prop, 1);
            }
        }

        /* Sticky */
        if (sticky) {

            xa = ATOM(_NET_WM_DESKTOP);
            if (xa != None) {
                CARD32 xa_prop = 0xFFFFFFFF;

                XChangeProperty(display, window.window, xa, XA_CARDINAL, 32, PropModeAppend,
                    (unsigned char *) &xa_prop, 1);
            }

            xa = ATOM(_NET_WM_STATE);
            if (xa != None) {
                Atom xa_prop = ATOM(_NET_WM_STATE_STICKY);

                XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                    (unsigned char *) &xa_prop, 1);
            }
        }

        /* Skip taskbar */
        if (skip_taskbar) {

            xa = ATOM(_NET_WM_STATE);
            if (xa != None) {
                Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_TASKBAR);

                XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                    (unsigned char *) &xa_prop, 1);
            }
        }

        /* Skip pager */
        if (skip_pager) {

            xa = ATOM(_NET_WM_STATE);
            if (xa != None) {
                Atom xa_prop = ATOM(_NET_WM_STATE_SKIP_PAGER);

                XChangeProperty(display, window.window, xa, XA_ATOM, 32, PropModeAppend,
                    (unsigned char *) &xa_prop, 1);
            }
        }
    }

    if (opacity != OPAQUE)
        set_window_opacity(opacity);

    if (no_input) {
        Region region;

        region = XCreateRegion();
        if (region) {
            XShapeCombineRegion(display, window.window, ShapeInput, 0, 0, region, ShapeSet);
            XDestroyRegion(region);
        }
    }

    if (shape) {
        mask = XCreatePixmap(display, window.window, window.width, window.height, 1);
        mask_gc = XCreateGC(display, mask, 0, &xgcv);

        switch (shape) {
        //Nothing special to be done if it's a rectangle
        case SHAPE_CIRCLE: {
            /* fill mask */
            XSetForeground(display, mask_gc, 0);
            XFillRectangle(display, mask, mask_gc, 0, 0, window.width, window.height);

            XSetForeground(display, mask_gc, 1);
            XFillArc(display, mask, mask_gc, 0, 0, window.width, window.height, 0, 23040);
            break;
        }
        case SHAPE_TRIG: {
            XPoint points[3] = { { 0, window.height }, { window.width / 2, 0 },
                { window.width, window.height } };

            XSetForeground(display, mask_gc, 0);
            XFillRectangle(display, mask, mask_gc, 0, 0, window.width, window.height);

            XSetForeground(display, mask_gc, 1);
            XFillPolygon(display, mask, mask_gc, points, 3, Complex, CoordModeOrigin);
            break;
        }
        default: break;
        }
        /* combine */
        XShapeCombineMask(display, window.window, ShapeBounding, 0, 0, mask, ShapeSet);
    }

    if (!force_attach) {
        XMapWindow(display, window.window);
        XSync(display, window.window);
    }

    char *m = strstr(wid_arg, wid_placeholder);
    if (m != NULL)
        sprintf(m, "0x%x", (int) window.window);

    pid = fork();

    switch (pid) {
    case -1: die("fork failed:");
    case 0:
        execvp(child_argv[0], child_argv);
        perror(child_argv[0]);
        exit(2);
    }

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    for (;;) {
        if (force_attach) {
            for (i = 0; i < 100; i++) {
                usleep(100 * 1000);
                window.child = find_child_window(pid);
                if (window.child != 0)
                    break;
            }
            if (window.child == 0) {
                fprintf(stderr, "could not find any child window");
                break;
            }

            XReparentWindow(display, window.child, window.window, 0, 0);
            XResizeWindow(display, window.child, window.width, window.height);

            XMapWindow(display, window.window);
            XSync(display, window.window);
        }

        if (waitpid(pid, &status, 0) != -1) {
            if (WIFEXITED(status))
                fprintf(stderr, "%s died, exit status %d\n", child_argv[0], WEXITSTATUS(status));

            break;
        }
    }

    XDestroyWindow(display, window.window);
    XCloseDisplay(display);

    return 0;
}
