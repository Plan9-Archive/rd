#include <u.h>
#include <libc.h>
#include <auth.h>
#include <draw.h>
#include <mouse.h>
#include "dat.h"
#include "fns.h"

Rdp conn = {
	.fd = -1,
	.depth = 16,
	.windom = "",
	.passwd = "",
	.shell = "",
	.rwd = "",
};

char Eshort[]=	"short data";
char Esmall[]=	"buffer too small";
char Ebignum[]=	"number too big";

void	sendmouse(Rdp* c, Mouse m);

static void
usage(void)
{
	fprint(2, "usage: rd [-0A] [-T title] [-a depth] [-c wdir] [-d dom] [-k keyspec] [-n term] [-s shell] [net!]server[!port]\n");
	exits("usage");
}

static int
startmouseproc(Rdp* c)
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
	readdevmouse(c);
	exits("mouse eof");
	return 0;
}

static int
startkbdproc(Rdp* c)
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
	readkbd(c);
	exits("kbd eof");
	return 0;
}

static int
startsnarfproc(Rdp* c)
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
	initsnarf();
	pollsnarf(c);
	exits("snarf eof");
	return 0;
}

void
initscreen(Rdp* c)
{
	if(initdraw(drawerror, nil, c->label) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);

	c->ysz = Dy(screen->r);
	c->xsz = Dx(screen->r);
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
	int doauth;
	char *server, *addr, *keyspec;
	UserPasswd *creds;
	Rdp* c;

	c = &conn;
	keyspec = "";
	doauth = 1;

	ARGBEGIN{
	case 'A':
		doauth = 0;
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 'T':
		c->label = strdup(EARGF(usage()));
		break;
	case 'd':
		c->windom = strdup(EARGF(usage()));
		break;
	case 's':
		c->shell = strdup(EARGF(usage()));
		break;
	case 'c':
		c->rwd = strdup(EARGF(usage()));
		break;
	case 'a':
		c->depth = atol(EARGF(usage()));
		break;
	case '0':
		c->wantconsole = 1;
		break;
	default:
		usage();
	}ARGEND

	if (argc != 1)
		usage();

	server = argv[0];

	c->local = getenv("sysname");
	c->user = getenv("user");
	if(c->local == nil)
		sysfatal("set $sysname or use -n\n");
	if(c->user == nil)
		sysfatal("set $user");
	if(doauth){
		creds = auth_getuserpasswd(auth_getkey, "proto=pass service=rdp %s", keyspec);
		if(creds == nil)
			fprint(2, "factotum: %r\n");
		else {
			c->user = creds->user;
			c->passwd = creds->passwd;
		}
	}else
		c->user = "";
	if(c->label == nil)
		c->label = smprint("rd %s", server);

	initvc(c);

	addr = netmkaddr(server, "tcp", "3389");
	c->fd = dial(addr, nil, nil, nil);
	if(c->fd < 0)
		sysfatal("dial %s: %r", addr);
	if(x224handshake(c) < 0)
		sysfatal("X.224 handshake: %r");
	initscreen(c);
	if(rdphandshake(c) < 0)
		sysfatal("handshake: %r");

	atexit(atexitkiller);
	atexitkill(getpid());
	atexitkill(startmouseproc(c));
	atexitkill(startkbdproc(c));
	atexitkill(startsnarfproc(c));

	readnet(c);

	x224hangup(c);
	if(!c->active)
		exits(nil);
	if(c->hupreason)
		sysfatal("disconnect reason code %d", c->hupreason);
	sysfatal("hangup");
}

void
readkbd(Rdp* c)
{
	char buf[256], k[10];
	int ctlfd, fd, kr, kn, w;
	Rune r;

	if((fd = open("/dev/cons", OREAD)) < 0)
		sysfatal("open %s: %r", buf);
	if((ctlfd = open("/dev/consctl", OWRITE)) < 0)
		sysfatal("open %s: %r", buf);
	write(ctlfd, "rawon", 5);

	kn = 0;
	for(;;){
		while(!fullrune(k, kn)){
			kr = read(fd, k+kn, sizeof k - kn);
			if(kr <= 0)
				sysfatal("bad read from kbd");
			kn += kr;
		}
		w = chartorune(&r, k);
		kn -= w;
		memmove(k, &k[w], kn);
		sendkbd(c, r);
	}
}

static int mfd = -1;

void
readdevmouse(Rdp* c)
{
	Mouse m;
	char ev[1+4*12];

	if((mfd = open("/dev/mouse", ORDWR)) < 0)
		sysfatal("open /dev/mouse: %r");

	for(;;){
		if(read(mfd, ev, sizeof ev) != sizeof ev)
			sysfatal("mouse eof");
		if(*ev == 'm'){
			m.xy.x = atoi(ev+1+0*12);
			m.xy.y = atoi(ev+1+1*12);
			m.buttons = atoi(ev+1+2*12) & 0x1F;
			m.msec = atoi(ev+1+3*12);
			sendmouse(c, m);
		}else
			eresized(c, 1);
	}
}

void
warpmouse(int x, int y)
{
	if(mfd < 0)
		return;

	fprint(mfd, "m%d %d", x, y);
}
