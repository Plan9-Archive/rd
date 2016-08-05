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
	c->xsz = (Dx(screen->r) +3) & ~3;
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

	ARGBEGIN {
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
	} ARGEND

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

void
readnet(Rdp* c)
{
	Msg r;

	for(;;){
		if(readmsg(c, &r) <= 0)
			return;

		switch(r.type){
		case Mclosing:
			return;
		case Mvchan:
			scanvc(c, &r);
			break;
		case Aupdate:
			scanupdates(c, &r);
			break;
		case 0:
			fprint(2, "unsupported PDU\n");
			break;
		default:
			fprint(2, "r.type %d is not expected\n", r.type);
		}
	}
}

void
scanupdates(Rdp* c, Msg* m)
{
	int n;
	uchar *p, *ep;
	Share u;

	p = m->data;
	ep = m->data + m->ndata;

	for(; p < ep; p += n){
		n = m->getshare(&u, p, ep-p);
		if(n < 0)
			sysfatal("scanupdates: %r");

		switch(u.type){
		default:
			if(u.type != 0)
				fprint(2, "scanupdates: unhandled %d\n", u.type);
			break;
		case ShDeactivate:
			deactivating(c, &u);
			break;
		case ShActivate:	// server may engage capability re-exchange
			activating(c, &u);
			break;
		case ShEinfo:
			c->hupreason = u.err;
			break;
		case ShUorders:
			scanorders(c, &u);
			break;
		case ShUimg:
			drawimgupdate(c, &u);
			break;
		case ShUcmap:
			loadcmap(c, &u);
			break;
		case ShUwarp:
			warpmouse(u.x, u.y);
			break;
		case Aflow:
			break;
		}
	}
}

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
void
drawimgupdate(Rdp *c, Share* as)
{
	uchar* p, *ep;
	int n, err, nr;
	static Image* img;
	Rectangle r, rs, d;
	Imgupd iu;

	assert(as->type == ShUimg);
	p = as->data;
	ep = as->data + as->ndata;
	nr = as->nrect;

	rs = rectaddpt(Rpt(ZP, Pt(c->xsz, c->ysz)), screen->r.min);

	if(display->locking)
		lockdisplay(display);

	if(img==nil || !eqrect(img->r, rs)){
		if(img != nil)
			freeimage(img);
		img = allocimage(display, rs, c->chan, 0, DNofill);
		if(img == nil)
			sysfatal("drawimgupdate: %r");
	}

	while(p<ep && nr>0){
		/* 2.2.9.1.1.3.1.2.2 Bitmap Data (TS_BITMAP_DATA) */
		if((n = getimgupd(&iu, p, ep-p)) < 0)
			sysfatal("getimgupd: %r");
		if(iu.depth != img->depth)
			sysfatal("bad image depth");

		d.min = Pt(iu.x, iu.y);
		d.max = Pt(iu.xm+1, iu.ym+1);
		r.min = ZP;
		r.max = Pt(iu.xsz, iu.ysz);
		r = rectaddpt(r, img->r.min);

		err = (iu.iscompr? loadrle : loadbmp)(img, r, iu.bytes, iu.nbytes, c->cmap);
		if(err < 0)
			sysfatal("%r");
		draw(screen, rectaddpt(d, screen->r.min), img, nil, img->r.min);
		p += n;
		nr--;
	}
	flushimage(display, 1);
	if(display->locking)
		unlockdisplay(display);
}
