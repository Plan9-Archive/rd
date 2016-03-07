#include <u.h>
#include <libc.h>
#include <auth.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Rdp rd = {
	.fd = -1,
	.chan = RGB16,
	.depth = 16,
	.dim = {800, 600},
	.windom = "",
	.passwd = "",
	.shell = "",
	.rwd = "",
};

char Eshort[]=	"short data";
char Esmall[]=	"buffer too small";
char Ebignum[]=	"number too big";

static void
usage(void)
{
	fprint(2, "usage: rd [-0A] [-T title] [-a depth] [-c wdir] [-d dom] [-k keyspec] [-n term] [-s shell] [net!]server[!port]\n");
	exits("usage");
}

long
writen(int fd, void* buf, long nbytes)
{
	long n, sofar;

	sofar = 0;
	while(sofar < nbytes){
		n = write(fd, buf, nbytes-sofar);
		if(n <= 0)
			break;
		sofar += n;
	}
	return sofar;
}

static int
startmouseproc(void)
{
	int mpid;

	switch(mpid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		break;
	default:
		return mpid;
	}
	atexit(atexitkiller);
	readdevmouse();
	exits("mouse eof");
	return 0;
}

static int
startkbdproc(void)
{
	int pid;
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		break;
	default:
		return pid;
	}
	atexit(atexitkiller);
	readkbd();
	exits("kbd eof");
	return 0;
}

static int
startsnarfproc(void)
{
	int pid;

	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		break;
	default:
		return pid;
	}
	atexit(atexitkiller);
	pollsnarf();
	exits("snarf eof");
	return 0;
}

static int killpid[32];
static int nkillpid;

void
atexitkiller(void)
{
	int i, pid;

	pid = getpid();
	for(i=0; i<nkillpid; i++)
		if(pid != killpid[i])
			postnote(PNPROC, killpid[i], "kill");
}
void
atexitkill(int pid)
{
	killpid[nkillpid++] = pid;
}

void
main(int argc, char *argv[])
{
	int doauth, fd;
	char *server, *addr, *keyspec;
	UserPasswd *creds;

	rd.local = getenv("sysname");
	rd.user = getenv("user");

	keyspec = "";
	doauth = 1;

	ARGBEGIN {
	case 'A':
		doauth = 0;
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 'T':
		rd.label = strdup(EARGF(usage()));
		break;
	case 'd':
		rd.windom = strdup(EARGF(usage()));
		break;
	case 's':
		rd.shell = strdup(EARGF(usage()));
		break;
	case 'c':
		rd.rwd = strdup(EARGF(usage()));
		break;
	case 'n':
		rd.local = estrdup(EARGF(usage()));
		break;
	case 'a':
		rd.depth = atol(EARGF(usage()));
		switch(rd.depth){
		case 8:
			rd.chan = CMAP8;
			break;
		case 15:
			rd.chan = RGB15;
			break;
		case 16:
			rd.chan = RGB16;
			break;
		case 24:
			rd.chan = RGB24;
			break;
		case 32:
			rd.chan = XRGB32;
			break;
		default:
			sysfatal("bad color depth");
		}
		break;
	case '0':
		rd.wantconsole = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc != 1)
		usage();

	server = argv[0];
	if(rd.local == nil)
		sysfatal("set $sysname or use -n\n");
	if(rd.user == nil)
		sysfatal("set $user");
	if (rd.label == nil)
		rd.label = smprint("rd %s", server);

	if(doauth){
		creds = auth_getuserpasswd(auth_getkey, "proto=pass service=rdp %s", keyspec);
		if(creds == nil)
			fprint(2, "factotum: %r\n");
		else {
			rd.user = creds->user;
			rd.passwd = creds->passwd;
			rd.autologon = 1;
		}
	}

	addr = netmkaddr(server, "tcp", "3389");
	fd = dial(addr, nil, nil, nil);
	if(fd < 0)
		sysfatal("dial %s: %r", addr);
	rd.fd = fd;
	if(x224connect(fd) < 0)
		sysfatal("initial handshake failed: %r");

	if(initdraw(drawerror, nil, rd.label) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);

	rd.dim.y = Dy(screen->r);
	rd.dim.x = Dx(screen->r);
	rd.dim.x = (rd.dim.x + 3) & ~3;	/* ensure width divides by 4 */

	if(rdphandshake(fd) < 0)
		sysfatal("handshake failed: %r");

	atexit(atexitkiller);
	atexitkill(getpid());
	atexitkill(startmouseproc());
	atexitkill(startkbdproc());
	initsnarf();
	atexitkill(startsnarfproc());

	readnet(rd.fd);

	x224disconnect(rd.fd);
	close(rd.fd);
	lockdisplay(display);
	closedisplay(display);

	if(!rd.active)
		exits(nil);
	if(rd.hupreason)
		sysfatal("hangup: %d", rd.hupreason);
	sysfatal("hangup");
}

void*
emalloc(ulong n)
{
	void *b;

	b = mallocz(n, 1);
	if(b == nil)
		sysfatal("out of memory allocating %lud: %r", n);
	setmalloctag(b, getcallerpc(&n));
	return b;
}

void*
erealloc(void *a, ulong n)
{
	void *b;

	b = realloc(a, n);
	if(b == nil)
		sysfatal("out of memory re-allocating %lud: %r", n);
	setrealloctag(b, getcallerpc(&a));
	return b;
}

char*
estrdup(const char *s)
{
	char *b;

	b = strdup(s);
	if(b == nil)
		sysfatal("strdup: %r");
	setmalloctag(b, getcallerpc(&s));
	return b;
}
