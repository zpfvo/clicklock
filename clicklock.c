/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500

#include <ctype.h>
#include <errno.h>
// #include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/Xlib.h>

enum {
	INIT,
	INPUT,
	FAILED,
	NUMCOLS
};

static const char *colorname[NUMCOLS] = {
	[INIT] =   "black",     /* after initialization */
	[INPUT] =  "#005577",   /* during input */
	[FAILED] = "#CC3333",   /* wrong password */
};

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[NUMCOLS];
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static void
waitforevent(Display *dpy)
{
	XEvent ev;

	running = True;
	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress || ev.type == ButtonPress) {
			running = False;
		}
	}
}

static void
unlockscreen(Display *dpy, Lock *lock)
{
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), lock->colors, NUMCOLS, 0);
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

	if (!running || dpy == NULL || screen < 0 || !(lock = malloc(sizeof(Lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	for (i = 0; i < NUMCOLS; i++) {
		XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), colorname[i], &color, &dummy);
		lock->colors[i] = color.pixel;
	}

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = lock->colors[INIT];
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
	                          0, DefaultDepth(dpy, lock->screen), CopyFromParent,
	                          DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);


	/* Try to grab mouse pointer *and* keyboard, else fail the lock */
	for (len = 1000; len; len--) {
		if (XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		    GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if (!len) {
		fprintf(stderr, "clicklock: unable to grab mouse pointer for screen %d\n", screen);
	} else {
		for (len = 1000; len; len--) {
			if (XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
				/* everything fine, we grabbed both inputs */
				XSelectInput(dpy, lock->root, SubstructureNotifyMask);
				return lock;
			}
			usleep(1000);
		}
		fprintf(stderr, "clicklock: unable to grab keyboard for screen %d\n", screen);
	}
	/* grabbing one of the inputs failed */
	running = 0;
	unlockscreen(dpy, lock);
	return NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: clicklock [-v|POST_LOCK_CMD]\n");
	exit(1);
}

int
main(int argc, char **argv) {
	Display *dpy;
	int screen;

	if ((argc == 2) && !strcmp("-v", argv[1]))
		die("clicklock based on: slock-%s, © 2006-2016 slock engineers\n", VERSION);

	if ((argc == 2) && !strcmp("-h", argv[1]))
		usage();

	if (!(dpy = XOpenDisplay(0)))
		die("clicklock: cannot open display\n");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	if (!(locks = malloc(sizeof(Lock*) * nscreens)))
		die("clicklock: malloc: %s\n", strerror(errno));
	int nlocks = 0;
	for (screen = 0; screen < nscreens; screen++) {
		if ((locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { /* nothing to protect */
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	if (argc >= 2 && fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		execvp(argv[1], argv+1);
		die("clicklock: execvp %s failed: %s\n", argv[1], strerror(errno));
	}

	waitforevent(dpy);

	/* Password ok, unlock everything and quit. */
	for (screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
