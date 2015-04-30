/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#define PASSLEN 256

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_appl.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define PAM_REALM "login"

enum {
	INIT,
	INPUT,
	EMPTY,
	NUMCOLS
};

#include "config.h"

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[NUMCOLS];
} Lock;

static Lock **locks;
static int nscreens;
static Bool rr;
static int rrevbase;
static int rrerrbase;

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>

static void
dontkillme(void)
{
	int fd;

	fd = open("/proc/self/oom_score_adj", O_WRONLY);
	if (fd < 0 && errno == ENOENT)
		return;
	if (fd < 0 || write(fd, "-1000\n", 6) != 6 || close(fd) != 0)
		die("cannot disable the out-of-memory killer for this process\n");
}
#endif

static void
blank(Display *dpy, int color)
{
	int screen;

	for (screen = 0; screen < nscreens; ++screen) {
		XSetWindowBackground(dpy,
		  locks[screen]->win,
		  locks[screen]->colors[color]);
		XClearWindow(dpy, locks[screen]->win);
	}
}

static void
readpw(Display *dpy, char *passwd)
{
	char buf[32];
	int num, screen;
	unsigned int len, llen;
	KeySym ksym;
	XEvent ev;

	len = llen = 0;

	while (!XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);

			if (IsKeypadKey(ksym)) {
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}

			if (IsFunctionKey(ksym) ||
			    IsKeypadKey(ksym) ||
			    IsMiscFunctionKey(ksym) ||
			    IsPFKey(ksym) ||
			    IsPrivateKeypadKey(ksym))
				continue;

			switch (ksym) {
			case XK_Return:
				passwd[len] = 0;

				return;
			case XK_Escape:
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					--len;
				break;
			default:
				if (num && !iscntrl((int) buf[0])
				    && len + num < PASSLEN) {
					memcpy(passwd + len, buf, num);
					len += num;
					passwd[len] = 0;
				}
				break;
			}

			if (llen == 0 && len != 0) {
				blank(dpy, INPUT);
			} else if (llen != 0 && len == 0) {
				blank(dpy, EMPTY);
			}
			llen = len;
		} else if (rr && ev.type == rrevbase + RRScreenChangeNotify) {
			XRRScreenChangeNotifyEvent *rre =
			  (XRRScreenChangeNotifyEvent*) &ev;

			for (screen = 0; screen < nscreens; ++screen) {
				if (locks[screen]->win == rre->window) {
					XResizeWindow(dpy, locks[screen]->win,
					              rre->width, rre->height);
					XClearWindow(dpy, locks[screen]->win);
				}
			}
		} else {
			for (screen = 0; screen < nscreens; ++screen)
				XRaiseWindow(dpy, locks[screen]->win);
		}
	}
}

static int
pamconv(int num_msg, const struct pam_message **msg, struct pam_response **resp,
        void *appdata_ptr)
{
	int i;

	*resp = (struct pam_response *) calloc(num_msg, sizeof(struct pam_response));

	for (i = 0; i < num_msg; ++i) {
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
			if ((resp[i]->resp = malloc(PASSLEN)) == NULL)
				die("Not enough memory");

			readpw((Display *) appdata_ptr, resp[i]->resp);
		}
		resp[i]->resp_retcode = 0;
	}

	return PAM_SUCCESS;
}

static void
unlockscreen(Display *dpy, Lock *lock)
{
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen),
	            lock->colors, NUMCOLS, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static Lock *
lockscreen(Display *dpy, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	int i;
	Lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if (dpy == NULL || screen < 0)
		return NULL;

	lock = malloc(sizeof(Lock));
	if (lock == NULL)
		return NULL;

	lock->screen = screen;

	lock->root = RootWindow(dpy, lock->screen);

	for (i = 0; i < NUMCOLS; ++i) {
		XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen),
		                 colorname[i], &color, &dummy);
		lock->colors[i] = color.pixel;
	}

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = lock->colors[INIT];
	lock->win = XCreateWindow(dpy, lock->root, 0, 0,
	                          DisplayWidth(dpy, lock->screen),
	                          DisplayHeight(dpy, lock->screen),
	                          0, DefaultDepth(dpy, lock->screen),
	                          CopyFromParent,
	                          DefaultVisual(dpy, lock->screen),
	                          CWOverrideRedirect | CWBackPixel, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color,
	                                &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);

	if (rr)
		XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

	for (len = 1000; len; --len) {
		const int flags = ButtonPressMask |
		                  ButtonReleaseMask |
		                  PointerMotionMask;

		if (XGrabPointer(dpy, lock->root, False, flags,
		                 GrabModeAsync, GrabModeAsync, None, invisible,
		                 CurrentTime)
		      == GrabSuccess)
			break;
		usleep(1000);
	}

	if (len > 0) {
		for (len = 1000; len; --len) {
			if (XGrabKeyboard(dpy, lock->root, True, GrabModeAsync,
			                  GrabModeAsync, CurrentTime)
			      == GrabSuccess)
				break;
			usleep(1000);
		}
	}

	if (len <= 0) {
		unlockscreen(dpy, lock);
		lock = NULL;
	} else {
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);
	}

	return lock;
}

static void
usage(void)
{
	fprintf(stderr, "usage: slock [-v]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	pam_handle_t *pamh = NULL;
	int pamret;
	struct pam_conv conv;
	char *passwd = NULL;
	Display *dpy;
	int screen;

	if (argc == 2 && !strcmp("-v", argv[1]))
		die("slock-pam, © 2006-2015 slock engineers\n");
	else if (argc != 1)
		usage();

#ifdef __linux__
	dontkillme();
#endif

	if (!getpwuid(getuid()))
		die("slock: no passwd entry for you\n");

	if (!(dpy = XOpenDisplay(0)))
		die("slock: cannot open display\n");

	rr = XRRQueryExtension(dpy, &rrevbase, &rrerrbase);

	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	locks = malloc(sizeof(Lock *) * nscreens);
	if (locks == NULL)
		die("slock: malloc: %s\n", strerror(errno));

	int nlocks = 0;
	for (screen = 0; screen < nscreens; ++screen) {
		if ((locks[screen] = lockscreen(dpy, screen)) != NULL)
			++nlocks;
	}

	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { /* nothing to protect */
		free(locks);
		XCloseDisplay(dpy);

		return 1;
	}

	if ((passwd = malloc(PASSLEN)) == NULL)
		die("Not enough memory");
	passwd[0] = 0;

	conv.conv = pamconv;
	conv.appdata_ptr = dpy;

	/* Everything is now blank. Now wait for the correct password. */
	pamret = pam_start(PAM_REALM, getenv("USER"), &conv, &pamh);
	if (pamret != PAM_SUCCESS)
		die("PAM not available");

	for (;;) {
		pamret = pam_authenticate(pamh, 0);
		if (pamret == PAM_SUCCESS)
			break;

		blank(dpy, EMPTY);
		XBell(dpy, 100);
        }

	if (pam_end(pamh, pamret) != PAM_SUCCESS)
		pamh = NULL;

	/* Password ok, unlock everything and quit. */
	for (screen = 0; screen < nscreens; ++screen)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	free(passwd);
	XCloseDisplay(dpy);

	return 0;
}
