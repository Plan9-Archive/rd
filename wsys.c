#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static int snarffd;
static ulong snarfvers;

#define MAXSNARF 100*1024
#ifndef BUFSIZE
#define BUFSIZE	4*1024
#endif

static int
ishidden(void)
{
	int wctl;
	long n;
	uchar buf[80];
	char *p;

	wctl = open("/dev/wctl", OREAD);
	if(wctl < 0)
		return 0;
	n = read(wctl, buf, sizeof(buf));
	close(wctl);
	if(n < 12*4)
		return 0;
	buf[sizeof(buf)-1] = 0;
	p = (char*)buf;
	if(strstr(p, "hidden") != nil)
		return 1;
	return 0;
}

void
eresized(Rdp* c, int)
{
	int fd;
	Point d;

	lockdisplay(display);
	if(getwindow(display, Refnone) < 0)
		sysfatal("resize failed: %r");

	/* lifted from /sys/src/cmd/vnc/wsys.c */
	d = addpt(Pt(c->xsz, c->ysz), Pt(2*Borderwidth, 2*Borderwidth));
	if(d.x < Dx(screen->r) || d.y < Dy(screen->r)){
		fd = open("/dev/wctl", OWRITE);
		if(fd >= 0){
			fprint(fd, "resize -dx %d -dy %d", d.x, d.y);
			close(fd);
		}
	}
	turnupdates(c, 0);
	turnupdates(c, !ishidden());
	unlockdisplay(display);
}
void
initsnarf(void)
{
	snarffd = open("/dev/snarf", OREAD);
}

/*
 * /dev/snarf updates when the file is closed, so we must open our own
 * fd here rather than use snarffd
 */
void
putsnarf(char* s, int nb)
{
	int fd, i;

	if(nb==0 || nb > MAXSNARF)
		return;
	fd = open("/dev/snarf", OWRITE);
	if(fd < 0)
		return;
	snarfvers++;
	while(nb > 0){
		i = write(fd, s, nb);
		if(i < 0)
			break;
		s += i;
		nb -= i;
	}
	close(fd);
}

char*
getsnarf(int *pnb)
{
	int i, n;
	char *s, buf[BUFSIZE];

	if(snarffd < 0)
		return nil;

	s = nil;
	i = 0;
	seek(snarffd, 0, 0);
	while((n = read(snarffd, buf, sizeof(buf))) > 0){
		s = erealloc(s, i+n+1);
		memmove(s+i, buf, n);
		i += n;
		s[i] = 0;
	}
	*pnb = i+1;	// for terminating zero
	return s;
}

/* lifted from /sys/src/cmd/vnc/wsys.c */
void
pollsnarf(Rdp* c)
{
	Dir *dir;

	while(snarffd < 0){
		snarffd = open("/dev/snarf", OREAD);
		if(snarffd < 0)
			sleep(1000*60);
	}

	for(;;){
		sleep(1000);

		dir = dirstat("/dev/snarf");
		if(dir == nil)	/* old drawterm */
			continue;
		if(dir->qid.vers > snarfvers){
			clipannounce(c);
			snarfvers = dir->qid.vers;
		}
		free(dir);
	}
}
